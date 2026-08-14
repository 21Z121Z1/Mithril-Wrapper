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
#include "../MG_Backend/DirectVulkan/Resources.h"

// Standard GL 3.3 Core framebuffer/renderbuffer/query enums that the project's
// minimal glcorearb.h does not define. Guarded so a fuller header is harmless.
#ifndef GL_FRONT_LEFT
#define GL_FRONT_LEFT                       0x0400
#endif
#ifndef GL_FRONT_RIGHT
#define GL_FRONT_RIGHT                      0x0401
#endif
#ifndef GL_BACK_LEFT
#define GL_BACK_LEFT                        0x0402
#endif
#ifndef GL_BACK_RIGHT
#define GL_BACK_RIGHT                       0x0403
#endif
#ifndef GL_LEFT
#define GL_LEFT                             0x0406
#endif
#ifndef GL_RIGHT
#define GL_RIGHT                            0x0407
#endif
#ifndef GL_TEXTURE
#define GL_TEXTURE                          0x1702
#endif
#ifndef GL_SRGB
#define GL_SRGB                             0x8C40
#endif
#ifndef GL_RG32I
#define GL_RG32I                            0x823B
#endif
#ifndef GL_RG32UI
#define GL_RG32UI                           0x823C
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING
#define GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING      0x8210
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE
#define GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE            0x8212
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE
#define GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE          0x8213
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE
#define GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE           0x8214
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE
#define GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE          0x8215
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE
#define GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE          0x8216
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE
#define GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE        0x8217
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE
#define GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE         0x8CD0
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME
#define GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME         0x8CD1
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL
#define GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL       0x8CD2
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE
#define GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE 0x8CD3
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_LAYERED
#define GL_FRAMEBUFFER_ATTACHMENT_LAYERED             0x8DA8
#endif
#ifndef GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS
#define GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS          0x8CD9
#endif
#ifndef GL_RENDERBUFFER_WIDTH
#define GL_RENDERBUFFER_WIDTH                         0x8D42
#endif
#ifndef GL_RENDERBUFFER_HEIGHT
#define GL_RENDERBUFFER_HEIGHT                        0x8D43
#endif
#ifndef GL_RENDERBUFFER_INTERNAL_FORMAT
#define GL_RENDERBUFFER_INTERNAL_FORMAT               0x8D44
#endif
#ifndef GL_RENDERBUFFER_RED_SIZE
#define GL_RENDERBUFFER_RED_SIZE                      0x8D50
#endif
#ifndef GL_RENDERBUFFER_GREEN_SIZE
#define GL_RENDERBUFFER_GREEN_SIZE                    0x8D51
#endif
#ifndef GL_RENDERBUFFER_BLUE_SIZE
#define GL_RENDERBUFFER_BLUE_SIZE                     0x8D52
#endif
#ifndef GL_RENDERBUFFER_ALPHA_SIZE
#define GL_RENDERBUFFER_ALPHA_SIZE                    0x8D53
#endif
#ifndef GL_RENDERBUFFER_DEPTH_SIZE
#define GL_RENDERBUFFER_DEPTH_SIZE                    0x8D54
#endif
#ifndef GL_RENDERBUFFER_STENCIL_SIZE
#define GL_RENDERBUFFER_STENCIL_SIZE                  0x8D55
#endif
#ifndef GL_RENDERBUFFER_SAMPLES
#define GL_RENDERBUFFER_SAMPLES                       0x8CAB
#endif

extern "C" {

void glGenFramebuffers(GLsizei n, GLuint* framebuffers) {
    MITHRIL_ENSURE_INIT();
    if (n <= 0 || !framebuffers) return;
    mithril::state_gen_names("fbo", n, framebuffers);
    for (GLsizei i = 0; i < n; ++i) {
        mithril::Framebuffer fbo{};
        fbo.id = framebuffers[i];
        fbo.drawBuffers[0] = GL_COLOR_ATTACHMENT0;
        fbo.drawBufferCount = 1;
        fbo.readBuffer = GL_COLOR_ATTACHMENT0;
        g_state->framebuffers[framebuffers[i]] = fbo;
    }
}

GLboolean glIsFramebuffer(GLuint framebuffer) {
    if (!g_state) return GL_FALSE;
    return g_state->fboNames.valid(framebuffer) ? GL_TRUE : GL_FALSE;
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
        g_state->fboNames.release(name);
    }
}

void glBindFramebuffer(GLenum target, GLuint framebuffer) {
    MITHRIL_ENSURE_INIT();
    if (target != GL_DRAW_FRAMEBUFFER && target != GL_READ_FRAMEBUFFER &&
        target != GL_FRAMEBUFFER) {
        mithril::state_set_error(GL_INVALID_ENUM);
        return;
    }
    if (framebuffer != 0 && g_state->framebuffers.find(framebuffer) == g_state->framebuffers.end()) {
        mithril::Framebuffer fbo{};
        fbo.id = framebuffer;
        fbo.drawBuffers[0] = GL_COLOR_ATTACHMENT0;
        fbo.drawBufferCount = 1;
        fbo.readBuffer = GL_COLOR_ATTACHMENT0;
        g_state->framebuffers[framebuffer] = fbo;
    }
    const GLuint previousRead = g_state->currentReadFBO;
    const GLuint previousDraw = g_state->currentDrawFBO;
    if (target == GL_READ_FRAMEBUFFER || target == GL_FRAMEBUFFER) {
        g_state->currentReadFBO = framebuffer;
    }
    if (target == GL_DRAW_FRAMEBUFFER || target == GL_FRAMEBUFFER) {
        // Clear stale invalidate flags when switching draw FBOs to prevent
        // flags from a previous FBO's glInvalidateFramebuffer from applying
        // to the new FBO's next render pass.
        if (g_state->currentDrawFBO != framebuffer) {
            backend_set_invalidate_attachments(0, false, false);
        }
        g_state->currentDrawFBO = framebuffer;
    }
    static int bindDiag = 0;
    if (bindDiag < 64 && (previousRead != g_state->currentReadFBO ||
                          previousDraw != g_state->currentDrawFBO)) {
        MITHRIL_LOG_WARN("fbodiag", "bind target=0x%x fbo=%u read=%u->%u draw=%u->%u",
                         (unsigned)target, (unsigned)framebuffer,
                         (unsigned)previousRead, (unsigned)g_state->currentReadFBO,
                         (unsigned)previousDraw, (unsigned)g_state->currentDrawFBO);
        ++bindDiag;
    }
}

