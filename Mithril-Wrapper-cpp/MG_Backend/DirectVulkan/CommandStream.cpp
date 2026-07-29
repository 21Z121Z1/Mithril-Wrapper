// Mithril-Wrapper - MG_Backend/DirectVulkan/CommandStream.cpp
// Render-pass orchestration via VK_KHR_dynamic_rendering (vkCmdBeginRendering)
// + encoder dynamic-state setters + draw recording + per-frame submit.
#include "CommandStream.h"
#include "Device.h"
#include "Swapchain.h"
#include "../Backend.h"
#include "../../MG_Impl/Log.h"

#include <cstring>
#include <vector>

namespace mithril {
namespace vk {

namespace {

// Encoder state carried between begin_render_pass() and the draw calls.
struct EncoderState {
    bool passActive = false;
    VkPipeline boundPipeline = VK_NULL_HANDLE;

    // Pending clear values (applied to the load op of the next pass).
    float clearColor[4] = {0, 0, 0, 0};
    double clearDepth = 1.0;
    GLint clearStencil = 0;
    // Per-attachment pending clear flags (set by deferred glClear, consumed
    // by begin_render_pass). Replaces the single loadClear flag so that
    // glClear(COLOR) only clears color, glClear(DEPTH) only clears depth, etc.
    bool pendingClearColor = false;
    bool pendingClearDepth = false;
    bool pendingClearStencil = false;

    // Color/depth attachment views for the active pass.
    VkImageView colorViews[8] = {};
    int colorCount = 0;
    VkImageView depthView = VK_NULL_HANDLE;
    int width = 0;
    int height = 0;

    // The swapchain whose currently-acquired image backs framebuffer 0.
    // nullptr when no EGLSurface is current (headless) or the active FBO is
    // a user-created framebuffer object. Set by EGL after each acquire; read
    // by begin_render_pass() / commit_frame() to record layout barriers and
    // signal pendingRenderFinished.
    Swapchain* activeSwapchain = nullptr;

