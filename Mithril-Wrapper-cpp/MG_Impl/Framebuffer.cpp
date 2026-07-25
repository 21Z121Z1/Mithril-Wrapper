// Mithril-Wrapper - MG_Impl/Framebuffer.cpp
// Framebuffer objects and attachment resolution.
//
// This is the Vulkan/MoltenVK rewrite of the former gl/framebuffer.cpp. The
// attachment resolution (collect_draw_fbo_attachments) now returns Vulkan
// VkImageView handles — the swapchain image views for the EGL default
// framebuffer (FBO 0) and backend_get_texture_view() for user FBO color/depth
// attachments — so the drawing path can build the dynamic-rendering attachment
// list.
#include "includes.h"
#include "Framebuffer.h"

extern "C" {

void glGenFramebuffers(GLsizei n, GLuint* framebuffers) {
    MITHRIL_ENSURE_INIT();
    if (n <= 0 || !framebuffers) return;
    for (GLsizei i = 0; i < n; ++i) {
        GLuint name = g_state->nextName++;
        g_state->framebuffers[name] = mithril::Framebuffer{};
        g_state->framebuffers[name].id = name;
        g_state->framebuffers[name].drawBuffers[0] = GL_COLOR_ATTACHMENT0;
        g_state->framebuffers[name].drawBufferCount = 1;
        g_state->framebuffers[name].readBuffer = GL_COLOR_ATTACHMENT0;
        framebuffers[i] = name;
    }
}

void glDeleteFramebuffers(GLsizei n, const GLuint* framebuffers) {
    MITHRIL_ENSURE_INIT();
    if (n <= 0 || !framebuffers) return;
    for (GLsizei i = 0; i < n; ++i) {
        GLuint name = framebuffers[i];
        if (name == 0) continue;
        if (g_state->currentDrawFBO == name) g_state->currentDrawFBO = 0;
        if (g_state->currentReadFBO == name) g_state->currentReadFBO = 0;
        g_state->framebuffers.erase(name);
    }
}

void glBindFramebuffer(GLenum target, GLuint framebuffer) {
    MITHRIL_ENSURE_INIT();
    if (framebuffer != 0 && g_state->framebuffers.find(framebuffer) == g_state->framebuffers.end()) {
        g_state->framebuffers[framebuffer] = mithril::Framebuffer{};
        g_state->framebuffers[framebuffer].id = framebuffer;
        g_state->framebuffers[framebuffer].drawBuffers[0] = GL_COLOR_ATTACHMENT0;
        g_state->framebuffers[framebuffer].drawBufferCount = 1;
        g_state->framebuffers[framebuffer].readBuffer = GL_COLOR_ATTACHMENT0;
    }
    if (target == GL_READ_FRAMEBUFFER || target == GL_FRAMEBUFFER) {
        g_state->currentReadFBO = framebuffer;
    }
    if (target == GL_DRAW_FRAMEBUFFER || target == GL_FRAMEBUFFER) {
        g_state->currentDrawFBO = framebuffer;
    }
}

void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget,
                            GLuint texture, GLint level) {
    MITHRIL_ENSURE_INIT();
    mithril::Framebuffer* fbo = (target == GL_READ_FRAMEBUFFER)
        ? mithril::state_get_framebuffer(g_state->currentReadFBO)
        : mithril::state_get_framebuffer(g_state->currentDrawFBO);
    if (!fbo) return;
    mithril::FBOAttachment a{};
    a.texture = texture;
    a.textarget = textarget;
    a.level = level;
    if (attachment >= GL_COLOR_ATTACHMENT0 && attachment <= GL_COLOR_ATTACHMENT7) {
        fbo->colors[attachment - GL_COLOR_ATTACHMENT0] = a;
    } else if (attachment == GL_DEPTH_ATTACHMENT) {
        fbo->depth = a;
    } else if (attachment == GL_STENCIL_ATTACHMENT) {
        fbo->stencil = a;
    } else if (attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
        fbo->depth = a; fbo->stencil = a;
    }
}

