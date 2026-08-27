// Mithril-Wrapper - MG_Impl/includes.h
// Central internal header pulled in by every GL-entry-point translation unit.
// Replaces the former gl/includes.h — Metal headers are gone; the backend is
// the Vulkan C API in MG_Backend/Backend.h.
#ifndef MITHRIL_INCLUDES_H
#define MITHRIL_INCLUDES_H

#include <GL/gl.h>

#include "Log.h"
#include "../MG_State/State.h"
#include "Framebuffer.h"
#include "../MG_Backend/Backend.h"

// Bring the global GL state pointer into the global namespace so that
// `extern "C"` GL entry points (which live in the global namespace) can refer
// to it as `g_state` without a `mithril::` qualifier.
using mithril::g_state;

#ifdef __cplusplus
extern "C" {
#endif

void proc_init(void);

/* Conditional-render 门控（Stubs.cpp 实现，复用 query_get_result64 取遮挡
 * 查询结果）。draw / glClear / glClearBuffer* 入口先问它：false = 按
 * glBeginConditionalRender 语义丢弃本次操作。无条件渲染时恒 true。 */
bool mg_conditional_render_allows(void);

#ifdef __cplusplus
}
#endif

// Lightweight guard placed at the top of each GL entry point.
//
// P0-1 fix: g_state is now thread_local.  If EGL has been initialised but the
// current thread has no current context, we must NOT create a phantom global
// state — GL calls in that situation should produce GL_INVALID_OPERATION per
// the spec.  Only create the implicit global state when EGL is not in use
// (e.g. headless unit tests that call GL directly without EGL).
//
// FIX (root cause - headless backend never brought up):
// The load-time initialiser (static_block_t in init.cpp) calls state_init(),
// which allocates g_state. That makes the `!g_state` branch below false on the
// very first GL entry point, so proc_init() — the headless backend bring-up
// that owns backend_init() → init_device() → b->initialized=true — was never
// reached. The MoltenVK backend stayed uninitialized, so every backend_* call
// (glClear/glDrawArrays/glReadPixels) bailed on its `!b->initialized` guard and
// became a silent no-op → offscreen render read back all-black.
//
// Fix: on the headless path (EGL not in use) also run proc_init() when g_state
// already exists but the backend is not yet available. proc_init() is
// idempotent (static `done` + backend_init() idempotence), so this is safe.
// On the EGL/iOS path g_eglInitialized is true, this branch is skipped, and
// eglInitialize() owns backend bring-up exactly as before.
#define MITHRIL_ENSURE_INIT() \
    do { \
        if (!::mithril::g_state) { \
            if (!::mithril::g_eglInitialized) ::proc_init(); \
        } else if (!::mithril::g_eglInitialized && !backend_available()) { \
            ::proc_init(); \
        } \
    } while (0)

#ifdef __cplusplus
/*
 * DirectVulkan user-FBO attachment tracking.
 *
 * The Vulkan backend cannot recover a GL texture name from a VkImageView, but
 * it needs the texture name to find TextureEntry::currentLayout and emit the
 * explicit attachment <-> sampled-image layout barriers around every render
 * pass. The ordinary draw path historically registered these ids manually,
 * while glClear/glClearBuffer* called backend_begin_render_pass() directly.
 * A clear-only FBO pass could therefore execute against a texture whose
 * tracked layout stayed UNDEFINED; sampling that texture in the next pass then
 * bound a SHADER_READ_ONLY descriptor for an image that had never been
 * transitioned/bookkept as such. On MoltenVK this manifests as missing or
 * constant-colour composited passes in Minecraft.
 *
 * Make registration structural instead of call-site dependent: every GL
 * frontend render-pass begin first describes the current user FBO to the
 * DirectVulkan backend. For FBO 0 we explicitly clear any stale registration.
 * DirectMetal is untouched.
 */
static inline void mithril_frontend_begin_render_pass(
        VkImageView* color_views, int color_count, VkImageView depth_view,
        int width, int height, int samples) {
    if (backend_active_kind() == MITHRIL_BACKEND_KIND_VULKAN) {
        if (::mithril::g_state && ::mithril::g_state->currentDrawFBO != 0) {
            ::mithril::Framebuffer* fbo = ::mithril::state_get_framebuffer(
                ::mithril::g_state->currentDrawFBO);
            if (fbo) {
                GLuint color_tex_ids[8] = {0};
                int n = color_count;
                if (n < 0) n = 0;
                if (n > 8) n = 8;
                for (int i = 0; i < n; ++i) {
                    color_tex_ids[i] = ::mithril::fbo_attachment_texture(fbo->colors[i]);
                }
                const GLuint depth_tex_id = ::mithril::fbo_attachment_texture(fbo->depth);
                backend_set_fbo_attachment_tex_ids(color_tex_ids, n, depth_tex_id);
            } else {
                backend_set_fbo_attachment_tex_ids(nullptr, 0, 0);
            }
        } else {
            backend_set_fbo_attachment_tex_ids(nullptr, 0, 0);
        }
    }

    backend_begin_render_pass(color_views, color_count, depth_view,
                              width, height, samples);
}