    // True once any command has been recorded into the current command buffer
    // since the last commit_frame() / vkBeginCommandBuffer. Used by
    // commit_frame() to skip empty submits (eglWaitClient + eglSwapBuffers
    // both call backend_commit; the second call would otherwise submit an
    // empty command buffer, which is wasteful and — under resize/destruction
    // races — can submit against a destroyed swapchain's semaphore, triggering
    // MoltenVK / IOSurface UAF crashes).
    bool hasCommands = false;
};

EncoderState& encoder() {
    static EncoderState s;
    return s;
}

GLbitfield clearMaskPending = 0;

/*
 * Record an image-memory barrier transitioning `image` from `oldLayout` to
 * `newLayout` on the active command buffer. Used by begin_render_pass() /
 * commit_frame() to put swapchain images into COLOR_ATTACHMENT_OPTIMAL before
 * dynamic rendering and back into PRESENT_SRC_KHR before present. Without
 * these barriers MoltenVK sees a PRESENT_SRC image used as a colour
 * attachment, which is spec-illegal and produces a black screen.
 *
 *   isDepthStencil : selects the aspect mask (color vs depth+stencil) and
 *                    the destination pipeline stage.
 */
void record_layout_barrier(VkCommandBuffer cb, VkImage image, VkFormat format,
                           VkImageLayout oldLayout, VkImageLayout newLayout,
                           bool isDepthStencil) {
    if (image == VK_NULL_HANDLE || oldLayout == newLayout) return;

    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout = oldLayout;
    b.newLayout = newLayout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;

    if (isDepthStencil) {
        // D32_SFLOAT_S8_UINT has separate depth + stencil aspects.
        b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    } else {
        b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }
    b.subresourceRange.baseMipLevel = 0;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.baseArrayLayer = 0;
    b.subresourceRange.layerCount = 1;
    (void)format;

    // Source stage mask: who wrote the image in oldLayout.
    VkPipelineStageFlags srcStage;
    VkAccessFlags srcAccess;
    switch (oldLayout) {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            // No prior writes; contents discarded.
            srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            srcAccess = 0;
            break;
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            // Image came back from present; the presentation engine read it.
            srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            srcAccess = VK_ACCESS_MEMORY_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            srcAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            srcStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                       VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            srcAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            break;
        default:
            srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            srcAccess = VK_ACCESS_MEMORY_WRITE_BIT;
            break;
    }

    // Destination stage mask: who will read/write the image in newLayout.
    VkPipelineStageFlags dstStage;
    VkAccessFlags dstAccess;
    switch (newLayout) {
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dstAccess = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                       VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            dstAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            // Present engine reads the image.
            dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            dstAccess = VK_ACCESS_MEMORY_READ_BIT;
            break;
        default:
            dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            dstAccess = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            break;
    }

    b.srcAccessMask = srcAccess;
    b.dstAccessMask = dstAccess;

    vkCmdPipelineBarrier(cb, srcStage, dstStage, 0,
                         0, nullptr, 0, nullptr, 1, &b);
}

// Count a vkBeginCommandBuffer failure toward the persistent-fault breaker.
// Under GPU OOM, begin failures (VK_NOT_READY / OUT_OF_DEVICE_MEMORY /
// DEVICE_LOST) can repeat forever while vkQueueSubmit is never even reached,
// so the submit-failure counter alone never trips deviceLost and the render
// thread spins recording vkCmd* into a dead buffer — MoltenVK then emits one
// VK_NOT_READY message per command (observed: 3M+ lines, 655 MB log). Counting
// begin failures reuses the same threshold: >= 3 consecutive failures suspend
// rendering entirely (commit_frame / present / acquire all early-return on
// deviceLost).
void note_begin_failure(Backend* b, const char* where) {
    b->consecutiveSubmitFailures++;
    if (b->consecutiveSubmitFailures >= 3 && !b->deviceLost) {
        b->deviceLost = true;
        MITHRIL_LOG_ERROR("vk", "Persistent GPU fault detected after %d "
                          "consecutive begin/submit failures (%s) — rendering "
                          "suspended to prevent log flooding",
                          b->consecutiveSubmitFailures, where);
    }
}

} // namespace

bool render_pass_active() { return encoder().passActive; }

void set_clear_color(float r, float g, float b, float a) {
    auto& e = encoder();
    e.clearColor[0] = r; e.clearColor[1] = g; e.clearColor[2] = b; e.clearColor[3] = a;
}
void set_clear_depth(double d) { encoder().clearDepth = d; }
void set_clear_stencil(int s)  { encoder().clearStencil = s; }
void set_load_clear(bool clear){ (void)clear; }

void set_pending_clear(unsigned int mask) {
    auto& e = encoder();
    if (mask & GL_COLOR_BUFFER_BIT)   e.pendingClearColor = true;
    if (mask & GL_DEPTH_BUFFER_BIT)   e.pendingClearDepth = true;
    if (mask & GL_STENCIL_BUFFER_BIT) e.pendingClearStencil = true;
}

void set_all_pending_clear() {
    auto& e = encoder();
    e.pendingClearColor = true;
    e.pendingClearDepth = true;
    e.pendingClearStencil = true;
}

bool has_pending_clear() {
    auto& e = encoder();
    return e.pendingClearColor || e.pendingClearDepth || e.pendingClearStencil;
}

void set_active_swapchain(Swapchain* sc) {
    encoder().activeSwapchain = sc;
}

void begin_render_pass(VkImageView* color_views, int color_count,
                       VkImageView depth_view, int width, int height, int samples) {
    (void)samples;
    Backend* b = backend();
    if (!b->initialized || !b->commandBuffer) return;
    // Recording into a non-recording command buffer is UB: MoltenVK emits one
    // VK_NOT_READY message per vkCmd* call (the 655 MB log flood). The buffer
    // only leaves the recording state on a begin failure under GPU fault;
    // skip the pass entirely until commit_frame recovers or trips deviceLost.
    if (!b->commandBufferRecording || b->deviceLost) {
        // d2ccb1b 守卫：command buffer 未录制或 deviceLost 时静默跳过。
        // 添加限流埋点使红屏根因可见——draw 由此路径被跳过会导致画面空白。
        static int skip_count = 0;
        if (skip_count < 4 || skip_count % 1000 == 0) {
            MITHRIL_LOG_WARN("vk", "begin_render_pass skipped: "
                              "commandBufferRecording=%d deviceLost=%d (count=%d)",
                              b->commandBufferRecording, b->deviceLost, skip_count);
        }
        skip_count++;
        return;
    }
    EncoderState& e = encoder();
    if (e.passActive) {
        // Coalesce: if attachments are the same, skip beginning a new pass.
        // If attachments changed (different FBO), end the current pass and
        // begin a new one. This mirrors MobileGL's approach of keeping the
        // render pass active for multiple draws to the same target.
        bool same = (e.colorCount == color_count &&
                     e.depthView == depth_view &&
                     e.width == width &&
                     e.height == height);
        if (same) {
            for (int i = 0; i < e.colorCount && same; ++i) {
                if (e.colorViews[i] != (color_views ? color_views[i] : VK_NULL_HANDLE))
                    same = false;
            }
        }
        if (same) return;  // coalesce into existing pass
        end_render_pass();  // attachments changed, end current and begin new
    }

    // The command buffer is already in the recording state — either begun by
    // init_device() (first frame) or by commit_frame() (subsequent frames).
    // Pre-frame commands (layout transitions, texture uploads, etc.) recorded
    // before this point are preserved and will be submitted with this pass.
    // Do NOT reset the command buffer here; that would discard those records.

    // Record the per-frame attachments so draw commands can reference them.
    e.colorCount = color_count > 8 ? 8 : color_count;
    for (int i = 0; i < e.colorCount; ++i) e.colorViews[i] = color_views ? color_views[i] : VK_NULL_HANDLE;
    e.depthView = depth_view;
    e.width = width;
    e.height = height;

    // ---- Layout barriers for the swapchain-backed default framebuffer ----
    // dynamic-rendering hard-codes imageLayout = COLOR_ATTACHMENT_OPTIMAL in
    // the VkRenderingAttachmentInfo below. The swapchain image comes back from
    // acquire in PRESENT_SRC_KHR (or UNDEFINED on first use); without an
    // explicit barrier transitioning it to COLOR_ATTACHMENT_OPTIMAL, MoltenVK
    // sees an illegal layout and renders nothing (black screen). The depth
    // image is created with initialLayout = UNDEFINED and needs the same
    // one-shot transition to DEPTH_STENCIL_ATTACHMENT_OPTIMAL on first use.
    //
    // swapchainColorWasUndefined: set when the colour image was transitioned
    // out of UNDEFINED this frame. Used below to pick DONT_CARE for the load
    // op (LOAD on an image whose contents were discarded is wasteful and
    // spec-discouraged; DONT_CARE matches the discard semantics).
    bool swapchainColorWasUndefined = false;
    bool swapchainDepthWasUndefined = false;
    if (e.activeSwapchain) {
        Swapchain* sc = e.activeSwapchain;
        if (sc->currentImage >= 0 && sc->currentImage < (int)sc->views.size()) {
            // Only barrier the image if one of the bound colour attachments is
            // the swapchain's current view (i.e. we're rendering to FBO 0).
            bool swapchainBound = false;
            for (int i = 0; i < e.colorCount; ++i) {
                if (e.colorViews[i] == sc->views[sc->currentImage]) {
                    swapchainBound = true;
                    break;
                }
            }
            if (swapchainBound && sc->currentColorLayout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
                swapchainColorWasUndefined = (sc->currentColorLayout == VK_IMAGE_LAYOUT_UNDEFINED);
                record_layout_barrier(b->commandBuffer,
                                      sc->images[sc->currentImage], sc->format,
                                      sc->currentColorLayout,
                                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                      /*isDepthStencil=*/false);
                sc->currentColorLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            }
        }
        // Depth: one-shot UNDEFINED -> DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        // per-swapchain-image (each swapchain image has its own depth image).
        if (sc->currentImage >= 0 && sc->currentImage < (int)sc->depthImages.size()) {
            VkImage depthImg = sc->depthImages[sc->currentImage];
            VkImageView depthVw = sc->depthViews[sc->currentImage];
            if (depthImg != VK_NULL_HANDLE && depthVw != VK_NULL_HANDLE &&
                e.depthView == depthVw && !sc->depthLayoutInitialized[sc->currentImage]) {
                record_layout_barrier(b->commandBuffer,
                                      depthImg, VK_FORMAT_D32_SFLOAT_S8_UINT,
                                      VK_IMAGE_LAYOUT_UNDEFINED,
                                      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                                      /*isDepthStencil=*/true);
                sc->depthLayoutInitialized[sc->currentImage] = true;
                swapchainDepthWasUndefined = true;
            }
        }
    }

    // Begin dynamic rendering.
    VkRenderingAttachmentInfoKHR colorAttachs[8] = {};
    for (int i = 0; i < e.colorCount; ++i) {
        colorAttachs[i].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
        colorAttachs[i].imageView = e.colorViews[i];
        colorAttachs[i].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        // loadOp:
        //   * CLEAR      — glClear passes (loadClear).
        //   * DONT_CARE  — draw passes where the swapchain colour image was
        //                  just acquired (UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL
        //                  this frame). The previous contents were discarded
        //                  by the UNDEFINED source layout, so LOAD would read
        //                  garbage and waste bandwidth.
        //   * LOAD       — draw passes where the swapchain image came back
        //                  from present (PRESENT_SRC_KHR) or against a user
        //                  FBO whose attachment already has valid contents.
        if (e.pendingClearColor) {
            colorAttachs[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        } else if (swapchainColorWasUndefined &&
                   e.activeSwapchain &&
                   !e.activeSwapchain->colorRenderedThisFrame &&
                   e.colorViews[i] == e.activeSwapchain->views[e.activeSwapchain->currentImage]) {
            colorAttachs[i].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        } else {
            colorAttachs[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        }
        colorAttachs[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachs[i].clearValue.color.float32[0] = e.clearColor[0];
        colorAttachs[i].clearValue.color.float32[1] = e.clearColor[1];
        colorAttachs[i].clearValue.color.float32[2] = e.clearColor[2];
        colorAttachs[i].clearValue.color.float32[3] = e.clearColor[3];
    }
    VkRenderingAttachmentInfoKHR depthAttach{};
    depthAttach.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
    depthAttach.imageView = e.depthView;
    depthAttach.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    if (e.pendingClearDepth) {
        depthAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    } else if (swapchainDepthWasUndefined) {
        depthAttach.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    } else {
        depthAttach.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    }
    depthAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttach.clearValue.depthStencil.depth = (float)e.clearDepth;
    depthAttach.clearValue.depthStencil.stencil = (uint32_t)e.clearStencil;

    VkRenderingInfoKHR ri{};
    ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
    ri.renderArea.offset.x = 0;
    ri.renderArea.offset.y = 0;
    ri.renderArea.extent.width = (uint32_t)width;
    ri.renderArea.extent.height = (uint32_t)height;
    ri.layerCount = 1;
    ri.colorAttachmentCount = (uint32_t)e.colorCount;
    ri.pColorAttachments = e.colorCount > 0 ? colorAttachs : nullptr;
    ri.pDepthAttachment = e.depthView ? &depthAttach : nullptr;
    ri.pStencilAttachment = e.depthView ? &depthAttach : nullptr;

    // Resolve the dynamic-rendering entry point (Vulkan 1.2 + extension).
    static PFN_vkCmdBeginRenderingKHR fn = nullptr;
    if (!fn) {
        fn = (PFN_vkCmdBeginRenderingKHR)vkGetDeviceProcAddr(b->device, "vkCmdBeginRendering");
        if (!fn) fn = (PFN_vkCmdBeginRenderingKHR)vkGetDeviceProcAddr(b->device, "vkCmdBeginRenderingKHR");
    }
    if (fn) fn(b->commandBuffer, &ri);

    // Log the first few render passes so developers can trace loadOp,
    // attachment count, and format decisions — critical for diagnosing
    // "red screen" / "black screen" / no-render issues.
    {
        static int pass_count = 0;
        if (pass_count < 10) {
            const char* loadOpStr = "LOAD";
            if (e.pendingClearColor) loadOpStr = "CLEAR";
            else if (swapchainColorWasUndefined) loadOpStr = "DONT_CARE(UNDEFINED)";
            else if (swapchainDepthWasUndefined) loadOpStr = "DONT_CARE(depth)";
            MITHRIL_LOG_WARN("vk", "begin_render_pass #%d: loadOp=%s "
                              "colorCount=%d depthView=%s size=%dx%d "
                              "swapchainFmt=0x%x(%s) swapchainWasUndef=%d",
                              pass_count + 1, loadOpStr,
                              e.colorCount, depth_view ? "yes" : "no",
                              width, height,
                              (unsigned)(e.activeSwapchain ? e.activeSwapchain->format : 0u),
                              e.activeSwapchain ? "BGRA8" : "none",
                              (int)swapchainColorWasUndefined);
            pass_count++;
        }
    }

    e.passActive = true;
    e.hasCommands = true;  // begin_render_pass recorded real commands
    // Mark the swapchain color image as rendered-to this acquire cycle so
    // subsequent passes (after coalescing fails) use LOAD instead of
    // DONT_CARE to preserve this pass's content.
    if (e.activeSwapchain && e.colorCount > 0) {
        e.activeSwapchain->colorRenderedThisFrame = true;
    }
    // Consume pending clear flags — they've been applied as loadOp above.
    e.pendingClearColor = false;
    e.pendingClearDepth = false;
    e.pendingClearStencil = false;
}

void end_render_pass() {
    Backend* b = backend();
    EncoderState& e = encoder();
    if (!e.passActive || !b->commandBuffer) return;

    static PFN_vkCmdEndRenderingKHR fn = nullptr;
    if (!fn) {
        fn = (PFN_vkCmdEndRenderingKHR)vkGetDeviceProcAddr(b->device, "vkCmdEndRendering");
        if (!fn) fn = (PFN_vkCmdEndRenderingKHR)vkGetDeviceProcAddr(b->device, "vkCmdEndRenderingKHR");
    }
    if (fn) fn(b->commandBuffer);

    e.passActive = false;
}

void commit_frame() {
    Backend* b = backend();
    if (b->deviceLost) {
        // 持久性故障已挂起，跳过 submit 避免死循环刷屏
        return;
    }
    if (!b->initialized || !b->commandBuffer) return;
    EncoderState& e = encoder();
    if (e.passActive) end_render_pass();

    // ---- Decide whether we need to submit at all ----
    // MobileGL's Present() logic (VulkanRenderer.cpp:6912):
    //   shouldSubmit = hasCommandBufferRecorded || needsLayoutTransitionForPresent
    //
    // We MUST submit (and signal renderFinished) whenever:
    //   (a) commands were recorded this frame (normal draw pass), OR
    //   (b) the swapchain image is NOT already in PRESENT_SRC_KHR (it is in
    //       COLOR_ATTACHMENT_OPTIMAL from a previous render pass and needs a
    //       layout-transition barrier before present), OR
    //   (c) imageAvailable has not been consumed yet this frame (the very
    //       first commit after acquire — even with no draw commands, we must
    //       wait on imageAvailable so the GPU does not race ahead of the
    //       presentation engine; this is the MobileGL TransitionToPresent
    //       fallback path).
    //
    // Without this, a frame where the app only calls glClear (no draws) or a
    // frame where eglWaitClient already flushed the draws would skip submit,
    // leave renderFinished unsignaled, and present would wait on a semaphore
    // that is never signaled -> MoltenVK hangs / black screen.
    Swapchain* sc = e.activeSwapchain;
    bool hasCommands = e.hasCommands;
    bool needsLayoutTransition = false;
    bool needsImageAvailableWait = false;
    if (sc && sc->currentImage >= 0 && sc->currentImage < (int)sc->images.size()) {
        if (sc->currentColorLayout != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR &&
            sc->currentColorLayout != VK_IMAGE_LAYOUT_UNDEFINED &&
            sc->currentColorLayout != VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR) {
            needsLayoutTransition = true;
        }
        needsImageAvailableWait = !sc->imageAvailableConsumed;
    }
    bool shouldSubmit = hasCommands || needsLayoutTransition || needsImageAvailableWait;

    if (!shouldSubmit) {
        return;
    }

    // If the command buffer is not in the recording state (e.g. a previous
    // commit_frame vkEndCommandBuffer'd it but then vkQueueSubmit failed and
    // we returned early before reset+begin), we cannot record the present
    // barrier below. Force a reset+begin here so the barrier lands in a fresh
    // buffer. Without this, vkCmdPipelineBarrier on a non-recording buffer is
    // UB and triggers the VK_NOT_READY death spiral reported under GPU OOM.
    if (!b->commandBufferRecording) {
        vkResetCommandBuffer(b->commandBuffer, 0);
        VkCommandBufferBeginInfo rbi{};
        rbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        rbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(b->commandBuffer, &rbi) != VK_SUCCESS) {
            note_begin_failure(b, "commit_frame recovery");
            if (b->consecutiveSubmitFailures == 1) {
                MITHRIL_LOG_ERROR("vk", "commit_frame: vkBeginCommandBuffer recovery failed");
            }
            e.hasCommands = false;
            return;
        }
        b->commandBufferRecording = true;
        // After a forced reset, the previously-recorded commands (draws etc.)
        // are gone. Only the present barrier below will be in the buffer.
        // Submitting it is still correct — it transitions the swapchain image
        // to PRESENT_SRC_KHR — but the frame's actual rendering is lost.
        // This is acceptable under GPU OOM (the rendering likely failed too).
    }

    // Transition the swapchain color image back to PRESENT_SRC_KHR before
    // vkEndCommandBuffer so vkQueuePresentKHR sees a legal layout. Without
    // this, present is spec-illegal and MoltenVK drops the frame (black screen).
    if (sc && needsLayoutTransition) {
        record_layout_barrier(b->commandBuffer,
                              sc->images[sc->currentImage], sc->format,
                              sc->currentColorLayout,
                              VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                              /*isDepthStencil=*/false);
        sc->currentColorLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }

    VkResult r = vkEndCommandBuffer(b->commandBuffer);
    if (r != VK_SUCCESS) {
        MITHRIL_LOG_ERROR("vk", "vkEndCommandBuffer failed (rc=%d)", (int)r);
        // Buffer is now in an invalid state. Force reset+begin so the next
        // begin_render_pass has a recording buffer to write into; otherwise
        // the render thread spins forever issuing vkCmd* into a dead buffer.
        vkResetCommandBuffer(b->commandBuffer, 0);
        VkCommandBufferBeginInfo rbi{};
        rbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        rbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(b->commandBuffer, &rbi) == VK_SUCCESS) {
            b->commandBufferRecording = true;
        } else {
            note_begin_failure(b, "post-end-fail begin");
        }
        e.hasCommands = false;
        if (sc) sc->needsRebuild = true;
        return;
    }
    b->commandBufferRecording = false;  // executable, not recording

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &b->commandBuffer;

    // ---- Wait on imageAvailable (acquire semaphore) if not yet consumed ----
    // This is the CRITICAL fix for the black screen: vkAcquireNextImageKHR
    // signals imageAvailable, and the FIRST vkQueueSubmit of the frame MUST
    // wait on it (at COLOR_ATTACHMENT_OUTPUT stage) so the GPU does not start
    // writing to the swapchain image before the presentation engine releases
    // it. Without this wait, the GPU renders into a stale/owned-by-presenter
    // image and the rendered contents never reach the display -> black screen.
    //
    // Only the first commit_frame() per frame waits on imageAvailable (mid-
    // frame flushes via eglWaitClient consume it on the first submit). This
    // mirrors MobileGL's imageAvailableSemaphoreConsumed flag
    // (FrameContext.cpp:191-193).
    VkSemaphore waitSemaphore = VK_NULL_HANDLE;
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    if (sc && sc->imageAvailable != VK_NULL_HANDLE && !sc->imageAvailableConsumed) {
        waitSemaphore = sc->imageAvailable;
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &waitSemaphore;
        si.pWaitDstStageMask = &waitStage;
    }

    // ---- Signal renderFinished so present can wait on it ----
    // Always signal (if not already signaled this frame) so present has a
    // semaphore to wait on. Without this, present would either wait on a
    // never-signaled semaphore (hang) or skip the wait (race). MobileGL's
    // GetSubmitInfo unconditionally sets signalSemaphoreCount=1
    // (FrameContext.cpp:196).
    VkSemaphore signalSemaphore = VK_NULL_HANDLE;
    if (sc && sc->pendingRenderFinished != VK_NULL_HANDLE &&
        !sc->renderFinishedSignaled) {
        signalSemaphore = sc->pendingRenderFinished;
        sc->renderFinishedSignaled = true;
    }
    if (signalSemaphore != VK_NULL_HANDLE) {
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &signalSemaphore;
    }

    VkFence fence = b->frameFences[b->currentFrame];
    vkResetFences(b->device, 1, &fence);
    r = vkQueueSubmit(b->graphicsQueue, 1, &si, fence);
    if (r != VK_SUCCESS) {
        // Classify the failure: VK_ERROR_OUT_OF_DATE_KHR / VK_SUBOPTIMAL_KHR
        // are swapchain-rebuild signals handled by the eglSwapBuffers path,
        // NOT fatal GPU faults. Every other non-success result (DEVICE_LOST,
        // OUT_OF_DEVICE_MEMORY, OUT_OF_HOST_MEMORY, INVALID_EXTERNAL_HANDLE,
        // and any other negative VkResult) indicates a persistent GPU fault
        // and is counted toward the deviceLost threshold.
        if (r != VK_ERROR_OUT_OF_DATE_KHR && r != VK_SUBOPTIMAL_KHR) {
            b->consecutiveSubmitFailures++;
            if (b->consecutiveSubmitFailures >= 3 && !b->deviceLost) {
                b->deviceLost = true;
                MITHRIL_LOG_ERROR("vk", "Persistent GPU fault detected after %d "
                                  "consecutive submit failures — rendering "
                                  "suspended to prevent log flooding",
                                  b->consecutiveSubmitFailures);
            }
        }
        // Throttle the per-failure log: first failure, then every 100th.
        // Once deviceLost is set, commit_frame returns early above and this
        // path is no longer reached.
        if (b->consecutiveSubmitFailures == 1 ||
            b->consecutiveSubmitFailures % 100 == 0) {
            MITHRIL_LOG_ERROR("vk", "vkQueueSubmit failed (rc=%d) — marking "
                              "swapchain for rebuild", (int)r);
        }
        // vkQueueSubmit failure (e.g. VK_ERROR_OUT_OF_DEVICE_MEMORY /
        // VK_ERROR_DEVICE_LOST) means the command buffer was NOT consumed.
        // The fence will NOT be signaled, so we must NOT wait on it below
        // (vkWaitForFences would hang forever). Reset the fence manually and
        // mark the swapchain dead so EGL rebuilds it on the next swap.
        vkResetFences(b->device, 1, &fence);
        // Roll back the semaphore state: neither imageAvailable nor
        // renderFinished was actually consumed/signal'd (submit failed), so
        // the next commit must be allowed to wait/signal again. Without
        // these rollbacks, the state would be inconsistent (e.g. next submit
        // would skip the imageAvailable wait, racing the presenter again).
        if (sc) {
            sc->renderFinishedSignaled = false;
            // imageAvailableConsumed stays false (we never set it true on
            // failure) — correct, the next submit should still wait.
            sc->needsRebuild = true;
        }
        // Reset the dummy texture layout flag: the command buffer is about to
        // be reset, discarding any recorded layout-transition barrier for the
        // dummy texture. Without this reset, the flag stays true and the next
        // frame will skip the barrier, referencing the dummy image with
        // SHADER_READ_ONLY_OPTIMAL layout while it is still UNDEFINED ->
        // kIOGPUCommandBufferCallbackErrorInvalidResource.
        b->dummyTextureLayoutInitialized = false;
        // Reset+begin so the next frame has a recording buffer.
        vkResetCommandBuffer(b->commandBuffer, 0);
        VkCommandBufferBeginInfo rbi{};
        rbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        rbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(b->commandBuffer, &rbi) == VK_SUCCESS) {
            b->commandBufferRecording = true;
        }
        e.hasCommands = false;
        return;
    }

    // Success: a clean submit clears the consecutive-failure counter (only
    // persistent faults should keep it climbing toward the deviceLost threshold).
    b->consecutiveSubmitFailures = 0;

    // Submit succeeded: imageAvailable is now consumed (the wait was honored).
    if (sc) {
        sc->imageAvailableConsumed = true;
    }

    // Wait on the frame we just submitted so the command buffer is reusable.
    vkWaitForFences(b->device, 1, &fence, VK_TRUE, UINT64_MAX);

    // ---- Drain deferred-release queues ----
    // Now that vkWaitForFences has completed, the GPU is done with all
    // command buffers from the previous frame. Any textures/buffers that
    // were deleted mid-frame (and whose entries were moved to the pending
    // queues by destroy_buffer_entry / destroy_texture_entry) can finally
    // be safely destroyed — the GPU no longer references them.
    for (auto& entry : b->pendingBufferReleases) {
        if (entry.buffer)  { vkDestroyBuffer(b->device, entry.buffer, nullptr); }
        if (entry.memory)  { vkFreeMemory(b->device, entry.memory, nullptr); }
    }
    b->pendingBufferReleases.clear();
    for (auto& entry : b->pendingTextureReleases) {
        if (entry.view)          { vkDestroyImageView(b->device, entry.view, nullptr); }
        if (entry.image)         { vkDestroyImage(b->device, entry.image, nullptr); }
        if (entry.memory)        { vkFreeMemory(b->device, entry.memory, nullptr); }
        if (entry.stagingBuffer) { vkDestroyBuffer(b->device, entry.stagingBuffer, nullptr); }
        if (entry.stagingMemory) { vkFreeMemory(b->device, entry.stagingMemory, nullptr); }
    }
    b->pendingTextureReleases.clear();

    b->currentFrame = (b->currentFrame + 1) % kMaxFramesInFlight;
    // Monotonic generation bump: descriptor pools are reset on first draw of
    // each generation (see DescriptorSet.cpp), so this must advance every frame
    // regardless of the cycling currentFrame value.
    b->frameGeneration++;

    // Begin a fresh command buffer so subsequent uploads/records have somewhere
    // to go. (Render pass begins will reset + begin again.) Check the return
    // value: under GPU OOM, vkBeginCommandBuffer can return VK_NOT_READY or
    // VK_ERROR_OUT_OF_DEVICE_MEMORY. Without this check, the buffer is left in
    // an invalid state and every subsequent vkCmd* call is UB, trapping the
    // render thread in the death spiral reported in the field.
    vkResetCommandBuffer(b->commandBuffer, 0);
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(b->commandBuffer, &bi) == VK_SUCCESS) {
        b->commandBufferRecording = true;
    } else {
        note_begin_failure(b, "post-submit begin");
        if (b->consecutiveSubmitFailures == 1) {
            MITHRIL_LOG_ERROR("vk", "post-submit vkBeginCommandBuffer failed — "
                              "command buffer unusable until next commit");
        }
        b->commandBufferRecording = false;
    }

