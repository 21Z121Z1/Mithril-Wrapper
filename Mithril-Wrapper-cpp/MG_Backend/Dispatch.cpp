// Mithril-Wrapper - MG_Backend/Dispatch.cpp
// Dual-backend dispatcher: implements the global backend_* C API declared in
// Backend.h and forwards every call to the ACTIVE backend's implementation.
//
// How the per-backend symbols are produced
// ----------------------------------------
// DirectVulkan's sources were renamed at the SOURCE level (every backend_
// token -> dvk_), so its exported symbols are dvk_*. DirectMetal sources
// define dmt_* directly. This file owns the real, unrenamed backend_* entry
// points the GL frontend links against.
//
// The renamed prototypes come from two BUILD-GENERATED headers
// (BackendVulkanDecls.h / BackendMetalDecls.h) that CMake derives from the
// single source of truth Backend.h with a word-boundary perl replace — no
// hand-copied ~110 signatures to keep in sync.
//
// Backend selection
// -----------------
//   MITHRIL_BACKEND=metal    force DirectMetal
//   MITHRIL_BACKEND=vulkan   force DirectVulkan (MoltenVK on Apple)
//   unset                    platform default: Metal on Apple, Vulkan elsewhere
// An explicitly requested backend that fails to come up falls back to the
// other one WITH a loud warning (a running game beats a black screen); an
// unknown value is reported and ignored. The choice is fixed for the process
// lifetime after backend_init() succeeds.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "Backend.h"
#include "BackendVulkanDecls.h"   // generated: dvk_* prototypes

#if defined(__APPLE__) && defined(MITHRIL_ENABLE_METAL_BACKEND)
#define MITHRIL_HAS_METAL 1
#include "BackendMetalDecls.h"    // generated: dmt_* prototypes
#else
#define MITHRIL_HAS_METAL 0
#endif

/* DirectVulkan internal recovery helpers. These are NOT part of the extern
 * "C" contract — Device.h declares them inside namespace mithril::vk, so
 * after the backend_=dvk_ rewrite their true names are the C++-linkage
 * symbols below (signatures mirror DirectVulkan/Device.h). */
namespace mithril { namespace vk {
int  dvk_poll_completed_frames();
bool dvk_is_device_lost();
void dvk_reset_device_lost();
void dvk_purge_cached_resources_for_recovery();
void safe_device_wait_idle();   // no backend_ prefix: unrenamed in every TU
}}

/* ------------------------------------------------------------------ *
 * Kind state + selection
 * ------------------------------------------------------------------ */
static MGBackendKind g_active_kind = MITHRIL_BACKEND_KIND_NONE;
static bool          g_selected    = false;   // backend_init() ran to completion

static bool kind_is_metal(void) {
    return g_active_kind == MITHRIL_BACKEND_KIND_METAL;
}

/* Dispatch macros. MITHRIL_BC(fn, args...) forwards to the active backend;
 * compiles down to a plain dvk_ call when the Metal backend is absent. */
#if MITHRIL_HAS_METAL
#  define MITHRIL_BC(fn, ...) (kind_is_metal() ? dmt_##fn(__VA_ARGS__) \
                                               : dvk_##fn(__VA_ARGS__))
#else
#  define MITHRIL_BC(fn, ...) (dvk_##fn(__VA_ARGS__))
#endif

MGBackendKind backend_active_kind(void) {
    return g_active_kind;
}

const char* backend_api_string(void) {
#if MITHRIL_HAS_METAL
    if (kind_is_metal()) return "Metal 3 (DirectMetal)";
#endif
    if (g_active_kind == MITHRIL_BACKEND_KIND_VULKAN)
        return "Vulkan 1.2 (MoltenVK)";
    return "no backend";
}

/* ------------------------------------------------------------------ *
 * Lifecycle + selection
 * ------------------------------------------------------------------ */
