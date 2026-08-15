// Mithril-Wrapper - MG_Backend/DirectVulkan/DescriptorSet.h
// SPIR-V reflection (via SPIRV-Cross) -> VkDescriptorSetLayout / VkPipelineLayout
// / VkDescriptorPool, plus per-frame VkDescriptorSet allocation + write + bind.
//
// This closes the gap left by Pipeline.cpp's former empty pipeline layout:
// uniform buffers (GLSL `uniform` globals -> UBOs) and sampled images
// (`uniform sampler2D` etc.) now reach the shader. Reflection runs once at
// link time (inside dvk_get_or_create_pipeline); descriptor set binding
// runs per draw (inside prepare_draw, right after dvk_bind_pipeline).
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
 *   cs / cs_words : compute-stage SPIR-V words (may be NULL/0)
 *
 * A compute program passes NULL for vs/fs and vice versa — the reflection pass
 * simply skips absent stages, so one entry point serves both pipeline kinds.
 *
 * Safe to call from the link path (single-threaded). On a program with no
 * reflected bindings, pr.pipelineLayout is left VK_NULL_HANDLE and the caller
 * falls back to the process-wide empty layout.
 */
void ensure_program_layouts(GLuint program,
                            const uint32_t* vs, int vs_words,
                            const uint32_t* fs, int fs_words,
                            const uint32_t* cs, int cs_words);

/*
 * Allocate a fresh VkDescriptorSet (from the program's pool, reset once per
 * frame), populate it from the current Program.uniforms + g_state->boundTextures,
 * and vkCmdBindDescriptorSets it to the active command buffer. No-op when the
 * program has no descriptor bindings (or no pipeline layout).
 *
 * Must be called after dvk_bind_pipeline() and before the draw, with a
 * recording command buffer active.
 */
void bind_program_descriptors(GLuint program, VkPipelineBindPoint bindPoint);

// Reset per-bind-point descriptor-set shadow state. Called at command-buffer
// boundaries (render-pass end, compute submit, device-lost recovery) so the
// next vkCmdBindDescriptorSets re-binds instead of trusting a stale cache.
void on_command_buffer_boundary();

/*
 * FIX (GPU page fault root cause - descriptor set references destroyed resource):
 * Invalidate the per-program descriptor-set memo (descMemo) across ALL slots.
 *
 * bind_program_descriptors() caches a VkDescriptorSet per (program, slot) keyed
 * by a content signature that includes the VkImageView/VkBuffer handles it
 * references. When a texture/buffer/sampler is deferred-destroyed, the cached
 * set may still point at the now-freed view. If the driver later reuses the
 * same handle value and the memo returns the stale set WITHOUT rewriting it, the
 * next vkQueueSubmit reads a freed resource -> kIOGPUCommandBufferCallbackErrorPageFault.
 *
 * This is called from defer_destroy_texture_entry / defer_destroy_buffer_entry /
 * defer_destroy_sampler_entry so a destroyed resource can never be re-bound
 * from the memo. The memo miss path (setCursor reuse / fresh allocate) always
 * rewrites the set with fresh descriptors, so it is safe after teardown.
 */
void invalidate_descriptor_memo();

/*
 * FIX (swapchain rebuild death loop): Reset ALL descriptor pools across ALL
 * programs and ALL frame-in-flight slots. Frees every allocated
 * VkDescriptorSet, releasing the Metal descriptor resources they hold.
 *
 * Called by dvk_purge_cached_resources_for_recovery() BEFORE a swapchain
 * rebuild attempt during deviceLost recovery. Without this, hundreds of
 * VkDescriptorSet objects (each backed by MoltenVK/Metal descriptor resources)
 * remain allocated, consuming memory that the swapchain creation needs.
 *
 * After reset, the next bind_program_descriptors() call per program/slot will
 * re-allocate descriptor sets from the freshly-reset pool. The cursor rewind
 * at frame start ensures this happens cleanly.
 */
void reset_all_descriptor_pools();

/*
 * FIX (GPU page fault root cause — re-entrant mid-frame purge):
 * Request a full pipeline + descriptor-pool purge. reset_all_descriptor_pools()
 * + clear_all_pipeline_caches() are only safe when the current command buffer
 * has NO live descriptor-set / pipeline binds. If one is actively recording
 * (a render pass open, or a descriptor set already vkCmdBindDescriptorSets'd
 * into it), purging re-entrantly would vkResetDescriptorPool the current slot's
 * pool — invalidating the sets that buffer already references — and destroy
 * its bound pipeline, so the next vkQueueSubmit faults the GPU
 * (kIOGPUCommandBufferCallbackErrorPageFault). In that case this defers the
 * purge to process_pending_purge(), run at the next safe command-buffer
 * boundary (see ensure_command_buffer_recording). If it is already safe it
 * runs immediately.
 */
void request_purge();

// Run the deferred purge now if (a) one is pending and (b) it is safe (the
// current buffer is not recording with live binds). Called at command-buffer
// boundaries after the buffer has been flushed/re-begun empty.
void process_pending_purge();

/*
 * Ensure the process-wide default 1x1 black texture (view + sampler) is ready.
 * Returns true iff a valid (view, sampler) pair is available for the descriptor
 * completeness fallback.
 *
 * FIX (GPU page fault root cause): default_texture() was lazily initialized on
 * first bind. Under VRAM pressure during world-load (block/entity atlas spikes),
 * that first init could itself fail to allocate the image / transition it /
 * create the sampler. The descriptor completeness guard then had NO valid
 * fallback, so a COMBINED_IMAGE_SAMPLER binding was left UNWRITTEN in the set.
 * MoltenVK samples that uninitialized descriptor as a garbage address on the
 * GPU → kIOGPUCommandBufferCallbackErrorPageFault at the next vkQueueSubmit.
 *
 * Eagerly creating the tiny 1x1 texture at device init (when memory is
 * plentiful) guarantees the fallback is always available later.
 */
bool ensure_default_texture_ready();

} // namespace vk
} // namespace mithril

#endif // MITHRIL_DIRECTVULKAN_DESCRIPTOR_SET_H