void glFramebufferTextureLayer(GLenum target, GLenum attachment, GLuint texture,
                               GLint level, GLint layer) {
    MITHRIL_ENSURE_INIT();
    mithril::Framebuffer* fbo = (target == GL_READ_FRAMEBUFFER)
        ? mithril::state_get_framebuffer(g_state->currentReadFBO)
        : mithril::state_get_framebuffer(g_state->currentDrawFBO);
    if (!fbo) return;
    mithril::FBOAttachment a{};
    a.texture = texture;
    a.level = level;
    a.layer = layer;
    a.layered = true;
    if (attachment >= GL_COLOR_ATTACHMENT0 && attachment <= GL_COLOR_ATTACHMENT7) {
        fbo->colors[attachment - GL_COLOR_ATTACHMENT0] = a;
    } else if (attachment == GL_DEPTH_ATTACHMENT) {
        fbo->depth = a;
    } else if (attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
        fbo->depth = a; fbo->stencil = a;
    }
}

void glFramebufferTexture(GLenum target, GLenum attachment, GLuint texture, GLint level) {
    MITHRIL_ENSURE_INIT();
    mithril::Framebuffer* fbo = (target == GL_READ_FRAMEBUFFER)
        ? mithril::state_get_framebuffer(g_state->currentReadFBO)
        : mithril::state_get_framebuffer(g_state->currentDrawFBO);
    if (!fbo) return;
    mithril::FBOAttachment a{};
    a.texture = texture;
    a.level = level;
    a.layered = true;
    if (attachment >= GL_COLOR_ATTACHMENT0 && attachment <= GL_COLOR_ATTACHMENT7) {
        fbo->colors[attachment - GL_COLOR_ATTACHMENT0] = a;
    } else if (attachment == GL_DEPTH_ATTACHMENT) {
        fbo->depth = a;
    } else if (attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
        fbo->depth = a; fbo->stencil = a;
    }
}

void glDrawBuffers(GLsizei n, const GLenum* bufs) {
    MITHRIL_ENSURE_INIT();
    mithril::Framebuffer* fbo = mithril::state_get_framebuffer(g_state->currentDrawFBO);
    if (!fbo || n <= 0 || !bufs) return;
    for (int i = 0; i < 8; ++i) fbo->drawBuffers[i] = GL_NONE;
    fbo->drawBufferCount = 0;
    for (GLsizei i = 0; i < n && i < 8; ++i) {
        fbo->drawBuffers[i] = bufs[i];
        if (bufs[i] != GL_NONE) fbo->drawBufferCount = i + 1;
    }
}

void glDrawBuffer(GLenum mode) {
    MITHRIL_ENSURE_INIT();
    mithril::Framebuffer* fbo = mithril::state_get_framebuffer(g_state->currentDrawFBO);
    if (!fbo) return;
    for (int i = 0; i < 8; ++i) fbo->drawBuffers[i] = GL_NONE;
    if (mode == GL_FRONT || mode == GL_BACK || mode == GL_NONE) {
        fbo->drawBuffers[0] = (mode == GL_NONE) ? GL_NONE : GL_COLOR_ATTACHMENT0;
        fbo->drawBufferCount = (mode == GL_NONE) ? 0 : 1;
    } else if (mode >= GL_COLOR_ATTACHMENT0 && mode <= GL_COLOR_ATTACHMENT7) {
        fbo->drawBuffers[0] = mode;
        fbo->drawBufferCount = 1;
    }
}

void glReadBuffer(GLenum mode) {
    MITHRIL_ENSURE_INIT();
    mithril::Framebuffer* fbo = mithril::state_get_framebuffer(g_state->currentReadFBO);
    if (fbo) fbo->readBuffer = mode;
}

