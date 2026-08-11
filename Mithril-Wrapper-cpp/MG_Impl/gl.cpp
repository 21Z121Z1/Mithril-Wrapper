// Mithril-Wrapper - MG_Impl/gl.cpp
// Core state-control GL entry points: clear, enable/disable, viewport, blend,
// depth, stencil, rasterizer, pixel store, active texture, flush/finish.
//
// This is the Vulkan/MoltenVK rewrite of the former gl/gl.cpp. The Metal
// clear/render-pass/commit calls (metal_set_clear_color / metal_begin_render_pass
// / metal_commit / ...) are replaced with the Vulkan backend C API
// (backend_set_clear_color / backend_begin_render_pass / backend_commit / ...)
// declared in MG_Backend/Backend.h.
#include "includes.h"
#include "Framebuffer.h"

// glClearBuffer* uses GL_COLOR / GL_DEPTH / GL_STENCIL (GL 3.0+ buffer tokens),
// which are not defined in glcorearb.h. Guard so the file still compiles on
// SDKs that do define them.
#ifndef GL_COLOR
#define GL_COLOR   0x1800
#endif
#ifndef GL_DEPTH
#define GL_DEPTH   0x1801
#endif
#ifndef GL_STENCIL
#define GL_STENCIL 0x1802
#endif

extern "C" {

/* ---- Clear ---- */
void glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) {
    MITHRIL_ENSURE_INIT();
    g_state->clearColor[0] = r;
    g_state->clearColor[1] = g;
    g_state->clearColor[2] = b;
    g_state->clearColor[3] = a;
    backend_set_clear_color(r, g, b, a);
}

void glClearDepth(GLclampd d) {
    MITHRIL_ENSURE_INIT();
    g_state->clearDepth = d;
    backend_set_clear_depth(d);
}

void glClearDepthf(GLclampf d) {
    glClearDepth((GLclampd)d);
}

void glClearStencil(GLint s) {
    MITHRIL_ENSURE_INIT();
    g_state->clearStencil = s;
    backend_set_clear_stencil(s);
}

void glClear(GLbitfield mask) {
    MITHRIL_ENSURE_INIT();
    // Resolve current draw framebuffer attachments.
    VkImageView colors[8] = {VK_NULL_HANDLE};
    VkImageView depth = VK_NULL_HANDLE;
    int w = 0, h = 0;
    int n = mithril::collect_draw_fbo_attachments(colors, &depth, &w, &h);

    // FIX (Root Cause J - glClear 帧内提前提交):
    // GL 语义: glClear 是录制命令，仅追加 clear 到当前 command buffer，不触发帧提交。
    // 帧边界是 eglSwapBuffers（eglWaitClient/glFinish 是显式同步点）。
    // 旧代码在 glClear 末尾调用 backend_commit() → 提交 command buffer 并信号
    // renderFinished → 后续 glDraw* 无法追加到同一 command buffer（已提交）。
    // 典型帧序列 glClear→glDraw→eglSwapBuffers 被破坏：compositor 只看到 clear
    // color（黑屏），draw 命令丢失或错位到下一帧 → 进游戏后黑屏有声音。
    // 移除 backend_commit()，clear 命令保留在当前 command buffer，由 eglSwapBuffers
    // 统一提交。对标 MobileGL Clear() (VulkanRenderer.cpp:4230-4358): 仅录
    // vkCmdClearAttachments，不调用 EndCommandBuffer/QueueSubmit。
    //
    // 同时保留 LOAD+vkCmdClearAttachments 模式（旧 FIX）：只清除 mask 指定的 aspect，
    // 避免 glClear(GL_DEPTH_BUFFER_BIT) 误清颜色缓冲。
    backend_set_load_load();
    backend_begin_render_pass(colors, n, depth, w, h, 1);
    backend_clear_attachments(mask, 0, 0, w, h);
    backend_end_render_pass();
    // 不调用 backend_commit() — 帧提交由 eglSwapBuffers 统一处理。
}

/* ---- glClearBuffer* (GL 3.0+) ----
 * Clear a specific buffer to a specific value, without changing the global
 * clear color/depth/stencil state. Each variant saves the current clear
 * value, sets the new one, clears, then restores.
 * 对照 MobileGL ClearBuffer() implementation.
 */