    e.hasCommands = false;  // fresh command buffer, no commands yet
}

/*
 * Drain GPU work that references the active swapchain, then detach the
 * swapchain from the encoder. Called by EGL BEFORE backend_destroy_swapchain()
 * (eglDestroySurface / ensure_swapchain resize path).
 *
 * Sequence:
 *   1. end_render_pass() + commit_frame() — flush any pending commands that
 *      reference the swapchain's images into the GPU.
 *   2. vkDeviceWaitIdle() — block until the GPU has finished executing those
 *      commands, so the swapchain's IOSurface-backed images are no longer
 *      referenced by the driver. Without this wait, vkDestroySwapchainKHR /
 *      vkDestroyImageView would free IOSurfaces that the GPU is still reading,
 *      and the next IOSurfaceBindAccel call in the Metal driver would crash
 *      with SIGSEGV (UAF).
 *   3. set_active_swapchain(nullptr) — clear the encoder's raw pointer to the
 *      swapchain so begin_render_pass() / commit_frame() cannot record layout
 *      barriers against its (soon-to-be-freed) images.
 */
void drain_and_detach_swapchain() {
    Backend* b = backend();
    if (!b->initialized || !b->device) {
        set_active_swapchain(nullptr);
        return;
    }
    end_render_pass();
    commit_frame();
    vkDeviceWaitIdle(b->device);
    // Drain deferred-release queues. The GPU is now idle, so any textures/
    // buffers deleted mid-frame (whose entries were moved to the pending
    // queues by destroy_buffer_entry / destroy_texture_entry) can be safely
    // destroyed before the swapchain is rebuilt/resized.
    for (auto& entry : b->pendingBufferReleases) {
        if (entry.buffer)  { vkDestroyBuffer(b->device, entry.buffer, nullptr); }
        if (entry.memory)  { vkFreeMemory(b->device, entry.memory, nullptr); }
    }
    b->pendingBufferReleases.clear();
    for (auto& entry : b->pendingTextureReleases) {
        if (entry.view)          { vkDestroyImageView(b->device, entry.view, nullptr); }
        if (entry.image)         { vkDestroyImage(b->device, entry.image, nullptr); }
        if (entry.memory)        { vkFreeMemory(b->device, entry.memory, nullptr); }
        if (entry.stagingBuffer) { vkDestroyBuffer(b->device, entry.stagingBuffer, nullptr); }
        if (entry.stagingMemory) { vkFreeMemory(b->device, entry.stagingMemory, nullptr); }
    }
    b->pendingTextureReleases.clear();
    set_active_swapchain(nullptr);
}

} // namespace vk
} // namespace mithril

