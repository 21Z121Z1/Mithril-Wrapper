# Tasks — Descriptor Pool UAF Fix

## Task 1: Update `ProgramResources` struct to per-slot pools

**File**: [Pipeline.h](file:///workspace/Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/Pipeline.h)

**Current** (lines 47-56):
```cpp
// ---- Descriptor set management (built once by ensure_program_layouts) ----
VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
VkPipelineLayout      pipelineLayout = VK_NULL_HANDLE;
VkDescriptorPool      descriptorPool = VK_NULL_HANDLE;
std::vector<DescriptorBinding> bindings;  // reflected VS+FS binding set
bool layoutsBuilt = false;
// Monotonic frame-generation value (see advance_frame_generation()) at
// which this program's descriptor pool was last reset. bind_program_descriptors()
// resets the pool once per frame so sets can be reused across frames.
uint64_t lastResetGen = 0;
```

**Target**:
```cpp
// ---- Descriptor set management (built once by ensure_program_layouts) ----
VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
VkPipelineLayout      pipelineLayout = VK_NULL_HANDLE;
// One pool per frame-in-flight slot. bind_program_descriptors() resets
// descriptorPools[b->currentFrame] on the first draw of a new frameGeneration.
// This is safe because ensure_command_buffer_recording() has already waited on
// frameFences[currentFrame] — guaranteeing the GPU work (and MoltenVK's Metal
// encoding of descriptor references) submitted to this slot kMaxFramesInFlight
// frames ago is complete. A single shared pool reset across all slots was a
// UAF: it invalidated descriptor sets still referenced by the just-submitted
// slot's in-flight command buffer, crashing MoltenVK's Metal encoder in
// MVKGraphicsResourcesCommandEncoderState::encodeImpl at the next
// vkEndCommandBuffer (SIGSEGV in objc_msgSend on a zombie MTLBuffer/MTLTexture).
VkDescriptorPool      descriptorPools[kMaxFramesInFlight] = {};
std::vector<DescriptorBinding> bindings;  // reflected VS+FS binding set
bool layoutsBuilt = false;
// Per-slot monotonic frame-generation value at which each slot's pool was last
// reset. A program drawn only on alternate frames still resets the correct
// slot's pool on its next use (the per-slot gen lags the global gen until the
// slot is actually revisited).
uint64_t lastResetGen[kMaxFramesInFlight] = {};
```

Ensure `kMaxFramesInFlight` is visible in `Pipeline.h`. It is defined in
`Device.h:23`; `Pipeline.h` includes `DescriptorSet.h` and `../Backend.h`. Add
`#include "Device.h"` if not already transitively visible (verify at edit time).

---

## Task 2: Create per-slot pools in `ensure_program_layouts()`

**File**: [DescriptorSet.cpp](file:///workspace/Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/DescriptorSet.cpp)

**Current** (lines 99-129): creates a single `pr.descriptorPool` with `maxSets=256` and 256 descriptors per type.

**Target**: create `kMaxFramesInFlight` pools in a loop, each with the same capacity. Replace the single `vkCreateDescriptorPool` call with:

```cpp
// ---- VkDescriptorPool (one per frame-in-flight slot) ----
// maxSets=256, 256 descriptors per type, PER SLOT. Each slot's pool is reset
// on the first draw of a new frameGeneration for that slot (see
// bind_program_descriptors). Per-slot pools prevent the UAF where a shared
// pool reset invalidated descriptor sets still referenced by an in-flight
// command buffer on another slot.
bool hasUBO = false, hasImg = false;
for (const auto& db : pr.bindings) {
    if (db.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) hasUBO = true;
    else if (db.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) hasImg = true;
}
std::vector<VkDescriptorPoolSize> poolSizes;
if (hasUBO) {
    VkDescriptorPoolSize ps{};
    ps.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ps.descriptorCount = 256;
    poolSizes.push_back(ps);
}
if (hasImg) {
    VkDescriptorPoolSize ps{};
    ps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ps.descriptorCount = 256;
    poolSizes.push_back(ps);
}
VkDescriptorPoolCreateInfo dpci{};
dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
dpci.maxSets = 256;
dpci.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
dpci.pPoolSizes = poolSizes.data();
for (int i = 0; i < kMaxFramesInFlight; ++i) {
    if (vkCreateDescriptorPool(b->device, &dpci, nullptr, &pr.descriptorPools[i]) != VK_SUCCESS) {
        MITHRIL_LOG_WARN("vk", "vkCreateDescriptorPool failed (program %u, slot %d)", program, i);
        pr.descriptorPools[i] = VK_NULL_HANDLE;
        // Layout is still valid; bind_program_descriptors will skip slots with a null pool.
    }
}
```

**Guard update**: the early-return guard in `bind_program_descriptors()` at line 276 currently checks `pr.descriptorPool == VK_NULL_HANDLE`. Update it to check the current slot's pool: `pr.descriptorPools[b->currentFrame] == VK_NULL_HANDLE`.

---

## Task 3: Reset/allocate from the current slot's pool in `bind_program_descriptors()`

**File**: [DescriptorSet.cpp](file:///workspace/Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/DescriptorSet.cpp)

**Current** (lines 283-311): resets the shared pool on `lastResetGen != frameGeneration`, then allocates from it; on exhaustion, resets again and retries.

**Target** (replace lines 283-311):

```cpp
// Per-slot pool reset. ensure_command_buffer_recording() (called above, line 269)
// has ALREADY waited on frameFences[currentFrame] before this point — so the
// command buffer submitted to this slot kMaxFramesInFlight frames ago, and all
// of MoltenVK's Metal encoding of its descriptor references, is guaranteed
// complete on the GPU. Resetting this slot's pool is therefore safe: no
// in-flight command buffer references the sets being invalidated.
//
// (The previous shared-pool design reset a single pool across all slots on the
// first draw of a new frameGeneration, which invalidated descriptor sets still
// referenced by the just-submitted slot's in-flight command buffer — a UAF that
// crashed MoltenVK's Metal encoder in MVKGraphicsResourcesCommandEncoderState::
// encodeImpl at the next vkEndCommandBuffer, SIGSEGV in objc_msgSend.)
int slot = b->currentFrame;
if (pr.lastResetGen[slot] != b->frameGeneration) {
    if (pr.descriptorPools[slot] != VK_NULL_HANDLE) {
        vkResetDescriptorPool(b->device, pr.descriptorPools[slot], 0);
    }
    pr.lastResetGen[slot] = b->frameGeneration;
}

// Allocate a fresh set for this draw from the current slot's pool.
VkDescriptorSetAllocateInfo dsai{};
dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
dsai.descriptorPool = pr.descriptorPools[slot];
dsai.descriptorSetCount = 1;
dsai.pSetLayouts = &pr.descriptorSetLayout;
VkDescriptorSet set = VK_NULL_HANDLE;
if (vkAllocateDescriptorSets(b->device, &dsai, &set) != VK_SUCCESS) {
    // Pool exhausted mid-frame (>256 distinct sets this frame for this program).
    // Do NOT reset-and-retry: that would invalidate descriptor sets already
    // bound into the current frame's command buffer while it is still being
    // recorded (a UAF under MVK_CONFIG_PREFILL_METAL_COMMAND_BUFFERS=1).
    // Skip the bind for this draw; the shader will read stale/undefined
    // bindings, which is preferable to crashing.
    MITHRIL_LOG_WARN("vk", "vkAllocateDescriptorSets failed (program %u, slot %d, "
                      "gen %llu) — descriptor pool exhausted; skipping descriptor "
                      "bind for this draw",
                      program, slot, (unsigned long long)b->frameGeneration);
    return;
}
```

Note: `slot` is reused as a local for readability; all subsequent code in the
function that referenced `pr.descriptorPool` (the allocation) now uses
`pr.descriptorPools[slot]` via `dsai.descriptorPool`. The rest of the function
(writes + `vkCmdBindDescriptorSets`) is unchanged because it operates on the
allocated `set`, not the pool.

---

## Task 4: Destroy all per-slot pools in `delete_program_resources()`

**File**: [Pipeline.cpp](file:///workspace/Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/Pipeline.cpp)

**Current** (line 536): destroys a single `pr.descriptorPool`.

**Target** (replace line 536 with a loop):

```cpp
// Descriptor resources built by ensure_program_layouts. Pools must be
// destroyed before the set layout they were created from (Vulkan ordering);
// destroying a pool implicitly frees all sets allocated from it.
for (int i = 0; i < kMaxFramesInFlight; ++i) {
    if (pr.descriptorPools[i]) {
        vkDestroyDescriptorPool(b->device, pr.descriptorPools[i], nullptr);
        pr.descriptorPools[i] = VK_NULL_HANDLE;
    }
}
```

The surrounding `vkDeviceWaitIdle` (line 522) and `drain_all_disposal_queues()`
(line 526) already guarantee no in-flight work, so destroying all pools here is
safe. The destruction order (pools → pipelineLayout → descriptorSetLayout,
lines 536-538) is preserved.

---

## Task 5: Build and verify

- Build the backend CMake target; confirm no warnings/errors (especially no
  references to the removed `descriptorPool` / `lastResetGen` scalar fields).
- Grep the codebase for any remaining `pr.descriptorPool` (singular) or
  `\.lastResetGen` (singular) references and update them. Expected remaining
  sites: none outside the files modified above.
- Run the host game; confirm it renders past frame 1 without crashing at
  `vkEndCommandBuffer`.
- Trigger a swapchain resize; confirm no regression.
- Call `glDeleteProgram` (program switch); confirm no leak / crash (all
  per-slot pools destroyed).