void glClearBufferfv(GLenum buffer, GLint drawbuffer, const GLfloat* value) {
    MITHRIL_ENSURE_INIT();
    if (!value) return;
    (void)drawbuffer;  // backend_clear_attachments clears all color attachments

    VkImageView colors[8] = {VK_NULL_HANDLE};
    VkImageView depth = VK_NULL_HANDLE;
    int w = 0, h = 0;
    int n = mithril::collect_draw_fbo_attachments(colors, &depth, &w, &h);

    GLbitfield mask = 0;
    if (buffer == GL_COLOR) {
        GLfloat save[4] = {g_state->clearColor[0], g_state->clearColor[1],
                           g_state->clearColor[2], g_state->clearColor[3]};
        backend_set_clear_color(value[0], value[1], value[2], value[3]);
        g_state->clearColor[0] = value[0]; g_state->clearColor[1] = value[1];
        g_state->clearColor[2] = value[2]; g_state->clearColor[3] = value[3];
        mask = GL_COLOR_BUFFER_BIT;
        backend_set_load_load();
        backend_begin_render_pass(colors, n, depth, w, h, 1);
        backend_clear_attachments(mask, 0, 0, w, h);
        backend_end_render_pass();
        g_state->clearColor[0] = save[0]; g_state->clearColor[1] = save[1];
        g_state->clearColor[2] = save[2]; g_state->clearColor[3] = save[3];
        backend_set_clear_color(save[0], save[1], save[2], save[3]);
    } else if (buffer == GL_DEPTH) {
        GLdouble save = g_state->clearDepth;
        backend_set_clear_depth((double)value[0]);
        g_state->clearDepth = (GLclampd)value[0];
        mask = GL_DEPTH_BUFFER_BIT;
        backend_set_load_load();
        backend_begin_render_pass(colors, n, depth, w, h, 1);
        backend_clear_attachments(mask, 0, 0, w, h);
        backend_end_render_pass();
        g_state->clearDepth = save;
        backend_set_clear_depth(save);
    }
}

void glClearBufferiv(GLenum buffer, GLint drawbuffer, const GLint* value) {
    MITHRIL_ENSURE_INIT();
    if (!value) return;
    (void)drawbuffer;

    VkImageView colors[8] = {VK_NULL_HANDLE};
    VkImageView depth = VK_NULL_HANDLE;
    int w = 0, h = 0;
    int n = mithril::collect_draw_fbo_attachments(colors, &depth, &w, &h);

    GLbitfield mask = 0;
    if (buffer == GL_COLOR) {
        GLfloat save[4] = {g_state->clearColor[0], g_state->clearColor[1],
                           g_state->clearColor[2], g_state->clearColor[3]};
        backend_set_clear_color((float)value[0], (float)value[1], (float)value[2], (float)value[3]);
        g_state->clearColor[0] = (float)value[0]; g_state->clearColor[1] = (float)value[1];
        g_state->clearColor[2] = (float)value[2]; g_state->clearColor[3] = (float)value[3];
        mask = GL_COLOR_BUFFER_BIT;
        backend_set_load_load();
        backend_begin_render_pass(colors, n, depth, w, h, 1);
        backend_clear_attachments(mask, 0, 0, w, h);
        backend_end_render_pass();
        g_state->clearColor[0] = save[0]; g_state->clearColor[1] = save[1];
        g_state->clearColor[2] = save[2]; g_state->clearColor[3] = save[3];
        backend_set_clear_color(save[0], save[1], save[2], save[3]);
    } else if (buffer == GL_STENCIL) {
        GLint save = g_state->clearStencil;
        backend_set_clear_stencil(value[0]);
        g_state->clearStencil = value[0];
        mask = GL_STENCIL_BUFFER_BIT;
        backend_set_load_load();
        backend_begin_render_pass(colors, n, depth, w, h, 1);
        backend_clear_attachments(mask, 0, 0, w, h);
        backend_end_render_pass();
        g_state->clearStencil = save;
        backend_set_clear_stencil(save);
    }
}