// ===========================================================================
// Public C API (declared in MG_Backend/Backend.h)
// ===========================================================================
extern "C" {

void backend_set_clear_color(float r, float g, float b, float a) {
    mithril::vk::set_clear_color(r, g, b, a);
}
void backend_set_clear_depth(double d) { mithril::vk::set_clear_depth(d); }
void backend_set_clear_stencil(int s)  { mithril::vk::set_clear_stencil(s); }
void backend_set_load_clear(void)      { mithril::vk::set_all_pending_clear(); }
void backend_set_load_load(void)       { /* no-op: loadOp now determined by pending clear flags */ }
void backend_set_pending_clear(unsigned int mask) { mithril::vk::set_pending_clear(mask); }
int  backend_has_pending_clear(void)   { return mithril::vk::has_pending_clear() ? 1 : 0; }

void backend_begin_render_pass(VkImageView* color_views, int color_count,
                               VkImageView depth_view, int width, int height, int samples) {
    mithril::vk::begin_render_pass(color_views, color_count, depth_view, width, height, samples);
}

void backend_end_render_pass(void) { mithril::vk::end_render_pass(); }
void backend_commit(void)          { mithril::vk::commit_frame(); }

int backend_render_pass_active(void) {
    return mithril::vk::render_pass_active() ? 1 : 0;
}

VkCommandBuffer backend_get_command_buffer(void) {
    mithril::vk::Backend* b = mithril::vk::backend();
    return b->commandBuffer;
}

void backend_set_active_swapchain(void* swapchain_state) {
    mithril::vk::set_active_swapchain((mithril::vk::Swapchain*)swapchain_state);
}

void backend_drain_and_detach_swapchain(void) {
    mithril::vk::drain_and_detach_swapchain();
}

void backend_update_swapchain_layout(VkImageLayout layout) {
    mithril::vk::Swapchain* sc = mithril::vk::encoder().activeSwapchain;
    if (sc) {
        sc->currentColorLayout = layout;
    }
}

// Any vkCmd* on a command buffer that is not in the recording state is UB;
// under a GPU fault (begin failure) MoltenVK emits one VK_NOT_READY message
// per call, which flooded a field log to 655 MB. Every recording entry point
// below must use this guard instead of checking commandBuffer alone.
static inline bool cmd_recording_ok(mithril::vk::Backend* b) {
    return b->commandBuffer && b->commandBufferRecording;
}

void backend_bind_pipeline(VkPipeline pipeline) {
    mithril::vk::Backend* b = mithril::vk::backend();
    if (cmd_recording_ok(b) && pipeline) {
        vkCmdBindPipeline(b->commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    }
}

void backend_set_viewport(int x, int y, int w, int h, double znear, double zfar) {
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!cmd_recording_ok(b)) return;
    // Use the GL viewport directly. GL's viewport is bottom-left origin while
    // Vulkan's is top-left, but MoltenVK flips the Y axis when translating to
    // Metal, so passing the GL values through unchanged matches the on-screen
    // behaviour of the previous Metal backend (and what host apps expect).
    VkViewport vp{};
    vp.x        = (float)x;
    vp.y        = (float)y;
    vp.width    = (float)w;
    vp.height   = (float)h;
    vp.minDepth = (float)znear;
    vp.maxDepth = (float)zfar;
    vkCmdSetViewport(b->commandBuffer, 0, 1, &vp);
}

void backend_set_scissor(int x, int y, int w, int h) {
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!cmd_recording_ok(b)) return;
    VkRect2D sc{};
    sc.offset.x = x; sc.offset.y = y;
    sc.extent.width = (uint32_t)w; sc.extent.height = (uint32_t)h;
    vkCmdSetScissor(b->commandBuffer, 0, 1, &sc);
}

