// Mithril-Wrapper GL layer -- S5 framebuffer object / renderbuffer bridge.
// Owns the GL object tables for framebuffers and renderbuffers, and forwards
// attachment changes into the selected backend (mapping texture + renderbuffer
// attachments onto resident Vk images). Draw, clear and readback already
// target the bound framebuffer in the backend, so this TU keeps the bindings
// in sync and answers the object/status queries.

#include "internal.h"

#include <algorithm>
#include <cstring>

namespace s = mithril::state;
namespace v = mithril::backend;

// GL renderbuffer object: format + size as set by glRenderbufferStorage, plus
// a mirror of the produced specs for renderbuffer-size FBO checks.
struct RbState {
    GLenum internalformat = 0;
    GLsizei width = 0, height = 0;
    GLsizei samples = 0;
    bool defined = false;
};

// One attachment slot of a GL framebuffer object: either a texture (tex id +
// mip level + array/cube layer) or a renderbuffer id.
struct Attach {
    bool is_texture = false;
    bool present = false;
    GLuint tex_id = 0;
    GLuint rbo_id = 0;
    GLint level = 0;
    GLint layer = 0;
};

// Complete attachment set for one framebuffer object. `width`/`height` are
// the resolved render target size (from the attached images).
struct FbState {
    Attach color[8];                 // GL_COLOR_ATTACHMENT0..7 (MRT)
    int n_color = 0;                 // highest attached color index + 1
    bool has_depth = false;
    Attach depth;                    // GL_DEPTH_ATTACHMENT / _STENCIL_
    GLsizei width = 0, height = 0;
    GLenum draw_bufs[8] = {GL_COLOR_ATTACHMENT0};
    int n_draw = 1;
    GLenum read_buf = GL_COLOR_ATTACHMENT0;
    bool complete = false;           // last glCheckFramebufferStatus result
    bool dirty = true;               // needs a v::SetFramebuffer push
};

static std::unordered_map<GLuint, RbState> g_renderbuffers;
static std::unordered_map<GLuint, FbState> g_framebuffers;
static GLuint g_bound_draw_fbo = 0, g_bound_read_fbo = 0;   // 0 => default
static GLuint g_bound_rbo = 0;
static GLuint g_next_rbo = 1, g_next_fbo = 1;

// Live entry for a framebuffer id (null for the default / unknown ids).
static FbState* FboGet(GLuint id) {
    if (!id) return nullptr;
    auto it = g_framebuffers.find(id);
    return it == g_framebuffers.end() ? nullptr : &it->second;
}

// Size of a single attachment at the resolved size; empty attachments (or
// textures without an image) report failure so the FBO counts as incomplete.
static bool AttachDimensions(const Attach& a, GLsizei* w, GLsizei* h) {
    if (!a.present) return false;
    if (a.is_texture) {
        auto it = g_textures.find(a.tex_id);
        if (it == g_textures.end()) return false;
        const TexState& t = it->second;
        if (t.width == 0 || t.height == 0) return false;
        *w = std::max<GLsizei>(1, (GLsizei)t.width >> a.level);
        *h = std::max<GLsizei>(1, (GLsizei)t.height >> a.level);
    } else {
        auto rit = g_renderbuffers.find(a.rbo_id);
        if (rit == g_renderbuffers.end() || !rit->second.defined) return false;
        *w = rit->second.width;
        *h = rit->second.height;
    }
    return true;
}

static bool AttachSampleCount(const Attach& attachment, GLsizei* samples) {
    if (!attachment.present || !samples) return false;
    if (attachment.is_texture) {
        auto texture = g_textures.find(attachment.tex_id);
        if (texture == g_textures.end() || !texture->second.has_image)
            return false;
        *samples = static_cast<GLsizei>(texture->second.samples);
        return true;
    }
    auto renderbuffer = g_renderbuffers.find(attachment.rbo_id);
    if (renderbuffer == g_renderbuffers.end() || !renderbuffer->second.defined)
        return false;
    *samples = std::max<GLsizei>(renderbuffer->second.samples, 1);
    return true;
}