void backend_init(void) {
    if (g_selected) {
        /* Idempotent re-entry: forward straight to the active backend. */
#if MITHRIL_HAS_METAL
        if (kind_is_metal()) { dmt_init(); return; }
#endif
        dvk_init();
        return;
    }

    MGBackendKind requested;
    const char* env = getenv("MITHRIL_BACKEND");
    if (env && *env) {
        if (strcmp(env, "metal") == 0)       requested = MITHRIL_BACKEND_KIND_METAL;
        else if (strcmp(env, "vulkan") == 0) requested = MITHRIL_BACKEND_KIND_VULKAN;
        else {
            fprintf(stderr,
                    "[mithril] MITHRIL_BACKEND='%s' is unknown (expected "
                    "'metal' or 'vulkan'); using the platform default\n", env);
            requested =
#if defined(__APPLE__)
                MITHRIL_BACKEND_KIND_METAL;
#else
                MITHRIL_BACKEND_KIND_VULKAN;
#endif
        }
    } else {
#if defined(__APPLE__)
        requested = MITHRIL_BACKEND_KIND_METAL;
#else
        requested = MITHRIL_BACKEND_KIND_VULKAN;
#endif
    }

#if !MITHRIL_HAS_METAL
    if (requested == MITHRIL_BACKEND_KIND_METAL) {
        fprintf(stderr,
                "[mithril] MITHRIL_BACKEND=metal but this build carries no "
                "DirectMetal backend; falling back to Vulkan\n");
        requested = MITHRIL_BACKEND_KIND_VULKAN;
    }
#endif

    if (requested == MITHRIL_BACKEND_KIND_METAL) {
#if MITHRIL_HAS_METAL
        dmt_init();
        if (dmt_available()) {
            g_active_kind = MITHRIL_BACKEND_KIND_METAL;
            g_selected = true;
            return;
        }
        fprintf(stderr,
                "[mithril] Metal backend failed to initialize; falling back "
                "to Vulkan\n");
#endif
        dvk_init();
        if (dvk_available()) {
            g_active_kind = MITHRIL_BACKEND_KIND_VULKAN;
            g_selected = true;
        }
        return;
    }

    /* requested == VULKAN */
    dvk_init();
    if (dvk_available()) {
        g_active_kind = MITHRIL_BACKEND_KIND_VULKAN;
        g_selected = true;
        return;
    }
#if MITHRIL_HAS_METAL
    fprintf(stderr,
            "[mithril] Vulkan backend failed to initialize; falling back "
            "to Metal\n");
    dmt_init();
    if (dmt_available()) {
        g_active_kind = MITHRIL_BACKEND_KIND_METAL;
        g_selected = true;
    }
#endif
}

void backend_shutdown(void) {
#if MITHRIL_HAS_METAL
    if (kind_is_metal()) { dmt_shutdown(); return; }
#endif
    dvk_shutdown();
}

int backend_available(void) {
#if MITHRIL_HAS_METAL
    if (kind_is_metal()) return dmt_available();
#endif
    return dvk_available();
}

/* ------------------------------------------------------------------ *
 * Forwarded entry points (order mirrors Backend.h)
 * ------------------------------------------------------------------ */
float backend_device_max_sampler_anisotropy(float fallback) {
    return MITHRIL_BC(device_max_sampler_anisotropy, fallback);
}

const char* backend_physical_device_name(void) { return MITHRIL_BC(physical_device_name); }
uint64_t    backend_vram_bytes(void)           { return MITHRIL_BC(vram_bytes); }

/* ---- Clear state ---- */
void backend_set_clear_color(float r, float g, float b, float a) {
    MITHRIL_BC(set_clear_color, r, g, b, a);
}
void backend_set_clear_depth(double d)          { MITHRIL_BC(set_clear_depth, d); }
void backend_set_clear_stencil(int s)           { MITHRIL_BC(set_clear_stencil, s); }
void backend_set_load_clear(void)               { MITHRIL_BC(set_load_clear); }
void backend_set_load_load(void)                { MITHRIL_BC(set_load_load); }
void backend_clear_attachments(GLbitfield mask, int x, int y, int w, int h) {
    MITHRIL_BC(clear_attachments, mask, x, y, w, h);
}
void backend_clear_buffer_indexed(GLenum buffer, GLint drawbuffer,
                                  const float color[4], float depth,
                                  GLuint stencil) {
    MITHRIL_BC(clear_buffer_indexed, buffer, drawbuffer, color, depth, stencil);
}

/* ---- Render pass ---- */
void backend_begin_render_pass(VkImageView* color_views, int color_count,
                               VkImageView depth_view, int width, int height,
                               int samples) {
    MITHRIL_BC(begin_render_pass, color_views, color_count, depth_view,
               width, height, samples);
}
void backend_set_fbo_attachment_tex_ids(GLuint* color_tex_ids, int color_count,
                                        GLuint depth_tex_id) {
    MITHRIL_BC(set_fbo_attachment_tex_ids, color_tex_ids, color_count,
               depth_tex_id);
}
void backend_set_invalidate_attachments(uint32_t color_mask, bool depth,
                                        bool stencil) {
    MITHRIL_BC(set_invalidate_attachments, color_mask, depth, stencil);
}
void backend_end_render_pass(void) { MITHRIL_BC(end_render_pass); }
void backend_commit(void)          { MITHRIL_BC(commit); }