void backend_set_vertex_buffer(int slot, VkBuffer buffer, VkDeviceSize offset) {
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!cmd_recording_ok(b) || !buffer) return;
    VkDeviceSize offsets[1] = { offset };
    vkCmdBindVertexBuffers(b->commandBuffer, (uint32_t)slot, 1, &buffer, offsets);
}

void backend_set_fragment_buffer(int slot, VkBuffer buffer, VkDeviceSize offset) {
    // No-op: fragment-stage UBO binding is handled by descriptor sets built in
    // DescriptorSet.cpp (backend_bind_program_descriptors). There is no Vulkan
    // "bind buffer to fragment stage slot" command outside of descriptor sets,
    // so this entry point exists only to satisfy the C API contract.
    (void)slot; (void)buffer; (void)offset;
}

void backend_set_vertex_texture(int slot, VkImageView view, VkSampler sampler) {
    // No-op: see backend_set_fragment_buffer — descriptor binding is centralised
    // in DescriptorSet.cpp (backend_bind_program_descriptors).
    (void)slot; (void)view; (void)sampler;
}

void backend_set_fragment_texture(int slot, VkImageView view, VkSampler sampler) {
    // No-op: see backend_set_fragment_buffer — descriptor binding is centralised
    // in DescriptorSet.cpp (backend_bind_program_descriptors).
    (void)slot; (void)view; (void)sampler;
}

