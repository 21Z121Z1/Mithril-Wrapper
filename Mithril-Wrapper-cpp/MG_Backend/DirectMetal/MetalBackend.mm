// Mithril-Wrapper - MG_Backend/DirectMetal/MetalBackend.mm
// DirectMetal ENTRY LAYER: every dmt_* C entry point of the dual-backend
// contract (Backend.h, renamed backend_ -> dmt_ by the build-generated
// BackendMetalDecls.h) is defined here and forwarded to the module
// implementations:
//
//   MetalDevice.mm         device / queue / UBO arena / serials / limits
//   MetalCommandStream.mm  encoders, shadow state, draws, frame commit
//   MetalResources.mm      buffer/texture/sampler tables + image ops
//   MetalPipeline.mm       SPIR-V -> MSL, PSO caches, draw-time binding
//   MetalQueries.mm        occlusion / timestamp query backing
//   MetalSwapchain.mm      CAMetalLayer drawable + depth management
//
// SIGNATURES: never hand-copied — this TU includes BackendMetalDecls.h, so
// the compiler enforces that every definition below matches the single
// source of truth Backend.h exactly (a drift is a hard build error, not a
// silent ABI mismatch).
//
// SEMANTIC MIRRORS (what the Vulkan twin does, and why the Metal version is
// shaped the same):
//   * no-op entry points (set_fbo_attachment_tex_ids, transition_texture_
//     layout, set_color_write_mask, set_fragment_buffer, set_*_texture)
//     exist because Vulkan bakes the concern into objects/layouts Metal does
//     not have — Metal hazard-tracks resources across encoder boundaries and
//     bakes blend+write masks into the PSO signature (color_write_mask is a
//     get_or_create_pipeline cache key).
//   * dmt_commit submits WITHOUT presenting; dmt_present_and_acquire mirrors
//     the vkQueueSubmit-then-vkQueuePresentKHR split the EGL layer drives.
//   * read/blit entry points flush the frame first exactly like their Vulkan
//     twins (dvk_read_pixels / dvk_blit_images_impl) so one-shot command
//     buffers observe the latest pixels.
#ifdef __APPLE__

#import <Foundation/Foundation.h>

#include "BackendMetalDecls.h"   // generated: the full dmt_* contract

#include "MetalDevice.h"
#include "MetalCommandStream.h"
#include "MetalResources.h"
#include "MetalPipeline.h"
#include "MetalQueries.h"
#include "MetalSwapchain.h"

#include "../DirectVulkan/FormatMap.h"   // mithril::vk::gl_internal_to_vk (shared)
#include "../../MG_State/State.h"        // g_state / state_get_program / FBO tables

#include <cstdio>
#include <cstring>
#include <ctime>

namespace mithril {
namespace dmt {

/* ---- Static helper buffers owned by this TU ----------------------------
 * The zero buffer backs disabled vertex-attribute slots (Drawing.cpp binds
 * it at offset 0 for every slot the VAO did not enable); the generic-attrib
 * buffer backs glVertexAttrib* constants (contract API — currently unused by
 * the frontend, kept for C-API completeness). Shared storage is legal for
 * buffers on every Apple platform (including discrete macOS). */

static MetalBuffer* g_zeroBuffer = nullptr;
static MetalBuffer* g_genericAttribs = nullptr;

static MetalBuffer* make_static_buffer(MetalBuffer*& slot, NSUInteger len,
                                       bool zeroFill) {
    if (slot) return slot;
    Backend* b = backend();
    if (!b->initialized) return nullptr;
    @autoreleasepool {
        id<MTLBuffer> buf = [b->device newBufferWithLength:len
                                                   options:MTLResourceStorageModeShared];
        if (buf == nil) return nullptr;
        if (zeroFill && buf.contents) std::memset(buf.contents, 0, len);
        MetalBuffer* w = new MetalBuffer();
        w->buf = buf;
        w->capacity = len;
        w->contents = buf.contents;
        slot = w;
        return w;
    }
}

static uint64_t monotonic_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

} // namespace dmt
} // namespace mithril

/* ========================================================================= *
 * extern "C" entry points (order mirrors Backend.h)
 * ========================================================================= */