/* ---- Swapchain state ---- */
void backend_set_active_swapchain(void* s)          { MITHRIL_BC(set_active_swapchain, s); }
void backend_swapchain_set_drawable_size(void* s, int w, int h) {
    MITHRIL_BC(swapchain_set_drawable_size, s, w, h);
}
void backend_swapchain_mark_rebuild(void* s)        { MITHRIL_BC(swapchain_mark_rebuild, s); }
void backend_drain_and_detach_swapchain(void)       { MITHRIL_BC(drain_and_detach_swapchain); }
int  backend_swapchain_needs_rebuild(void* s)       { return MITHRIL_BC(swapchain_needs_rebuild, s); }

/* ---- Dynamic state ---- */
void backend_bind_pipeline(VkPipeline pipeline) { MITHRIL_BC(bind_pipeline, pipeline); }
void backend_set_viewport(int x, int y, int w, int h, double znear, double zfar) {
    MITHRIL_BC(set_viewport, x, y, w, h, znear, zfar);
}
void backend_set_scissor(int x, int y, int w, int h) {
    MITHRIL_BC(set_scissor, x, y, w, h);
}
void backend_set_vertex_buffer(int slot, VkBuffer buffer, VkDeviceSize offset) {
    MITHRIL_BC(set_vertex_buffer, slot, buffer, offset);
}
void backend_set_fragment_buffer(int slot, VkBuffer buffer, VkDeviceSize offset) {
    MITHRIL_BC(set_fragment_buffer, slot, buffer, offset);
}
void backend_set_vertex_texture(int slot, VkImageView view, VkSampler sampler) {
    MITHRIL_BC(set_vertex_texture, slot, view, sampler);
}
void backend_set_fragment_texture(int slot, VkImageView view, VkSampler sampler) {
    MITHRIL_BC(set_fragment_texture, slot, view, sampler);
}
void backend_set_blend_color(float r, float g, float b, float a) {
    MITHRIL_BC(set_blend_color, r, g, b, a);
}
void backend_set_depth_bias(float slope, float clamp) {
    MITHRIL_BC(set_depth_bias, slope, clamp);
}
void backend_set_cull_mode(int mode)            { MITHRIL_BC(set_cull_mode, mode); }
void backend_set_front_face(int ccw)            { MITHRIL_BC(set_front_face, ccw); }
void backend_set_depth_test(int enabled, int write_mask, int compare_func) {
    MITHRIL_BC(set_depth_test, enabled, write_mask, compare_func);
}
void backend_set_color_write_mask(int r, int g, int b, int a) {
    MITHRIL_BC(set_color_write_mask, r, g, b, a);
}
void backend_set_stencil_state(int enabled, int func, int ref, int mask,
                               int sfail, int dpfail, int dppass) {
    MITHRIL_BC(set_stencil_state, enabled, func, ref, mask, sfail, dpfail,
               dppass);
}