void backend_set_blend_color(float r, float g, float b, float a) {
    // NOTE: parameter `b` is the blue blend constant (float); the backend ptr
    // is renamed to avoid shadowing it.
    mithril::vk::Backend* bk = mithril::vk::backend();
    if (!cmd_recording_ok(bk)) return;
    float bc[4] = { r, g, b, a };
    vkCmdSetBlendConstants(bk->commandBuffer, bc);
}

void backend_set_depth_bias(float slope, float clamp) {
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!cmd_recording_ok(b)) return;
    vkCmdSetDepthBias(b->commandBuffer, slope, clamp, 0.0f);
}

void backend_set_cull_mode(int mode) {
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!cmd_recording_ok(b)) return;
    VkCullModeFlags cull = VK_CULL_MODE_NONE;
    if (mode == 1) cull = VK_CULL_MODE_FRONT_BIT;
    else if (mode == 2) cull = VK_CULL_MODE_BACK_BIT;
    vkCmdSetCullMode(b->commandBuffer, cull);
}

void backend_set_front_face(int ccw) {
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!cmd_recording_ok(b)) return;
    vkCmdSetFrontFace(b->commandBuffer, ccw ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE);
}

void backend_set_depth_test(int enabled, int write_mask, int compare_func) {
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!cmd_recording_ok(b)) return;
    vkCmdSetDepthTestEnable(b->commandBuffer, enabled ? VK_TRUE : VK_FALSE);
    vkCmdSetDepthWriteEnable(b->commandBuffer, write_mask ? VK_TRUE : VK_FALSE);
    VkCompareOp op = VK_COMPARE_OP_LESS;
    switch (compare_func) {
        case 0x200: op = VK_COMPARE_OP_NEVER; break;    // GL_NEVER
        case 0x201: op = VK_COMPARE_OP_LESS; break;     // GL_LESS
        case 0x202: op = VK_COMPARE_OP_EQUAL; break;    // GL_EQUAL
        case 0x203: op = VK_COMPARE_OP_LESS_OR_EQUAL; break;
        case 0x204: op = VK_COMPARE_OP_GREATER; break;
        case 0x205: op = VK_COMPARE_OP_NOT_EQUAL; break;
        case 0x206: op = VK_COMPARE_OP_GREATER_OR_EQUAL; break;
        case 0x207: op = VK_COMPARE_OP_ALWAYS; break;
        default: op = VK_COMPARE_OP_LESS; break;
    }
    vkCmdSetDepthCompareOp(b->commandBuffer, op);
}

