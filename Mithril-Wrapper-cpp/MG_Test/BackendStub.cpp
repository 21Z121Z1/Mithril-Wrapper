// Mithril-Wrapper - MG_Test/BackendStub.cpp
// Stub implementations of every `backend_*` C API function declared in
// MG_Backend/Backend.h. Linked into the `mithril_tests` binary so the pure-
// logic subsystems under test (State / Shader / FormatMap / EGLConfig) can
// reference the backend symbols without pulling in the real Vulkan backend
// (which would drag in VkDevice / MoltenVK / Metal framework dependencies).
//
// Stubs return safe defaults: 0 / nullptr / VK_NULL_HANDLE. They are NOT
// intended to be called by the unit tests themselves — the pure-logic
// subsystems under test never invoke `backend_*`. The stubs exist purely to
// satisfy the linker when the test binary pulls in translation units (e.g.
// State.cpp) that reference backend_* symbols indirectly through includes.
//
// The signatures below MUST match Backend.h exactly. To keep them in sync,
// this file #includes Backend.h so any signature drift surfaces as a
// compile error rather than a link error.
#include "../MG_Backend/Backend.h"

// VkImageLayout / VkFormat / VkPipeline etc. are pulled in transitively via
// Backend.h -> <vulkan/vulkan.h>. No additional includes are needed.

// ===========================================================================
// Lifecycle
// ===========================================================================
extern "C" {

void backend_init(void)                                  {}
void backend_shutdown(void)                              {}
int  backend_available(void)                             { return 0; }

const char* backend_physical_device_name(void)           { return nullptr; }
uint64_t    backend_vram_bytes(void)                     { return 0; }

// ---- Clear values / load ops ----
void backend_set_clear_color(float, float, float, float) {}
void backend_set_clear_depth(double)                     {}
void backend_set_clear_stencil(int)                      {}
void backend_set_load_clear(void)                        {}
void backend_set_load_load(void)                         {}

// ---- Render pass ----
void backend_begin_render_pass(VkImageView*, int, VkImageView, int, int, int) {}
void backend_end_render_pass(void)                       {}
void backend_commit(void)                                {}

// ---- Dynamic state setters ----
void backend_bind_pipeline(VkPipeline)                                          {}
void backend_set_viewport(int, int, int, int, double, double)                   {}
void backend_set_scissor(int, int, int, int)                                    {}
void backend_set_vertex_buffer(int, VkBuffer, VkDeviceSize)                     {}
void backend_set_fragment_buffer(int, VkBuffer, VkDeviceSize)                   {}
void backend_set_vertex_texture(int, VkImageView, VkSampler)                    {}
void backend_set_fragment_texture(int, VkImageView, VkSampler)                  {}
void backend_set_blend_color(float, float, float, float)                        {}
void backend_set_depth_bias(float, float)                                       {}
void backend_set_cull_mode(int)                                                 {}
void backend_set_front_face(int)                                                {}
void backend_set_depth_test(int, int, int)                                      {}
void backend_set_color_write_mask(int, int, int, int)                           {}
void backend_set_stencil_state(int, int, int, int, int, int, int)               {}

// ---- Draw ----
void backend_draw_arrays(int, int, int)                                         {}
void backend_draw_indexed(int, int, int, VkBuffer, VkDeviceSize)                {}
void backend_draw_arrays_instanced(int, int, int, int)                          {}
void backend_draw_indexed_instanced(int, int, int, VkBuffer, VkDeviceSize, int) {}

// ---- Buffers ----
VkBuffer backend_get_or_create_buffer(GLuint, const void*, size_t)              { return VK_NULL_HANDLE; }
void     backend_buffer_upload(GLuint, GLintptr, const void*, size_t)           {}
VkBuffer backend_get_buffer(GLuint)                                             { return VK_NULL_HANDLE; }
void     backend_delete_buffer(GLuint)                                          {}
VkBuffer backend_get_zero_buffer(void)                                          { return VK_NULL_HANDLE; }

// ---- Textures ----
VkImage backend_get_or_create_texture(GLuint, int, int, int, int, GLenum, GLenum, int) {
    return VK_NULL_HANDLE;
}
void backend_texture_upload(GLuint, int, int, int, int, int, int, int, GLenum,
                            GLenum, const void*, int)                          {}
void backend_texture_set_params(GLuint, GLint, GLint, GLint, GLint, GLint,
                                const float*)                                   {}
VkImageView backend_get_texture_view(GLuint)                                    { return VK_NULL_HANDLE; }
VkImage     backend_get_texture_image(GLuint)                                   { return VK_NULL_HANDLE; }
void        backend_delete_texture(GLuint)                                      {}
void        backend_transition_texture_layout(GLuint, VkImageLayout)            {}
void        backend_generate_mipmaps(GLuint)                                    {}

int backend_read_pixels(int, int, int, int, GLenum, GLenum, void*)              { return 0; }

void backend_blit_texture(GLuint, GLuint, int, int, int, int, int, int, int, int,
                          GLbitfield, GLenum)                                   {}
void backend_blit_images(VkImage, VkFormat, VkImage, VkFormat,
                         int, int, int, int, int, int, int, int,
                         GLbitfield, GLenum)                                    {}

// ---- Samplers ----
VkSampler backend_get_or_create_sampler(GLuint, GLint, GLint, GLint, GLint, GLint,
                                        const float*)                           { return VK_NULL_HANDLE; }

// ---- Format helpers ----
VkFormat backend_vk_format_for_gl(GLenum)                                       { return VK_FORMAT_UNDEFINED; }

// ---- Pipeline cache ----
VkPipeline backend_get_or_create_pipeline(GLuint,
                                          const uint32_t*, int,
                                          const uint32_t*, int,
                                          const struct MGVertexAttrib*, int,
                                          const VkFormat*, int,
                                          VkFormat,
                                          int, GLenum, GLenum, GLenum)         { return VK_NULL_HANDLE; }

void backend_delete_program_resources(GLuint)                                   {}

void backend_ensure_program_layouts(GLuint, const uint32_t*, int,
                                    const uint32_t*, int)                       {}

void backend_bind_program_descriptors(GLuint)                                   {}

// ---- Swapchain ----
void backend_present_and_acquire(void*)                                         {}

void* backend_create_swapchain(void*, int, int, int, int)                    { return nullptr; }
void  backend_destroy_swapchain(void*)                                          {}

VkImageView backend_swapchain_acquire_color(void*)                              { return VK_NULL_HANDLE; }
VkImageView backend_swapchain_acquire_depth(void*)                              { return VK_NULL_HANDLE; }
int          backend_swapchain_width(void*)                                     { return 0; }
int          backend_swapchain_height(void*)                                    { return 0; }

VkImage     backend_swapchain_current_color_image(void*)                        { return VK_NULL_HANDLE; }
VkFormat    backend_swapchain_color_format(void*)                               { return VK_FORMAT_UNDEFINED; }
VkImage     backend_swapchain_current_depth_image(void*)                        { return VK_NULL_HANDLE; }
VkFormat    backend_swapchain_depth_format(void*)                               { return VK_FORMAT_UNDEFINED; }

} // extern "C"