static bool fbo_validate_attachment(GLenum attachment) {
    // Accept GL_COLOR_ATTACHMENT0..N-1, GL_DEPTH, GL_STENCIL, GL_DEPTH_STENCIL.
    // Color attachments beyond the implementation max (still in the color-attachment
    // token family) and any other token are rejected with GL_INVALID_ENUM.
    if (attachment >= GL_COLOR_ATTACHMENT0 && attachment < GL_COLOR_ATTACHMENT0 + 32) {
        if (attachment >= GL_COLOR_ATTACHMENT0 + mithril::kMaxColorAttachments) {
            mithril::state_set_error(GL_INVALID_ENUM);
        }
        return attachment < GL_COLOR_ATTACHMENT0 + mithril::kMaxColorAttachments;
    }
    if (attachment == GL_DEPTH_ATTACHMENT || attachment == GL_STENCIL_ATTACHMENT ||
        attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
        return true;
    }
    mithril::state_set_error(GL_INVALID_ENUM);
    return false;
}

static mithril::Framebuffer* fbo_for_target(GLenum target) {
    if (target != GL_DRAW_FRAMEBUFFER && target != GL_READ_FRAMEBUFFER &&
        target != GL_FRAMEBUFFER) {
        mithril::state_set_error(GL_INVALID_ENUM);
        return nullptr;
    }
    GLuint id = (target == GL_READ_FRAMEBUFFER) ? g_state->currentReadFBO
                                                 : g_state->currentDrawFBO;
    return mithril::state_get_framebuffer(id);
}

void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget,
                            GLuint texture, GLint level) {
    MITHRIL_ENSURE_INIT();
    mithril::Framebuffer* fbo = fbo_for_target(target);
    if (!fbo) return;
    if (!fbo_validate_attachment(attachment)) return;
    mithril::FBOAttachment a{};
    a.texture = texture;
    a.textarget = textarget;
    a.level = level;
    if (attachment >= GL_COLOR_ATTACHMENT0 && attachment < GL_COLOR_ATTACHMENT0 + mithril::kMaxColorAttachments) {
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
    mithril::Framebuffer* fbo = fbo_for_target(target);
    if (!fbo) return;
    if (!fbo_validate_attachment(attachment)) return;
    mithril::FBOAttachment a{};
    a.texture = texture;
    a.level = level;
    a.layer = layer;
    a.layered = true;
    if (attachment >= GL_COLOR_ATTACHMENT0 && attachment < GL_COLOR_ATTACHMENT0 + mithril::kMaxColorAttachments) {
        fbo->colors[attachment - GL_COLOR_ATTACHMENT0] = a;
    } else if (attachment == GL_DEPTH_ATTACHMENT) {
        fbo->depth = a;
    } else if (attachment == GL_STENCIL_ATTACHMENT) {
        fbo->stencil = a;
    } else if (attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
        fbo->depth = a; fbo->stencil = a;
    }
}

void glFramebufferTexture(GLenum target, GLenum attachment, GLuint texture, GLint level) {
    MITHRIL_ENSURE_INIT();
    mithril::Framebuffer* fbo = fbo_for_target(target);
    if (!fbo) return;
    if (!fbo_validate_attachment(attachment)) return;
    mithril::FBOAttachment a{};
    a.texture = texture;
    a.level = level;
    a.layered = true;
    if (attachment >= GL_COLOR_ATTACHMENT0 && attachment < GL_COLOR_ATTACHMENT0 + mithril::kMaxColorAttachments) {
        fbo->colors[attachment - GL_COLOR_ATTACHMENT0] = a;
    } else if (attachment == GL_DEPTH_ATTACHMENT) {
        fbo->depth = a;
    } else if (attachment == GL_STENCIL_ATTACHMENT) {
        fbo->stencil = a;
    } else if (attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
        fbo->depth = a; fbo->stencil = a;
    }
}

void glDrawBuffers(GLsizei n, const GLenum* bufs) {
    MITHRIL_ENSURE_INIT();
    if (n < 0) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return;
    }
    if (n > mithril::kMaxColorAttachments) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return;
    }
    if (n > 0 && !bufs) {
        mithril::state_set_error(GL_INVALID_OPERATION);
        return;
    }
    // Validate per-buffer token legality based on whether the default FBO is
    // bound. Default FBO accepts GL_NONE / GL_FRONT / GL_BACK (+ LR variants);
    // user FBO accepts GL_NONE / GL_COLOR_ATTACHMENT0..N-1.
    bool isDefault = (g_state->currentDrawFBO == 0);
    for (GLsizei i = 0; i < n; ++i) {
        GLenum b = bufs[i];
        if (isDefault) {
            if (b != GL_NONE && b != GL_FRONT && b != GL_BACK &&
                b != GL_FRONT_LEFT && b != GL_FRONT_RIGHT &&
                b != GL_BACK_LEFT && b != GL_BACK_RIGHT) {
                mithril::state_set_error(GL_INVALID_ENUM);
                return;
            }
        } else {
            if (b != GL_NONE &&
                (b < GL_COLOR_ATTACHMENT0 ||
                 b >= GL_COLOR_ATTACHMENT0 + mithril::kMaxColorAttachments)) {
                mithril::state_set_error(GL_INVALID_ENUM);
                return;
            }
        }
    }
    mithril::Framebuffer* fbo = mithril::state_get_framebuffer(g_state->currentDrawFBO);
    if (!fbo) return;
    for (int i = 0; i < mithril::kMaxColorAttachments; ++i) fbo->drawBuffers[i] = GL_NONE;
    fbo->drawBufferCount = 0;
    for (GLsizei i = 0; i < n; ++i) {
        fbo->drawBuffers[i] = bufs[i];
        if (bufs[i] != GL_NONE) fbo->drawBufferCount = i + 1;
    }
}

void glDrawBuffer(GLenum mode) {
    MITHRIL_ENSURE_INIT();
    bool isDefault = (g_state->currentDrawFBO == 0);
    if (isDefault) {
        if (mode != GL_NONE && mode != GL_FRONT && mode != GL_BACK &&
            mode != GL_FRONT_LEFT && mode != GL_FRONT_RIGHT &&
            mode != GL_BACK_LEFT && mode != GL_BACK_RIGHT) {
            mithril::state_set_error(GL_INVALID_ENUM);
            return;
        }
    } else {
        if (mode != GL_NONE &&
            (mode < GL_COLOR_ATTACHMENT0 ||
             mode >= GL_COLOR_ATTACHMENT0 + mithril::kMaxColorAttachments)) {
            mithril::state_set_error(GL_INVALID_ENUM);
            return;
        }
    }
    mithril::Framebuffer* fbo = mithril::state_get_framebuffer(g_state->currentDrawFBO);
    if (!fbo) return;
    for (int i = 0; i < mithril::kMaxColorAttachments; ++i) fbo->drawBuffers[i] = GL_NONE;
    if (mode == GL_NONE) {
        fbo->drawBufferCount = 0;
    } else if (isDefault) {
        // GL_FRONT / GL_BACK on default FBO map to draw buffer 0.
        fbo->drawBuffers[0] = GL_COLOR_ATTACHMENT0;
        fbo->drawBufferCount = 1;
    } else {
        fbo->drawBuffers[0] = mode;
        fbo->drawBufferCount = 1;
    }
}