void backend_set_color_write_mask(int r, int g, int b, int a) {
    (void)r; (void)g; (void)b; (void)a;
    // VK_DYNAMIC_STATE_COLOR_WRITE_ENABLE_EXT is extension-only; colour
    // write is part of the pipeline's blend attachment for now.
}

void backend_set_stencil_state(int enabled, int func, int ref, int mask,
                               int sfail, int dpfail, int dppass) {
    (void)enabled; (void)func; (void)ref; (void)mask;
    (void)sfail; (void)dpfail; (void)dppass;
    // Stencil dynamic state deferred (bring-up).
}

// Draws require BOTH an active pass and a recording command buffer. If the
// buffer left the recording state (begin failure under GPU fault), recording
// vkCmdDraw* is UB and MoltenVK spams VK_NOT_READY per call — belt-and-braces
// with the same guard in begin_render_pass (passActive can't become true while
// not recording, but a fault can also strike mid-pass).
static inline bool draw_recording_ok(mithril::vk::Backend* b) {
    bool ok = b->commandBuffer &&
              b->commandBufferRecording &&
              mithril::vk::render_pass_active();
    if (!ok) {
        // d2ccb1b 守卫：draw 被静默跳过时无日志，红屏根因不可见。
        // 限流输出（首5+每1000），区分 passActive / commandBufferRecording。
        static int skip_count = 0;
        if (skip_count < 5 || skip_count % 1000 == 0) {
            MITHRIL_LOG_WARN("vk", "draw skipped: passActive=%d "
                              "commandBufferRecording=%d cmdBuf=%p (count=%d)",
                              mithril::vk::render_pass_active() ? 1 : 0,
                              b->commandBufferRecording ? 1 : 0,
                              (void*)b->commandBuffer, skip_count);
        }
        skip_count++;
    }
    return ok;
}