void glClearBufferuiv(GLenum buffer, GLint drawbuffer, const GLuint* value) {
    MITHRIL_ENSURE_INIT();
    if (!value) return;
    (void)drawbuffer;

    VkImageView colors[8] = {VK_NULL_HANDLE};
    VkImageView depth = VK_NULL_HANDLE;
    int w = 0, h = 0;
    int n = mithril::collect_draw_fbo_attachments(colors, &depth, &w, &h);

    if (buffer == GL_COLOR) {
        GLfloat save[4] = {g_state->clearColor[0], g_state->clearColor[1],
                           g_state->clearColor[2], g_state->clearColor[3]};
        backend_set_clear_color((float)value[0], (float)value[1], (float)value[2], (float)value[3]);
        g_state->clearColor[0] = (float)value[0]; g_state->clearColor[1] = (float)value[1];
        g_state->clearColor[2] = (float)value[2]; g_state->clearColor[3] = (float)value[3];
        backend_set_load_load();
        backend_begin_render_pass(colors, n, depth, w, h, 1);
        backend_clear_attachments(GL_COLOR_BUFFER_BIT, 0, 0, w, h);
        backend_end_render_pass();
        g_state->clearColor[0] = save[0]; g_state->clearColor[1] = save[1];
        g_state->clearColor[2] = save[2]; g_state->clearColor[3] = save[3];
        backend_set_clear_color(save[0], save[1], save[2], save[3]);
    }
}

void glClearBufferfi(GLenum buffer, GLint drawbuffer, GLfloat depth_val, GLint stencil_val) {
    MITHRIL_ENSURE_INIT();
    (void)drawbuffer;

    if (buffer != GL_DEPTH_STENCIL) return;

    VkImageView colors[8] = {VK_NULL_HANDLE};
    VkImageView depthView = VK_NULL_HANDLE;
    int w = 0, h = 0;
    int n = mithril::collect_draw_fbo_attachments(colors, &depthView, &w, &h);

    GLclampd saveD = g_state->clearDepth;
    GLint saveS = g_state->clearStencil;
    backend_set_clear_depth((double)depth_val);
    backend_set_clear_stencil(stencil_val);
    g_state->clearDepth = (GLclampd)depth_val;
    g_state->clearStencil = stencil_val;

    backend_set_load_load();
    backend_begin_render_pass(colors, n, depthView, w, h, 1);
    backend_clear_attachments(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT, 0, 0, w, h);
    backend_end_render_pass();

    g_state->clearDepth = saveD;
    g_state->clearStencil = saveS;
    backend_set_clear_depth(saveD);
    backend_set_clear_stencil(saveS);
}

void glClearBufferData(GLenum target, GLenum internalformat,
                       GLenum format, GLenum type, const void* data) {
    (void)target; (void)internalformat; (void)format; (void)type; (void)data;
    MITHRIL_LOG_WARN("gl", "glClearBufferData: not implemented (placeholder)");
}

void glClearBufferSubData(GLenum target, GLenum internalformat,
                          GLintptr offset, GLsizeiptr size,
                          GLenum format, GLenum type, const void* data) {
    (void)target; (void)internalformat; (void)offset; (void)size;
    (void)format; (void)type; (void)data;
    MITHRIL_LOG_WARN("gl", "glClearBufferSubData: not implemented (placeholder)");
}

/* ---- Enable / Disable ----
 * P0-6: capabilities are now managed solely by GLState::setCapability /
 * isCapabilityEnabled (bool fields). The old enabledCaps set is gone, so
 * there is a single source of truth — no possibility of the set and the
 * fields drifting out of sync.
 *
 * FIX (root cause AF - Primitive Restart): GL_PRIMITIVE_RESTART_FIXED_INDEX
 * 在 State.cpp 的 setCapability 中未列项（GL_PRIMITIVE_RESTART 已列项）。
 * 这里在 glEnable/glDisable 入口直接维护 g_state->primitiveRestartFixedIndex，
 * 让 Pipeline.cpp 的 ia.primitiveRestartEnable 能在两者任一启用时为 VK_TRUE。
 * 深度对照 MobileGL VulkanRenderer.cpp:3861-3877。
 */
void glEnable(GLenum cap) {
    MITHRIL_ENSURE_INIT();
    if (cap == GL_PRIMITIVE_RESTART_FIXED_INDEX) {
        g_state->primitiveRestartFixedIndex = true;
        g_state->bumpRenderVersion();
        return;
    }
    // GL spec: enabling an unrecognised capability raises GL_INVALID_ENUM.
    if (!g_state->isKnownCapability(cap)) {
        mithril::state_set_error(GL_INVALID_ENUM);
        return;
    }
    g_state->setCapability(cap, true);
}

