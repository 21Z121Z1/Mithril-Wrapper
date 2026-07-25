// Mithril-Wrapper - MG_Backend/DirectVulkan/Pipeline.h
// VkShaderModule creation (from SPIR-V) + VkGraphicsPipeline caching keyed by
// a hash signature built from (program, vertex format, attachment formats,
// blend state, primitive mode). Implements backend_get_or_create_pipeline()
// and backend_delete_program_resources() declared in MG_Backend/Backend.h.
#ifndef MITHRIL_DIRECTVULKAN_PIPELINE_H
#define MITHRIL_DIRECTVULKAN_PIPELINE_H

#include <vulkan/vulkan.h>
#include <GL/gl.h>

#include <cstdint>
#include <unordered_map>

namespace mithril {
namespace vk {

// Per-program owned Vulkan resources. Each Program GL object gets one of
// these; it holds the shader modules built from the linked SPIR-V and the
// cache of pipelines derived from that program.
struct ProgramResources {
    VkShaderModule vertexModule = VK_NULL_HANDLE;
    VkShaderModule fragmentModule = VK_NULL_HANDLE;
    // Cached pipelines keyed by a 64-bit signature (see Pipeline.cpp).
    std::unordered_map<uint64_t, VkPipeline> pipelines;
};

// Accessor for the per-program resource table (keyed by GL program name).
std::unordered_map<GLuint, ProgramResources>& program_table();

// Build (or fetch from cache) a VkPipeline for the given configuration.
// All arguments mirror backend_get_or_create_pipeline() in Backend.h.
VkPipeline get_or_create_pipeline(GLuint program,
                                  const uint32_t* vertex_spirv, int vertex_word_count,
                                  const uint32_t* fragment_spirv, int fragment_word_count,
                                  const struct MGVertexAttrib* attribs, int attrib_count,
                                  const VkFormat* color_formats, int color_count,
                                  VkFormat depth_format,
                                  int blend_enabled, GLenum blend_src, GLenum blend_dst,
                                  GLenum gl_primitive_mode);

// Release all Vulkan resources owned by a program (shader modules + pipelines).
void delete_program_resources(GLuint program);

} // namespace vk
} // namespace mithril

#endif // MITHRIL_DIRECTVULKAN_PIPELINE_H
