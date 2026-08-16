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
#ifndef GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE
#define GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE 0x8D56
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
    mithril::semantic_trace_event_oncef("framebuffer_mrt", "framebuffer.mrt_indexed_clear", "glBindFramebuffer", "target=0x%x object=%s", target, framebuffer ? "nonzero" : "zero");
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
    mithril::semantic_trace_event_oncef("framebuffer_mrt", "framebuffer.mrt_indexed_clear", "glFramebufferTexture2D", "target=0x%x attachment=0x%x textarget=0x%x object=%s level=%d", target, attachment, textarget, texture ? "nonzero" : "zero", level);
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
    // images must share the same dimensions AND the same sample count
    // (GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE).
    bool hasAttachment = false;
    GLsizei refW = 0, refH = 0;
    GLsizei refSamples = -1;
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
        const GLsizei s = t->samples > 1 ? t->samples : 1;
        if (refSamples < 0) refSamples = s;
        else if (s != refSamples) { refSamples = -2; return false; }
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
        const GLsizei s = r->samples > 1 ? r->samples : 1;
        if (refSamples < 0) refSamples = s;
        else if (s != refSamples) { refSamples = -2; return false; }
        return true;
    };

    // checkTex/checkRb 失败时：refSamples == -2 表示采样数不一致，否则是
    // 尺寸不一致。（延迟求值 —— refSamples 在下面的检查中才被置位。）
    auto incompl = [&]() {
        return (refSamples == -2) ? GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE
                                  : GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS;
    };
    for (int i = 0; i < mithril::kMaxColorAttachments; ++i) {
        const mithril::FBOAttachment& a = fbo->colors[i];
        if (a.texture) {
            hasAttachment = true;
            if (!checkTex(a.texture)) return incompl();
        } else if (a.renderbuffer) {
            hasAttachment = true;
            if (!checkRb(a.renderbuffer)) return incompl();
        }
    }
    if (fbo->depth.texture) {
        hasAttachment = true;
        if (!checkTex(fbo->depth.texture)) return incompl();
    } else if (fbo->depth.renderbuffer) {
        hasAttachment = true;
        if (!checkRb(fbo->depth.renderbuffer)) return incompl();
    }
    if (fbo->stencil.texture) {
        hasAttachment = true;
        if (!checkTex(fbo->stencil.texture)) return incompl();
    } else if (fbo->stencil.renderbuffer) {
        hasAttachment = true;
        if (!checkRb(fbo->stencil.renderbuffer)) return incompl();
    }

    if (!hasAttachment) return GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
    return GL_FRAMEBUFFER_COMPLETE;
}

void glBlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                       GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                       GLbitfield mask, GLenum filter) {
    MITHRIL_ENSURE_INIT();
    mithril::semantic_trace_event_oncef("framebuffer_mrt", "framebuffer.mrt_indexed_clear", "glBlitFramebuffer", "mask=0x%x filter=0x%x scaled=%s src_origin=%s dst_origin=%s", mask, filter, ((srcX1-srcX0)!=(dstX1-dstX0) || (srcY1-srcY0)!=(dstY1-dstY0)) ? "yes" : "no", (srcX0 || srcY0) ? "nonzero" : "zero", (dstX0 || dstY0) ? "nonzero" : "zero");

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
        int dsrc_samples = 1, ddst_samples = 1;
        bool d_dst_default = (g_state->currentDrawFBO == 0);

        if (g_state->currentReadFBO == 0) {
            dsrc = g_state->eglDefaultDepthImage;
            dsrc_fmt = g_state->eglDefaultDepthFormat;
        } else {
            mithril::Framebuffer* sf = mithril::state_get_framebuffer(g_state->currentReadFBO);
            GLuint dtex = sf ? mithril::fbo_attachment_texture(sf->depth) : 0;
            if (sf && dtex) {
                dsrc = backend_get_texture_image(dtex);
                mithril::Texture* t = mithril::state_get_texture(dtex);
                if (t) {
                    dsrc_fmt = backend_vk_format_for_gl((GLenum)t->internalFormat);
                    dsrc_samples = t->samples > 1 ? t->samples : 1;
                }
            }
        }
        if (d_dst_default) {
            ddst = g_state->eglDefaultDepthImage;
            ddst_fmt = g_state->eglDefaultDepthFormat;
            ddst_h = g_state->eglDefaultHeight;
        } else {
            mithril::Framebuffer* df = mithril::state_get_framebuffer(g_state->currentDrawFBO);
            GLuint dtex = df ? mithril::fbo_attachment_texture(df->depth) : 0;
            if (df && dtex) {
                ddst = backend_get_texture_image(dtex);
                mithril::Texture* t = mithril::state_get_texture(dtex);
                if (t) {
                    ddst_fmt = backend_vk_format_for_gl((GLenum)t->internalFormat);
                    ddst_h = t->height;
                    ddst_samples = t->samples > 1 ? t->samples : 1;
                }
            }
        }
        if (dsrc != VK_NULL_HANDLE && ddst != VK_NULL_HANDLE &&
            dsrc_fmt != VK_FORMAT_UNDEFINED && ddst_fmt != VK_FORMAT_UNDEFINED) {
            if (dsrc_samples > 1 && ddst_samples == 1) {
                // MSAA depth resolve（GL spec：矩形必须 1:1、filter 会被
                // 忽略成 NEAREST）。Vulkan 核心不支持 depth resolve → 后端
                // 内部告警并跳过；Metal 原生支持。
                backend_resolve_images(dsrc, dsrc_fmt, ddst, ddst_fmt,
                                       dstX0, dstY0,
                                       dstX1 - dstX0, dstY1 - dstY0,
                                       mask & (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT),
                                       d_dst_default ? 1 : 0, ddst_h);
            } else if (dsrc_samples == 1 && ddst_samples == 1) {
                backend_blit_images(dsrc, dsrc_fmt, ddst, ddst_fmt,
                                    srcX0, srcY0, srcX1, srcY1,
                                    dstX0, dstY0, dstX1, dstY1,
                                    mask & (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT),
                                    GL_NEAREST, d_dst_default ? 1 : 0, ddst_h);
            } else {
                // MS→MS（等采样数）blit：Vulkan/Metal 的 blit 与 resolve 均不
                // 接受多采样目的图像。GL 规格允许，此处按不支持处理（一次性
                // 告警 + INVALID_OPERATION）。
                static bool warnedMsMs = false;
                if (!warnedMsMs) {
                    warnedMsMs = true;
                    MITHRIL_LOG_WARN("gl", "glBlitFramebuffer: MS->MS depth blit "
                                     "is not supported by the backends — skipped");
                }
                mithril::state_set_error(GL_INVALID_OPERATION);
            }
        }
        // 若同时有 color bit，继续走下面的 color 路径；否则返回。
        if (!(mask & GL_COLOR_BUFFER_BIT)) return;
        // color 路径需要重新 end_render_pass/commit（上面已做，但 color 路径
        // 自身也会做；为保持原流程不变，这里不重复，直接进入 color 解析）。
    }

    // Flush any pending rendering into the source/destination so the blit
    // sees the latest pixels and subsequent draws see the blit's result.
    backend_end_render_pass();
    backend_commit();

    // Resolve the source FBO's colour attachment. The read FBO is the source.
    //   - FBO 0 (EGL default): use the swapchain image installed on g_state.
    //   - User FBO: use the texture attached to GL_COLOR_ATTACHMENT0 (or the
    //     buffer selected by glReadBuffer, but MC Java only uses attachment 0).
    //     renderbuffer 附件取其影子纹理（fbo_attachment_texture）。
    VkImage src_image = VK_NULL_HANDLE;
    VkFormat src_format = VK_FORMAT_UNDEFINED;
    int src_samples = 1;
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
                tex = mithril::fbo_attachment_texture(fbo->colors[fbo->readBuffer - GL_COLOR_ATTACHMENT0]);
            } else if (fbo->readBuffer >= GL_COLOR_ATTACHMENT0 && fbo->readBuffer <= GL_COLOR_ATTACHMENT7) {
                tex = mithril::fbo_attachment_texture(fbo->colors[fbo->readBuffer - GL_COLOR_ATTACHMENT0]);
            } else {
                tex = mithril::fbo_attachment_texture(fbo->colors[0]);
            }
            if (tex) {
                src_image = backend_get_texture_image(tex);
                mithril::Texture* t = mithril::state_get_texture(tex);
                if (t) {
                    src_format = backend_vk_format_for_gl((GLenum)t->internalFormat);
                    src_samples = t->samples > 1 ? t->samples : 1;
                }
            }
        }
    }

    // Resolve the destination FBO's colour attachment. The draw FBO is the
    // destination. Also capture the destination height for the Y-flip
    // computation (needed when dst is the default framebuffer — see below).
    VkImage dst_image = VK_NULL_HANDLE;
    VkFormat dst_format = VK_FORMAT_UNDEFINED;
    int dst_height = 0;
    int dst_samples = 1;
    bool is_dst_default_fbo = (g_state->currentDrawFBO == 0);
    if (is_dst_default_fbo) {
        dst_image  = g_state->eglDefaultColorImage;
        dst_format = g_state->eglDefaultColorFormat;
        dst_height = g_state->eglDefaultHeight;
    } else {
        mithril::Framebuffer* fbo = mithril::state_get_framebuffer(g_state->currentDrawFBO);
        if (fbo) {
            GLuint tex = mithril::fbo_attachment_texture(fbo->colors[0]);
            if (tex) {
                dst_image = backend_get_texture_image(tex);
                mithril::Texture* t = mithril::state_get_texture(tex);
                if (t) {
                    dst_format = backend_vk_format_for_gl((GLenum)t->internalFormat);
                    dst_height = t->height;
                    dst_samples = t->samples > 1 ? t->samples : 1;
                }
            }
        }
    }

    if (src_image == VK_NULL_HANDLE || dst_image == VK_NULL_HANDLE) return;
    if (src_format == VK_FORMAT_UNDEFINED) src_format = VK_FORMAT_R8G8B8A8_UNORM;
    if (dst_format == VK_FORMAT_UNDEFINED) dst_format = VK_FORMAT_R8G8B8A8_UNORM;

    // MSAA resolve 分支（GL 4.6 §17.4.3.2）：多采样读缓冲 → 单采样绘制缓冲
    // 就是 resolve。规格强制：filter 必须是 NEAREST、源/目的矩形尺寸一致、
    // 格式一致，违规即 GL_INVALID_OPERATION。MS→MS（等采样）与 SS→MS 后端
    // 均不支持（vkCmdResolveImage/MTLStoreActionMultisampleResolve 都只接受
    // 单采样目的），显式报错并一次性告警。
    if (src_samples > 1 && dst_samples == 1) {
        if (filter != GL_NEAREST ||
            (srcX1 - srcX0) != (dstX1 - dstX0) ||
            (srcY1 - srcY0) != (dstY1 - dstY0) ||
            src_format != dst_format) {
            mithril::state_set_error(GL_INVALID_OPERATION);
            return;
        }
        backend_resolve_images(src_image, src_format, dst_image, dst_format,
                               dstX0, dstY0, dstX1 - dstX0, dstY1 - dstY0,
                               GL_COLOR_BUFFER_BIT,
                               is_dst_default_fbo ? 1 : 0, dst_height);
        return;
    }
    if (src_samples > 1 || dst_samples > 1) {
        static bool warnedMsMsColor = false;
        if (!warnedMsMsColor) {
            warnedMsMsColor = true;
            MITHRIL_LOG_WARN("gl", "glBlitFramebuffer: MS->MS / SS->MS color "
                             "blit is not supported by the backends — rejected");
        }
        mithril::state_set_error(GL_INVALID_OPERATION);
        return;
    }

    // Y-flip handling: the draw path now flips vertex Y in the shader for
    // default-FBO rendering (MVK_CONFIG_SHADER_CONVERSION_FLIP_VERTEX_Y=0),
    // so default-FBO content is in Vulkan's top-left orientation. But blits
    // bypass the vertex shader, so blitting TO the default framebuffer needs
    // the destination Y flipped to convert GL bottom-left coords to Vulkan
    // top-left coords. User FBOs (no Y flip in draw path) keep GL orientation,
    // so their blit coords pass through unchanged. Deep reference: MobileGL
    // ApplyNativeBlitDefaultFramebufferTransform (identity branch).
    // Source Y is never flipped (MobileGL never flips src Y).
    backend_blit_images(src_image, src_format,
                        dst_image, dst_format,
                        srcX0, srcY0, srcX1, srcY1,
                        dstX0, dstY0, dstX1, dstY1,
                        mask, filter,
                        is_dst_default_fbo ? 1 : 0, dst_height);
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
        // 影子纹理随 renderbuffer 一起销毁（后端资源 + 纹理命名空间的名字）。
        mithril::Renderbuffer* rb = mithril::state_get_renderbuffer(name);
        if (rb && rb->shadowTexture != 0) {
            backend_delete_texture(rb->shadowTexture);
            g_state->textures.erase(rb->shadowTexture);
            g_state->textureNames.release(rb->shadowTexture);
        }
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