void glReadBuffer(GLenum mode) {
    MITHRIL_ENSURE_INIT();
    bool isDefault = (g_state->currentReadFBO == 0);
    if (isDefault) {
        if (mode != GL_NONE && mode != GL_FRONT && mode != GL_BACK &&
            mode != GL_LEFT && mode != GL_RIGHT &&
            mode != GL_FRONT_LEFT && mode != GL_FRONT_RIGHT &&
            mode != GL_BACK_LEFT && mode != GL_BACK_RIGHT) {
            mithril::state_set_error(GL_INVALID_ENUM);
            return;
        }
    } else {
        if (mode != GL_NONE &&
            (mode < GL_COLOR_ATTACHMENT0 ||
             mode >= GL_COLOR_ATTACHMENT0 + mithril::kMaxColorAttachments)) {
            mithril::state_set_error(GL_INVALID_ENUM);
            return;
        }
    }
    mithril::Framebuffer* fbo = mithril::state_get_framebuffer(g_state->currentReadFBO);
    if (fbo) fbo->readBuffer = mode;
}

GLenum glCheckFramebufferStatus(GLenum target) {
    MITHRIL_ENSURE_INIT();
    if (target != GL_DRAW_FRAMEBUFFER && target != GL_READ_FRAMEBUFFER &&
        target != GL_FRAMEBUFFER) {
        mithril::state_set_error(GL_INVALID_ENUM);
        return 0;
    }
    GLuint fboId = (target == GL_READ_FRAMEBUFFER) ? g_state->currentReadFBO
                                                    : g_state->currentDrawFBO;
    // Default framebuffer (EGL surface) is always complete.
    if (fboId == 0) return GL_FRAMEBUFFER_COMPLETE;
    mithril::Framebuffer* fbo = mithril::state_get_framebuffer(fboId);
    if (!fbo) return GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;

    // Walk every attachment: at least one must be present, and all attached
    // images must share the same dimensions.
    bool hasAttachment = false;
    GLsizei refW = 0, refH = 0;
    bool haveRefDims = false;

    auto checkTex = [&](GLuint tex) -> bool {
        if (tex == 0) return true;
        mithril::Texture* t = mithril::state_get_texture(tex);
        if (!t) return true;
        if (!haveRefDims) {
            refW = t->width; refH = t->height; haveRefDims = true;
        } else if (t->width != refW || t->height != refH) {
            return false;
        }
        return true;
    };
    auto checkRb = [&](GLuint rb) -> bool {
        if (rb == 0) return true;
        mithril::Renderbuffer* r = mithril::state_get_renderbuffer(rb);
        if (!r) return true;
        if (!haveRefDims) {
            refW = r->width; refH = r->height; haveRefDims = true;
        } else if (r->width != refW || r->height != refH) {
            return false;
        }
        return true;
    };

    for (int i = 0; i < mithril::kMaxColorAttachments; ++i) {
        const mithril::FBOAttachment& a = fbo->colors[i];
        if (a.texture) {
            hasAttachment = true;
            if (!checkTex(a.texture)) return GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS;
        } else if (a.renderbuffer) {
            hasAttachment = true;
            if (!checkRb(a.renderbuffer)) return GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS;
        }
    }
    if (fbo->depth.texture) {
        hasAttachment = true;
        if (!checkTex(fbo->depth.texture)) return GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS;
    } else if (fbo->depth.renderbuffer) {
        hasAttachment = true;
        if (!checkRb(fbo->depth.renderbuffer)) return GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS;
    }
    if (fbo->stencil.texture) {
        hasAttachment = true;
        if (!checkTex(fbo->stencil.texture)) return GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS;
    } else if (fbo->stencil.renderbuffer) {
        hasAttachment = true;
        if (!checkRb(fbo->stencil.renderbuffer)) return GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS;
    }

    if (!hasAttachment) return GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
    return GL_FRAMEBUFFER_COMPLETE;
}

void glBlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                       GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                       GLbitfield mask, GLenum filter) {
    MITHRIL_ENSURE_INIT();

    static int blitDiag = 0;
    const int blitNumber = blitDiag + 1;
    const bool sampleBlit =
        blitNumber <= 4 || blitNumber == 8 || blitNumber == 16 ||
        blitNumber == 24 || blitNumber == 30 || blitNumber == 32 ||
        blitNumber == 40 || blitNumber == 48 || blitNumber == 50 ||
        blitNumber == 52 || blitNumber == 56 || blitNumber == 60 ||
        blitNumber == 64 || blitNumber == 80 || blitNumber == 100 ||
        blitNumber == 120;
    if (sampleBlit) {
        const mithril::Framebuffer* readFbo =
            mithril::state_get_framebuffer(g_state->currentReadFBO);
        const mithril::Framebuffer* drawFbo =
            mithril::state_get_framebuffer(g_state->currentDrawFBO);
        MITHRIL_LOG_WARN("fbodiag",
                         "blit #%d read=%u draw=%u src=%d,%d-%d,%d dst=%d,%d-%d,%d "
                         "mask=0x%x filter=0x%x readColor=%u readDepth=%u "
                         "drawColor=%u drawDepth=%u",
                         blitNumber, (unsigned)g_state->currentReadFBO,
                         (unsigned)g_state->currentDrawFBO,
                         srcX0, srcY0, srcX1, srcY1,
                         dstX0, dstY0, dstX1, dstY1,
                         (unsigned)mask, (unsigned)filter,
                         (unsigned)(readFbo ? readFbo->colors[0].texture : 0),
                         (unsigned)(readFbo ? readFbo->depth.texture : 0),
                         (unsigned)(drawFbo ? drawFbo->colors[0].texture : 0),
                         (unsigned)(drawFbo ? drawFbo->depth.texture : 0));
    }
    ++blitDiag;

    // FIX (Iris 阴影贴图必需): depth/stencil blit 支持。Iris 的阴影贴图
    // cascade copy、深度 pre-pass 重建依赖 glBlitFramebuffer(...,
    // GL_DEPTH_BUFFER_BIT, GL_NEAREST)。原实现直接 return → 拿到未初始化的
    // depth buffer → 阴影完全错乱或消失。
    //
    // GL spec: depth/stencil blit 强制 GL_NEAREST（忽略 filter 参数）。
    // backend_blit_images 通过 aspect_for_format(format) 自动选择
    // VK_IMAGE_ASPECT_DEPTH_BIT / STENCIL_BIT，所以只需传入 depth 格式即可。
    if (mask & (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)) {
        backend_end_render_pass();
        backend_commit();

        VkImage dsrc = VK_NULL_HANDLE, ddst = VK_NULL_HANDLE;
        VkFormat dsrc_fmt = VK_FORMAT_UNDEFINED, ddst_fmt = VK_FORMAT_UNDEFINED;
        int ddst_h = 0;
        bool d_dst_default = (g_state->currentDrawFBO == 0);

        if (g_state->currentReadFBO == 0) {
            dsrc = g_state->eglDefaultDepthImage;
            dsrc_fmt = g_state->eglDefaultDepthFormat;
        } else {
            mithril::Framebuffer* sf = mithril::state_get_framebuffer(g_state->currentReadFBO);
            if (sf && sf->depth.texture) {
                dsrc = backend_get_texture_image(sf->depth.texture);
                mithril::Texture* t = mithril::state_get_texture(sf->depth.texture);
                if (t) dsrc_fmt = backend_vk_format_for_gl((GLenum)t->internalFormat);
            }
        }
        if (d_dst_default) {
            ddst = g_state->eglDefaultDepthImage;
            ddst_fmt = g_state->eglDefaultDepthFormat;
            ddst_h = g_state->eglDefaultHeight;
        } else {
            mithril::Framebuffer* df = mithril::state_get_framebuffer(g_state->currentDrawFBO);
            if (df && df->depth.texture) {
                ddst = backend_get_texture_image(df->depth.texture);
                mithril::Texture* t = mithril::state_get_texture(df->depth.texture);
                if (t) { ddst_fmt = backend_vk_format_for_gl((GLenum)t->internalFormat); ddst_h = t->height; }
            }
        }
        if (dsrc != VK_NULL_HANDLE && ddst != VK_NULL_HANDLE &&
            dsrc_fmt != VK_FORMAT_UNDEFINED && ddst_fmt != VK_FORMAT_UNDEFINED) {
            backend_blit_images(dsrc, dsrc_fmt, ddst, ddst_fmt,
                                srcX0, srcY0, srcX1, srcY1,
                                dstX0, dstY0, dstX1, dstY1,
                                mask & (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT),
                                GL_NEAREST, d_dst_default ? 1 : 0, ddst_h);
        }
        // 若同时有 color bit，继续走下面的 color 路径；否则返回。
        if (!(mask & GL_COLOR_BUFFER_BIT)) return;
        // color 路径需要重新 end_render_pass/commit（上面已做，但 color 路径
        // 自身也会做；为保持原流程不变，这里不重复，直接进入 color 解析）。
    }

    // End the source render pass, but keep the command buffer open when the
    // destination is the EGL default framebuffer.  Minecraft's normal
    // presentation path is user-FBO -> swapchain; recording the transfer in
    // the same command buffer as the source render avoids a second-submit
    // semaphore seam between the rendered texture and the drawable.
    const bool is_dst_default_fbo = (g_state->currentDrawFBO == 0);
    backend_end_render_pass();
    if (!is_dst_default_fbo) {
        backend_commit();
    }

    // Physical-device diagnostic: sample the user-FBO source after its draw
    // submit and before the swapchain blit. This separates a black draw
    // attachment from a transfer/present loss without changing the normal
    // source selection or relying on a screenshot of the final drawable.
    static int sourceProbe = 0;
    if (!is_dst_default_fbo && sourceProbe < 4 && g_state->currentReadFBO != 0) {
        unsigned char sample[8 * 8 * 4] = {};
        const int ok = backend_read_pixels(0, 0, 8, 8,
                                           GL_RGBA, GL_UNSIGNED_BYTE, sample);
        unsigned int maxChannel = 0;
        unsigned int nonBlack = 0;
        for (size_t i = 0; i < sizeof(sample); i += 4) {
            unsigned int v = sample[i] | sample[i + 1] |
                             sample[i + 2] | sample[i + 3];
            if (v > maxChannel) maxChannel = v;
            if ((sample[i] | sample[i + 1] | sample[i + 2]) > 8) ++nonBlack;
        }
        MITHRIL_LOG_WARN("fbodiag",
                         "source readback #%d ok=%d max=%u nonBlack=%u/64",
                         sourceProbe + 1, ok, maxChannel, nonBlack);
        ++sourceProbe;
    }

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
    // destination. Also capture the destination height for the Y-flip
    // computation (needed when dst is the default framebuffer — see below).
    VkImage dst_image = VK_NULL_HANDLE;
    VkFormat dst_format = VK_FORMAT_UNDEFINED;
    int dst_height = 0;
    if (is_dst_default_fbo) {
        dst_image  = g_state->eglDefaultColorImage;
        dst_format = g_state->eglDefaultColorFormat;
        dst_height = g_state->eglDefaultHeight;
    } else {
        mithril::Framebuffer* fbo = mithril::state_get_framebuffer(g_state->currentDrawFBO);
        if (fbo) {
            GLuint tex = fbo->colors[0].texture;
            if (tex) {
                dst_image = backend_get_texture_image(tex);
                mithril::Texture* t = mithril::state_get_texture(tex);
                if (t) {
                    dst_format = backend_vk_format_for_gl((GLenum)t->internalFormat);
                    dst_height = t->height;
                }
            }
        }
    }

    if (sampleBlit) {
        MITHRIL_LOG_WARN(
            "fbodiag",
            "resolve #%d srcImage=0x%llx srcFmt=%d dstImage=0x%llx dstFmt=%d "
            "readFbo=%u drawFbo=%u state=%p defaultColorView=0x%llx "
            "defaultColor=0x%llx defaultFmt=%d defaultSize=%dx%d",
            blitNumber,
            (unsigned long long)(uintptr_t)src_image,
            (int)src_format,
            (unsigned long long)(uintptr_t)dst_image,
            (int)dst_format,
            (unsigned)g_state->currentReadFBO,
            (unsigned)g_state->currentDrawFBO,
            (void*)g_state,
            (unsigned long long)(uintptr_t)g_state->eglDefaultColor,
            (unsigned long long)(uintptr_t)g_state->eglDefaultColorImage,
            (int)g_state->eglDefaultColorFormat,
            (int)g_state->eglDefaultWidth,
            (int)g_state->eglDefaultHeight);
    }
    if (src_image == VK_NULL_HANDLE || dst_image == VK_NULL_HANDLE) {
        if (sampleBlit) {
            MITHRIL_LOG_WARN("fbodiag", "resolve #%d aborted: missing image", blitNumber);
        }
        return;
    }
    if (src_format == VK_FORMAT_UNDEFINED) src_format = VK_FORMAT_R8G8B8A8_UNORM;
    if (dst_format == VK_FORMAT_UNDEFINED) dst_format = VK_FORMAT_R8G8B8A8_UNORM;

    // Y-flip handling: the draw path now flips vertex Y in the shader for
    // default-FBO rendering (MVK_CONFIG_SHADER_CONVERSION_FLIP_VERTEX_Y=0),
    // so default-FBO content is in Vulkan's top-left orientation. But blits
    // bypass the vertex shader, so blitting TO the default framebuffer needs
    // the destination Y flipped to convert GL bottom-left coords to Vulkan
    // top-left coords. User FBOs (no Y flip in draw path) keep GL orientation,
    // so their blit coords pass through unchanged. Deep reference: MobileGL
    // ApplyNativeBlitDefaultFramebufferTransform (identity branch).
    // Source Y is never flipped (MobileGL never flips src Y).
    // The source user-FBO is in its sampled layout after end_render_pass.  For
    // the inline path the acquired swapchain image is still tracked as
    // UNDEFINED until this command buffer first uses it; the transfer fully
    // overwrites the destination, so UNDEFINED is the correct discard-old
    // layout.  Non-default destinations retain the explicit one-shot path.
    const VkImageLayout src_layout =
        (g_state->currentReadFBO == 0)
            ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
            : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    const VkImageLayout dst_layout =
        is_dst_default_fbo
            ? VK_IMAGE_LAYOUT_UNDEFINED
            : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    static int layoutDiag = 0;
    if (layoutDiag < 8) {
        MITHRIL_LOG_WARN("fbodiag", "blit layouts src=%d->%d dst=%d->%d",
                         (int)src_layout, (int)src_layout,
                         (int)dst_layout, (int)dst_layout);
        ++layoutDiag;
    }
    if (is_dst_default_fbo) {
        MITHRIL_LOG_WARN("fbodiag", "inline blit srcFbo=%d dstDefault=1",
                         g_state->currentReadFBO);
        backend_record_blit_images_with_layouts(
            src_image, src_format,
            src_layout, src_layout,
            dst_image, dst_format,
            // Match MobileGL: restore the acquired swapchain image to
            // COLOR_ATTACHMENT_OPTIMAL after the transfer. backend_commit()
            // then emits the final COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR
            // barrier immediately before queue submission/present.
            dst_layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            srcX0, srcY0, srcX1, srcY1,
            dstX0, dstY0, dstX1, dstY1,
            mask, filter, 1, dst_height);
        // Submit the render commands and the inline blit together.  The
        // normal eglSwapBuffers commit becomes a no-op and present waits on
        // this single submit's render-finished semaphore.
        backend_commit();

        // Diagnostic-only post-submit readback.  The source-clear probe above
        // makes the expected source pixels deterministic; reading both images
        // before present distinguishes a failed vkCmdBlitImage from a later
        // presentation/layer problem.  Remove after the physical diagnosis.
        static int postBlitProbe = 0;
        const int postBlitFrame = postBlitProbe++;
        // Sample both the first few frames and later frames.  The first four
        // samples alone cannot explain a final black screenshot if Minecraft
        // later stops producing visible source content or a later submission
        // changes the acquired drawable.
        const bool samplePostBlit =
            postBlitFrame < 4 || postBlitFrame == 15 ||
            postBlitFrame == 30 || postBlitFrame == 32 ||
            postBlitFrame == 40 || postBlitFrame == 50 ||
            postBlitFrame == 55 || postBlitFrame == 60 ||
            postBlitFrame == 90;
        if (samplePostBlit) {
            constexpr int probeWidth = 64;
            constexpr int probeHeight = 64;
            unsigned char srcSample[probeWidth * probeHeight * 4] = {};
            unsigned char dstSample[probeWidth * probeHeight * 4] = {};
            const int srcOk = backend_debug_read_image(
                src_image, src_format, src_layout,
                probeWidth, probeHeight, srcSample);
            const int dstOk = backend_debug_read_image(
                dst_image, dst_format, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                probeWidth, probeHeight, dstSample);
            unsigned int srcMax = 0;
            unsigned int dstMax = 0;
            unsigned int srcGreen = 0;
            unsigned int dstGreen = 0;
            unsigned int srcNonBlack = 0;
            unsigned int dstNonBlack = 0;
            for (size_t i = 0; i < sizeof(srcSample); i += 4) {
                srcMax = std::max(srcMax, (unsigned int)srcSample[i] |
                                           (unsigned int)srcSample[i + 1] |
                                           (unsigned int)srcSample[i + 2]);
                dstMax = std::max(dstMax, (unsigned int)dstSample[i] |
                                           (unsigned int)dstSample[i + 1] |
                                           (unsigned int)dstSample[i + 2]);
                if ((srcSample[i] | srcSample[i + 1] | srcSample[i + 2]) > 8)
                    ++srcNonBlack;
                if ((dstSample[i] | dstSample[i + 1] | dstSample[i + 2]) > 8)
                    ++dstNonBlack;
                if (srcSample[i + 1] > 200 && srcSample[i] < 32 && srcSample[i + 2] < 32)
                    ++srcGreen;
                if (dstSample[i + 1] > 200 && dstSample[i] < 32 && dstSample[i + 2] < 32)
                    ++dstGreen;
            }
            MITHRIL_LOG_WARN("fbodiag",
                             "post-blit readback #%d srcOk=%d srcMax=%u srcGreen=%u/4096 "
                             "dstOk=%d dstMax=%u dstGreen=%u/4096 "
                             "srcNonBlack=%u/%u dstNonBlack=%u/%u "
                             "src0=%u,%u,%u,%u dst0=%u,%u,%u,%u",
                             postBlitFrame + 1, srcOk, srcMax, srcGreen,
                             dstOk, dstMax, dstGreen,
                             srcNonBlack, probeWidth * probeHeight,
                             dstNonBlack, probeWidth * probeHeight,
                             srcSample[0], srcSample[1], srcSample[2], srcSample[3],
                             dstSample[0], dstSample[1], dstSample[2], dstSample[3]);

            // The final blit always reads FBO 2, while the world transition
            // visibly records draws into FBO 3 and FBO 17.  Read those two
            // colour attachments only at the first sample and around the
            // black transition.  This is deliberately after the submit above:
            // the one-shot copy then observes the completed render pass rather
            // than racing the still-recording command buffer.
            const bool sampleIntermediate =
                postBlitFrame == 0 || postBlitFrame == 50 ||
                postBlitFrame == 55 || postBlitFrame == 60;
            if (sampleIntermediate) {
                auto readIntermediate = [&](GLuint fboId, const char* label) {
                    mithril::Framebuffer* fbo =
                        mithril::state_get_framebuffer(fboId);
                    const GLuint texId =
                        (fbo != nullptr) ? fbo->colors[0].texture : 0;
                    auto& table = mithril::vk::texture_table();
                    auto it = table.find(texId);
                    if (it == table.end() || it->second.image == VK_NULL_HANDLE) {
                        MITHRIL_LOG_WARN("fbodiag",
                                         "%s fbo=%u tex=%u image=0 entry=%d",
                                         label, (unsigned)fboId, (unsigned)texId,
                                         it != table.end() ? 1 : 0);
                        return;
                    }
                    const auto& entry = it->second;
                    const int w = std::min(probeWidth, entry.width);
                    const int h = std::min(probeHeight, entry.height);
                    unsigned char sample[probeWidth * probeHeight * 4] = {};
                    const int ok = (w > 0 && h > 0 &&
                                    entry.currentLayout != VK_IMAGE_LAYOUT_UNDEFINED)
                                       ? backend_debug_read_image(
                                             entry.image, entry.format,
                                             entry.currentLayout, w, h, sample)
                                       : 0;
                    unsigned int maxChannel = 0;
                    unsigned int nonBlack = 0;
                    for (int y = 0; y < h; ++y) {
                        for (int x = 0; x < w; ++x) {
                            const size_t i = (size_t)(y * probeWidth + x) * 4;
                            maxChannel = std::max(
                                maxChannel,
                                (unsigned int)sample[i] |
                                    (unsigned int)sample[i + 1] |
                                    (unsigned int)sample[i + 2]);
                            if ((sample[i] | sample[i + 1] | sample[i + 2]) > 8)
                                ++nonBlack;
                        }
                    }
                    MITHRIL_LOG_WARN(
                        "fbodiag",
                        "%s fbo=%u tex=%u image=0x%llx fmt=%d layout=%d "
                        "size=%dx%d ok=%d max=%u nonBlack=%u/%d "
                        "p0=%u,%u,%u,%u",
                        label, (unsigned)fboId, (unsigned)texId,
                        (unsigned long long)(uintptr_t)entry.image,
                        (int)entry.format, (int)entry.currentLayout,
                        entry.width, entry.height, ok, maxChannel, nonBlack,
                        w * h, sample[0], sample[1], sample[2], sample[3]);
                };
                readIntermediate(3, "intermediate-fbo3");
                readIntermediate(17, "intermediate-fbo17");

                // The first black world draw uses a cubemap sampler.  Read
                // the first face after the submitted frame so the shader
                // hypothesis is separated from an upload/content failure.
                static int cubeTextureDiag = 0;
                if (cubeTextureDiag < 4 && g_state != nullptr) {
                    for (const auto& texturePair : g_state->textures) {
                        const GLuint texId = texturePair.first;
                        const mithril::Texture& glTex = texturePair.second;
                        if (glTex.target != GL_TEXTURE_CUBE_MAP) continue;
                        auto it = mithril::vk::texture_table().find(texId);
                        if (it == mithril::vk::texture_table().end() ||
                            it->second.image == VK_NULL_HANDLE) {
                            continue;
                        }
                        const auto& entry = it->second;
                        const int w = std::min(probeWidth, entry.width);
                        const int h = std::min(probeHeight, entry.height);
                        for (uint32_t layer = 0; layer < 6; ++layer) {
                            unsigned char sample[probeWidth * probeHeight * 4] = {};
                            const int ok = (w > 0 && h > 0 &&
                                            entry.currentLayout != VK_IMAGE_LAYOUT_UNDEFINED)
                                               ? backend_debug_read_image_layer(
                                                     entry.image, entry.format,
                                                     entry.currentLayout, layer,
                                                     w, h, sample)
                                               : 0;
                            unsigned int maxChannel = 0;
                            unsigned int nonBlack = 0;
                            for (int y = 0; y < h; ++y) {
                                for (int x = 0; x < w; ++x) {
                                    const size_t i = (size_t)(y * probeWidth + x) * 4;
                                    maxChannel = std::max(
                                        maxChannel,
                                        (unsigned int)sample[i] |
                                            (unsigned int)sample[i + 1] |
                                            (unsigned int)sample[i + 2]);
                                    if ((sample[i] | sample[i + 1] | sample[i + 2]) > 8)
                                        ++nonBlack;
                                }
                            }
                            MITHRIL_LOG_WARN(
                                "fbodiag",
                                "cubetex tex=%u layer=%u image=0x%llx fmt=%d layout=%d "
                                "size=%dx%d ok=%d max=%u nonBlack=%u/%d "
                                "p0=%u,%u,%u,%u",
                                (unsigned)texId, (unsigned)layer,
                                (unsigned long long)(uintptr_t)entry.image,
                                (int)entry.format, (int)entry.currentLayout,
                                entry.width, entry.height, ok, maxChannel,
                                nonBlack, w * h, sample[0], sample[1],
                                sample[2], sample[3]);
                        }
                        ++cubeTextureDiag;
                        if (cubeTextureDiag >= 4) break;
                    }
                }
            }
        }
    } else {
        backend_blit_images_with_layouts(src_image, src_format,
                                         src_layout, src_layout,
                                         dst_image, dst_format,
                                         dst_layout, dst_layout,
                                         srcX0, srcY0, srcX1, srcY1,
                                         dstX0, dstY0, dstX1, dstY1,
                                         mask, filter,
                                         0, dst_height);
    }

}