static bool IsDepthRenderbufferFormat(GLenum format) {
    return format == GL_DEPTH_COMPONENT || format == GL_DEPTH_COMPONENT16 ||
           format == GL_DEPTH_COMPONENT24 || format == GL_DEPTH_COMPONENT32F ||
           format == GL_DEPTH_STENCIL || format == GL_DEPTH24_STENCIL8 ||
           format == GL_DEPTH32F_STENCIL8;
}

static bool AttachIsDepth(const Attach& attachment, bool* is_depth) {
    if (!attachment.present || !is_depth) return false;
    if (attachment.is_texture) {
        auto texture = g_textures.find(attachment.tex_id);
        if (texture == g_textures.end() || !texture->second.has_image)
            return false;
        *is_depth = texture->second.image_backend_format ==
                    v::TexelFormat::Depth32Float;
        return true;
    }
    auto renderbuffer = g_renderbuffers.find(attachment.rbo_id);
    if (renderbuffer == g_renderbuffers.end() || !renderbuffer->second.defined)
        return false;
    *is_depth = IsDepthRenderbufferFormat(renderbuffer->second.internalformat);
    return true;
}

// Push the current attachments into the Vulkan engine (idempotent unless the
// FBO changed); marks `complete` for glCheckFramebufferStatus. A GL FBO must
// have at least one colour attachment to be renderable (the backend has a
// color target), so colour-less FBOs stay incomplete.
static void PushVkFramebuffer(GLuint id);

static bool FillAttach(const Attach& a, v::FboAttach* out) {
    if (!a.present) return false;
    if (a.is_texture) {
        auto it = g_textures.find(a.tex_id);
        if (it == g_textures.end() || it->second.width == 0) return false;
        out->is_texture = true;
        out->tex_id = a.tex_id;
        out->level = (uint32_t)std::max<GLint>(0, a.level);
        out->layer = (uint32_t)std::max<GLint>(0, a.layer);
    } else {
        auto rit = g_renderbuffers.find(a.rbo_id);
        if (rit == g_renderbuffers.end() || !rit->second.defined) return false;
        out->rbo_id = a.rbo_id;
    }
    return true;
}

static void PushVkFramebuffer(GLuint id) {
    FbState* f = FboGet(id);
    if (!f || !f->dirty) return;

    v::FboSpec spec;
    GLsizei cw = 0, ch = 0, framebuffer_samples = 0;
    bool col_ok = false;
    bool attachments_match = true;
    for (int i = 0; i < f->n_color; ++i) {
        v::FboAttach ca;
        if (FillAttach(f->color[i], &ca)) {
            spec.color.push_back(ca);
            GLsizei width = 0, height = 0, samples = 0;
            bool is_depth = false;
            if (!AttachDimensions(f->color[i], &width, &height) ||
                !AttachSampleCount(f->color[i], &samples) ||
                !AttachIsDepth(f->color[i], &is_depth) || is_depth) {
                attachments_match = false;
            } else if (!col_ok) {
                cw = width;
                ch = height;
                framebuffer_samples = samples;
                col_ok = true;
            } else if (width != cw || height != ch ||
                       samples != framebuffer_samples) {
                attachments_match = false;
            }
        } else {
            v::FboAttach empty;
            spec.color.push_back(empty);
        }
    }
    if (col_ok && f->has_depth && f->depth.present) {
        GLsizei dw = 0, dh = 0, depth_samples = 0;
        bool is_depth = false;
        if (AttachDimensions(f->depth, &dw, &dh) && dw == cw && dh == ch &&
            AttachSampleCount(f->depth, &depth_samples) &&
            depth_samples == framebuffer_samples &&
            AttachIsDepth(f->depth, &is_depth) && is_depth) {
            spec.has_depth = true;
            FillAttach(f->depth, &spec.depth);
        } else {
            attachments_match = false;
        }
    }

    spec.read_buf = f->read_buf;
    for (int i = 0; i < f->n_draw; ++i) spec.draw_bufs.push_back(f->draw_bufs[i]);

    if (col_ok && attachments_match) {
        spec.width = cw;
        spec.height = ch;
    }

    v::SetFramebuffer(id, spec);
    f->complete = col_ok && attachments_match;
    f->width = cw;
    f->height = ch;
    f->dirty = false;
}