void backend_draw_arrays(int primitive, int first, int count) {
    (void)primitive;
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!draw_recording_ok(b)) return;
    vkCmdDraw(b->commandBuffer, (uint32_t)count, 1, (uint32_t)first, 0);
}

void backend_draw_indexed(int primitive, int count, int index_type,
                          VkBuffer index_buffer, VkDeviceSize index_offset) {
    (void)primitive;
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!draw_recording_ok(b) || !index_buffer) return;
    VkIndexType t = (index_type == 1) ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
    vkCmdBindIndexBuffer(b->commandBuffer, index_buffer, index_offset, t);
    vkCmdDrawIndexed(b->commandBuffer, (uint32_t)count, 1, 0, 0, 0);
}

void backend_draw_arrays_instanced(int primitive, int first, int count, int primcount) {
    (void)primitive;
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!draw_recording_ok(b)) return;
    vkCmdDraw(b->commandBuffer, (uint32_t)count, (uint32_t)primcount, (uint32_t)first, 0);
}

void backend_draw_indexed_instanced(int primitive, int count, int index_type,
                                    VkBuffer index_buffer, VkDeviceSize index_offset,
                                    int primcount) {
    (void)primitive;
    mithril::vk::Backend* b = mithril::vk::backend();
    if (!draw_recording_ok(b) || !index_buffer) return;
    VkIndexType t = (index_type == 1) ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
    vkCmdBindIndexBuffer(b->commandBuffer, index_buffer, index_offset, t);
    vkCmdDrawIndexed(b->commandBuffer, (uint32_t)count, (uint32_t)primcount, 0, 0, 0);
}

} // extern "C"