/* ---- Draws ---- */
void backend_draw_arrays(int primitive, int first, int count) {
    MITHRIL_BC(draw_arrays, primitive, first, count);
}
void backend_draw_indexed(int primitive, int count, int index_type,
                          VkBuffer index_buffer, VkDeviceSize index_offset) {
    MITHRIL_BC(draw_indexed, primitive, count, index_type, index_buffer,
               index_offset);
}
void backend_draw_arrays_instanced(int primitive, int first, int count,
                                   int primcount) {
    MITHRIL_BC(draw_arrays_instanced, primitive, first, count, primcount);
}
void backend_draw_indexed_instanced(int primitive, int count, int index_type,
                                    VkBuffer index_buffer,
                                    VkDeviceSize index_offset, int primcount) {
    MITHRIL_BC(draw_indexed_instanced, primitive, count, index_type,
               index_buffer, index_offset, primcount);
}
void backend_draw_indirect(int primitive, VkBuffer indirect_buffer,
                           VkDeviceSize indirect_offset, int draw_count,
                           int stride) {
    MITHRIL_BC(draw_indirect, primitive, indirect_buffer, indirect_offset,
               draw_count, stride);
}
void backend_draw_indexed_indirect(int primitive, int index_type,
                                   VkBuffer index_buffer,
                                   VkDeviceSize index_offset,
                                   VkBuffer indirect_buffer,
                                   VkDeviceSize indirect_offset,
                                   int draw_count, int stride) {
    MITHRIL_BC(draw_indexed_indirect, primitive, index_type, index_buffer,
               index_offset, indirect_buffer, indirect_offset, draw_count,
               stride);
}
void backend_draw_indirect_count(int primitive, VkBuffer indirect_buffer,
                                 VkDeviceSize indirect_offset,
                                 VkBuffer count_buffer,
                                 VkDeviceSize count_offset, int max_drawcount,
                                 int stride) {
    MITHRIL_BC(draw_indirect_count, primitive, indirect_buffer,
               indirect_offset, count_buffer, count_offset, max_drawcount,
               stride);
}
void backend_draw_indexed_indirect_count(int primitive, int index_type,
                                         VkBuffer index_buffer,
                                         VkDeviceSize index_offset,
                                         VkBuffer indirect_buffer,
                                         VkDeviceSize indirect_offset,
                                         VkBuffer count_buffer,
                                         VkDeviceSize count_offset,
                                         int max_drawcount, int stride) {
    MITHRIL_BC(draw_indexed_indirect_count, primitive, index_type,
               index_buffer, index_offset, indirect_buffer, indirect_offset,
               count_buffer, count_offset, max_drawcount, stride);
}

/* ---- Buffers ---- */
VkBuffer backend_get_or_create_buffer(GLuint name, const void* data,
                                      size_t size) {
    return MITHRIL_BC(get_or_create_buffer, name, data, size);
}
VkBuffer backend_create_buffer_storage(GLuint name, VkDeviceSize size,
                                       VkBufferUsageFlags extra_usage,
                                       bool persistent, bool coherent) {
    return MITHRIL_BC(create_buffer_storage, name, size, extra_usage,
                      persistent, coherent);
}
void backend_buffer_upload(GLuint name, GLintptr offset, const void* data,
                           size_t size) {
    MITHRIL_BC(buffer_upload, name, offset, data, size);
}
void* backend_get_buffer_mapped_pointer(GLuint name) {
    return MITHRIL_BC(get_buffer_mapped_pointer, name);
}
VkBuffer backend_get_buffer(GLuint name) { return MITHRIL_BC(get_buffer, name); }
VkDeviceSize backend_get_buffer_capacity(GLuint name) {
    return MITHRIL_BC(get_buffer_capacity, name);
}
void backend_delete_buffer(GLuint name) { MITHRIL_BC(delete_buffer, name); }
VkBuffer backend_get_zero_buffer(void)  { return MITHRIL_BC(get_zero_buffer); }
void backend_update_generic_attribs(const float* values, int count) {
    MITHRIL_BC(update_generic_attribs, values, count);
}
VkBuffer backend_get_generic_attrib_buffer(void) {
    return MITHRIL_BC(get_generic_attrib_buffer);
}