void NotifyTextureStorageChanged(GLuint texture) {
    for (auto& entry : g_framebuffers) {
        FbState& framebuffer = entry.second;
        bool references_texture = framebuffer.has_depth &&
            framebuffer.depth.present && framebuffer.depth.is_texture &&
            framebuffer.depth.tex_id == texture;
        for (const Attach& color : framebuffer.color)
            references_texture = references_texture ||
                (color.present && color.is_texture && color.tex_id == texture);
        if (references_texture) framebuffer.dirty = true;
    }
    if (g_bound_draw_fbo) PushVkFramebuffer(g_bound_draw_fbo);
    if (g_bound_read_fbo && g_bound_read_fbo != g_bound_draw_fbo)
        PushVkFramebuffer(g_bound_read_fbo);
}

// The framebuffer an attachment call targets (GL_FRAMEBUFFER / DRAW uses the
// draw binding; READ uses the read binding).
static GLuint AttachmentFbo(GLenum target) {
    switch (target) {
        case GL_FRAMEBUFFER:
        case GL_DRAW_FRAMEBUFFER: return g_bound_draw_fbo;
        case GL_READ_FRAMEBUFFER: return g_bound_read_fbo;
        default: return 0;
    }
}

// Map a GL attachment enum to a color slot index (-1 for depth, -2 invalid).
static int ColorAttachIndex(GLenum attachment) {
    if (attachment == GL_DEPTH_ATTACHMENT ||
        attachment == GL_DEPTH_STENCIL_ATTACHMENT)
        return -1;
    if (attachment < GL_COLOR_ATTACHMENT0) return -2;
    int i = (int)(attachment - GL_COLOR_ATTACHMENT0);
    return i < 8 ? i : -2;
}

// Attach a texture (texture == 0 => detach) to an attachment of `fbo`.
static void SetTextureAttachment(GLenum attachment, GLuint fbo, GLuint texture,
                                 GLint level, GLint layer) {
    FbState* f = FboGet(fbo);
    if (!f) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    int ci = ColorAttachIndex(attachment);
    if (ci < -1) { PUSH_ERROR(GL_INVALID_ENUM); return; }
    Attach& a = ci < 0 ? f->depth : f->color[ci];
    a.present = texture != 0;
    a.is_texture = true;
    a.tex_id = texture;
    a.level = level;
    a.layer = layer;
    a.rbo_id = 0;
    if (ci < 0)
        f->has_depth = texture != 0;
    else if (ci + 1 > f->n_color)
        f->n_color = ci + 1;
    f->dirty = true;
    if (fbo == g_bound_draw_fbo) PushVkFramebuffer(fbo);
}

