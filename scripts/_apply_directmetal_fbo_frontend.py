#!/usr/bin/env python3
from pathlib import Path

p = Path("src/gl/fbo.cpp")
s = p.read_text()

def rep(old, new, label):
    global s
    n = s.count(old)
    if n != 1:
        raise SystemExit(f"{label}: expected 1 match, got {n}")
    s = s.replace(old, new)

rep('''// Size of a single attachment at the resolved size; empty attachments (or
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
''', '''static bool TextureSubresourceValid(const Attach& attachment) {
    if (!attachment.present || !attachment.is_texture ||
        attachment.level < 0 || attachment.layer < 0)
        return false;
    auto found = g_textures.find(attachment.tex_id);
    if (found == g_textures.end()) return false;
    const TexState& texture = found->second;
    if (!texture.has_image || texture.width == 0 || texture.height == 0 ||
        texture.target == GL_TEXTURE_BUFFER)
        return false;
    if (texture.target == GL_TEXTURE_2D_MULTISAMPLE)
        return attachment.level == 0 && attachment.layer == 0;
    if (static_cast<size_t>(attachment.level) >= texture.mip.size()) return false;
    switch (texture.target) {
        case GL_TEXTURE_CUBE_MAP:
            return attachment.layer < 6;
        case GL_TEXTURE_3D: {
            const uint32_t depth = std::max<uint32_t>(
                1, texture.depth >> static_cast<uint32_t>(attachment.level));
            return static_cast<uint32_t>(attachment.layer) < depth;
        }
        case GL_TEXTURE_1D_ARRAY:
        case GL_TEXTURE_2D_ARRAY:
            return static_cast<uint32_t>(attachment.layer) < texture.depth;
        default:
            return attachment.layer == 0;
    }
}

// Size of a single selected attachment image. Invalid mip/slice/depth-plane
// selections fail completeness instead of silently falling back to level 0.
static bool AttachDimensions(const Attach& a, GLsizei* w, GLsizei* h) {
    if (!a.present) return false;
    if (a.is_texture) {
        if (!TextureSubresourceValid(a)) return false;
        const TexState& t = g_textures.at(a.tex_id);
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
''', "subresource validation")

rep('''    if (attachment.is_texture) {
        auto texture = g_textures.find(attachment.tex_id);
        if (texture == g_textures.end() || !texture->second.has_image)
            return false;
        *samples = static_cast<GLsizei>(texture->second.samples);
        return true;
    }
''', '''    if (attachment.is_texture) {
        if (!TextureSubresourceValid(attachment)) return false;
        const TexState& texture = g_textures.at(attachment.tex_id);
        *samples = static_cast<GLsizei>(texture.samples);
        return true;
    }
''', "sample validation")

rep('''    if (attachment.is_texture) {
        auto texture = g_textures.find(attachment.tex_id);
        if (texture == g_textures.end() || !texture->second.has_image)
            return false;
        *is_depth = texture->second.image_backend_format ==
                    v::TexelFormat::Depth32Float;
        return true;
    }
''', '''    if (attachment.is_texture) {
        if (!TextureSubresourceValid(attachment)) return false;
        *is_depth = g_textures.at(attachment.tex_id).image_backend_format ==
                    v::TexelFormat::Depth32Float;
        return true;
    }
''', "depth validation")

rep('''// Push the current attachments into the Vulkan engine (idempotent unless the
// FBO changed); marks `complete` for glCheckFramebufferStatus. A GL FBO must
// have at least one colour attachment to be renderable (the backend has a
// color target), so colour-less FBOs stay incomplete.
''', '''// Push the current attachments into the selected backend and cache the GL
// completeness decision. Depth-only FBOs are valid when draw/read selectors
// are GL_NONE; dimensions and sample counts come from the first live image.
''', "comment")

rep('''    if (a.is_texture) {
        auto it = g_textures.find(a.tex_id);
        if (it == g_textures.end() || it->second.width == 0) return false;
        out->is_texture = true;
        out->tex_id = a.tex_id;
        out->level = (uint32_t)std::max<GLint>(0, a.level);
        out->layer = (uint32_t)std::max<GLint>(0, a.layer);
''', '''    if (a.is_texture) {
        if (!TextureSubresourceValid(a)) return false;
        out->is_texture = true;
        out->tex_id = a.tex_id;
        out->level = static_cast<uint32_t>(a.level);
        out->layer = static_cast<uint32_t>(a.layer);
''', "FBO material")