/* Renderbuffers: full state-machine implementation (used rarely by MC Java,
 * but required for GL 3.3 Core completeness). Storage metadata lives in
 * g_state->renderbuffers; attachment to a FBO writes FBOAttachment.renderbuffer
 * and clears the texture field. */
void glGenRenderbuffers(GLsizei n, GLuint* rbs) {
    MITHRIL_ENSURE_INIT();
    if (n <= 0 || !rbs) return;
    mithril::state_gen_names("renderbuffer", n, rbs);
    for (GLsizei i = 0; i < n; ++i) {
        mithril::Renderbuffer rb{};
        rb.id = rbs[i];
        g_state->renderbuffers[rbs[i]] = rb;
    }
}

GLboolean glIsRenderbuffer(GLuint renderbuffer) {
    if (!g_state) return GL_FALSE;
    return g_state->renderbufferNames.valid(renderbuffer) ? GL_TRUE : GL_FALSE;
}

void glDeleteRenderbuffers(GLsizei n, const GLuint* rbs) {
    MITHRIL_ENSURE_INIT();
    if (n <= 0 || !rbs) return;
    for (GLsizei i = 0; i < n; ++i) {
        GLuint name = rbs[i];
        if (name == 0) continue;
        if (g_state->currentRenderbuffer == name) g_state->currentRenderbuffer = 0;
        g_state->renderbuffers.erase(name);
        g_state->renderbufferNames.release(name);
    }
}