void glDisable(GLenum cap) {
    MITHRIL_ENSURE_INIT();
    if (cap == GL_PRIMITIVE_RESTART_FIXED_INDEX) {
        g_state->primitiveRestartFixedIndex = false;
        g_state->bumpRenderVersion();
        return;
    }
    // GL spec: disabling an unrecognised capability raises GL_INVALID_ENUM.
    if (!g_state->isKnownCapability(cap)) {
        mithril::state_set_error(GL_INVALID_ENUM);
        return;
    }
    g_state->setCapability(cap, false);
}

GLboolean glIsEnabled(GLenum cap) {
    if (!g_state) return GL_FALSE;
    // FIX (root cause AF): GL_PRIMITIVE_RESTART_FIXED_INDEX 未在
    // State.cpp isCapabilityEnabled 中列项，这里直接读 g_state 字段。
    if (cap == GL_PRIMITIVE_RESTART_FIXED_INDEX) {
        return g_state->primitiveRestartFixedIndex ? GL_TRUE : GL_FALSE;
    }
    return g_state->isCapabilityEnabled(cap) ? GL_TRUE : GL_FALSE;
}

/* Indexed enable/disable: GL_BLEND is the only capability that is per-draw-
 * buffer. For all other caps the index is ignored (GL_INVALID_OPERATION is
 * not required by the spec for caps that are not per-buffer). */
void glEnablei(GLenum cap, GLuint index) {
    MITHRIL_ENSURE_INIT();
    if (cap == GL_BLEND && index < mithril::kMaxColorAttachments) {
        g_state->blends[index].enabled = true;
        g_state->bumpRenderVersion();
        return;
    }
    g_state->setCapability(cap, true);
}

void glDisablei(GLenum cap, GLuint index) {
    MITHRIL_ENSURE_INIT();
    if (cap == GL_BLEND && index < mithril::kMaxColorAttachments) {
        g_state->blends[index].enabled = false;
        g_state->bumpRenderVersion();
        return;
    }
    g_state->setCapability(cap, false);
}

GLboolean glIsEnabledi(GLenum cap, GLuint index) {
    if (!g_state) return GL_FALSE;
    if (cap == GL_BLEND && index < mithril::kMaxColorAttachments) {
        return g_state->blends[index].enabled ? GL_TRUE : GL_FALSE;
    }
    return g_state->isCapabilityEnabled(cap) ? GL_TRUE : GL_FALSE;
}

/* ---- Viewport / scissor / depth range ---- */
void glViewport(GLint x, GLint y, GLsizei w, GLsizei h) {
    MITHRIL_ENSURE_INIT();
    g_state->viewportX = x;
    g_state->viewportY = y;
    g_state->viewportW = w;
    g_state->viewportH = h;
}

void glDepthRange(GLclampd n, GLclampd f) {
    MITHRIL_ENSURE_INIT();
    g_state->depthNear = n;
    g_state->depthFar  = f;
}

void glDepthRangef(GLclampf n, GLclampf f) { glDepthRange((GLclampd)n, (GLclampd)f); }

void glScissor(GLint x, GLint y, GLsizei w, GLsizei h) {
    MITHRIL_ENSURE_INIT();
    g_state->scissorX = x;
    g_state->scissorY = y;
    g_state->scissorW = w;
    g_state->scissorH = h;
}

/* ---- Blend (per-draw-buffer) ----
 * The non-indexed variants set the blend state for ALL draw buffers (GL spec).
 * The *i variants set the state for a single draw buffer `buf`.
 */
void glBlendFunc(GLenum sf, GLenum df) {
    MITHRIL_ENSURE_INIT();
    for (int i = 0; i < mithril::kMaxColorAttachments; ++i) {
        g_state->blends[i].srcRGB = sf; g_state->blends[i].srcA = sf;
        g_state->blends[i].dstRGB = df; g_state->blends[i].dstA = df;
    }
    g_state->bumpRenderVersion();
}

void glBlendFuncSeparate(GLenum sRGB, GLenum dRGB, GLenum sA, GLenum dA) {
    MITHRIL_ENSURE_INIT();
    for (int i = 0; i < mithril::kMaxColorAttachments; ++i) {
        g_state->blends[i].srcRGB = sRGB; g_state->blends[i].dstRGB = dRGB;
        g_state->blends[i].srcA   = sA;   g_state->blends[i].dstA   = dA;
    }
    g_state->bumpRenderVersion();
}