extern "C" {

// ---------------------------------------------------------------------------
// Renderbuffer objects
// ---------------------------------------------------------------------------

void APIENTRY glGenRenderbuffers(GLsizei n, GLuint* renderbuffers) {
    if (n < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (!renderbuffers || n == 0) return;
    for (GLsizei i = 0; i < n; ++i) {
        while (g_renderbuffers.count(g_next_rbo)) ++g_next_rbo;
        renderbuffers[i] = g_next_rbo++;
        g_renderbuffers.emplace(renderbuffers[i], RbState{});
    }
}

void APIENTRY glDeleteRenderbuffers(GLsizei n, const GLuint* renderbuffers) {
    if (n < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (!renderbuffers) return;
    for (GLsizei i = 0; i < n; ++i) {
        v::DestroyRenderbuffer(renderbuffers[i]);
        g_renderbuffers.erase(renderbuffers[i]);
    }
}

GLboolean APIENTRY glIsRenderbuffer(GLuint id) {
    return id && g_renderbuffers.count(id) ? GL_TRUE : GL_FALSE;
}

void APIENTRY glBindRenderbuffer(GLenum target, GLuint renderbuffer) {
    if (target != GL_RENDERBUFFER) { PUSH_ERROR(GL_INVALID_ENUM); return; }
    if (renderbuffer && !glIsRenderbuffer(renderbuffer)) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return;
    }
    g_bound_rbo = renderbuffer;
}

void APIENTRY glRenderbufferStorage(GLenum target, GLenum internalformat,
                                    GLsizei width, GLsizei height) {
    glRenderbufferStorageMultisample(target, 0, internalformat, width, height);
}

void APIENTRY glRenderbufferStorageMultisample(GLenum target, GLsizei samples,
                                               GLenum internalformat,
                                               GLsizei width, GLsizei height) {
    if (target != GL_RENDERBUFFER) { PUSH_ERROR(GL_INVALID_ENUM); return; }
    if (!g_renderbuffers.count(g_bound_rbo)) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return;
    }
    if (width < 0 || height < 0 || samples < 0) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    RbState& r = g_renderbuffers[g_bound_rbo];
    r.internalformat = internalformat;
    r.width = width;
    r.height = height;
    r.samples = samples;
    r.defined = true;
    v::CreateRenderbuffer(g_bound_rbo, internalformat, (uint32_t)width,
                          (uint32_t)height, (uint32_t)samples);
    PushVkFramebuffer(g_bound_draw_fbo);
}

void APIENTRY glGetRenderbufferParameteriv(GLenum target, GLenum pname,
                                           GLint* params) {
    if (target != GL_RENDERBUFFER) { PUSH_ERROR(GL_INVALID_ENUM); return; }
    if (!params) return;
    auto it = g_renderbuffers.find(g_bound_rbo);
    if (it == g_renderbuffers.end()) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    const RbState& r = it->second;
    switch (pname) {
        case GL_RENDERBUFFER_INTERNAL_FORMAT:
            *params = r.defined ? (GLint)r.internalformat : 0;
            break;
        case GL_RENDERBUFFER_WIDTH:  *params = r.width; break;
        case GL_RENDERBUFFER_HEIGHT: *params = r.height; break;
        case GL_RENDERBUFFER_SAMPLES: *params = r.samples; break;
        default:
            PUSH_ERROR(GL_INVALID_ENUM);
    }
}

// ---------------------------------------------------------------------------
// Framebuffer objects
// ---------------------------------------------------------------------------

void APIENTRY glGenFramebuffers(GLsizei n, GLuint* framebuffers) {
    if (n < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (!framebuffers || n == 0) return;
    for (GLsizei i = 0; i < n; ++i) {
        while (g_framebuffers.count(g_next_fbo)) ++g_next_fbo;
        framebuffers[i] = g_next_fbo++;
        g_framebuffers.emplace(framebuffers[i], FbState{});
    }
}

void APIENTRY glDeleteFramebuffers(GLsizei n, const GLuint* framebuffers) {
    if (n < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (!framebuffers) return;
    for (GLsizei i = 0; i < n; ++i) {
        if (framebuffers[i] == g_bound_draw_fbo) {
            g_bound_draw_fbo = 0;
            v::BindDrawFramebuffer(0);
        }
        if (framebuffers[i] == g_bound_read_fbo) {
            g_bound_read_fbo = 0;
            v::BindReadFramebuffer(0);
        }
        v::DestroyFramebuffer(framebuffers[i]);
        g_framebuffers.erase(framebuffers[i]);
    }
}

GLboolean APIENTRY glIsFramebuffer(GLuint name) {
    return name && g_framebuffers.count(name) ? GL_TRUE : GL_FALSE;
}

void APIENTRY glBindFramebuffer(GLenum target, GLuint framebuffer) {
    bool draw = false, read = false;
    switch (target) {
        case GL_FRAMEBUFFER: draw = read = true; break;
        case GL_DRAW_FRAMEBUFFER: draw = true; break;
        case GL_READ_FRAMEBUFFER: read = true; break;
        default:
            PUSH_ERROR(GL_INVALID_ENUM);
            return;
    }
    if (framebuffer && !glIsFramebuffer(framebuffer)) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return;
    }
    if (draw) {
        g_bound_draw_fbo = framebuffer;
        v::BindDrawFramebuffer(framebuffer);
        if (framebuffer) {
            FbState* f = FboGet(framebuffer);
            if (f) f->dirty = true;
            PushVkFramebuffer(framebuffer);
        }
    }
    if (read) {
        g_bound_read_fbo = framebuffer;
        v::BindReadFramebuffer(framebuffer);
    }
}

GLenum APIENTRY glCheckFramebufferStatus(GLenum target) {
    switch (target) {
        case GL_FRAMEBUFFER:
        case GL_DRAW_FRAMEBUFFER:
        case GL_READ_FRAMEBUFFER:
            break;
        default:
            PUSH_ERROR(GL_INVALID_ENUM);
            return 0;
    }
    GLuint id = (target == GL_READ_FRAMEBUFFER) ? g_bound_read_fbo
                                                : g_bound_draw_fbo;
    if (!id) return GL_FRAMEBUFFER_COMPLETE;
    FbState* f = FboGet(id);
    if (!f) return GL_FRAMEBUFFER_UNSUPPORTED;
    if (f->dirty) PushVkFramebuffer(id);
    return f->complete ? GL_FRAMEBUFFER_COMPLETE
                       : GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
}

// Manually chosen draw buffer on the draw FBO (GL_NONE clears colour writes).
void APIENTRY glDrawBuffer(GLenum buf) {
    glDrawBuffers(1, &buf);
}

void APIENTRY glDrawBuffers(GLsizei n, const GLenum* bufs) {
    if (n < 0 || !bufs) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    FbState* f = FboGet(g_bound_draw_fbo);
    if (!f) {
        if (n == 1 && bufs[0] == GL_BACK) return;   // default framebuffer
        PUSH_ERROR(GL_INVALID_OPERATION);
        return;
    }
    if (n > 8) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    for (GLsizei i = 0; i < n; ++i) {
        GLenum b = bufs[i];
        if (b == GL_NONE) {
            f->draw_bufs[i] = b;
            continue;
        }
        if (b == GL_BACK) { b = GL_COLOR_ATTACHMENT0; }
        if (b < GL_COLOR_ATTACHMENT0 ||
            b >= GL_COLOR_ATTACHMENT0 + (GLenum)8) {
            PUSH_ERROR(GL_INVALID_ENUM);
            return;
        }
        int ci = (int)(b - GL_COLOR_ATTACHMENT0);
        if (ci >= f->n_color) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
        f->draw_bufs[i] = b;
    }
    f->n_draw = n;
    f->dirty = true;
    if (g_bound_draw_fbo) PushVkFramebuffer(g_bound_draw_fbo);
}

// The buffer glReadPixels / blit read from (`buf` selects a colour
// attachment, GL_NONE or a back/depth buffer are not supported here).
void APIENTRY glReadBuffer(GLenum buf) {
    FbState* f = FboGet(g_bound_read_fbo);
    if (!f) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    if (buf == GL_NONE) {
        f->read_buf = buf;
        f->dirty = true;
        if (g_bound_read_fbo == g_bound_draw_fbo) PushVkFramebuffer(g_bound_read_fbo);
        return;
    }
    if (buf < GL_COLOR_ATTACHMENT0 ||
        buf >= GL_COLOR_ATTACHMENT0 + (GLenum)8) {
        PUSH_ERROR(GL_INVALID_ENUM);
        return;
    }
    int ci = (int)(buf - GL_COLOR_ATTACHMENT0);
    if (ci >= f->n_color) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    f->read_buf = buf;
    f->dirty = true;
    v::RefreshReadback();
    if (g_bound_read_fbo == g_bound_draw_fbo || g_bound_read_fbo)
        PushVkFramebuffer(g_bound_read_fbo);
}

static void AttachTexture(GLenum attachment, GLuint fbo, GLuint texture,
                          GLint level, GLint layer) {
    bool has_depth = attachment == GL_DEPTH_ATTACHMENT ||
                     attachment == GL_DEPTH_STENCIL_ATTACHMENT;
    if (!has_depth && ColorAttachIndex(attachment) < 0) {
        PUSH_ERROR(GL_INVALID_ENUM);
        return;
    }
    SetTextureAttachment(attachment, fbo, texture, level, layer);
}

void APIENTRY glFramebufferTexture2D(GLenum target, GLenum attachment,
                                     GLenum textarget, GLuint texture,
                                     GLint level) {
    if (target != GL_FRAMEBUFFER && target != GL_DRAW_FRAMEBUFFER &&
        target != GL_READ_FRAMEBUFFER) {
        PUSH_ERROR(GL_INVALID_ENUM);
        return;
    }
    if (level < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    GLint layer = 0;
    GLenum object_target = textarget;
    if (textarget >= GL_TEXTURE_CUBE_MAP_POSITIVE_X &&
        textarget <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z) {
        layer = textarget - (GLint)GL_TEXTURE_CUBE_MAP_POSITIVE_X;
        object_target = GL_TEXTURE_CUBE_MAP;
        textarget = GL_TEXTURE_2D;
    }
    if (texture && textarget != GL_TEXTURE_2D &&
        textarget != GL_TEXTURE_2D_MULTISAMPLE) {
        PUSH_ERROR(GL_INVALID_ENUM);
        return;
    }
    if (texture) {
        auto object = g_textures.find(texture);
        if (object == g_textures.end() || object->second.target != object_target) {
            PUSH_ERROR(GL_INVALID_OPERATION);
            return;
        }
    }
    GLuint fbo = AttachmentFbo(target);
    if (texture && !fbo) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    AttachTexture(attachment, fbo, texture, level, layer);
}

void APIENTRY glFramebufferTextureLayer(GLenum target, GLenum attachment,
                                        GLuint texture, GLint level,
                                        GLint layer) {
    if (target != GL_FRAMEBUFFER && target != GL_DRAW_FRAMEBUFFER &&
        target != GL_READ_FRAMEBUFFER) {
        PUSH_ERROR(GL_INVALID_ENUM);
        return;
    }
    if (level < 0 || layer < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    GLuint fbo = AttachmentFbo(target);
    if (texture && !fbo) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    AttachTexture(attachment, fbo, texture, level, layer);
}

void APIENTRY glFramebufferTexture(GLenum target, GLenum attachment,
                                   GLuint texture, GLint level) {
    if (level < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    GLuint fbo = AttachmentFbo(target);
    if (texture && !fbo) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    AttachTexture(attachment, fbo, texture, level, 0);
}

void APIENTRY glFramebufferTexture1D(GLenum target, GLenum attachment,
                                     GLenum textarget, GLuint texture,
                                     GLint level) {
    if (textarget != GL_TEXTURE_1D) { PUSH_ERROR(GL_INVALID_ENUM); return; }
    glFramebufferTexture(target, attachment, texture, level);
}

void APIENTRY glFramebufferTexture3D(GLenum target, GLenum attachment,
                                     GLenum textarget, GLuint texture,
                                     GLint level, GLint zoffset) {
    if (textarget != GL_TEXTURE_3D) { PUSH_ERROR(GL_INVALID_ENUM); return; }
    if (zoffset < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    GLuint fbo = AttachmentFbo(target);
    if (texture && !fbo) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    AttachTexture(attachment, fbo, texture, level, zoffset);
}

void APIENTRY glFramebufferRenderbuffer(GLenum target, GLenum attachment,
                                        GLenum renderbuffertarget,
                                        GLuint renderbuffer) {
    if (target != GL_FRAMEBUFFER && target != GL_DRAW_FRAMEBUFFER &&
        target != GL_READ_FRAMEBUFFER) {
        PUSH_ERROR(GL_INVALID_ENUM);
        return;
    }
    if (renderbuffertarget != GL_RENDERBUFFER) {
        PUSH_ERROR(GL_INVALID_ENUM);
        return;
    }
    if (renderbuffer && !glIsRenderbuffer(renderbuffer)) {
        PUSH_ERROR(GL_INVALID_OPERATION);
        return;
    }
int ci = ColorAttachIndex(attachment);
    if (ci < -1) {
        PUSH_ERROR(GL_INVALID_ENUM);
        return;
    }
    GLuint fbo = AttachmentFbo(target);
    if (renderbuffer && !fbo) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    FbState* f = FboGet(fbo);
    if (!f) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    Attach& a = ci < 0 ? f->depth : f->color[ci];
    a.present = renderbuffer != 0;
    a.is_texture = false;
    a.rbo_id = renderbuffer;
    a.tex_id = 0;
    if (ci < 0)
        f->has_depth = renderbuffer != 0;
    else if (ci + 1 > f->n_color)
        f->n_color = ci + 1;
    f->dirty = true;
    if (fbo == g_bound_draw_fbo) PushVkFramebuffer(fbo);
}

void APIENTRY glGetFramebufferAttachmentParameteriv(GLenum target,
                                                    GLenum attachment,
                                                    GLenum pname,
                                                    GLint* params) {
    if (target != GL_FRAMEBUFFER && target != GL_DRAW_FRAMEBUFFER &&
        target != GL_READ_FRAMEBUFFER) {
        PUSH_ERROR(GL_INVALID_ENUM);
        return;
    }
    if (!params) return;
    int ci = ColorAttachIndex(attachment);
    if (ci < -1) { PUSH_ERROR(GL_INVALID_ENUM); return; }
    GLuint id = AttachmentFbo(target);
    const FbState* f = FboGet(id);
    if (!f) {
        if (pname == GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE)
            *params = GL_FRAMEBUFFER_DEFAULT;
        else
            *params = 0;
        return;
    }
    const Attach& a = ci < 0 ? f->depth : f->color[ci];
    switch (pname) {
        case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE:
            *params = a.present ? (a.is_texture ? GL_TEXTURE : GL_RENDERBUFFER)
                                : GL_NONE;
            break;
        case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME:
            *params = a.present ? (GLint)(a.is_texture ? a.tex_id : a.rbo_id)
                                : 0;
            break;
        case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL:
            *params = a.is_texture ? a.level : 0;
            break;
        case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE:
            *params = (a.is_texture && a.layer >= 0 && a.layer < 6)
                          ? (GLint)(GL_TEXTURE_CUBE_MAP_POSITIVE_X + a.layer)
                          : 0;
            break;
        default:
            PUSH_ERROR(GL_INVALID_ENUM);
    }
}

// ---------------------------------------------------------------------------
// Framebuffer blit
// ---------------------------------------------------------------------------

void APIENTRY glBlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1,
                                GLint srcY1, GLint dstX0, GLint dstY0,
                                GLint dstX1, GLint dstY1, GLbitfield mask,
                                GLenum filter) {
    if (filter != GL_NEAREST && filter != GL_LINEAR) {
        PUSH_ERROR(GL_INVALID_ENUM);
        return;
    }
    if (mask & ~(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
                 GL_STENCIL_BUFFER_BIT)) {
        PUSH_ERROR(GL_INVALID_VALUE);
        return;
    }
    if (srcX0 == srcX1 || srcY0 == srcY1 || dstX0 == dstX1 || dstY0 == dstY1)
        return;   // zero-area blit is a no-op
    v::SubmitFlush(true);
    v::BlitFramebuffer(g_bound_read_fbo, g_bound_draw_fbo,
                       srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1,
                       mask, filter);
}

} // extern "C"