GLenum glCheckFramebufferStatus(GLenum target) {
    MITHRIL_ENSURE_INIT();
    return GL_FRAMEBUFFER_COMPLETE;
}

void glBlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                       GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                       GLbitfield mask, GLenum filter) {
    MITHRIL_ENSURE_INIT();

    // Only colour blits are implemented (depth/stencil blits are rare in MC
    // Java's modern pipeline and require NEAREST filtering + aspect masks).
    if (!(mask & GL_COLOR_BUFFER_BIT)) return;

    // Flush any pending rendering into the source/destination so the blit
    // sees the latest pixels and subsequent draws see the blit's result.
    backend_end_render_pass();
    backend_commit();

    // Resolve the source FBO's colour attachment. The read FBO is the source.
    //   - FBO 0 (EGL default): use the swapchain image installed on g_state.
    //   - User FBO: use the texture attached to GL_COLOR_ATTACHMENT0 (or the
    //     buffer selected by glReadBuffer, but MC Java only uses attachment 0).
    VkImage src_image = VK_NULL_HANDLE;
    VkFormat src_format = VK_FORMAT_UNDEFINED;
    if (g_state->currentReadFBO == 0) {
        src_image  = g_state->eglDefaultColorImage;
        src_format = g_state->eglDefaultColorFormat;
    } else {
        mithril::Framebuffer* fbo = mithril::state_get_framebuffer(g_state->currentReadFBO);
        if (fbo) {
            GLuint tex = 0;
            // glReadBuffer selects which color attachment is the read source.
            // Default is GL_COLOR_ATTACHMENT0. MC Java doesn't change this.
            if (g_state->currentReadFBO == g_state->currentDrawFBO &&
                fbo->readBuffer >= GL_COLOR_ATTACHMENT0 && fbo->readBuffer <= GL_COLOR_ATTACHMENT7) {
                tex = fbo->colors[fbo->readBuffer - GL_COLOR_ATTACHMENT0].texture;
            } else if (fbo->readBuffer >= GL_COLOR_ATTACHMENT0 && fbo->readBuffer <= GL_COLOR_ATTACHMENT7) {
                tex = fbo->colors[fbo->readBuffer - GL_COLOR_ATTACHMENT0].texture;
            } else {
                tex = fbo->colors[0].texture;
            }
            if (tex) {
                src_image = backend_get_texture_image(tex);
                mithril::Texture* t = mithril::state_get_texture(tex);
                if (t) src_format = backend_vk_format_for_gl((GLenum)t->internalFormat);
            }
        }
    }

    // Resolve the destination FBO's colour attachment. The draw FBO is the
    // destination.
    VkImage dst_image = VK_NULL_HANDLE;
    VkFormat dst_format = VK_FORMAT_UNDEFINED;
    if (g_state->currentDrawFBO == 0) {
        dst_image  = g_state->eglDefaultColorImage;
        dst_format = g_state->eglDefaultColorFormat;
    } else {
        mithril::Framebuffer* fbo = mithril::state_get_framebuffer(g_state->currentDrawFBO);
        if (fbo) {
            GLuint tex = fbo->colors[0].texture;
            if (tex) {
                dst_image = backend_get_texture_image(tex);
                mithril::Texture* t = mithril::state_get_texture(tex);
                if (t) dst_format = backend_vk_format_for_gl((GLenum)t->internalFormat);
            }
        }
    }

    if (src_image == VK_NULL_HANDLE || dst_image == VK_NULL_HANDLE) return;
    if (src_format == VK_FORMAT_UNDEFINED) src_format = VK_FORMAT_R8G8B8A8_UNORM;
    if (dst_format == VK_FORMAT_UNDEFINED) dst_format = VK_FORMAT_R8G8B8A8_UNORM;

    // GL's framebuffer origin is bottom-left; Vulkan's is top-left. MoltenVK
    // flips the Y axis when translating to Metal, so passing the GL coordinates
    // through unchanged matches the on-screen behaviour expected by the host
    // app (this mirrors how backend_set_viewport handles the Y flip).
    backend_blit_images(src_image, src_format,
                        dst_image, dst_format,
                        srcX0, srcY0, srcX1, srcY1,
                        dstX0, dstY0, dstX1, dstY1,
                        mask, filter);
}