void glBlendEquation(GLenum mode) {
    MITHRIL_ENSURE_INIT();
    for (int i = 0; i < mithril::kMaxColorAttachments; ++i) {
        g_state->blends[i].eqRGB = mode; g_state->blends[i].eqA = mode;
    }
    g_state->bumpRenderVersion();
}

void glBlendEquationSeparate(GLenum mRGB, GLenum mA) {
    MITHRIL_ENSURE_INIT();
    for (int i = 0; i < mithril::kMaxColorAttachments; ++i) {
        g_state->blends[i].eqRGB = mRGB;
        g_state->blends[i].eqA   = mA;
    }
    g_state->bumpRenderVersion();
}

void glBlendColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) {
    MITHRIL_ENSURE_INIT();
    g_state->blendColor[0] = r;
    g_state->blendColor[1] = g;
    g_state->blendColor[2] = b;
    g_state->blendColor[3] = a;
}

void glBlendFunci(GLuint buf, GLenum src, GLenum dst) {
    MITHRIL_ENSURE_INIT();
    if (buf >= mithril::kMaxColorAttachments) { mithril::state_set_error(GL_INVALID_VALUE); return; }
    g_state->blends[buf].srcRGB = src; g_state->blends[buf].srcA = src;
    g_state->blends[buf].dstRGB = dst; g_state->blends[buf].dstA = dst;
    g_state->bumpRenderVersion();
}
void glBlendFuncSeparatei(GLuint buf, GLenum sR, GLenum dR, GLenum sA, GLenum dA) {
    MITHRIL_ENSURE_INIT();
    if (buf >= mithril::kMaxColorAttachments) { mithril::state_set_error(GL_INVALID_VALUE); return; }
    g_state->blends[buf].srcRGB = sR; g_state->blends[buf].dstRGB = dR;
    g_state->blends[buf].srcA   = sA; g_state->blends[buf].dstA   = dA;
    g_state->bumpRenderVersion();
}
void glBlendEquationi(GLuint buf, GLenum mode) {
    MITHRIL_ENSURE_INIT();
    if (buf >= mithril::kMaxColorAttachments) { mithril::state_set_error(GL_INVALID_VALUE); return; }
    g_state->blends[buf].eqRGB = mode; g_state->blends[buf].eqA = mode;
    g_state->bumpRenderVersion();
}
void glBlendEquationSeparatei(GLuint buf, GLenum mRGB, GLenum mA) {
    MITHRIL_ENSURE_INIT();
    if (buf >= mithril::kMaxColorAttachments) { mithril::state_set_error(GL_INVALID_VALUE); return; }
    g_state->blends[buf].eqRGB = mRGB; g_state->blends[buf].eqA = mA;
    g_state->bumpRenderVersion();
}

/* ---- Depth / stencil / color mask ---- */
void glDepthFunc(GLenum func) {
    MITHRIL_ENSURE_INIT();
    g_state->depthFunc = func;
}

void glDepthMask(GLboolean flag) {
    MITHRIL_ENSURE_INIT();
    g_state->depthMask = (flag != 0);
}

void glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) {
    MITHRIL_ENSURE_INIT();
    // GL spec: glColorMask sets the mask for ALL draw buffers.
    for (int i = 0; i < mithril::kMaxColorAttachments; ++i) {
        g_state->colorMask[i][0] = (r != 0);
        g_state->colorMask[i][1] = (g != 0);
        g_state->colorMask[i][2] = (b != 0);
        g_state->colorMask[i][3] = (a != 0);
    }
    g_state->bumpRenderVersion();
}

void glColorMaski(GLuint buf, GLboolean r, GLboolean g, GLboolean b, GLboolean a) {
    MITHRIL_ENSURE_INIT();
    if (buf >= mithril::kMaxColorAttachments) { mithril::state_set_error(GL_INVALID_VALUE); return; }
    g_state->colorMask[buf][0] = (r != 0);
    g_state->colorMask[buf][1] = (g != 0);
    g_state->colorMask[buf][2] = (b != 0);
    g_state->colorMask[buf][3] = (a != 0);
    g_state->bumpRenderVersion();
}

void glStencilMask(GLuint mask) {
    MITHRIL_ENSURE_INIT();
    g_state->stencilMask = mask;
    g_state->stencilBackMask = mask;
}

