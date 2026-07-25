// Mithril-Wrapper - MG_Backend/Backend.h
// Abstract backend interface (C API) for the Vulkan 1.2 / MoltenVK backend.
//
// This is the Vulkan equivalent of the former metal/metal_context.h +
// metal_objects.h + metal_pipeline.h trio. The implementation lives in
// MG_Backend/DirectVulkan/ and talks to Vulkan directly; MoltenVK then
// cross-translates the SPIR-V shaders and Vulkan commands to Metal 2
// internally, so no Metal code remains in this project.
//
// Handles passed across this boundary (VkBuffer / VkImage / VkImageView /
// VkSampler / VkPipeline) are real Vulkan handles. The GL frontend never
// creates or destroys them directly — it goes through backend_get_or_create_*.
#ifndef MITHRIL_BACKEND_H
#define MITHRIL_BACKEND_H

#include <cstdint>
#include <cstddef>

#include <vulkan/vulkan.h>
#include <GL/gl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Lifecycle ----
 * backend_init() creates the VkInstance / VkPhysicalDevice / VkDevice /
 * VkQueue / VkCommandPool once. It is idempotent. backend_available() reports
 * whether the Vulkan backend came up (eglInitialize gates on this).
 */
void backend_init(void);
void backend_shutdown(void);
int  backend_available(void);

/*
 * Physical-device introspection (used by Getter_gpu.cpp to build the
 * GL_RENDERER string from VkPhysicalDeviceProperties instead of MTLDevice).
 */
const char* backend_physical_device_name(void);   // e.g. "Apple A14 GPU"
uint64_t    backend_vram_bytes(void);             // 0 if unknown

/* ---- Swapchain (created/owned by EGL) ----
 * EGL installs the per-frame swapchain color/depth VkImageViews onto the
 * active GLState (eglDefaultColor/Depth). The backend never creates the
 * swapchain itself — that is the EGL layer's job (it owns the CAMetalLayer).
 */

/* ---- Clear values applied to the load op of the next render pass ---- */
void backend_set_clear_color(float r, float g, float b, float a);
void backend_set_clear_depth(double d);
void backend_set_clear_stencil(int s);

/* Load op for the next pass: CLEAR (glClear) or LOAD (draw pass). */
void backend_set_load_clear(void);
void backend_set_load_load(void);

/*
 * Begin a dynamic-rendering pass against the given attachments.
 *   color_views : array of VkImageView (VK_NULL_HANDLE entries allowed)
 *   color_count : number of color attachments
 *   depth_view  : VkImageView for depth/stencil (may be VK_NULL_HANDLE)
 *   width/height: render area
 *   samples     : 1 for now
 * If a pass is already active, this is a no-op (coalesce draws into one pass).
 */
void backend_begin_render_pass(VkImageView* color_views, int color_count,
                               VkImageView depth_view, int width, int height,
                               int samples);

/* End + commit the active render pass / command buffer. */
void backend_end_render_pass(void);
void backend_commit(void);

/*
 * Encoder-side dynamic state setters (vkCmdSet* under dynamic rendering).
 * Each is a no-op when no render pass is active. Stage: 0 = vertex, 1 = fragment
 * (used for buffer/texture/sampler binding).
 */
void backend_bind_pipeline(VkPipeline pipeline);
void backend_set_viewport(int x, int y, int w, int h, double znear, double zfar);
void backend_set_scissor(int x, int y, int w, int h);
void backend_set_vertex_buffer(int slot, VkBuffer buffer, VkDeviceSize offset);
void backend_set_fragment_buffer(int slot, VkBuffer buffer, VkDeviceSize offset);
void backend_set_vertex_texture(int slot, VkImageView view, VkSampler sampler);
void backend_set_fragment_texture(int slot, VkImageView view, VkSampler sampler);
void backend_set_blend_color(float r, float g, float b, float a);
void backend_set_depth_bias(float slope, float clamp);
void backend_set_cull_mode(int mode);        /* 0=None,1=Front,2=Back */
void backend_set_front_face(int ccw);        /* 1=CCW, 0=CW */
void backend_set_depth_test(int enabled, int write_mask, int compare_func);
void backend_set_color_write_mask(int r, int g, int b, int a);
void backend_set_stencil_state(int enabled, int func, int ref, int mask,
                               int sfail, int dpfail, int dppass);

/* Draw primitives. `index_type` 0=U16 (VK_INDEX_TYPE_UINT16), 1=U32. */
void backend_draw_arrays(int primitive, int first, int count);
void backend_draw_indexed(int primitive, int count, int index_type,
                          VkBuffer index_buffer, VkDeviceSize index_offset);
void backend_draw_arrays_instanced(int primitive, int first, int count, int primcount);
void backend_draw_indexed_instanced(int primitive, int count, int index_type,
                                    VkBuffer index_buffer, VkDeviceSize index_offset,
                                    int primcount);

/* ---- Buffers ---- */
VkBuffer backend_get_or_create_buffer(GLuint name, const void* data, size_t size);
void     backend_buffer_upload(GLuint name, GLintptr offset, const void* data, size_t size);
VkBuffer backend_get_buffer(GLuint name);
void     backend_delete_buffer(GLuint name);