/* ---- Textures ---- */
VkImage backend_get_or_create_texture(GLuint name, int width, int height,
                                      int depth, int levels,
                                      GLenum internal_format, GLenum target,
                                      int samples) {
    return MITHRIL_BC(get_or_create_texture, name, width, height, depth,
                      levels, internal_format, target, samples);
}
void backend_texture_upload(GLuint name, int level, int x, int y, int z,
                            int w, int h, int d, GLenum format, GLenum type,
                            const void* pixels,
                            const struct MGUnpackParams* unpack,
                            int is_full_upload) {
    MITHRIL_BC(texture_upload, name, level, x, y, z, w, h, d, format, type,
               pixels, unpack, is_full_upload);
}
void backend_texture_upload_compressed(GLuint name, int level, int x, int y,
                                       int z, int w, int h, int d,
                                       GLenum internalFormat, GLsizei dataLen,
                                       const void* pixels,
                                       int is_full_upload) {
    MITHRIL_BC(texture_upload_compressed, name, level, x, y, z, w, h, d,
               internalFormat, dataLen, pixels, is_full_upload);
}
void backend_texture_set_params(GLuint name, GLint min_filter, GLint mag_filter,
                                GLint wrap_s, GLint wrap_t, GLint wrap_r,
                                const float* border_color) {
    MITHRIL_BC(texture_set_params, name, min_filter, mag_filter, wrap_s,
               wrap_t, wrap_r, border_color);
}
VkImageView backend_get_texture_view(GLuint name) {
    return MITHRIL_BC(get_texture_view, name);
}
VkImage backend_get_texture_image(GLuint name) {
    return MITHRIL_BC(get_texture_image, name);
}
void backend_delete_texture(GLuint name) { MITHRIL_BC(delete_texture, name); }
void backend_clear_texture(GLuint name, int level, int x, int y, int z,
                           int w, int h, int d, GLenum format, GLenum type,
                           const void* data) {
    MITHRIL_BC(clear_texture, name, level, x, y, z, w, h, d, format, type,
               data);
}
void backend_invalidate_sampler_cache(GLuint name) {
    MITHRIL_BC(invalidate_sampler_cache, name);
}
void backend_transition_texture_layout(GLuint name, VkImageLayout target) {
    MITHRIL_BC(transition_texture_layout, name, target);
}
void backend_generate_mipmaps(GLuint name) { MITHRIL_BC(generate_mipmaps, name); }
int  backend_read_pixels(int x, int y, int w, int h, GLenum format, GLenum type,
                         void* out_pixels) {
    return MITHRIL_BC(read_pixels, x, y, w, h, format, type, out_pixels);
}
void backend_blit_texture(GLuint src_name, GLuint dst_name,
                          int srcX0, int srcY0, int srcX1, int srcY1,
                          int dstX0, int dstY0, int dstX1, int dstY1,
                          GLbitfield mask, GLenum filter) {
    MITHRIL_BC(blit_texture, src_name, dst_name, srcX0, srcY0, srcX1, srcY1,
               dstX0, dstY0, dstX1, dstY1, mask, filter);
}
void backend_blit_images(VkImage src_image, VkFormat src_format,
                         VkImage dst_image, VkFormat dst_format,
                         int srcX0, int srcY0, int srcX1, int srcY1,
                         int dstX0, int dstY0, int dstX1, int dstY1,
                         GLbitfield mask, GLenum filter,
                         int is_dst_default_fbo, int dst_height) {
    MITHRIL_BC(blit_images, src_image, src_format, dst_image, dst_format,
               srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask,
               filter, is_dst_default_fbo, dst_height);
}
void backend_resolve_images(VkImage src_image, VkFormat src_format,
                            VkImage dst_image, VkFormat dst_format,
                            int x, int y, int width, int height,
                            GLbitfield mask,
                            int is_dst_default_fbo, int dst_height) {
    MITHRIL_BC(resolve_images, src_image, src_format, dst_image, dst_format,
               x, y, width, height, mask, is_dst_default_fbo, dst_height);
}

/* ---- Samplers / formats ---- */
VkSampler backend_get_or_create_sampler(GLuint name, GLint min_filter,
                                        GLint mag_filter, GLint wrap_s,
                                        GLint wrap_t, GLint wrap_r,
                                        const float* border_color) {
    return MITHRIL_BC(get_or_create_sampler, name, min_filter, mag_filter,
                      wrap_s, wrap_t, wrap_r, border_color);
}
VkFormat backend_vk_format_for_gl(GLenum internal_format) {
    return MITHRIL_BC(vk_format_for_gl, internal_format);
}

/* ---- Pipelines ---- */
VkPipeline backend_get_or_create_pipeline(GLuint program,
                                          const uint32_t* vertex_spirv,
                                          int vertex_word_count,
                                          const uint32_t* fragment_spirv,
                                          int fragment_word_count,
                                          const struct MGVertexAttrib* attribs,
                                          int attrib_count,
                                          const VkFormat* color_formats,
                                          int color_count,
                                          VkFormat depth_format,
                                          int blend_enabled,
                                          GLenum blend_src, GLenum blend_dst,
                                          GLenum blend_src_alpha,
                                          GLenum blend_dst_alpha,
                                          int color_write_mask,
                                          GLenum gl_primitive_mode,
                                          int is_default_fbo) {
    return MITHRIL_BC(get_or_create_pipeline, program, vertex_spirv,
                      vertex_word_count, fragment_spirv, fragment_word_count,
                      attribs, attrib_count, color_formats, color_count,
                      depth_format, blend_enabled, blend_src, blend_dst,
                      blend_src_alpha, blend_dst_alpha, color_write_mask,
                      gl_primitive_mode, is_default_fbo);
}
VkPipeline backend_get_or_create_compute_pipeline(GLuint program) {
    return MITHRIL_BC(get_or_create_compute_pipeline, program);
}
void backend_dispatch_compute(uint32_t gx, uint32_t gy, uint32_t gz) {
    MITHRIL_BC(dispatch_compute, gx, gy, gz);
}
void backend_dispatch_compute_indirect(VkBuffer buffer, VkDeviceSize offset) {
    MITHRIL_BC(dispatch_compute_indirect, buffer, offset);
}
void backend_memory_barrier(GLbitfield barriers) {
    MITHRIL_BC(memory_barrier, barriers);
}
void backend_delete_program_resources(GLuint program) {
    MITHRIL_BC(delete_program_resources, program);
}