void glStencilFunc(GLenum func, GLint ref, GLuint mask) {
    MITHRIL_ENSURE_INIT();
    g_state->stencilFunc = func;
    g_state->stencilRef  = ref;
    g_state->stencilValueMask = mask;
    g_state->stencilBackFunc = func;
    g_state->stencilBackRef  = ref;
    g_state->stencilBackValueMask = mask;
}

void glStencilOp(GLenum sfail, GLenum dpfail, GLenum dppass) {
    MITHRIL_ENSURE_INIT();
    g_state->stencilSfail  = sfail;
    g_state->stencilDpfail = dpfail;
    g_state->stencilDppass = dppass;
    g_state->stencilBackSfail  = sfail;
    g_state->stencilBackDpfail = dpfail;
    g_state->stencilBackDppass = dppass;
}

void glStencilMaskSeparate(GLenum face, GLuint mask) {
    MITHRIL_ENSURE_INIT();
    if (face == GL_FRONT) g_state->stencilMask = mask;
    else if (face == GL_BACK) g_state->stencilBackMask = mask;
    else { g_state->stencilMask = mask; g_state->stencilBackMask = mask; }
}

void glStencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask) {
    MITHRIL_ENSURE_INIT();
    if (face == GL_FRONT || face == GL_FRONT_AND_BACK) {
        g_state->stencilFunc = func;
        g_state->stencilRef  = ref;
        g_state->stencilValueMask = mask;
    }
    if (face == GL_BACK || face == GL_FRONT_AND_BACK) {
        g_state->stencilBackFunc = func;
        g_state->stencilBackRef  = ref;
        g_state->stencilBackValueMask = mask;
    }
}

void glStencilOpSeparate(GLenum face, GLenum sfail, GLenum dpfail, GLenum dppass) {
    MITHRIL_ENSURE_INIT();
    if (face == GL_FRONT || face == GL_FRONT_AND_BACK) {
        g_state->stencilSfail = sfail;
        g_state->stencilDpfail = dpfail;
        g_state->stencilDppass = dppass;
    }
    if (face == GL_BACK || face == GL_FRONT_AND_BACK) {
        g_state->stencilBackSfail = sfail;
        g_state->stencilBackDpfail = dpfail;
        g_state->stencilBackDppass = dppass;
    }
}

/* ---- Rasterizer ---- */
void glCullFace(GLenum mode) {
    MITHRIL_ENSURE_INIT();
    g_state->cullMode = mode;
}

void glFrontFace(GLenum mode) {
    MITHRIL_ENSURE_INIT();
    g_state->frontFace = mode;
}

void glPolygonMode(GLenum face, GLenum mode) {
    MITHRIL_ENSURE_INIT();
    if (face == GL_FRONT || face == GL_FRONT_AND_BACK) g_state->polygonModeFront = mode;
    if (face == GL_BACK  || face == GL_FRONT_AND_BACK) g_state->polygonModeBack  = mode;
}

void glPolygonOffset(GLfloat factor, GLfloat units) {
    MITHRIL_ENSURE_INIT();
    g_state->polygonOffsetFactor = factor;
    g_state->polygonOffsetUnits  = units;
}

// GL 4.6 ARB_polygon_offset_clamp
void glPolygonOffsetClamp(GLfloat factor, GLfloat units, GLfloat clamp) {
    MITHRIL_ENSURE_INIT();
    g_state->polygonOffsetFactor = factor;
    g_state->polygonOffsetUnits  = units;
    g_state->polygonOffsetClamp  = clamp;
}

// GL 4.0 ARB_sample_shading — state field already exists, just needs entry point
void glMinSampleShading(GLfloat value) {
    MITHRIL_ENSURE_INIT();
    g_state->minSampleShading = value;
    g_state->sampleShadingEnabled = (value > 0.0f);
}

// GL 4.0 ARB_texture_multisample — sample mask
void glSampleMaski(GLuint maskNumber, GLbitfield mask) {
    MITHRIL_ENSURE_INIT();
    if (maskNumber < mithril::kMaxSampleMaskWords) {
        g_state->sampleMaskValue[maskNumber] = mask;
        g_state->sampleMask = true;
    }
}

