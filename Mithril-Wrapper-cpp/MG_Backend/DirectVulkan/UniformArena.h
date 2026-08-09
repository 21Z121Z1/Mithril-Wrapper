// Mithril-Wrapper - MG_Backend/DirectVulkan/UniformArena.h
// Per-frame-in-flight linear (bump) allocator for transient uniform data.
//
// Why this exists
// ---------------
// DescriptorSet.cpp used to keep ONE VkBuffer per (program, binding), named
// `program * 1000000 + binding`, and overwrite it in place on every draw. That
// is a GPU/CPU synchronisation violation the moment a frame does
//
//     glUniform...(); glDrawElements();      // draw A
//     glUniform...(); glDrawElements();      // draw B, same program
//
// which is Minecraft's normal pattern (ModelViewMat / ColorModulator change
// between draws). Draw A's command has only been *recorded* at that point, not
// executed; rewriting the buffer for draw B corrupts the data draw A will
// eventually read. The symptom is not a crash but subtly wrong transforms —
// every draw in a batch silently sees the last draw's uniforms.
//
// An arena removes the hazard by construction: each upload gets a FRESH byte
// range, so two draws in a frame can never alias. The range is addressed with
// a dynamic offset (VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC), which also
// keeps the VkDescriptorSet contents constant across draws — that is what lets
// DescriptorSet.cpp's memo reuse a set instead of re-writing it.
//
// Mirrors MobileGL's BufferArena (Renderer/BufferArena.cpp) + VkBufferManager
// ::UploadTransient, adapted to our plain-Vulkan (no VMA) allocation path.
//
// Lifetime / safety
// -----------------
// Storage is per frame-in-flight slot. A slot is only rewound from
// ensure_command_buffer_recording(), i.e. AFTER that slot's fence has been
// waited on, so no in-flight command buffer can still be reading the bytes we
// are about to hand out again. This is the same invariant the texture staging
// arena in Device.h already relies on.
//
// Growth
// ------
// Deliberately NOT one big up-front allocation: Apple devices are memory
// constrained and MoltenVK's maxMemoryAllocationCount is small. Each slot
// starts at 256 KB and grows as a CHAIN of blocks (256 KB, 512 KB, 1 MB, ...
// capped at 4 MB each). Chaining rather than reallocating matters for
// correctness: slices already handed out earlier in the same frame stay valid
// because their block is never freed or moved mid-frame.
#ifndef MITHRIL_DIRECTVULKAN_UNIFORM_ARENA_H
#define MITHRIL_DIRECTVULKAN_UNIFORM_ARENA_H

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>

namespace mithril {
namespace vk {

// A sub-range of an arena block. `buffer` is owned by the arena and stays
// valid until the owning frame slot is rewound (next time that slot is
// recycled, which is gated on its fence).
struct UboSlice {
    VkBuffer     buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceSize size   = 0;
    void*        mapped = nullptr;

    bool valid() const { return buffer != VK_NULL_HANDLE; }
};

// Bump-allocate `size` bytes from frame slot `slot`, aligned to the device's
// minUniformBufferOffsetAlignment. Returns false only if the block could not
// be created (out of device memory) — callers must treat that as "skip this
// binding", never as a reason to fall back to overwriting shared storage.
bool ubo_arena_allocate(int slot, VkDeviceSize size, UboSlice& out);

// ubo_arena_allocate + memcpy of `data` into the slice.
bool ubo_arena_upload(int slot, const void* data, VkDeviceSize size, UboSlice& out);

// Rewind `slot` back to its first block. ONLY legal after that slot's fence
// has been waited on. Called from ensure_command_buffer_recording().
void ubo_arena_rewind(int slot);

// Destroy every block of every slot. Called from shutdown_device() after
// vkDeviceWaitIdle.
void ubo_arena_shutdown();

// The alignment every arena slice (and therefore every dynamic offset)
// satisfies: max(minUniformBufferOffsetAlignment, 16). Cached on first call.
VkDeviceSize ubo_arena_alignment();

/*
 * How many VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC descriptors one
 * descriptor set may hold on this device.
 *
 * This is the MoltenVK/Metal 2 constraint that forces a degradation path.
 * The Vulkan spec floor for maxDescriptorSetUniformBuffersDynamic is 8, and
 * MoltenVK reports exactly 8 on several Apple GPUs (it maps dynamic UBOs onto
 * a limited set of Metal buffer-argument slots). A program whose reflected UBO
 * count exceeds this cannot use dynamic offsets at all — vkCreateDescriptorSet
 * Layout would fail and the program would render nothing.
 *
 * ensure_program_layouts() therefore decides per program: dynamic when the UBO
 * count fits, plain UNIFORM_BUFFER otherwise (arena offsets then go into
 * VkDescriptorBufferInfo::offset instead). See ProgramResources::uboDynamic.
 */
uint32_t ubo_arena_max_dynamic_ubos();

} // namespace vk
} // namespace mithril

#endif // MITHRIL_DIRECTVULKAN_UNIFORM_ARENA_H
