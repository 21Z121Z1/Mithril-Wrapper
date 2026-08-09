# Fix: Descriptor Pool Reset Race (UAF) — `objc_msgSend` crash in `MVKGraphicsResourcesCommandEncoderState::encodeImpl`

## Problem Statement

The host application (a Java/LWJGL game using the Mithril-Wrapper Vulkan/MoltenVK
backend) crashes with a `SIGSEGV` in `objc_msgSend+0x20` (libobjc.A.dylib) inside
`MVKGraphicsResourcesCommandEncoderState::encodeImpl` / `encodeBindings`, reached
via `vkEndCommandBuffer` → `mithril::vk::commit_frame()` → `eglSwapBuffers`.

The crash occurs **after** the log line `[egl_bridge] First frame rendered, game
is ready` — i.e. frame 1 completes successfully and the crash happens during
frame 2's `vkEndCommandBuffer`. The faulting address
`si_addr: 0x0000014800000018` is a wild Objective-C object pointer, indicating a
freed/zombie `MTLBuffer` / `MTLTexture` is being dereferenced during Metal
resource encoding.

## Root Cause

A single per-program `VkDescriptorPool` is shared across all `kMaxFramesInFlight`
(=2) frame slots, but is reset as if it were single-buffered. This invalidates
descriptor sets still referenced by an in-flight command buffer, producing a
Use-After-Free of the underlying Metal resources.

### Detailed mechanism