/* Shared 16-byte zero-filled buffer for unbound vertex attribute slots. */
VkBuffer backend_get_zero_buffer(void);

/* ---- Textures ---- */
VkImage     backend_get_or_create_texture(GLuint name, int width, int height, int depth,
                                          int levels, GLenum internal_format, GLenum target,
                                          int samples);
void        backend_texture_upload(GLuint name, int level, int x, int y, int z,
                                   int w, int h, int d, GLenum format, GLenum type,
                                   const void* pixels, int unpack_alignment);
void        backend_texture_set_params(GLuint name, GLint min_filter, GLint mag_filter,
                                       GLint wrap_s, GLint wrap_t, GLint wrap_r,
                                       const float* border_color);
VkImageView backend_get_texture_view(GLuint name);
VkImage     backend_get_texture_image(GLuint name);
void        backend_delete_texture(GLuint name);

/* ---- Samplers ---- */
VkSampler backend_get_or_create_sampler(GLuint name, GLint min_filter, GLint mag_filter,
                                        GLint wrap_s, GLint wrap_t, GLint wrap_r,
                                        const float* border_color);

/* ---- Format helpers ----
 * Map a GL internal format to the matching VkFormat. Returns VK_FORMAT_UNDEFINED
 * when the format is unsupported. Used by the drawing path to describe pipeline
 * color/depth attachment formats.
 */
VkFormat backend_vk_format_for_gl(GLenum internal_format);

/* ---- Pipeline cache ----
 * Description of one bound vertex attribute used to build the
 * VkPipelineVertexInputStateCreateInfo.
 */
struct MGVertexAttrib {
    int     location;     /* GL attribute index */
    int     size;         /* 1..4 */
    GLenum  type;         /* GL_FLOAT, GL_UNSIGNED_BYTE, etc. */
    int     normalized;   /* 0/1 */
    int     integer;      /* 0/1 (integer attribs) */
    int     stride;
    int     offset;       /* byte offset within the bound vertex buffer */
    int     enabled;      /* 0/1 */
    GLuint  buffer_name;  /* GL VBO name backing this attrib */
};

/*
 * Build a VkShaderModule from SPIR-V words (cached on the program) and a
 * VkGraphicsPipeline matching the given vertex format and framebuffer
 * color/depth VkFormats. Blend state is part of the pipeline signature.
 *
 *   vertex_spirv / vertex_word_count   : vertex-stage SPIR-V
 *   fragment_spirv / fragment_word_count: fragment-stage SPIR-V (may be NULL/0)
 *   attribs / attrib_count             : enabled vertex attributes
 *   color_formats / color_count        : VkFormat values for color attachments
 *   depth_format                       : VkFormat for depth (VK_FORMAT_UNDEFINED = none)
 *   blend_enabled                      : 0/1
 *   blend_src / blend_dst              : GL blend factor enums (GL_SRC_ALPHA, etc.)
 *   gl_primitive_mode                  : GL primitive mode (cache key only)
 *
 * Returns a cached VkPipeline (VK_NULL_HANDLE on failure).
 */
VkPipeline backend_get_or_create_pipeline(GLuint program,
                                          const uint32_t* vertex_spirv, int vertex_word_count,
                                          const uint32_t* fragment_spirv, int fragment_word_count,
                                          const struct MGVertexAttrib* attribs, int attrib_count,
                                          const VkFormat* color_formats, int color_count,
                                          VkFormat depth_format,
                                          int blend_enabled, GLenum blend_src, GLenum blend_dst,
                                          GLenum gl_primitive_mode);

/* Release all Vulkan resources owned by a program (shader modules + pipelines). */
void backend_delete_program_resources(GLuint program);

/*
 * Present + acquire helpers used by eglSwapBuffers. EGL owns the swapchain;
 * these forward into the per-frame command submission. The EGL layer calls
 * backend_end_render_pass() + backend_commit() before backend_present().
 */
void backend_present_and_acquire(void* swapchain_state);

/*
 * Create the Vulkan surface + swapchain for a CAMetalLayer. Returns an opaque
 * pointer the EGL layer holds onto. The depth VkImage/View is created here
 * (Depth32Float + Stencil8 → VK_FORMAT_D32_SFLOAT_S8_UINT).
 *   layer      : CAMetalLayer* (bridged void*)
 *   width/height: drawable size
 *   want_depth_stencil: 1 to allocate a depth/stencil image
 */
void* backend_create_swapchain(void* cametal_layer, int width, int height,
                               int want_depth_stencil);
void  backend_destroy_swapchain(void* swapchain_state);

/* Acquire the next swapchain image and return its color VkImageView (plus the
 * depth VkImageView if allocated). Used by eglSwapBuffers / eglMakeCurrent to
 * install the per-frame attachments on the GLState. */
VkImageView backend_swapchain_acquire_color(void* swapchain_state);
VkImageView backend_swapchain_acquire_depth(void* swapchain_state);
int          backend_swapchain_width(void* swapchain_state);
int          backend_swapchain_height(void* swapchain_state);

#ifdef __cplusplus
}
#endif

#endif // MITHRIL_BACKEND_H
