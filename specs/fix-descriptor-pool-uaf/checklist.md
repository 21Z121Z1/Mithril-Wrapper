# Checklist — Descriptor Pool UAF Fix

## Root cause confirmed
- [x] Crash signature matches: `vkEndCommandBuffer` → `commit_frame()` → `eglSwapBuffers`, in `MVKGraphicsResourcesCommandEncoderState::encodeImpl` / `encodeBindings`.
- [x] Crash occurs AFTER "First frame rendered" log → frame 2 `vkEndCommandBuffer`, consistent with pool-reset race.
- [x] Wild pointer `si_addr: 0x0000014800000018` → zombie Objective-C (Metal) object dereference.
- [x] `MVK_CONFIG_PREFILL_METAL_COMMAND_BUFFERS=1` confirmed ([Device.cpp:116](file:///workspace/Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/Device.cpp#L116)) — MoltenVK encodes Metal at `vkEndCommandBuffer`.
- [x] Single shared `VkDescriptorPool` per program confirmed ([Pipeline.h:50](file:///workspace/Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/Pipeline.h#L50)).
- [x] Pool reset keyed on `frameGeneration` confirmed ([DescriptorSet.cpp:290-293](file:///workspace/Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/DescriptorSet.cpp#L290-L293)).
- [x] Misleading comment ("commit_frame waits on previous frame's fence") confirmed false — `ensure_command_buffer_recording` waits on the **current** slot's fence (submitted `kMaxFramesInFlight` frames ago), not the just-submitted slot.
- [x] Secondary mid-frame pool-exhaustion reset confirmed ([DescriptorSet.cpp:302-311](file:///workspace/Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/DescriptorSet.cpp#L302-L311)) — also a UAF.
- [x] `disposalQueue` deferred-destruction mechanism does NOT cover descriptor pools (gap confirmed).
- [x] Same class of UAF as the previously-fixed `glDeleteBuffers` case ([Device.h:33-40](file:///workspace/Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/Device.h#L33-L40)).

## Design
- [x] Fix approach selected: per-frame-slot descriptor pools (`VkDescriptorPool descriptorPools[kMaxFramesInFlight]`), indexed by `b->currentFrame`.
- [x] Why it's safe: `ensure_command_buffer_recording()` already waits on `frameFences[currentFrame]` before the slot is reused, guaranteeing the slot's prior GPU work (and MoltenVK's Metal encoding of its descriptor references) is complete.
- [x] Mirrors existing per-slot design of `commandBuffers[]` / `frameFences[]` / `disposalQueue[]`.
- [x] `lastResetGen` becomes per-slot array so programs drawn on alternate frames still reset the correct pool.
- [x] Mid-frame exhaustion reset-retry removed (was itself a UAF); replaced with warn-and-skip.

## Implementation
- [x] `Pipeline.h`: `descriptorPool` → `descriptorPools[kMaxFramesInFlight]`; `lastResetGen` → `lastResetGen[kMaxFramesInFlight]`; added `#include "Device.h"`.
- [x] `DescriptorSet.cpp` `ensure_program_layouts()`: create `kMaxFramesInFlight` pools in a loop.
- [x] `DescriptorSet.cpp` `bind_program_descriptors()`: reset/allocate from `descriptorPools[b->currentFrame]`; use `lastResetGen[b->currentFrame]`.
- [x] `DescriptorSet.cpp` `bind_program_descriptors()`: remove mid-frame exhaustion `vkResetDescriptorPool` + retry; replace with warn-and-skip.
- [x] `DescriptorSet.cpp`: corrected the misleading comment (now documents the per-slot fence-wait guarantee).
- [x] `Pipeline.cpp` `delete_program_resources()`: destroy all `kMaxFramesInFlight` pools in a loop.
- [x] Verified: no remaining singular `pr.descriptorPool` / `lastResetGen` references (grep across whole repo). `kMaxFramesInFlight` visible in all 3 modified files.

## Verification
- [ ] Backend compiles without warnings.
- [ ] Host game renders past frame 1 without crashing at `vkEndCommandBuffer`.
- [ ] Swapchain resize still works (no regression in `drain_and_detach_swapchain` path).
- [ ] `glDeleteProgram` / `glLinkProgram` still tears down all descriptor resources (all per-slot pools destroyed).
- [ ] No new validation-layer errors (per-slot pools only reset after fence wait).

## Out of scope (explicitly not touched)
- [x] `disposalQueue` mechanism — unchanged.
- [x] `default_texture()` — unchanged.
- [x] Swapchain lifecycle — unchanged.
- [x] `commit_frame()` / `ensure_command_buffer_recording()` fence logic — unchanged.
- [x] Dynamic pool growth — not added (256-set-per-slot fixed capacity retained).
- [x] `defer_destroy_sampler_entry` dead code / sampler leak — not addressed (pre-existing leak, not a UAF).
- [x] `vkDeviceWaitIdle` stall in `glLinkProgram` — not addressed (perf, not correctness).