extern "C" {

/* ---- Lifecycle + identity ---------------------------------------------- */

void dmt_init(void) {
    mithril::dmt::init_device();
}

void dmt_shutdown(void) {
    mithril::dmt::shutdown_device();
}

int dmt_available(void) {
    mithril::dmt::Backend* b = mithril::dmt::backend();
    return (b && b->initialized) ? 1 : 0;
}

float dmt_device_max_sampler_anisotropy(float fallback) {
    (void)fallback;
    // Every Apple GPU family the backend supports exposes 16x anisotropic
    // filtering (MTLSamplerDescriptor.maxAnisotropy clamps at 16).
    return 16.0f;
}

const char* dmt_physical_device_name(void) {
    static char nameBuf[160];
    mithril::dmt::Backend* b = mithril::dmt::backend();
    if (!b || !b->initialized) return "";
    @autoreleasepool {
        NSString* n = b->device.name;   // e.g. "Apple A17 Pro GPU"
        if (n == nil) return "";
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", n.UTF8String);
    }
    return nameBuf;
}

uint64_t dmt_vram_bytes(void) {
    mithril::dmt::Backend* b = mithril::dmt::backend();
    if (!b || !b->initialized) return 0;
    uint64_t sz = 0;
    if (@available(macOS 10.15, iOS 13.0, *)) {
        // Discrete GPUs: the working-set ceiling (VRAM). Unified memory:
        // reported as a share of physical RAM; fall through to physicalMemory
        // when the device reports nothing usable.
        sz = (uint64_t)b->device.recommendedMaxWorkingSetSize;
    }
    if (sz == 0) sz = (uint64_t)[NSProcessInfo processInfo].physicalMemory;
    return sz;
}

/* ---- Clear state (load op of the next pass) ---------------------------- */

void dmt_set_clear_color(float r, float g, float b, float a) {
    mithril::dmt::EncoderState& e = mithril::dmt::enc();
    e.clearColor[0] = r; e.clearColor[1] = g;
    e.clearColor[2] = b; e.clearColor[3] = a;
}

void dmt_set_clear_depth(double d) {
    mithril::dmt::enc().clearDepth = d;
}

void dmt_set_clear_stencil(int s) {
    mithril::dmt::enc().clearStencil = (uint32_t)s;
}

void dmt_set_load_clear(void) { mithril::dmt::enc().loadClear = true; }
void dmt_set_load_load(void)  { mithril::dmt::enc().loadClear = false; }

void dmt_clear_attachments(GLbitfield mask, int x, int y, int w, int h) {
    mithril::dmt::clear_attachments(mask, x, y, w, h);
}

void dmt_clear_buffer_indexed(GLenum buffer, GLint drawbuffer,
                              const float color[4], float depth,
                              GLuint stencil) {
    mithril::dmt::clear_buffer_indexed(buffer, drawbuffer, color, depth, stencil);
}

/* ---- Render pass ------------------------------------------------------- */

void dmt_begin_render_pass(VkImageView* color_views, int color_count,
                           VkImageView depth_view, int width, int height,
                           int samples) {
    mithril::dmt::MetalTexture* views[8] = {};
    const int n = color_count > 8 ? 8 : (color_count < 0 ? 0 : color_count);
    for (int i = 0; i < n; ++i) {
        views[i] = mithril::dmt::as_tex(color_views ? color_views[i]
                                                    : VK_NULL_HANDLE);
    }
    mithril::dmt::begin_render_pass(views, n,
                                    mithril::dmt::as_tex(depth_view),
                                    width, height, samples);
}

void dmt_set_fbo_attachment_tex_ids(GLuint* color_tex_ids, int color_count,
                                    GLuint depth_tex_id) {
    // Vulkan-only concern: dynamic-rendering attachment layout barriers
    // (root cause Y). Metal tracks texture hazards itself — no-op.
    (void)color_tex_ids; (void)color_count; (void)depth_tex_id;
}

void dmt_set_invalidate_attachments(uint32_t color_mask, bool depth,
                                    bool stencil) {
    mithril::dmt::EncoderState& e = mithril::dmt::enc();
    e.invalidateColorMask = color_mask;
    e.invalidateDepth = depth;
    e.invalidateStencil = stencil;
}

void dmt_end_render_pass(void) { mithril::dmt::end_render_pass(); }

void dmt_commit(void) {
    // Submit WITHOUT presenting — the Vulkan backend_commit counterpart.
    // eglSwapBuffers follows up with dmt_present_and_acquire.
    mithril::dmt::commit_frame(nullptr);
}

/* ---- Swapchain state --------------------------------------------------- */

void dmt_set_active_swapchain(void* swapchain_state) {
    mithril::dmt::set_active_swapchain(
        static_cast<mithril::dmt::MetalSwapchain*>(swapchain_state));
}

void dmt_swapchain_set_drawable_size(void* swapchain_state, int w, int h) {
    mithril::dmt::swapchain_set_drawable_size(
        static_cast<mithril::dmt::MetalSwapchain*>(swapchain_state), w, h);
}

void dmt_swapchain_mark_rebuild(void* swapchain_state) {
    mithril::dmt::swapchain_mark_rebuild(
        static_cast<mithril::dmt::MetalSwapchain*>(swapchain_state));
}

void dmt_drain_and_detach_swapchain(void) {
    using namespace mithril::dmt;
    Backend* b = backend();
    if (b && b->initialized) {
        // Flush anything still recording against the swapchain's images,
        // then block on every in-flight buffer. waitUntilCompleted is only
        // ever called on COMMITTED buffers here (commit_frame closed them).
        end_render_pass();
        commit_frame(nullptr);
        for (int i = 0; i < MITHRIL_DMT_MAX_FRAMES_IN_FLIGHT; ++i) {
            @autoreleasepool {
                if (b->slotCmd[i] != nil) [b->slotCmd[i] waitUntilCompleted];
            }
        }
    }
    set_active_swapchain(nullptr);
}

int dmt_swapchain_needs_rebuild(void* swapchain_state) {
    return mithril::dmt::swapchain_needs_rebuild(
        static_cast<mithril::dmt::MetalSwapchain*>(swapchain_state)) ? 1 : 0;
}

/* ---- Dynamic state ----------------------------------------------------- */

void dmt_bind_pipeline(VkPipeline pipeline) {
    mithril::dmt::bind_pipeline(mithril::dmt::as_pipeline(pipeline));
}

void dmt_set_viewport(int x, int y, int w, int h, double znear, double zfar) {
    mithril::dmt::set_viewport(x, y, w, h, znear, zfar);
}

void dmt_set_scissor(int x, int y, int w, int h) {
    mithril::dmt::set_scissor(x, y, w, h);
}

void dmt_set_vertex_buffer(int slot, VkBuffer buffer, VkDeviceSize offset) {
    mithril::dmt::set_vertex_buffer(slot, mithril::dmt::as_buffer(buffer),
                                    (NSUInteger)offset);
}

void dmt_set_fragment_buffer(int slot, VkBuffer buffer, VkDeviceSize offset) {
    // No-op twin of the Vulkan stub: fragment UBO/SSBO binding is centralised
    // in bind_program_descriptors (mirrors Drawing.cpp's note that the
    // Vulkan set_fragment_buffer is likewise never called).
    (void)slot; (void)buffer; (void)offset;
}

void dmt_set_vertex_texture(int slot, VkImageView view, VkSampler sampler) {
    // Textures/samplers are resolved from g_state->boundTextures by
    // bind_program_descriptors at draw time — no per-slot encoder state.
    (void)slot; (void)view; (void)sampler;
}

void dmt_set_fragment_texture(int slot, VkImageView view, VkSampler sampler) {
    (void)slot; (void)view; (void)sampler;
}

void dmt_set_blend_color(float r, float g, float b, float a) {
    mithril::dmt::set_blend_color(r, g, b, a);
}

void dmt_set_depth_bias(float slope, float clamp) {
    mithril::dmt::set_depth_bias(slope, clamp);
}

void dmt_set_cull_mode(int mode) { mithril::dmt::set_cull_mode(mode); }
void dmt_set_front_face(int ccw) { mithril::dmt::set_front_face(ccw); }

void dmt_set_depth_test(int enabled, int write_mask, int compare_func) {
    mithril::dmt::set_depth_test(enabled, write_mask, compare_func);
}

void dmt_set_color_write_mask(int r, int g, int b, int a) {
    // Baked into the PSO: color_write_mask is part of the
    // get_or_create_pipeline cache signature, so the pipeline bound for this
    // draw was already created with the current mask (Vulkan uses the dynamic
    // vkCmdSetColorWriteMaskEXT — Metal has no equivalent, hence the
    // per-signature PSO cache).
    (void)r; (void)g; (void)b; (void)a;
}

void dmt_set_stencil_state(int enabled, int func, int ref, int mask,
                           int sfail, int dpfail, int dppass) {
    mithril::dmt::set_stencil_state(enabled, func, ref, mask,
                                    sfail, dpfail, dppass);
}

/* ---- Draws ------------------------------------------------------------- */

void dmt_draw_arrays(int primitive, int first, int count) {
    mithril::dmt::draw_arrays((GLenum)primitive, first, count);
}

void dmt_draw_indexed(int primitive, int count, int index_type,
                      VkBuffer index_buffer, VkDeviceSize index_offset) {
    mithril::dmt::draw_indexed((GLenum)primitive, count, index_type,
                               mithril::dmt::as_buffer(index_buffer),
                               (NSUInteger)index_offset);
}

void dmt_draw_arrays_instanced(int primitive, int first, int count,
                               int primcount) {
    mithril::dmt::draw_arrays_instanced((GLenum)primitive, first, count,
                                        primcount);
}

void dmt_draw_indexed_instanced(int primitive, int count, int index_type,
                                VkBuffer index_buffer, VkDeviceSize index_offset,
                                int primcount) {
    mithril::dmt::draw_indexed_instanced((GLenum)primitive, count, index_type,
                                         mithril::dmt::as_buffer(index_buffer),
                                         (NSUInteger)index_offset, primcount);
}

void dmt_draw_indirect(int primitive, VkBuffer indirect_buffer,
                       VkDeviceSize indirect_offset,
                       int draw_count, int stride) {
    mithril::dmt::draw_indirect((GLenum)primitive,
                                mithril::dmt::as_buffer(indirect_buffer),
                                (NSUInteger)indirect_offset, draw_count, stride);
}

void dmt_draw_indexed_indirect(int primitive, int index_type,
                               VkBuffer index_buffer, VkDeviceSize index_offset,
                               VkBuffer indirect_buffer,
                               VkDeviceSize indirect_offset,
                               int draw_count, int stride) {
    mithril::dmt::draw_indexed_indirect((GLenum)primitive, index_type,
                                        mithril::dmt::as_buffer(index_buffer),
                                        (NSUInteger)index_offset,
                                        mithril::dmt::as_buffer(indirect_buffer),
                                        (NSUInteger)indirect_offset,
                                        draw_count, stride);
}

void dmt_draw_indirect_count(int primitive, VkBuffer indirect_buffer,
                             VkDeviceSize indirect_offset,
                             VkBuffer count_buffer, VkDeviceSize count_offset,
                             int max_drawcount, int stride) {
    mithril::dmt::draw_indirect_count((GLenum)primitive,
                                      mithril::dmt::as_buffer(indirect_buffer),
                                      (NSUInteger)indirect_offset,
                                      mithril::dmt::as_buffer(count_buffer),
                                      (NSUInteger)count_offset,
                                      max_drawcount, stride);
}

void dmt_draw_indexed_indirect_count(int primitive, int index_type,
                                     VkBuffer index_buffer,
                                     VkDeviceSize index_offset,
                                     VkBuffer indirect_buffer,
                                     VkDeviceSize indirect_offset,
                                     VkBuffer count_buffer,
                                     VkDeviceSize count_offset,
                                     int max_drawcount, int stride) {
    mithril::dmt::draw_indexed_indirect_count(
        (GLenum)primitive, index_type,
        mithril::dmt::as_buffer(index_buffer), (NSUInteger)index_offset,
        mithril::dmt::as_buffer(indirect_buffer), (NSUInteger)indirect_offset,
        mithril::dmt::as_buffer(count_buffer), (NSUInteger)count_offset,
        max_drawcount, stride);
}

/* ---- Buffers ----------------------------------------------------------- */

VkBuffer dmt_get_or_create_buffer(GLuint name, const void* data, size_t size) {
    return mithril::dmt::to_vkbuf(
        mithril::dmt::dmt_internal_get_or_create_buffer(name, data, size));
}

VkBuffer dmt_create_buffer_storage(GLuint name, VkDeviceSize size,
                                   VkBufferUsageFlags extra_usage,
                                   bool persistent, bool coherent) {
    // extra_usage: MTLBuffer usage is context-free (any encoder may consume a
    // buffer), so Vulkan usage bits have no Metal counterpart.
    // coherent: Shared/Managed storage already provides the GL_MAP_COHERENT
    //_BIT visibility model this flag selects on the Vulkan side.
    (void)extra_usage; (void)coherent;
    return mithril::dmt::to_vkbuf(
        mithril::dmt::dmt_internal_create_buffer_storage(
            name, (NSUInteger)size, persistent));
}

void dmt_buffer_upload(GLuint name, GLintptr offset, const void* data,
                       size_t size) {
    mithril::dmt::dmt_internal_buffer_upload(name, offset, data, size);
}

void* dmt_get_buffer_mapped_pointer(GLuint name) {
    mithril::dmt::MetalBuffer* b = mithril::dmt::buffer_table_get(name);
    return (b && b->persistent) ? b->persistentHost : nullptr;
}

VkBuffer dmt_get_buffer(GLuint name) {
    mithril::dmt::MetalBuffer* b = mithril::dmt::buffer_table_get(name);
    return b ? mithril::dmt::to_vkbuf(b) : VK_NULL_HANDLE;
}

VkDeviceSize dmt_get_buffer_capacity(GLuint name) {
    mithril::dmt::MetalBuffer* b = mithril::dmt::buffer_table_get(name);
    return b ? (VkDeviceSize)b->capacity : 0;
}

void dmt_delete_buffer(GLuint name) {
    mithril::dmt::buffer_table_erase(name);
}

VkBuffer dmt_get_zero_buffer(void) {
    // 16 bytes covers the largest vertex format read (vec4) with every
    // component zero — Drawing.cpp binds it at offset 0 to disabled slots.
    mithril::dmt::MetalBuffer* b =
        mithril::dmt::make_static_buffer(mithril::dmt::g_zeroBuffer, 16, true);
    return b ? mithril::dmt::to_vkbuf(b) : VK_NULL_HANDLE;
}

void dmt_update_generic_attribs(const float* values, int count) {
    mithril::dmt::MetalBuffer* b =
        mithril::dmt::make_static_buffer(mithril::dmt::g_genericAttribs,
                                         16 * 16, true);
    if (!b || !values || count <= 0) return;
    if (count > 16) count = 16;
    if (b->contents) std::memcpy(b->contents, values, (size_t)count * 16);
}

VkBuffer dmt_get_generic_attrib_buffer(void) {
    mithril::dmt::MetalBuffer* b =
        mithril::dmt::make_static_buffer(mithril::dmt::g_genericAttribs,
                                         16 * 16, true);
    return b ? mithril::dmt::to_vkbuf(b) : VK_NULL_HANDLE;
}

/* ---- Textures ---------------------------------------------------------- */

VkImage dmt_get_or_create_texture(GLuint name, int width, int height, int depth,
                                  int levels, GLenum internal_format,
                                  GLenum target, int samples) {
    return mithril::dmt::to_vkimg(
        mithril::dmt::get_or_create_texture(name, width, height, depth, levels,
                                            internal_format, target, samples));
}

void dmt_texture_upload(GLuint name, int level, int x, int y, int z,
                        int w, int h, int d, GLenum format, GLenum type,
                        const void* pixels, const MGUnpackParams* unpack,
                        int is_full_upload) {
    mithril::dmt::dmt_internal_texture_upload(name, level, x, y, z, w, h, d,
                                              format, type, pixels, unpack,
                                              is_full_upload);
}

void dmt_texture_upload_compressed(GLuint name, int level, int x, int y,
                                   int z, int w, int h, int d,
                                   GLenum internalFormat, GLsizei dataLen,
                                   const void* pixels, int is_full_upload) {
    mithril::dmt::dmt_internal_texture_upload_compressed(
        name, level, x, y, z, w, h, d, internalFormat, dataLen, pixels,
        is_full_upload);
}

void dmt_texture_set_params(GLuint name, GLint min_filter, GLint mag_filter,
                            GLint wrap_s, GLint wrap_t, GLint wrap_r,
                            const float* border_color) {
    // Params travel WITH every sampler fetch (dmt_internal_get_or_create_
    // sampler receives them from the frontend's current state), so the only
    // duty here is dropping a stale cached MTLSamplerState — the next fetch
    // rebuilds from the new params.
    (void)min_filter; (void)mag_filter;
    (void)wrap_s; (void)wrap_t; (void)wrap_r; (void)border_color;
    mithril::dmt::sampler_table_erase(name);
}

VkImageView dmt_get_texture_view(GLuint name) {
    mithril::dmt::MetalTexture* t = mithril::dmt::texture_table_get(name);
    return t ? mithril::dmt::to_vkview(t) : VK_NULL_HANDLE;
}

VkImage dmt_get_texture_image(GLuint name) {
    mithril::dmt::MetalTexture* t = mithril::dmt::texture_table_get(name);
    return t ? mithril::dmt::to_vkimg(t) : VK_NULL_HANDLE;
}

void dmt_delete_texture(GLuint name) {
    mithril::dmt::texture_table_erase(name);
}

void dmt_clear_texture(GLuint name, int level, int x, int y, int z,
                       int w, int h, int d, GLenum format, GLenum type,
                       const void* data) {
    mithril::dmt::dmt_internal_clear_texture(name, level, x, y, z, w, h, d,
                                             format, type, data);
}

void dmt_invalidate_sampler_cache(GLuint name) {
    mithril::dmt::sampler_table_erase(name);
}

void dmt_transition_texture_layout(GLuint name, VkImageLayout target_layout) {
    // Vulkan image layouts do not exist in Metal; hazard tracking is
    // automatic. No-op by contract (Backend.h: "treated as hints").
    (void)name; (void)target_layout;
}

void dmt_generate_mipmaps(GLuint name) {
    mithril::dmt::dmt_internal_generate_mipmaps(name);
}

int dmt_read_pixels(int x, int y, int w, int h,
                    GLenum format, GLenum type, void* out_pixels) {
    using namespace mithril;
    // Flush pending rendering so the readback observes the latest pixels
    // (same sequence as dvk_read_pixels: end pass + commit, then the
    // one-shot copy in dmt_internal_read_pixels).
    dmt::end_render_pass();
    dmt::commit_frame(nullptr);

    dmt::MetalTexture* src = nullptr;
    if (!g_state || g_state->currentReadFBO == 0) {
        // EGL default framebuffer: the wrapper EGL installed as
        // eglDefaultColorImage (the swapchain's colorTex cookie).
        src = dmt::as_teximg(g_state ? g_state->eglDefaultColorImage
                                     : VK_NULL_HANDLE);
    } else {
        Framebuffer* fbo = state_get_framebuffer(g_state->currentReadFBO);
        if (!fbo || !fbo->colors[0].texture) return 0;
        src = dmt::texture_table_get(fbo->colors[0].texture);
    }
    if (!src || src->tex == nil) return 0;
    return dmt::dmt_internal_read_pixels(src, x, y, w, h, format, type,
                                         out_pixels);
}

void dmt_blit_texture(GLuint src_name, GLuint dst_name,
                      int srcX0, int srcY0, int srcX1, int srcY1,
                      int dstX0, int dstY0, int dstX1, int dstY1,
                      GLbitfield mask, GLenum filter) {
    // Texture-to-texture blit (glCopyTexSubImage path): both sides are user
    // textures in the table, so no default-FBO Y flip applies
    // (is_dst_default_fbo = 0 — the exact pass-through dvk_blit_texture
    // performs).
    mithril::dmt::end_render_pass();
    mithril::dmt::commit_frame(nullptr);
    mithril::dmt::dmt_internal_blit_images_raw(
        mithril::dmt::texture_table_get(src_name),
        mithril::dmt::texture_table_get(dst_name),
        srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1,
        mask, filter, 0, 0);
}

void dmt_blit_images(VkImage src_image, VkFormat src_format,
                     VkImage dst_image, VkFormat dst_format,
                     int srcX0, int srcY0, int srcX1, int srcY1,
                     int dstX0, int dstY0, int dstX1, int dstY1,
                     GLbitfield mask, GLenum filter,
                     int is_dst_default_fbo, int dst_height) {
    // Formats arrive as Vulkan tags but the wrappers carry their own vkFormat
    // (set at creation), so the impl derives everything from the wrappers.
    (void)src_format; (void)dst_format;
    mithril::dmt::end_render_pass();
    mithril::dmt::commit_frame(nullptr);
    mithril::dmt::dmt_internal_blit_images_raw(
        mithril::dmt::as_teximg(src_image),
        mithril::dmt::as_teximg(dst_image),
        srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1,
        mask, filter, is_dst_default_fbo, dst_height);
}

/* MSAA resolve（Backend.h 契约的 Metal 侧实现）。resolve 是 1:1 全纹理操作，
 * 无缩放无镜像 —— is_dst_default_fbo/dst_height 的 Y 翻转对全高度矩形等价于
 * 恒等变换，故不参与。mask 无需分派：Metal 的 blit resolve 对颜色与深度模板
 * 附件一视同仁（Vulkan 核心反而做不到）。 */
void dmt_resolve_images(VkImage src_image, VkFormat src_format,
                        VkImage dst_image, VkFormat dst_format,
                        int x, int y, int width, int height,
                        GLbitfield mask,
                        int is_dst_default_fbo, int dst_height) {
    (void)src_format; (void)dst_format; (void)mask;
    (void)is_dst_default_fbo; (void)dst_height;
    mithril::dmt::end_render_pass();
    mithril::dmt::commit_frame(nullptr);
    mithril::dmt::dmt_internal_resolve_images(
        mithril::dmt::as_teximg(src_image),
        mithril::dmt::as_teximg(dst_image),
        x, y, width, height);
}

/* ---- Samplers / formats ------------------------------------------------ */

VkSampler dmt_get_or_create_sampler(GLuint name, GLint min_filter,
                                    GLint mag_filter, GLint wrap_s,
                                    GLint wrap_t, GLint wrap_r,
                                    const float* border_color) {
    return mithril::dmt::to_vksmp(
        mithril::dmt::dmt_internal_get_or_create_sampler(
            name, min_filter, mag_filter, wrap_s, wrap_t, wrap_r,
            border_color));
}

VkFormat dmt_vk_format_for_gl(GLenum internal_format) {
    // Shared pure-logic table (DirectVulkan/FormatMap.cpp is compiled once
    // and linked by both backends — VkFormat values are the backend-neutral
    // tag space).
    return mithril::vk::gl_internal_to_vk(internal_format);
}

/* ---- Pipelines --------------------------------------------------------- */

VkPipeline dmt_get_or_create_pipeline(GLuint program,
                                      const uint32_t* vertex_spirv,
                                      int vertex_word_count,
                                      const uint32_t* fragment_spirv,
                                      int fragment_word_count,
                                      const MGVertexAttrib* attribs,
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
    // gl_primitive_mode is a Vulkan cache-key only: Metal PSOs are
    // primitive-agnostic (the topology is a draw argument), so it is dropped.
    (void)gl_primitive_mode;
    mithril::dmt::MetalPipeline* p = mithril::dmt::get_or_create_pipeline(
        program, vertex_spirv, vertex_word_count,
        fragment_spirv, fragment_word_count,
        attribs, attrib_count, color_formats, color_count, depth_format,
        blend_enabled, blend_src, blend_dst, blend_src_alpha, blend_dst_alpha,
        color_write_mask, is_default_fbo);
    return p ? mithril::dmt::to_vkpipe(p) : VK_NULL_HANDLE;
}

VkPipeline dmt_get_or_create_compute_pipeline(GLuint program) {
    mithril::Program* prog = mithril::state_get_program(program);
    if (!prog) return VK_NULL_HANDLE;
    mithril::dmt::MetalPipeline* p =
        mithril::dmt::get_or_create_compute_pipeline(program, prog);
    return p ? mithril::dmt::to_vkpipe(p) : VK_NULL_HANDLE;
}

void dmt_dispatch_compute(uint32_t groups_x, uint32_t groups_y,
                          uint32_t groups_z) {
    GLuint program = mithril::g_state ? mithril::g_state->currentProgram : 0;
    mithril::Program* prog = program ? mithril::state_get_program(program)
                                     : nullptr;
    if (!prog) return;
    mithril::dmt::dispatch_compute(program, prog, groups_x, groups_y,
                                   groups_z);
}

void dmt_dispatch_compute_indirect(VkBuffer buffer, VkDeviceSize offset) {
    GLuint program = mithril::g_state ? mithril::g_state->currentProgram : 0;
    mithril::Program* prog = program ? mithril::state_get_program(program)
                                     : nullptr;
    if (!prog) return;
    mithril::dmt::dispatch_compute_indirect(
        program, prog, mithril::dmt::as_buffer(buffer), (NSUInteger)offset);
}

void dmt_memory_barrier(GLbitfield barriers) {
    // GL barrier bits name Vulkan-style scopes; Metal's model:
    //   * ACROSS encoder boundaries (blit -> compute -> render, the shape
    //     every MC/Sodium glMemoryBarrier call site takes) hazard tracking
    //     is automatic — nothing to record.
    //   * WITHIN one render encoder (SSBO write -> later read) TBDR needs an
    //     explicit barrier; issue one when the Metal 3 API is available.
    (void)barriers;
    id<MTLRenderCommandEncoder> re = mithril::dmt::current_encoder();
    if (re != nil) {
        if (@available(macOS 13.0, iOS 16.0, *)) {
            [re memoryBarrierWithScope:MTLBarrierScopeBuffers |
                                          MTLBarrierScopeTextures
                            afterStages:MTLRenderStageFragment
                             beforeStages:MTLRenderStageVertex |
                                           MTLRenderStageFragment];
        }
    }
    // Compute-to-compute same-encoder ordering: compute dispatches through
    // this wrapper always end the encoder between logically distinct GL
    // passes, so the automatic cross-encoder ordering covers them.
}

void dmt_delete_program_resources(GLuint program) {
    mithril::dmt::delete_program_resources(program);
}

/* ---- Sync serials ------------------------------------------------------ */

uint64_t dmt_last_completed_serial(void) {
    return mithril::dmt::backend()->lastCompletedSerial.load(
        std::memory_order_acquire);
}

uint64_t dmt_current_submit_serial(void) {
    return mithril::dmt::backend()->submitSerial.load(
        std::memory_order_acquire);
}

bool dmt_wait_serial(uint64_t serial, uint64_t timeout_ns) {
    using namespace mithril::dmt;
    Backend* b = backend();
    if (!b->initialized) return true;
    if (serial <= b->lastCompletedSerial.load(std::memory_order_acquire))
        return true;
    // The serial was never submitted (fence created before a commit that has
    // not happened yet): no Metal object will ever signal it — do NOT block
    // (mirrors dvk_wait_serial's early-out; glClientWaitSync decides whether
    // to flush first).
    if (serial > b->submitSerial.load(std::memory_order_acquire))
        return false;
    if (b->deviceLost.load(std::memory_order_acquire)) return false;

    // Slot fences do not exist in Metal; the queue is FIFO, so the committed
    // slot buffers complete in serial order. Poll the completion watermark
    // (bumped by the command buffers' addCompletedHandler) until the target
    // serial is covered or the deadline passes.
    const bool infinite = (timeout_ns == UINT64_MAX);
    const uint64_t deadline = monotonic_ns() + timeout_ns;
    for (;;) {
        if (serial <= b->lastCompletedSerial.load(std::memory_order_acquire))
            return true;
        if (b->deviceLost.load(std::memory_order_acquire)) return false;
        if (!infinite && monotonic_ns() >= deadline) return false;
        struct timespec ts = {0, 200 * 1000};   // 200 us
        nanosleep(&ts, nullptr);
    }
}

/* ---- Queries ----------------------------------------------------------- */

bool dmt_query_pool_create(uint64_t query_id, int kind) {
    return mithril::dmt::query_pool_create(query_id, kind);
}

void dmt_query_pool_destroy(uint64_t query_id) {
    mithril::dmt::query_pool_destroy(query_id);
}

void dmt_query_begin(uint64_t query_id) {
    mithril::dmt::query_begin(query_id);
}

void dmt_query_end(uint64_t query_id) {
    mithril::dmt::query_end(query_id);
}

void dmt_query_write_timestamp(uint64_t query_id) {
    mithril::dmt::query_write_timestamp(query_id);
}

bool dmt_query_get_results(uint64_t query_id, bool wait,
                           uint64_t* out, bool* available) {
    return mithril::dmt::query_get_results(query_id, wait, out, available);
}

void dmt_query_copy_results(uint64_t query_id, uint32_t gl_buffer_id,
                            VkDeviceSize offset, bool with_availability) {
    mithril::dmt::query_copy_results(query_id, gl_buffer_id, offset,
                                     with_availability);
}

uint32_t dmt_query_timestamp_valid_bits(void) {
    return mithril::dmt::query_timestamp_valid_bits();
}

uint64_t dmt_query_timestamp_now_ns(void) {
    return mithril::dmt::query_timestamp_now_ns();
}

/* ---- Program descriptors ----------------------------------------------- */

void dmt_ensure_program_layouts(GLuint program, const uint32_t* vs,
                                int vs_words, const uint32_t* fs,
                                int fs_words) {
    // No descriptor-set objects exist in Metal: reflection + binding-index
    // remap (bufIdxVs/bufIdxFs/bufIdxCs) run inside get_or_create_pipeline's
    // MSL compile, and draw-time binding is centralised in
    // bind_program_descriptors. Nothing to pre-build per program.
    (void)program; (void)vs; (void)vs_words; (void)fs; (void)fs_words;
}

void dmt_bind_program_descriptors(GLuint program) {
    mithril::dmt::bind_program_descriptors(program);
}

/* ---- Presentation / swapchain ------------------------------------------ */

void dmt_present_and_acquire(void* swapchain_state) {
    using namespace mithril::dmt;
    MetalSwapchain* sc = static_cast<MetalSwapchain*>(swapchain_state);
    if (!sc) return;
    Backend* b = backend();
    if (!b->initialized) return;

    if (b->cmd != nil) {
        // Frame buffer still recording (caller skipped dmt_commit): encode
        // the present on it and submit — commit_frame handles everything.
        commit_frame(sc);
    } else if (sc->frameAcquired && sc->drawable != nil) {
        // Normal eglSwapBuffers flow: dmt_commit already submitted the frame.
        // Queue the present on a bare follow-up buffer; queue FIFO order
        // lands it after the frame's commands — the exact role of
        // vkQueuePresentKHR after vkQueueSubmit.
        id<MTLCommandBuffer> cb = new_oneshot_command_buffer();
        if (cb != nil) {
            [cb presentDrawable:sc->drawable];
            [cb commit];
        }
    }
    // Release the drawable. The "acquire" half of this entry point is the
    // NEXT swapchain_acquire_color() call — EGL's install_surface_on_state
    // issues it immediately after present (nextDrawable then paces the
    // producer exactly like FIFO vkAcquireNextImageKHR).
    swapchain_present(sc);
}

void* dmt_create_swapchain(void* native_window, int width, int height,
                           int want_depth_stencil, int platform_hint) {
    return mithril::dmt::create_swapchain(native_window, width, height,
                                          want_depth_stencil, platform_hint);
}

void dmt_destroy_swapchain(void* swapchain_state) {
    mithril::dmt::destroy_swapchain(
        static_cast<mithril::dmt::MetalSwapchain*>(swapchain_state));
}

VkImageView dmt_swapchain_acquire_color(void* swapchain_state) {
    mithril::dmt::MetalTexture* t = mithril::dmt::swapchain_acquire_color(
        static_cast<mithril::dmt::MetalSwapchain*>(swapchain_state));
    return t ? mithril::dmt::to_vkview(t) : VK_NULL_HANDLE;
}

VkImageView dmt_swapchain_acquire_depth(void* swapchain_state) {
    mithril::dmt::MetalTexture* t = mithril::dmt::swapchain_acquire_depth(
        static_cast<mithril::dmt::MetalSwapchain*>(swapchain_state));
    return t ? mithril::dmt::to_vkview(t) : VK_NULL_HANDLE;
}

int dmt_swapchain_width(void* swapchain_state) {
    mithril::dmt::MetalSwapchain* sc =
        static_cast<mithril::dmt::MetalSwapchain*>(swapchain_state);
    return sc ? sc->width : 0;
}

int dmt_swapchain_height(void* swapchain_state) {
    mithril::dmt::MetalSwapchain* sc =
        static_cast<mithril::dmt::MetalSwapchain*>(swapchain_state);
    return sc ? sc->height : 0;
}

VkImage dmt_swapchain_current_color_image(void* swapchain_state) {
    mithril::dmt::MetalSwapchain* sc =
        static_cast<mithril::dmt::MetalSwapchain*>(swapchain_state);
    if (!sc || !sc->colorTex || sc->colorTex->tex == nil)
        return VK_NULL_HANDLE;
    return mithril::dmt::to_vkimg(sc->colorTex);
}

VkFormat dmt_swapchain_color_format(void* swapchain_state) {
    mithril::dmt::MetalSwapchain* sc =
        static_cast<mithril::dmt::MetalSwapchain*>(swapchain_state);
    return sc ? sc->colorVkFormat : VK_FORMAT_UNDEFINED;
}

VkImage dmt_swapchain_current_depth_image(void* swapchain_state) {
    mithril::dmt::MetalSwapchain* sc =
        static_cast<mithril::dmt::MetalSwapchain*>(swapchain_state);
    if (!sc || !sc->depthTex || sc->depthTex->tex == nil)
        return VK_NULL_HANDLE;
    return mithril::dmt::to_vkimg(sc->depthTex);
}

VkFormat dmt_swapchain_depth_format(void* swapchain_state) {
    mithril::dmt::MetalSwapchain* sc =
        static_cast<mithril::dmt::MetalSwapchain*>(swapchain_state);
    return sc ? sc->depthVkFormat : VK_FORMAT_UNDEFINED;
}

/* ---- Device limits ----------------------------------------------------- */

int dmt_device_limit(int which, int fallback) {
    return mithril::dmt::device_limit(which, fallback);
}

/* ---- Recovery family --------------------------------------------------- */

int dmt_poll_completed_frames(void) {
    return mithril::dmt::backend()->drainPollBaseline();
}

int dmt_is_device_lost(void) {
    return mithril::dmt::backend()->deviceLost.load(std::memory_order_acquire)
               ? 1 : 0;
}

void dmt_reset_device_lost(void) {
    using namespace mithril::dmt;
    backend()->deviceLost.store(false, std::memory_order_release);
    // A stale passActive/boundPipeline would route the next frame's draws
    // into a dead encoder — rebase the shadow state (mirrors
    // vk::reset_encoder_state usage in the Vulkan recovery path).
    reset_encoder_state();
}

void dmt_purge_cached_resources_for_recovery(void) {
    using namespace mithril::dmt;
    purge_pipeline_caches();   // MSL functions + PSO caches (MetalPipeline.mm)
    reset_encoder_state();
}

void dmt_wait_idle_safe(void) {
    using namespace mithril::dmt;
    Backend* b = backend();
    if (!b->initialized) return;
    // Close any open encoder and SUBMIT before waiting — Metal only defines
    // waitUntilCompleted for committed buffers (an uncommitted wait is
    // undefined). commit_frame skips genuinely empty submits, so this stays
    // cheap for the eglWaitClient-style double flush.
    end_render_pass();
    commit_frame(nullptr);
    for (int i = 0; i < MITHRIL_DMT_MAX_FRAMES_IN_FLIGHT; ++i) {
        @autoreleasepool {
            if (b->slotCmd[i] != nil) [b->slotCmd[i] waitUntilCompleted];
        }
    }
}

} // extern "C"

#endif // __APPLE__