void glBindRenderbuffer(GLenum target, GLuint renderbuffer) {
    MITHRIL_ENSURE_INIT();
    if (target != GL_RENDERBUFFER) {
        mithril::state_set_error(GL_INVALID_ENUM);
        return;
    }
    if (renderbuffer != 0 && !mithril::state_get_renderbuffer(renderbuffer)) {
        // Lazy-create: a name that was reserved (or even unknown) gets an
        // empty Renderbuffer entry on first bind.
        mithril::Renderbuffer rb{};
        rb.id = renderbuffer;
        g_state->renderbuffers[renderbuffer] = rb;
    }
    g_state->currentRenderbuffer = renderbuffer;
}

void glRenderbufferStorage(GLenum target, GLenum internalformat,
                           GLsizei width, GLsizei height) {
    MITHRIL_ENSURE_INIT();
    if (target != GL_RENDERBUFFER) {
        mithril::state_set_error(GL_INVALID_ENUM);
        return;
    }
    if (width < 0 || height < 0) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return;
    }
    mithril::Renderbuffer* rb = mithril::state_get_renderbuffer(g_state->currentRenderbuffer);
    if (!rb) {
        mithril::state_set_error(GL_INVALID_OPERATION);
        return;
    }
    rb->internalFormat = internalformat;
    rb->width = width;
    rb->height = height;
    rb->samples = 0;
}