/*
 * Default-framebuffer pipeline format invariant.
 *
 * GLState deliberately owns an immortal Framebuffer object with name 0 so
 * draw/read-buffer query state has somewhere to live. Therefore
 * state_get_framebuffer(0) is non-null and MUST NOT be used to distinguish a
 * user FBO from the EGL default framebuffer. A regressed prepare_draw path did
 * exactly that: FBO 0 entered the user-FBO branch, its synthetic color
 * attachment had texture name 0, and color_formats[0] reached MoltenVK as
 * VK_FORMAT_UNDEFINED. MoltenVK can create such a pipeline while effectively
 * producing no color writes, leaving only clear/background colors visible.
 *
 * Make the invariant structural at the frontend/backend boundary. For a
 * DirectVulkan FBO-0 pipeline, every live color attachment must use the actual
 * EGL swapchain format. Only undefined entries are repaired, so correctly
 * supplied formats remain untouched. DirectMetal and user FBOs are unchanged.
 */
static inline VkPipeline mithril_frontend_get_or_create_pipeline(
        GLuint program,
        const uint32_t* vertex_spirv, int vertex_word_count,
        const uint32_t* fragment_spirv, int fragment_word_count,
        const struct MGVertexAttrib* attribs, int attrib_count,
        const VkFormat* color_formats, int color_count,
        VkFormat depth_format,
        int blend_enabled, GLenum blend_src, GLenum blend_dst,
        GLenum blend_src_alpha, GLenum blend_dst_alpha,
        int color_write_mask,
        GLenum gl_primitive_mode,
        int is_default_fbo) {
    if (backend_active_kind() == MITHRIL_BACKEND_KIND_VULKAN &&
        is_default_fbo && color_formats && color_count > 0) {
        VkFormat fixed_formats[8] = {VK_FORMAT_UNDEFINED};
        int n = color_count;
        if (n > 8) n = 8;
        VkFormat swapchain_format = VK_FORMAT_B8G8R8A8_UNORM;
        if (::mithril::g_state &&
            ::mithril::g_state->eglDefaultColorFormat != VK_FORMAT_UNDEFINED) {
            swapchain_format = ::mithril::g_state->eglDefaultColorFormat;
        }
        bool repaired = false;
        for (int i = 0; i < n; ++i) {
            fixed_formats[i] = color_formats[i];
            if (fixed_formats[i] == VK_FORMAT_UNDEFINED) {
                fixed_formats[i] = swapchain_format;
                repaired = true;
            }
        }
        if (repaired) {
            static unsigned repair_log_count = 0;
            if (repair_log_count < 8) {
                MITHRIL_LOG_WARN("vk",
                    "DEFAULT_FBO_PIPELINE_FORMAT_FIX program=%u count=%d format=0x%x",
                    (unsigned)program, n, (unsigned)swapchain_format);
                ++repair_log_count;
            }
        }
        return backend_get_or_create_pipeline(
            program,
            vertex_spirv, vertex_word_count,
            fragment_spirv, fragment_word_count,
            attribs, attrib_count,
            fixed_formats, n,
            depth_format,
            blend_enabled, blend_src, blend_dst,
            blend_src_alpha, blend_dst_alpha,
            color_write_mask,
            gl_primitive_mode,
            is_default_fbo);
    }

    return backend_get_or_create_pipeline(
        program,
        vertex_spirv, vertex_word_count,
        fragment_spirv, fragment_word_count,
        attribs, attrib_count,
        color_formats, color_count,
        depth_format,
        blend_enabled, blend_src, blend_dst,
        blend_src_alpha, blend_dst_alpha,
        color_write_mask,
        gl_primitive_mode,
        is_default_fbo);
}

// Frontend translation units include this header after Backend.h, so these
// macros only wrap call sites; the real backend declarations and dispatcher
// symbols remain unchanged.
#define backend_begin_render_pass mithril_frontend_begin_render_pass
#define backend_get_or_create_pipeline mithril_frontend_get_or_create_pipeline
#endif

#endif // MITHRIL_INCLUDES_H