/* Renderbuffers are minimally supported (used rarely by MC Java). */
void glGenRenderbuffers(GLsizei n, GLuint* rbs) {
    MITHRIL_ENSURE_INIT();
    if (n <= 0 || !rbs) return;
    for (GLsizei i = 0; i < n; ++i) rbs[i] = g_state->nextName++;
}
void glDeleteRenderbuffers(GLsizei, const GLuint*) { MITHRIL_ENSURE_INIT(); }
void glBindRenderbuffer(GLenum, GLuint) { MITHRIL_ENSURE_INIT(); }
void glRenderbufferStorage(GLenum, GLenum, GLsizei, GLsizei) { MITHRIL_ENSURE_INIT(); }
void glRenderbufferStorageMultisample(GLenum, GLsizei, GLenum, GLsizei, GLsizei) { MITHRIL_ENSURE_INIT(); }
void glFramebufferRenderbuffer(GLenum, GLenum, GLenum, GLuint) { MITHRIL_ENSURE_INIT(); }

} // extern "C"

namespace mithril {

int collect_draw_fbo_attachments(VkImageView out_color[8], VkImageView* out_depth,
                                 int* out_w, int* out_h) {
    for (int i = 0; i < 8; ++i) out_color[i] = VK_NULL_HANDLE;
    *out_depth = VK_NULL_HANDLE;
    if (out_w) *out_w = g_state->viewportW;
    if (out_h) *out_h = g_state->viewportH;

    /*
     * EGL-backed default framebuffer: when an EGLSurface is current, the
     * swapchain image's VkImageView is installed on the GLState. GL commands
     * against framebuffer 0 render straight into the on-screen drawable. EGL
     * swaps the image view per-frame (eglSwapBuffers acquires the next
     * swapchain image and replaces this handle).
     */
    if (g_state->currentDrawFBO == 0 && g_state->eglDefaultColor != VK_NULL_HANDLE) {
        out_color[0] = g_state->eglDefaultColor;
        if (g_state->eglDefaultDepth != VK_NULL_HANDLE) *out_depth = g_state->eglDefaultDepth;
        if (g_state->eglDefaultWidth > 0 && out_w) *out_w = g_state->eglDefaultWidth;
        if (g_state->eglDefaultHeight > 0 && out_h) *out_h = g_state->eglDefaultHeight;
        return 1;
    }

    Framebuffer* fbo = state_get_framebuffer(g_state->currentDrawFBO);
    if (!fbo) return 0;

    int count = 0;
    int w = 0, h = 0;
    for (int i = 0; i < 8; ++i) {
        if (i >= fbo->drawBufferCount) break;
        if (fbo->drawBuffers[i] == GL_NONE) break;
        GLuint tex = fbo->colors[i].texture;
        if (tex == 0) { out_color[i] = VK_NULL_HANDLE; continue; }
        VkImageView view = backend_get_texture_view(tex);
        out_color[i] = view;
        if (view != VK_NULL_HANDLE) { count = i + 1; }
        if (w == 0) {
            Texture* t = state_get_texture(tex);
            if (t) { w = t->width; h = t->height; }
        }
    }
    if (fbo->depth.texture) {
        *out_depth = backend_get_texture_view(fbo->depth.texture);
        if (w == 0) {
            Texture* t = state_get_texture(fbo->depth.texture);
            if (t) { w = t->width; h = t->height; }
        }
    }
    if (w > 0 && out_w) *out_w = w;
    if (h > 0 && out_h) *out_h = h;
    return count;
}

} // namespace mithril