/* glRenderbufferStorage(Multisample) 的共享实现：记录状态并（重）建影子
 * 纹理。renderbuffer 的存储由一张 GL 不可见的纹理承载（名字取自纹理命名
 * 空间，避免与纹理名撞车）；FBO 附件解析处统一把 rb 附件替换成它。
 * 尺寸/格式/采样数变化时 backend_get_or_create_texture 自动重建后端资源。 */
static void renderbuffer_allocate_storage(mithril::Renderbuffer* rb,
                                          GLenum internalformat, GLsizei width,
                                          GLsizei height, GLsizei samples) {
    rb->internalFormat = internalformat;
    rb->width = width;
    rb->height = height;
    rb->samples = samples;

    const GLenum target =
        samples > 1 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
    if (rb->shadowTexture == 0) {
        GLuint name = 0;
        mithril::state_gen_names("texture", 1, &name);
        rb->shadowTexture = name;
        mithril::Texture t{};
        t.id = name;
        g_state->textures[name] = t;
    }
    mithril::Texture* t = mithril::state_get_texture(rb->shadowTexture);
    if (t) {
        t->internalFormat = (GLint)internalformat;
        t->width  = width;
        t->height = height;
        t->depth  = 1;
        t->target = target;
        t->samples = samples;
        t->immutable = true;      // renderbuffer 存储不可再 glTexImage
        t->immutableLevels = 1;
        t->levels = 1;
        t->contentVersion++;
    }
    backend_get_or_create_texture(rb->shadowTexture, width, height, 1, 1,
                                  internalformat, target,
                                  samples > 1 ? samples : 1);
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
    renderbuffer_allocate_storage(rb, internalformat, width, height, 0);
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
    renderbuffer_allocate_storage(rb, internalformat, width, height, samples);
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

/* FBO 附件的后端纹理名：纹理附件直接用纹理名；renderbuffer 附件用其影子
 * 纹理（renderbuffer 存储的载体，见 renderbuffer_allocate_storage）。 */
GLuint fbo_attachment_texture(const FBOAttachment& a) {
    if (a.texture != 0) return a.texture;
    if (a.renderbuffer != 0) {
        Renderbuffer* rb = state_get_renderbuffer(a.renderbuffer);
        if (rb) return rb->shadowTexture;
    }
    return 0;
}

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
        GLuint tex = fbo_attachment_texture(fbo->colors[i]);
        if (tex == 0) { out_color[i] = VK_NULL_HANDLE; continue; }
        VkImageView view = backend_get_texture_view(tex);
        out_color[i] = view;
        if (view != VK_NULL_HANDLE) { count = i + 1; }
        if (w == 0) {
            Texture* t = state_get_texture(tex);
            if (t) { w = t->width; h = t->height; }
        }
    }
    GLuint depth_tex = fbo_attachment_texture(fbo->depth);
    if (depth_tex) {
        *out_depth = backend_get_texture_view(depth_tex);
        if (w == 0) {
            Texture* t = state_get_texture(depth_tex);
            if (t) { w = t->width; h = t->height; }
        }
    }
    if (w > 0 && out_w) *out_w = w;
    if (h > 0 && out_h) *out_h = h;
    return count;
}

} // namespace mithril