void glRenderbufferStorageMultisample(GLenum target, GLsizei samples,
                                      GLenum internalformat,
                                      GLsizei width, GLsizei height) {
    MITHRIL_ENSURE_INIT();
    if (target != GL_RENDERBUFFER) {
        mithril::state_set_error(GL_INVALID_ENUM);
        return;
    }
    if (width < 0 || height < 0 || samples < 0) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return;
    }
    mithril::Renderbuffer* rb = mithril::state_get_renderbuffer(g_state->currentRenderbuffer);
    if (!rb) {
        mithril::state_set_error(GL_INVALID_OPERATION);
        return;
    }
    rb->internalFormat = internalformat;
    rb->width = width;
    rb->height = height;
    rb->samples = samples;
}

void glFramebufferRenderbuffer(GLenum target, GLenum attachment,
                               GLenum renderbuffertarget, GLuint renderbuffer) {
    MITHRIL_ENSURE_INIT();
    if (renderbuffertarget != GL_RENDERBUFFER && renderbuffer != 0) {
        mithril::state_set_error(GL_INVALID_ENUM);
        return;
    }
    mithril::Framebuffer* fbo = fbo_for_target(target);
    if (!fbo) return;
    if (!fbo_validate_attachment(attachment)) return;
    mithril::FBOAttachment a{};
    a.renderbuffer = renderbuffer;
    // Attaching a renderbuffer clears any texture previously bound to this slot.
    if (attachment >= GL_COLOR_ATTACHMENT0 && attachment < GL_COLOR_ATTACHMENT0 + mithril::kMaxColorAttachments) {
        fbo->colors[attachment - GL_COLOR_ATTACHMENT0] = a;
    } else if (attachment == GL_DEPTH_ATTACHMENT) {
        fbo->depth = a;
    } else if (attachment == GL_STENCIL_ATTACHMENT) {
        fbo->stencil = a;
    } else if (attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
        fbo->depth = a; fbo->stencil = a;
    }
}

void glGetRenderbufferParameteriv(GLenum target, GLenum pname, GLint* params) {
    MITHRIL_ENSURE_INIT();
    if (target != GL_RENDERBUFFER) {
        mithril::state_set_error(GL_INVALID_ENUM);
        return;
    }
    if (!params) return;
    mithril::Renderbuffer* rb = mithril::state_get_renderbuffer(g_state->currentRenderbuffer);
    if (!rb) {
        mithril::state_set_error(GL_INVALID_OPERATION);
        return;
    }
    switch (pname) {
        case GL_RENDERBUFFER_WIDTH:            *params = rb->width; return;
        case GL_RENDERBUFFER_HEIGHT:           *params = rb->height; return;
        case GL_RENDERBUFFER_INTERNAL_FORMAT:  *params = static_cast<GLint>(rb->internalFormat); return;
        case GL_RENDERBUFFER_SAMPLES:          *params = rb->samples; return;
        case GL_RENDERBUFFER_RED_SIZE:
        case GL_RENDERBUFFER_GREEN_SIZE:
        case GL_RENDERBUFFER_BLUE_SIZE:
        case GL_RENDERBUFFER_ALPHA_SIZE:
        case GL_RENDERBUFFER_DEPTH_SIZE:
        case GL_RENDERBUFFER_STENCIL_SIZE:
            // Best-effort: 8 bits for the typical 8-bit formats, 0 otherwise.
            *params = 8;
            return;
        default:
            mithril::state_set_error(GL_INVALID_ENUM);
            return;
    }
}

void glGetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment,
                                           GLenum pname, GLint* params) {
    MITHRIL_ENSURE_INIT();
    if (!params) return;
    mithril::Framebuffer* fbo = fbo_for_target(target);
    if (!fbo) {
        mithril::state_set_error(GL_INVALID_OPERATION);
        return;
    }
    // Resolve the attachment slot. GL_DEPTH_STENCIL_ATTACHMENT queries are
    // answered through the depth attachment (per GL spec).
    const mithril::FBOAttachment* a = nullptr;
    if (attachment >= GL_COLOR_ATTACHMENT0 && attachment < GL_COLOR_ATTACHMENT0 + mithril::kMaxColorAttachments) {
        a = &fbo->colors[attachment - GL_COLOR_ATTACHMENT0];
    } else if (attachment == GL_DEPTH_ATTACHMENT || attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
        a = &fbo->depth;
    } else if (attachment == GL_STENCIL_ATTACHMENT) {
        a = &fbo->stencil;
    } else {
        mithril::state_set_error(GL_INVALID_ENUM);
        return;
    }

    bool hasTex = (a && a->texture != 0);
    bool hasRb  = (a && a->renderbuffer != 0);

    switch (pname) {
        case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE:
            *params = hasTex ? GL_TEXTURE : (hasRb ? GL_RENDERBUFFER : GL_NONE);
            return;
        case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME:
            if (hasRb)      *params = static_cast<GLint>(a->renderbuffer);
            else if (hasTex)*params = static_cast<GLint>(a->texture);
            else            *params = 0;
            return;
        case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL:
            if (hasTex) { *params = a->level; return; }
            mithril::state_set_error(GL_INVALID_OPERATION); return;
        case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE:
            if (hasTex) {
                *params = (a->textarget >= GL_TEXTURE_CUBE_MAP_POSITIVE_X &&
                           a->textarget <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)
                          ? static_cast<GLint>(a->textarget) : 0;
                return;
            }
            mithril::state_set_error(GL_INVALID_OPERATION); return;
        case GL_FRAMEBUFFER_ATTACHMENT_LAYERED:
            if (hasTex) { *params = a->layered ? GL_TRUE : GL_FALSE; return; }
            mithril::state_set_error(GL_INVALID_OPERATION); return;
        case GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE:
        case GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE:
        case GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE:
        case GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE:
        case GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE:
        case GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE: {
            // Best-effort bit-size guess from the source's internal format.
            GLenum fmt = 0;
            if (hasTex) {
                mithril::Texture* t = mithril::state_get_texture(a->texture);
                if (t) fmt = static_cast<GLenum>(t->internalFormat);
            } else if (hasRb) {
                mithril::Renderbuffer* r = mithril::state_get_renderbuffer(a->renderbuffer);
                if (r) fmt = r->internalFormat;
            }
            GLint bits = 0;
            if (pname == GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE) {
                if (fmt == GL_DEPTH_COMPONENT16)                       bits = 16;
                else if (fmt == GL_DEPTH_COMPONENT24 || fmt == GL_DEPTH24_STENCIL8 ||
                         fmt == GL_DEPTH_COMPONENT32)                  bits = 24;
                else if (fmt == GL_DEPTH_COMPONENT32F)                 bits = 32;
            } else if (pname == GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE) {
                if (fmt == GL_STENCIL_INDEX8 || fmt == GL_DEPTH24_STENCIL8) bits = 8;
            } else {
                if (fmt == GL_RGBA8 || fmt == GL_RGB8 || fmt == GL_RG8 ||
                    fmt == GL_R8 || fmt == GL_SRGB8_ALPHA8)            bits = 8;
                else if (fmt == GL_RGBA4)                              bits = 4;
                else if (fmt == GL_RGB565 || fmt == GL_RGB5_A1)        bits = 5;
                else if (fmt == GL_R32F || fmt == GL_RG32F || fmt == GL_RGBA32F ||
                         fmt == GL_R32I || fmt == GL_RG32I || fmt == GL_RGBA32I ||
                         fmt == GL_R32UI || fmt == GL_RG32UI || fmt == GL_RGBA32UI) bits = 32;
                else if (fmt == GL_R16F || fmt == GL_RG16F || fmt == GL_RGBA16F ||
                         fmt == GL_R16I || fmt == GL_RG16I || fmt == GL_RGBA16I ||
                         fmt == GL_R16UI || fmt == GL_RG16UI || fmt == GL_RGBA16UI) bits = 16;
            }
            *params = bits;
            return;
        }
        case GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING:
            // Best-effort: report sRGB for sRGB-encoded formats, linear otherwise.
            *params = (hasTex || hasRb) ? GL_LINEAR : GL_NONE;
            {
                GLenum fmt = 0;
                if (hasTex) {
                    mithril::Texture* t = mithril::state_get_texture(a->texture);
                    if (t) fmt = static_cast<GLenum>(t->internalFormat);
                } else if (hasRb) {
                    mithril::Renderbuffer* r = mithril::state_get_renderbuffer(a->renderbuffer);
                    if (r) fmt = r->internalFormat;
                }
                if (fmt == GL_SRGB8_ALPHA8 || fmt == GL_SRGB8) *params = GL_SRGB;
            }
            return;
        default:
            mithril::state_set_error(GL_INVALID_ENUM);
            return;
    }
}