// GL 4.5 ARB_clip_control — sets the clip volume origin and depth mode.
// GL_LOWER_LEFT + GL_NEGATIVE_ONE_TO_ONE (default) = traditional GL convention.
// GL_UPPER_LEFT + GL_ZERO_TO_ONE = Vulkan/Metal native convention.
// State is stored and queryable via glGetIntegerv(GL_CLIP_ORIGIN / GL_CLIP_DEPTH_MODE).
void glClipControl(GLenum origin, GLenum depth) {
    MITHRIL_ENSURE_INIT();
    if (origin != 0x8CA1 /*GL_LOWER_LEFT*/ && origin != 0x8CA2 /*GL_UPPER_LEFT*/) {
        mithril::state_set_error(GL_INVALID_ENUM);
        return;
    }
    if (depth != 0x935E /*GL_NEGATIVE_ONE_TO_ONE*/ && depth != 0x935F /*GL_ZERO_TO_ONE*/) {
        mithril::state_set_error(GL_INVALID_ENUM);
        return;
    }
    g_state->clipOrigin = origin;
    g_state->clipDepthMode = depth;
}

void glLineWidth(GLfloat w) { MITHRIL_ENSURE_INIT(); g_state->lineWidth = w; }
void glPointSize(GLfloat s) { MITHRIL_ENSURE_INIT(); g_state->pointSize = s; }
void glHint(GLenum, GLenum) { MITHRIL_ENSURE_INIT(); /* hints are advisory */ }

/* ---- Pixel store ----
 * Pnames map onto the PixelStoreState sub-struct (pack + unpack state was
 * moved off the flat GLState fields in the rewrite).
 */
void glPixelStorei(GLenum pname, GLint param) {
    MITHRIL_ENSURE_INIT();
    switch (pname) {
        case GL_UNPACK_ALIGNMENT:      g_state->pixelStore.unpackAlignment     = param; break;
        case GL_PACK_ALIGNMENT:        g_state->pixelStore.packAlignment       = param; break;
        case GL_UNPACK_ROW_LENGTH:     g_state->pixelStore.unpackRowLength     = param; break;
        case GL_UNPACK_IMAGE_HEIGHT:   g_state->pixelStore.unpackImageHeight   = param; break;
        case GL_UNPACK_SKIP_ROWS:      g_state->pixelStore.unpackSkipRows      = param; break;
        case GL_UNPACK_SKIP_PIXELS:    g_state->pixelStore.unpackSkipPixels    = param; break;
        case GL_UNPACK_SKIP_IMAGES:    g_state->pixelStore.unpackSkipImages    = param; break;
        case GL_PACK_ROW_LENGTH:       g_state->pixelStore.packRowLength        = param; break;
        case GL_PACK_IMAGE_HEIGHT:     g_state->pixelStore.packImageHeight     = param; break;
        case GL_PACK_SKIP_ROWS:        g_state->pixelStore.packSkipRows        = param; break;
        case GL_PACK_SKIP_PIXELS:      g_state->pixelStore.packSkipPixels      = param; break;
        case GL_PACK_SKIP_IMAGES:      g_state->pixelStore.packSkipImages      = param; break;
        default: break;
    }
}

void glPixelStoref(GLenum pname, GLfloat param) { glPixelStorei(pname, (GLint)param); }

/* ---- Active texture ----
 * P1-13: validate the texture unit index. glActiveTexture accepts
 * GL_TEXTURE0..GL_TEXTUREi where i < GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS.
 * Out-of-range units must record GL_INVALID_ENUM (per the GL spec) instead of
 * being silently ignored.
 */
void glActiveTexture(GLenum texture) {
    MITHRIL_ENSURE_INIT();
    if (texture < GL_TEXTURE0 || texture >= GL_TEXTURE0 + mithril::kMaxTextureUnits) {
        mithril::state_set_error(GL_INVALID_ENUM);
        return;
    }
    g_state->activeTextureUnit = texture - GL_TEXTURE0;
}

/* ---- Flush / finish ---- */
void glFlush(void) {
    MITHRIL_ENSURE_INIT();
    backend_end_render_pass();
    backend_commit();
}

void glFinish(void) {
    MITHRIL_ENSURE_INIT();
    backend_end_render_pass();
    backend_commit();
}

void glPrimitiveRestartIndex(GLuint index) {
    MITHRIL_ENSURE_INIT();
    g_state->primitiveRestartIndex = index;
}

} // extern "C"
