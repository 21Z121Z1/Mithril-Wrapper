// Mithril-Wrapper - MG_Backend/DirectVulkan/DescriptorSet.h
// SPIR-V reflection (via SPIRV-Cross) -> VkDescriptorSetLayout / VkPipelineLayout
// / VkDescriptorPool, plus per-frame VkDescriptorSet allocation + write + bind.
//
// This closes the gap left by Pipeline.cpp's former empty pipeline layout:
// uniform buffers (GLSL `uniform` globals -> UBOs) and sampled images
// (`uniform sampler2D` etc.) now reach the shader. Reflection runs once at
// link time (inside backend_get_or_create_pipeline); descriptor set binding
// runs per draw (inside prepare_draw, right after backend_bind_pipeline).
#ifndef MITHRIL_DIRECTVULKAN_DESCRIPTOR_SET_H
#define MITHRIL_DIRECTVULKAN_DESCRIPTOR_SET_H

#include <vulkan/vulkan.h>
#include <GL/gl.h>

#include "Reflect.h"  // DescriptorBinding / DescriptorBindingMember + reflect_stage / merge_bindings

namespace mithril {
namespace vk {

/*
 * Reflect the vertex + fragment SPIR-V of `program`, merge the binding sets
 * (same set/binding in both stages -> stageMask = VERTEX|FRAGMENT), build a
 * VkDescriptorSetLayout + VkPipelineLayout + VkDescriptorPool, and cache them
 * on the program's ProgramResources. Idempotent (guarded by layoutsBuilt).
 *
 *   vs / vs_words : vertex-stage SPIR-V words (may be NULL/0)
 *   fs / fs_words : fragment-stage SPIR-V words (may be NULL/0)
 *
 * Safe to call from the link path (single-threaded). On a program with no
 * reflected bindings, pr.pipelineLayout is left VK_NULL_HANDLE and the caller
 * falls back to the process-wide empty layout.
 */
void ensure_program_layouts(GLuint program,
                            const uint32_t* vs, int vs_words,
                            const uint32_t* fs, int fs_words);

/*
 * Allocate a fresh VkDescriptorSet (from the program's pool, reset once per
 * frame), populate it from the current Program.uniforms + g_state->boundTextures,
 * and vkCmdBindDescriptorSets it to the active command buffer. No-op when the
 * program has no descriptor bindings (or no pipeline layout).
 *
 * Must be called after backend_bind_pipeline() and before the draw, with a
 * recording command buffer active.
 */
void bind_program_descriptors(GLuint program);

} // namespace vk
} // namespace mithril

#endif // MITHRIL_DIRECTVULKAN_DESCRIPTOR_SET_H