start = s.index("static void PushVkFramebuffer(GLuint id) {")
end = s.index("\nvoid NotifyTextureStorageChanged", start)
new_body = '''static bool ColorBufferHasAttachment(const FbState& framebuffer, GLenum buffer) {
    if (buffer == GL_NONE) return true;
    if (buffer < GL_COLOR_ATTACHMENT0 ||
        buffer >= GL_COLOR_ATTACHMENT0 + (GLenum)8)
        return false;
    const int index = static_cast<int>(buffer - GL_COLOR_ATTACHMENT0);
    return index < framebuffer.n_color && framebuffer.color[index].present;
}

static void PushVkFramebuffer(GLuint id) {
    FbState* f = FboGet(id);
    if (!f || !f->dirty) return;

    v::FboSpec spec;
    GLsizei fw = 0, fh = 0, framebuffer_samples = 0;
    bool have_attachment = false;
    bool attachments_match = true;

    auto merge_attachment = [&](const Attach& attachment,
                                bool expect_depth) -> bool {
        GLsizei width = 0, height = 0, samples = 0;
        bool is_depth = false;
        if (!AttachDimensions(attachment, &width, &height) ||
            !AttachSampleCount(attachment, &samples) ||
            !AttachIsDepth(attachment, &is_depth) ||
            is_depth != expect_depth)
            return false;
        if (!have_attachment) {
            fw = width;
            fh = height;
            framebuffer_samples = samples;
            have_attachment = true;
            return true;
        }
        return width == fw && height == fh && samples == framebuffer_samples;
    };

    for (int i = 0; i < f->n_color; ++i) {
        v::FboAttach color;
        if (!f->color[i].present) {
            spec.color.push_back(color);
            continue;
        }
        if (!FillAttach(f->color[i], &color) ||
            !merge_attachment(f->color[i], false))
            attachments_match = false;
        spec.color.push_back(color);
    }

    if (f->has_depth && f->depth.present) {
        if (FillAttach(f->depth, &spec.depth) &&
            merge_attachment(f->depth, true)) {
            spec.has_depth = true;
        } else {
            attachments_match = false;
        }
    }

    spec.read_buf = f->read_buf;
    bool selectors_valid = ColorBufferHasAttachment(*f, f->read_buf);
    for (int i = 0; i < f->n_draw; ++i) {
        spec.draw_bufs.push_back(f->draw_bufs[i]);
        selectors_valid = selectors_valid &&
                          ColorBufferHasAttachment(*f, f->draw_bufs[i]);
    }

    const bool complete = have_attachment && attachments_match && selectors_valid;
    if (complete) {
        spec.width = fw;
        spec.height = fh;
    }

    v::SetFramebuffer(id, spec);
    f->complete = complete;
    f->width = have_attachment ? fw : 0;
    f->height = have_attachment ? fh : 0;
    f->dirty = false;
}
'''
s = s[:start] + new_body + s[end:]

rep('''    if (level < 0 || layer < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    GLuint fbo = AttachmentFbo(target);
''', '''    if (level < 0 || layer < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (texture) {
        auto object = g_textures.find(texture);
        if (object == g_textures.end() ||
            (object->second.target != GL_TEXTURE_3D &&
             object->second.target != GL_TEXTURE_1D_ARRAY &&
             object->second.target != GL_TEXTURE_2D_ARRAY)) {
            PUSH_ERROR(GL_INVALID_OPERATION);
            return;
        }
    }
    GLuint fbo = AttachmentFbo(target);
''', "glFramebufferTextureLayer")

rep('''void APIENTRY glFramebufferTexture(GLenum target, GLenum attachment,
                                   GLuint texture, GLint level) {
    if (level < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    GLuint fbo = AttachmentFbo(target);
    if (texture && !fbo) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    AttachTexture(attachment, fbo, texture, level, 0);
}
''', '''void APIENTRY glFramebufferTexture(GLenum target, GLenum attachment,
                                   GLuint texture, GLint level) {
    if (target != GL_FRAMEBUFFER && target != GL_DRAW_FRAMEBUFFER &&
        target != GL_READ_FRAMEBUFFER) {
        PUSH_ERROR(GL_INVALID_ENUM);
        return;
    }
    if (level < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (texture) {
        auto object = g_textures.find(texture);
        if (object == g_textures.end()) {
            PUSH_ERROR(GL_INVALID_OPERATION);
            return;
        }
        // Whole-level layered rendering is a separate milestone. Reject it
        // instead of silently treating an array/cube/3D texture as layer zero.
        if (object->second.IsCube() || object->second.Is3D() ||
            object->second.target == GL_TEXTURE_1D_ARRAY ||
            object->second.target == GL_TEXTURE_2D_ARRAY) {
            PUSH_ERROR(GL_INVALID_OPERATION);
            return;
        }
    }
    GLuint fbo = AttachmentFbo(target);
    if (texture && !fbo) { PUSH_ERROR(GL_INVALID_OPERATION); return; }
    AttachTexture(attachment, fbo, texture, level, 0);
}
''', "glFramebufferTexture")

rep('''    if (textarget != GL_TEXTURE_3D) { PUSH_ERROR(GL_INVALID_ENUM); return; }
    if (zoffset < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    GLuint fbo = AttachmentFbo(target);
''', '''    if (target != GL_FRAMEBUFFER && target != GL_DRAW_FRAMEBUFFER &&
        target != GL_READ_FRAMEBUFFER) {
        PUSH_ERROR(GL_INVALID_ENUM);
        return;
    }
    if (textarget != GL_TEXTURE_3D) { PUSH_ERROR(GL_INVALID_ENUM); return; }
    if (level < 0 || zoffset < 0) { PUSH_ERROR(GL_INVALID_VALUE); return; }
    if (texture) {
        auto object = g_textures.find(texture);
        if (object == g_textures.end() || object->second.target != GL_TEXTURE_3D) {
            PUSH_ERROR(GL_INVALID_OPERATION);
            return;
        }
    }
    GLuint fbo = AttachmentFbo(target);
''', "glFramebufferTexture3D")

s = s.replace("\nint ci = ColorAttachIndex(attachment);\n",
              "\n    int ci = ColorAttachIndex(attachment);\n", 1)
p.write_text(s)