/* ---- Sync serials ---- */
uint64_t backend_last_completed_serial(void) { return MITHRIL_BC(last_completed_serial); }
uint64_t backend_current_submit_serial(void) { return MITHRIL_BC(current_submit_serial); }
bool backend_wait_serial(uint64_t serial, uint64_t timeout_ns) {
    return MITHRIL_BC(wait_serial, serial, timeout_ns);
}

/* ---- Queries ---- */
bool backend_query_pool_create(uint64_t query_id, int kind) {
    return MITHRIL_BC(query_pool_create, query_id, kind);
}
void backend_query_pool_destroy(uint64_t query_id) {
    MITHRIL_BC(query_pool_destroy, query_id);
}
void backend_query_begin(uint64_t query_id) { MITHRIL_BC(query_begin, query_id); }
void backend_query_end(uint64_t query_id)   { MITHRIL_BC(query_end, query_id); }
void backend_query_write_timestamp(uint64_t query_id) {
    MITHRIL_BC(query_write_timestamp, query_id);
}
bool backend_query_get_results(uint64_t query_id, bool wait, uint64_t* out,
                               bool* available) {
    return MITHRIL_BC(query_get_results, query_id, wait, out, available);
}
void backend_query_copy_results(uint64_t query_id, uint32_t gl_buffer_id,
                                VkDeviceSize offset, bool with_availability) {
    MITHRIL_BC(query_copy_results, query_id, gl_buffer_id, offset,
               with_availability);
}
uint32_t backend_query_timestamp_valid_bits(void) {
    return MITHRIL_BC(query_timestamp_valid_bits);
}
uint64_t backend_query_timestamp_now_ns(void) {
    return MITHRIL_BC(query_timestamp_now_ns);
}

/* ---- Program descriptors ---- */
void backend_ensure_program_layouts(GLuint program, const uint32_t* vs,
                                    int vs_words, const uint32_t* fs,
                                    int fs_words) {
    MITHRIL_BC(ensure_program_layouts, program, vs, vs_words, fs, fs_words);
}
void backend_bind_program_descriptors(GLuint program) {
    MITHRIL_BC(bind_program_descriptors, program);
}

/* ---- Presentation ---- */
void backend_present_and_acquire(void* s) { MITHRIL_BC(present_and_acquire, s); }
void* backend_create_swapchain(void* native_window, int width, int height,
                               int want_depth_stencil, int platform_hint) {
    return MITHRIL_BC(create_swapchain, native_window, width, height,
                      want_depth_stencil, platform_hint);
}
void backend_destroy_swapchain(void* s) { MITHRIL_BC(destroy_swapchain, s); }
VkImageView backend_swapchain_acquire_color(void* s) {
    return MITHRIL_BC(swapchain_acquire_color, s);
}
VkImageView backend_swapchain_acquire_depth(void* s) {
    return MITHRIL_BC(swapchain_acquire_depth, s);
}
int backend_swapchain_width(void* s)  { return MITHRIL_BC(swapchain_width, s); }
int backend_swapchain_height(void* s) { return MITHRIL_BC(swapchain_height, s); }
VkImage backend_swapchain_current_color_image(void* s) {
    return MITHRIL_BC(swapchain_current_color_image, s);
}
VkFormat backend_swapchain_color_format(void* s) {
    return MITHRIL_BC(swapchain_color_format, s);
}
VkImage backend_swapchain_current_depth_image(void* s) {
    return MITHRIL_BC(swapchain_current_depth_image, s);
}
VkFormat backend_swapchain_depth_format(void* s) {
    return MITHRIL_BC(swapchain_depth_format, s);
}