/* ---- GL 4.3 ARB_invalidate_subdata ----
 *
 * glInvalidateFramebuffer / glInvalidateSubFramebuffer tell the implementation
 * that the contents of the specified attachments are no longer needed and may
 * be discarded. On TBDR GPUs (Apple Silicon, all iOS devices), this is CRITICAL
 * for performance and memory pressure: it maps to VK_ATTACHMENT_STORE_OP_DONT_CARE,
 * which skips the tile-memory → system-memory writeback at render pass end.
 *
 * MobileGL uses the same mechanism via VkAttachmentDescription.storeOp.
 *
 * Implementation: sets per-attachment discard flags on the encoder. The NEXT
 * begin_render_pass applies them as storeOp=DONT_CARE and clears the flags
 * (one-shot per GL spec). The sub-region variant (glInvalidateSubFramebuffer)
 * is treated the same — Vulkan storeOp is per-attachment, not per-region.
 *
 * Note: glInvalidateFramebuffer does NOT end the active render pass. The
 * discard takes effect on the next pass (typically next frame). This is
 * correct GL behavior — the spec says "may be discarded" at the
 * implementation's discretion.
 */
static bool parse_invalidate_attachments(GLsizei numAttachments,
                                         const GLenum* attachments,
                                         bool isDefault,
                                         uint32_t& outColorMask,
                                         bool& outInvDepth,
                                         bool& outInvStencil) {
    for (GLsizei i = 0; i < numAttachments; ++i) {
        GLenum a = attachments[i];
        if (a == GL_NONE) continue;
        if (a == GL_COLOR) {
            if (isDefault) {
                outColorMask |= 1u;  // default FBO: color attachment 0 only
            } else {
                for (int j = 0; j < mithril::kMaxColorAttachments; ++j)
                    outColorMask |= (1u << j);
            }
        } else if (a == GL_DEPTH) {
            outInvDepth = true;
        } else if (a == GL_STENCIL) {
            outInvStencil = true;
        } else if (a == GL_DEPTH_STENCIL) {
            outInvDepth = true;
            outInvStencil = true;
        } else if (a >= GL_COLOR_ATTACHMENT0 &&
                   a < GL_COLOR_ATTACHMENT0 + mithril::kMaxColorAttachments) {
            outColorMask |= (1u << (a - GL_COLOR_ATTACHMENT0));
        } else {
            mithril::state_set_error(GL_INVALID_ENUM);
            return false;
        }
    }
    return true;
}

void glInvalidateFramebuffer(GLenum target, GLsizei numAttachments,
                             const GLenum* attachments) {
    MITHRIL_ENSURE_INIT();
    if (target != GL_FRAMEBUFFER && target != GL_DRAW_FRAMEBUFFER &&
        target != GL_READ_FRAMEBUFFER) {
        mithril::state_set_error(GL_INVALID_ENUM);
        return;
    }
    if (numAttachments < 0) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return;
    }
    if (numAttachments > 0 && !attachments) {
        mithril::state_set_error(GL_INVALID_OPERATION);
        return;
    }
    bool isDefault = (target == GL_READ_FRAMEBUFFER)
                     ? (g_state->currentReadFBO == 0)
                     : (g_state->currentDrawFBO == 0);
    uint32_t colorMask = 0;
    bool invDepth = false, invStencil = false;
    if (!parse_invalidate_attachments(numAttachments, attachments, isDefault,
                                      colorMask, invDepth, invStencil))
        return;
    backend_set_invalidate_attachments(colorMask, invDepth, invStencil);
}

void glInvalidateSubFramebuffer(GLenum target, GLsizei numAttachments,
                                const GLenum* attachments,
                                GLint x, GLint y, GLsizei width, GLsizei height) {
    MITHRIL_ENSURE_INIT();
    if (target != GL_FRAMEBUFFER && target != GL_DRAW_FRAMEBUFFER &&
        target != GL_READ_FRAMEBUFFER) {
        mithril::state_set_error(GL_INVALID_ENUM);
        return;
    }
    if (numAttachments < 0) {
        mithril::state_set_error(GL_INVALID_VALUE);
        return;
    }
    if (numAttachments > 0 && !attachments) {
        mithril::state_set_error(GL_INVALID_OPERATION);
        return;
    }
    (void)x; (void)y; (void)width; (void)height;  // storeOp is per-attachment, not per-region
    bool isDefault = (target == GL_READ_FRAMEBUFFER)
                     ? (g_state->currentReadFBO == 0)
                     : (g_state->currentDrawFBO == 0);
    uint32_t colorMask = 0;
    bool invDepth = false, invStencil = false;
    if (!parse_invalidate_attachments(numAttachments, attachments, isDefault,
                                      colorMask, invDepth, invStencil))
        return;
    backend_set_invalidate_attachments(colorMask, invDepth, invStencil);
}

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