1. **Single shared pool.** `ProgramResources::descriptorPool`
   ([Pipeline.h:50](file:///workspace/Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/Pipeline.h#L50))
   is one `VkDescriptorPool` per GL program, used by every frame slot.

2. **Pool reset keyed on `frameGeneration`.** `bind_program_descriptors()`
   ([DescriptorSet.cpp:290-293](file:///workspace/Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/DescriptorSet.cpp#L290-L293))
   resets the pool the first time a program is drawn in a new `frameGeneration`:

   ```cpp
   if (pr.lastResetGen != b->frameGeneration) {
       vkResetDescriptorPool(b->device, pr.descriptorPool, 0);
       pr.lastResetGen = b->frameGeneration;
   }
   ```

   The accompanying comment (lines 283-289) asserts: *"commit_frame() waits on
   the previous frame's fence (so the prior frame's sets are no longer
   in-flight) and then bumps the monotonic frameGeneration."* **This assertion is
   false.**

3. **Actual fence-wait semantics.** `ensure_command_buffer_recording()`
   ([CommandStream.cpp:208-229](file:///workspace/Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/CommandStream.cpp#L208-L229))
   waits on `frameFences[currentFrame]` — the fence for the slot **about to be
   recorded into**, which was submitted `kMaxFramesInFlight` (=2) frames ago. It
   does NOT wait on the fence of the slot that was just submitted.

   `commit_frame()` advances `currentFrame` and bumps `frameGeneration` at
   [CommandStream.cpp:816, 841, 845](file:///workspace/Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/CommandStream.cpp#L816)
   **after** the submit, with no fence wait on the just-submitted slot.

4. **The UAF timeline (matches the observed crash):**

   | Step | `currentFrame` | `frameGeneration` | In-flight slots | Event |
   |------|----------------|-------------------|-----------------|-------|
   | Frame 1 draw | 0 | 0 | — | allocate descriptor sets from pool, bind into slot 0's CB |
   | Frame 1 `commit_frame` | 1 | 1 | **slot 0 (frame 1)** | submit slot 0; advance currentFrame; bump gen |
   | Frame 2 first draw | 1 | 1 | slot 0 still in-flight | `lastResetGen(0) != gen(1)` → **`vkResetDescriptorPool` invalidates frame 1's sets while slot 0 is in-flight** |
   | Frame 2 `commit_frame` `vkEndCommandBuffer` | 1 | 1 | slot 0 still in-flight | MoltenVK encodes Metal (PREFILL=1) → `encodeImpl` dereferences recycled descriptor storage → **SIGSEGV in `objc_msgSend`** |

5. **Why MoltenVK crashes here.** `MVK_CONFIG_PREFILL_METAL_COMMAND_BUFFERS=1`
   (set in [Device.cpp:116](file:///workspace/Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/Device.cpp#L116))
   makes MoltenVK finalize Metal encoding at `vkEndCommandBuffer`.
   `MVKGraphicsResourcesCommandEncoderState::encodeBindings` walks the bound
   descriptor sets and retains the underlying `MTLBuffer`/`MTLTexture` handles.
   After `vkResetDescriptorPool`, those handles' storage is recycled; the retain
   hits a zombie object → `objc_msgSend` on a wild pointer → SIGSEGV.

   This is the **same class of bug** documented in
   [Device.h:33-40](file:///workspace/Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/Device.h#L33-L40)
   for `glDeleteBuffers` (fixed via the `disposalQueue` deferred-destruction
   mechanism). Descriptor-pool resets bypass `disposalQueue` entirely, so the
   prior fix does not cover them.

### Secondary issue: mid-frame pool-exhaustion reset

[DescriptorSet.cpp:302-311](file:///workspace/Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/DescriptorSet.cpp#L302-L311)
performs a second `vkResetDescriptorPool` when allocation fails mid-frame (pool
exhausted at >256 sets). This is even more dangerous: it invalidates descriptor
sets already bound into the **current** frame's command buffer while it is still
being recorded. Under PREFILL this would crash at the very next
`vkEndCommandBuffer`.

## Fix Strategy

Replace the single shared `VkDescriptorPool` with an array of
`kMaxFramesInFlight` per-slot pools, indexed by `b->currentFrame`. This is the
Vulkan-canonical pattern and mirrors the existing per-slot design of
`commandBuffers[]` and `frameFences[]`.

### Why per-slot pools fix the UAF

`ensure_command_buffer_recording()` already waits on `frameFences[currentFrame]`
**before** the slot is reused. This guarantees that when frame N resets
`descriptorPool[currentFrame]`, the command buffer submitted to that same slot
`kMaxFramesInFlight` frames ago (and all of MoltenVK's Metal encoding of its
descriptor references) has completed on the GPU. The reset is therefore safe —
no in-flight command buffer references the sets being invalidated.

### Scope of changes

All changes are confined to the `DirectVulkan` backend. No GL-frontend,
EGL, or shader-translation changes are required.

#### Files to modify

1. **[Pipeline.h](file:///workspace/Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/Pipeline.h)** — `ProgramResources` struct
   - Replace `VkDescriptorPool descriptorPool` with `VkDescriptorPool descriptorPools[kMaxFramesInFlight]`.
   - Replace `uint64_t lastResetGen` with `uint64_t lastResetGen[kMaxFramesInFlight]` (one per slot, so a program drawn only on alternate frames still resets the correct slot's pool on its next use).
   - `#include "Device.h"` is already present transitively via `DescriptorSet.h`/`../Backend.h`; ensure `kMaxFramesInFlight` is visible.

2. **[DescriptorSet.cpp](file:///workspace/Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/DescriptorSet.cpp)** — `ensure_program_layouts()` and `bind_program_descriptors()`
   - `ensure_program_layouts()`: create `kMaxFramesInFlight` pools in a loop instead of one. Each pool gets `maxSets=256` and 256 descriptors per type (same capacity as before, now per-slot).
   - `bind_program_descriptors()`:
     - Reset `pr.descriptorPools[b->currentFrame]` (not the shared pool) when `pr.lastResetGen[b->currentFrame] != b->frameGeneration`. The fence wait in `ensure_command_buffer_recording()` (called at the top of this function, line 269) already guarantees this slot's prior GPU work is complete.
     - Allocate the set from `pr.descriptorPools[b->currentFrame]`.
     - **Remove the mid-frame exhaustion reset-retry.** Instead, on `vkAllocateDescriptorSets` failure, log a warning and skip the bind for this draw. (A correct grow-the-pool implementation is out of scope; the per-slot 256-set capacity is ample for the host's draw counts, and the previous reset-retry was itself a UAF. The warning surfaces the condition if it ever occurs.)
   - Update the misleading comment at lines 283-289 to accurately describe the per-slot fence-wait guarantee.

3. **[Pipeline.cpp](file:///workspace/Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/Pipeline.cpp)** — `delete_program_resources()`
   - Destroy all `kMaxFramesInFlight` pools in a loop (already gated by `vkDeviceWaitIdle` at line 522).
   - Keep the existing destruction order: pools → pipeline layout → descriptor set layout.

### What is NOT changed

- `disposalQueue` deferred-destruction mechanism — already correct, unchanged.
- `default_texture()` — already process-lifetime stable, unchanged.
- Swapchain lifecycle — already correct (`vkDeviceWaitIdle` before destroy), unchanged.
- `commit_frame()` / `ensure_command_buffer_recording()` fence logic — already correct, unchanged.
- Shader/pipeline destruction in `delete_program_resources` — already gated by `vkDeviceWaitIdle`, unchanged.

## Verification

- **Build**: compile the backend (CMake target) without warnings.
- **Functional**: the host game (Minecraft via LWJGL) must render past the first frame without crashing; the previous crash site (`vkEndCommandBuffer` in frame 2) is the regression target.
- **No regressions**: the deferred-destruction paths for buffers/textures/samplers remain intact; swapchain resize still works; `glDeleteProgram` still tears down all descriptor resources.
- **Validation** (if available): no `VK_ERROR_VALIDATION_FAILED` / `VUID-vkResetDescriptorPool-*` errors from the Vulkan validation layers, since per-slot pools are only reset after the slot's fence is waited.

## Out of scope

- Growing descriptor pools dynamically on exhaustion (the 256-set-per-slot capacity is retained as a fixed limit with a warning on overflow).
- Wiring up `defer_destroy_sampler_entry` (currently dead code; samplers leak — a pre-existing memory leak, not a UAF, not addressed here).
- Removing the `vkDeviceWaitIdle` stall in `delete_program_resources` / `glLinkProgram` (a performance issue, not a correctness issue).