/* ---- Device limits ---- */
int backend_device_limit(int which, int fallback) {
    return MITHRIL_BC(device_limit, which, fallback);
}

/* ------------------------------------------------------------------ *
 * Recovery family — DirectVulkan side is namespace-scoped C++ (see the
 * forward declarations above), so these are forwarded manually.
 * ------------------------------------------------------------------ */
int backend_poll_completed_frames(void) {
#if MITHRIL_HAS_METAL
    if (kind_is_metal()) return dmt_poll_completed_frames();
#endif
    return mithril::vk::dvk_poll_completed_frames();
}

int backend_is_device_lost(void) {
#if MITHRIL_HAS_METAL
    if (kind_is_metal()) return dmt_is_device_lost();
#endif
    return mithril::vk::dvk_is_device_lost() ? 1 : 0;
}

void backend_reset_device_lost(void) {
#if MITHRIL_HAS_METAL
    if (kind_is_metal()) { dmt_reset_device_lost(); return; }
#endif
    mithril::vk::dvk_reset_device_lost();
}

void backend_purge_cached_resources_for_recovery(void) {
#if MITHRIL_HAS_METAL
    if (kind_is_metal()) { dmt_purge_cached_resources_for_recovery(); return; }
#endif
    mithril::vk::dvk_purge_cached_resources_for_recovery();
}

void backend_wait_idle_safe(void) {
#if MITHRIL_HAS_METAL
    if (kind_is_metal()) { dmt_wait_idle_safe(); return; }
#endif
    mithril::vk::safe_device_wait_idle();
}

/* ------------------------------------------------------------------ *
 * Pure logic: texel size of a GL (format, type) pair. Backend-independent
 * (mirrors DirectVulkan/FormatMap.cpp host_texel_bytes so MG_Impl texture
 * paths do not need a backend round-trip).
 * ------------------------------------------------------------------ */
/* Legacy/DESKTOP client-format enums missing from the project's minimal
 * GL headers (values from the GL registry; identical everywhere). */
#ifndef GL_RED
#define GL_RED 0x1903
#endif
#ifndef GL_RG
#define GL_RG 0x8227
#endif
#ifndef GL_RG_INTEGER
#define GL_RG_INTEGER 0x8228
#endif
#ifndef GL_RED_INTEGER
#define GL_RED_INTEGER 0x8D22
#endif
#ifndef GL_RGB_INTEGER
#define GL_RGB_INTEGER 0x8D35
#endif
#ifndef GL_RGBA_INTEGER
#define GL_RGBA_INTEGER 0x8D36
#endif
#ifndef GL_BGR
#define GL_BGR 0x80E0
#endif
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#ifndef GL_LUMINANCE
#define GL_LUMINANCE 0x1909
#endif
#ifndef GL_LUMINANCE_ALPHA
#define GL_LUMINANCE_ALPHA 0x190A
#endif
#ifndef GL_INTENSITY
#define GL_INTENSITY 0x8049
#endif

int backend_host_texel_bytes(GLenum format, GLenum type) {
    int comp = 4;
    switch (format) {
        case GL_RED:
        case GL_RED_INTEGER:
        case GL_LUMINANCE:
        case GL_ALPHA:
        case GL_INTENSITY:        comp = 1; break;
        case GL_RG:
        case GL_RG_INTEGER:
        case GL_LUMINANCE_ALPHA:  comp = 2; break;
        case GL_RGB:
        case GL_RGB_INTEGER:
        case GL_BGR:              comp = 3; break;
        case GL_RGBA:
        case GL_RGBA_INTEGER:
        case GL_BGRA:             comp = 4; break;
        default: break;
    }
    switch (type) {
        case GL_UNSIGNED_BYTE:            return comp;
        case GL_BYTE:                     return comp;
        case GL_UNSIGNED_SHORT:
        case GL_SHORT:
        case GL_HALF_FLOAT:               return comp * 2;
        case GL_UNSIGNED_INT:
        case GL_INT:
        case GL_FLOAT:                    return comp * 4;
        case GL_UNSIGNED_SHORT_5_6_5:
        case GL_UNSIGNED_SHORT_4_4_4_4:
        case GL_UNSIGNED_SHORT_5_5_5_1:   return 2;
        case GL_UNSIGNED_INT_8_8_8_8:
        case GL_UNSIGNED_INT_8_8_8_8_REV: return 4;
        default:                          return comp;
    }
}
