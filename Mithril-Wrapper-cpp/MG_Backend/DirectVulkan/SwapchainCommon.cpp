// Mithril-Wrapper - MG_Backend/DirectVulkan/SwapchainCommon.cpp
// Platform-independent swapchain logic that runs AFTER the platform-specific
// file (SwapchainMetal.mm) has created the VkSurfaceKHR.
// Contains: surface-format query, vkCreateSwapchainKHR, swapchain image views,
// acquire semaphore, optional depth/stencil image, plus the destroy/acquire/
// present helpers and the per-Swapchain state lifecycle.
//
// The surface-creation step (VK_EXT_metal_surface)
// lives in the platform-specific TUs. This file does NOT define
// VK_USE_PLATFORM_* — it is plain C++ and compiles on every platform.
//
// Ownership contract for create_swapchain_post_surface():
//   * On success: the returned Swapchain takes ownership of `surface`;
//     destroy_swapchain() will call vkDestroySurfaceKHR on it.
//   * On failure (returns nullptr): ownership stays with the caller, which
//     must call vkDestroySurfaceKHR(b->instance, surface, nullptr) itself.
#include "Swapchain.h"
#include "Device.h"
#include "Resources.h"
#include "../../MG_Impl/Log.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace mithril {
namespace vk {

Swapchain* create_swapchain_post_surface(VkSurfaceKHR surface, int width, int height,
                                         int want_depth_stencil) {
    Backend* b = backend();
    if (!b->initialized || surface == VK_NULL_HANDLE || width <= 0 || height <= 0) return nullptr;

    Swapchain* sc = new Swapchain{};
    sc->width = width;
    sc->height = height;
    // Take ownership of the surface; destroy_swapchain() will free it on
    // teardown. On failure paths below we clear this before `delete sc` so
    // the caller remains responsible for destroying the surface itself.
    sc->surface = surface;

    // Surface format (prefer BGRA8Unorm, CAMetalLayer's default).
    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(b->physicalDevice, sc->surface, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(b->physicalDevice, sc->surface, &fmtCount, fmts.data());
    sc->format = VK_FORMAT_B8G8R8A8_UNORM;
    for (const auto& f : fmts) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_B8G8R8A8_SRGB) {
            sc->format = f.format;
            break;
        }
    }

    // Surface capabilities.
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(b->physicalDevice, sc->surface, &caps);
    // Image count: MUST match CAMetalLayer.maximumDrawableCount (set to 2 in
    // SurfaceMetal.mm). A mismatch (e.g. maximumDrawableCount=3 but swapchain
    // creates 2 images) causes the IOSurface pool to have more drawables than
    // swapchain images. When the Metal driver recycles the extra drawable's
    // IOSurface, it is not tracked by the swapchain → IOSurfaceBindAccel
    // dereferences a stale IOSurface → SIGSEGV (crash log: IOSurface+0x19cc).
    //
    // Previous code requested 3 (triple buffering) for a "deeper IOSurface
    // pool", but this is WRONG: the IOSurface pool size is determined by
    // maximumDrawableCount, not by swapchain image count. Setting image count
    // > maximumDrawableCount is harmless (extra images are never acquired),
    // but setting it < maximumDrawableCount causes the pool/image mismatch
    // crash. Since we now force maximumDrawableCount=2, request exactly 2.
    //
    // Clamp to [minImageCount, maxImageCount] for safety.
    uint32_t imgCount = 2;  // match kMaxFramesInFlight + maximumDrawableCount
    if (imgCount < caps.minImageCount) imgCount = caps.minImageCount;
    if (caps.maxImageCount > 0 && imgCount > caps.maxImageCount) imgCount = caps.maxImageCount;
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == 0xFFFFFFFFu || extent.width == 0) {
        extent.width = (uint32_t)width;
        extent.height = (uint32_t)height;
    }

    // Pick a supported composite alpha mode. The INHERIT bit may not be
    // available on all platforms; prefer OPAQUE (always supported on Metal)
    // and fall back to any bit the surface accepts.
    VkCompositeAlphaFlagBitsKHR compAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) {
        compAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    } else if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) {
        compAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    } else {
        // Pick the first available bit.
        for (uint32_t bit = 1; bit <= VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR; bit <<= 1) {
            if (caps.supportedCompositeAlpha & bit) {
                compAlpha = (VkCompositeAlphaFlagBitsKHR)bit;
                break;
            }
        }
    }
    MITHRIL_LOG_INFO("vk", "Swapchain: compositeAlpha=0x%x, supported=0x%x",
                     (unsigned)compAlpha, (unsigned)caps.supportedCompositeAlpha);

    // Present mode: prefer MAILBOX (lowest latency, VSync + tear-free), fall
    // back to IMMEDIATE (no VSync), then FIFO_RELAXED, then FIFO (always
    // supported). MobileGL's priority order (SwapchainObject.h:56-61):
    //   MAILBOX > IMMEDIATE > FIFO_RELAXED > FIFO
    // FIFO alone forces each present to wait for the next vblank; under high
    // frame rates this backs up the render thread and stresses the IOSurface
    // pool, contributing to the IOSurfaceBindAccel crash.
    uint32_t pmCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(b->physicalDevice, sc->surface, &pmCount, nullptr);
    std::vector<VkPresentModeKHR> pms(pmCount);
    if (pmCount > 0) {
        vkGetPhysicalDeviceSurfacePresentModesKHR(b->physicalDevice, sc->surface, &pmCount, pms.data());
    }
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;  // always available
    const VkPresentModeKHR pmPreference[] = {
        VK_PRESENT_MODE_MAILBOX_KHR,
        VK_PRESENT_MODE_IMMEDIATE_KHR,
        VK_PRESENT_MODE_FIFO_RELAXED_KHR,
        VK_PRESENT_MODE_FIFO_KHR,
    };
    for (VkPresentModeKHR want : pmPreference) {
        for (VkPresentModeKHR have : pms) {
            if (have == want) { presentMode = want; goto pm_done; }
        }
    }
pm_done:
    MITHRIL_LOG_WARN("vk", "Swapchain: presentMode=%d (available=%zu)",
                     (int)presentMode, pms.size());

    // Swapchain.
    VkSwapchainCreateInfoKHR scci{};
    scci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    scci.surface = sc->surface;
    scci.minImageCount = imgCount;
    scci.imageFormat = sc->format;
    scci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    scci.imageExtent = extent;
    scci.imageArrayLayers = 1;
    // imageUsage: COLOR_ATTACHMENT (rendering) + TRANSFER_DST (blit/clear to
    // swapchain). Add TRANSFER_SRC if the surface supports it — needed for
    // glReadPixels / glBlitFramebuffer on FBO 0. MobileGL's
    // SwapchainObject.cpp:205-215 does the same conditional add.
    scci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) {
        scci.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    scci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    scci.preTransform = caps.currentTransform;
    scci.compositeAlpha = compAlpha;
    scci.presentMode = presentMode;
    scci.clipped = VK_TRUE;
    if (vkCreateSwapchainKHR(b->device, &scci, nullptr, &sc->swapchain) != VK_SUCCESS) {
        // FIX (VK_ERROR_OUT_OF_DEVICE_MEMORY): 显存不足时 vkCreateSwapchainKHR
        // 会失败。降级重试：减少图像数量（3→2）和图像 usage（去掉 TRANSFER_SRC），
        // 降低显存占用后重试。这避免了 swapchain 创建失败 → eglSwapBuffers 重试
        // → 再次失败的死循环。
        MITHRIL_LOG_WARN("vk", "vkCreateSwapchainKHR failed (imgCount=%u, "
                          "usage=0x%x) — retrying with reduced resources",
                          (unsigned)imgCount, (unsigned)scci.imageUsage);
        // 降级 1: 减少图像数量到 minImageCount（通常 2）
        if (imgCount > caps.minImageCount) {
            imgCount = caps.minImageCount;
            scci.minImageCount = imgCount;
        }
        // 降级 2: 去掉 TRANSFER_SRC usage（只保留 COLOR_ATTACHMENT + TRANSFER_DST）
        scci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (vkCreateSwapchainKHR(b->device, &scci, nullptr, &sc->swapchain) != VK_SUCCESS) {
            // 降级 3: 只保留 COLOR_ATTACHMENT（最小 usage）
            scci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            if (vkCreateSwapchainKHR(b->device, &scci, nullptr, &sc->swapchain) != VK_SUCCESS) {
                MITHRIL_LOG_ERROR("vk", "vkCreateSwapchainKHR failed after all fallbacks");
                // Surface ownership stays with the caller (see contract above).
                sc->surface = VK_NULL_HANDLE;
                delete sc;
                return nullptr;
            }
        }
        // FIX: swapchain 走了降级路径，说明显存紧张。禁用 depth/stencil
        // 以节省显存（D32_SFLOAT_S8_UINT 在 1334x750 约 8MB，在 iPad Pro
        // 12.9" 约 43MB）。渲染器已支持无 depth 的 color-only pass。
        want_depth_stencil = 0;
        MITHRIL_LOG_WARN("vk", "vkCreateSwapchainKHR succeeded with fallback "
                          "(imgCount=%u, usage=0x%x, depth DISABLED to save VRAM)",
                          (unsigned)imgCount, (unsigned)scci.imageUsage);
    }
    sc->width = (int)extent.width;
    sc->height = (int)extent.height;

    // Swapchain images + views.
    uint32_t imgCount2 = 0;
    vkGetSwapchainImagesKHR(b->device, sc->swapchain, &imgCount2, nullptr);
    sc->images.resize(imgCount2);
    vkGetSwapchainImagesKHR(b->device, sc->swapchain, &imgCount2, sc->images.data());
    sc->views.resize(imgCount2);
    for (uint32_t i = 0; i < imgCount2; ++i) {
        VkImageViewCreateInfo vci{};
        vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vci.image = sc->images[i];
        vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vci.format = sc->format;
        vci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vci.subresourceRange.baseMipLevel = 0;
        vci.subresourceRange.levelCount = 1;
        vci.subresourceRange.baseArrayLayer = 0;
        vci.subresourceRange.layerCount = 1;
        vkCreateImageView(b->device, &vci, nullptr, &sc->views[i]);
    }

    // Acquire semaphore.
    VkSemaphoreCreateInfo semi{};
    semi.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    vkCreateSemaphore(b->device, &semi, nullptr, &sc->imageAvailable);

    // Per-swapchain-image render-finished semaphores. One per swapchain image
    // so present always waits on the exact semaphore signaled by the submit
    // that rendered into the image being presented. Mirrors MobileGL's
    // FrameContext (m_swapchainImageRenderFinishedSemaphores, FrameContext.h:122).
    // A single per-swapchain semaphore races under triple buffering: image A's
    // submit signals it, then image B's submit re-signals before present
    // consumes A's signal → present of A reads stale pixels (black screen) or
    // hits a spec violation (re-signaling a signaled binary semaphore).
    sc->renderFinishedPerImage.resize(imgCount2);
    sc->renderFinishedSignaledPerImage.assign(imgCount2, false);
    for (uint32_t i = 0; i < imgCount2; ++i) {
        vkCreateSemaphore(b->device, &semi, nullptr, &sc->renderFinishedPerImage[i]);
    }

    // Depth/stencil image (VK_FORMAT_D32_SFLOAT_S8_UINT).
    if (want_depth_stencil) {
        VkFormat depthFmt = VK_FORMAT_D32_SFLOAT_S8_UINT;
        VkImageCreateInfo dici{};
        dici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        dici.imageType = VK_IMAGE_TYPE_2D;
        dici.format = depthFmt;
        dici.extent = { (uint32_t)sc->width, (uint32_t)sc->height, 1 };
        dici.mipLevels = 1;
        dici.arrayLayers = 1;
        dici.samples = VK_SAMPLE_COUNT_1_BIT;
        dici.tiling = VK_IMAGE_TILING_OPTIMAL;
        dici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        dici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        dici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(b->device, &dici, nullptr, &sc->depthImage) == VK_SUCCESS) {
            VkMemoryRequirements req{};
            vkGetImageMemoryRequirements(b->device, sc->depthImage, &req);
            uint32_t mt = find_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            // SubTask 1.1: find_memory_type failure → skip entire depth
            // creation (no view). Without this, vkAllocateMemory would be
            // called with memoryTypeIndex=0xFFFFFFFFu and likely fail, but
            // the previous code still created a view on the unbound image —
            // the root cause of kIOGPUCommandBufferCallbackErrorInvalidResource.
            if (mt == 0xFFFFFFFFu) {
                MITHRIL_LOG_WARN("vk", "swapchain depth: find_memory_type returned invalid "
                                 "(memoryTypeBits=0x%x) — skipping depth attachment",
                                 (unsigned)req.memoryTypeBits);
            } else {
                VkMemoryAllocateInfo ai{};
                ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                ai.allocationSize = req.size;
                ai.memoryTypeIndex = mt;
                // SubTask 1.2 & 1.3: only create the view after BOTH
                // vkAllocateMemory and vkBindImageMemory succeed — a view
                // on an unbound VkImage has no backing storage and triggers
                // InvalidResource at submit time.
                if (try_allocate_memory_with_gc(b->device, &ai, nullptr, &sc->depthMemory) != VK_SUCCESS) {
                    MITHRIL_LOG_WARN("vk", "swapchain depth: vkAllocateMemory failed — "
                                     "skipping depth view (image has no memory)");
                } else {
                    VkResult bindRc = vkBindImageMemory(b->device, sc->depthImage, sc->depthMemory, 0);
                    if (bindRc != VK_SUCCESS) {
                        MITHRIL_LOG_WARN("vk", "swapchain depth: vkBindImageMemory failed (rc=%d) "
                                         "— skipping depth view (image has no memory)",
                                         (int)bindRc);
                    } else {
                        VkImageViewCreateInfo dvci{};
                        dvci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                        dvci.image = sc->depthImage;
                        dvci.viewType = VK_IMAGE_VIEW_TYPE_2D;
                        dvci.format = depthFmt;
                        dvci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
                        dvci.subresourceRange.baseMipLevel = 0;
                        dvci.subresourceRange.levelCount = 1;
                        dvci.subresourceRange.baseArrayLayer = 0;
                        dvci.subresourceRange.layerCount = 1;
                        // SubTask 1.4: check view creation; on failure keep
                        // depthView = VK_NULL_HANDLE so begin_render_pass
                        // omits the depth attachment and renders color-only.
                        VkResult viewRc = vkCreateImageView(b->device, &dvci, nullptr, &sc->depthView);
                        if (viewRc != VK_SUCCESS) {
                            MITHRIL_LOG_WARN("vk", "swapchain depth: vkCreateImageView failed (rc=%d) "
                                             "— depthView stays VK_NULL_HANDLE",
                                             (int)viewRc);
                            sc->depthView = VK_NULL_HANDLE;
                        }
                    }
                }
            }
        }
    }

    return sc;
}

void destroy_swapchain(Swapchain* sc) {
    if (!sc) return;
    Backend* b = backend();
    if (!b->device) { delete sc; return; }
    vkDeviceWaitIdle(b->device);
    if (sc->depthView)   { vkDestroyImageView(b->device, sc->depthView, nullptr); sc->depthView = VK_NULL_HANDLE; }
    if (sc->depthImage)  { vkDestroyImage(b->device, sc->depthImage, nullptr); sc->depthImage = VK_NULL_HANDLE; }
    if (sc->depthMemory) { vkFreeMemory(b->device, sc->depthMemory, nullptr); sc->depthMemory = VK_NULL_HANDLE; }
    for (auto& v : sc->views) if (v) vkDestroyImageView(b->device, v, nullptr);
    sc->views.clear();
    sc->images.clear();
    if (sc->imageAvailable) { vkDestroySemaphore(b->device, sc->imageAvailable, nullptr); sc->imageAvailable = VK_NULL_HANDLE; }
    for (auto& sem : sc->renderFinishedPerImage) {
        if (sem) { vkDestroySemaphore(b->device, sem, nullptr); sem = VK_NULL_HANDLE; }
    }
    sc->renderFinishedPerImage.clear();
    sc->renderFinishedSignaledPerImage.clear();
    if (sc->swapchain) { vkDestroySwapchainKHR(b->device, sc->swapchain, nullptr); sc->swapchain = VK_NULL_HANDLE; }
    if (sc->surface)   { vkDestroySurfaceKHR(b->instance, sc->surface, nullptr); sc->surface = VK_NULL_HANDLE; }
    delete sc;
}

VkImageView swapchain_acquire_color(Swapchain* sc) {
    if (!sc || !sc->swapchain) return VK_NULL_HANDLE;
    // If the swapchain was marked dead by a previous fatal error (OOM,
    // surface lost, device lost), refuse to acquire. EGL will see the null
    // return, detect needsRebuild, and rebuild the swapchain on the next
    // eglSwapBuffers. Without this gate, acquire would keep returning null
    // (vkAcquireNextImageKHR fails on a dead swapchain) and the render thread
    // would spin in a no-op loop burning CPU.
    if (sc->needsRebuild) {
        return VK_NULL_HANDLE;
    }
    Backend* b = backend();
    if (b->deviceLost) {
        return VK_NULL_HANDLE;  // 持久性故障已挂起，跳过 acquire
    }
    if (sc->currentImage < 0) {
        uint32_t idx = 0;
        VkResult r = vkAcquireNextImageKHR(b->device, sc->swapchain, UINT64_MAX,
                                           sc->imageAvailable, VK_NULL_HANDLE, &idx);
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
            // FIX (VK_TIMEOUT 根因 - 深度参考 MobileGL):
            // VK_TIMEOUT 在 UINT64_MAX 超时下意味着 GPU watchdog 触发
            // （MoltenVK 的 GPU 超时保护）。这不是 swapchain 死亡——
            // swapchain 本身可能还活着，只是 GPU 太忙/卡住了。
            //
            // 原实现把 VK_TIMEOUT 和 VK_ERROR_OUT_OF_DEVICE_MEMORY 一样
            // 标记 needsRebuild=true，导致 swapchain 被销毁重建。但重建
            // swapchain 不能解决 GPU 卡住的问题——新 swapchain 下次 acquire
            // 还会 VK_TIMEOUT，形成"acquire timeout → rebuild → acquire
            // timeout"死循环，日志中反复出现 VK_TIMEOUT。
            //
            // 正确处理：VK_TIMEOUT 时 NOT 标记 needsRebuild，而是触发 OOM GC
            // （vkDeviceWaitIdle + drain_all_disposal_queues）释放显存后
            // 跳过本帧。swapchain 保留，下帧重试 acquire。
            //
            // 参考 MobileGL：它不处理 VK_TIMEOUT（用 UINT64_MAX 超时期望
            // 永不超时），但在 SubmitPendingCommandBuffer 失败时（设备降级
            // 场景）也是返回 false 让调用方跳过，而非销毁 swapchain。
            if (r == VK_TIMEOUT) {
                static int timeoutCount = 0;
                timeoutCount++;
                if (timeoutCount <= 3 || timeoutCount % 50 == 0) {
                    MITHRIL_LOG_WARN("vk", "swapchain_acquire_color: "
                                      "vkAcquireNextImageKHR returned VK_TIMEOUT "
                                      "(GPU busy/watchdog, occurrence #%d) — "
                                      "triggering OOM GC, keeping swapchain, "
                                      "skipping frame",
                                      timeoutCount);
                }
                // GPU 卡住：等待所有工作完成 + 释放延迟资源
                if (b->device) {
                    vkDeviceWaitIdle(b->device);
                }
                drain_all_disposal_queues();
                // 清除所有 fencePending（vkDeviceWaitIdle 后所有 fence 已 signaled）
                for (int i = 0; i < kMaxFramesInFlight; ++i) {
                    b->fencePending[i] = false;
                }
                // FIX (缺口 3): acquire 返回 VK_TIMEOUT 时，imageAvailable
                // semaphore 的状态未定义（可能 signaled 也可能未 signaled）。
                // 标记为已消费，避免后续 commit_frame 等待一个可能永远不会
                // signal 的 semaphore（死锁）或等待一个已 signal 的 semaphore
                // （在本帧没有 acquire 成功的情况下等待 acquire 信号是 UB）。
                // 下次成功 acquire 时 swapchain_acquire_color 会重置为 false。
                sc->imageAvailableConsumed = true;
                // 不标记 needsRebuild：swapchain 还活着，下帧重试
                return VK_NULL_HANDLE;
            }

            // FIX (日志刷屏): 限流 — 首次 + 每 100 次打印一条。
            // acquire 失败后 needsRebuild=true 会阻止重复调用，但 EGL 重建
            // swapchain 后可能再次失败，形成循环。
            static int acquireFailCount = 0;
            acquireFailCount++;
            if (acquireFailCount <= 3 || acquireFailCount % 100 == 0) {
                MITHRIL_LOG_ERROR("vk", "swapchain_acquire_color: vkAcquireNextImageKHR "
                                  "failed (rc=%d, idx=%u, fail #%d) — marking "
                                  "swapchain for rebuild",
                                  (int)r, (unsigned)idx, acquireFailCount);
            }
            // Fatal acquire errors: VK_ERROR_OUT_OF_DEVICE_MEMORY (-4),
            // VK_ERROR_SURFACE_LOST_KHR (-7), VK_ERROR_DEVICE_LOST (-4).
            // Mark the swapchain dead so EGL rebuilds it; otherwise the next
            // eglSwapBuffers would call acquire again on the same dead
            // swapchain and spin forever.
            //
            // FIX (缺口 3): 这些致命错误下 imageAvailable semaphore 状态
            // 也未定义。标记为已消费，避免后续 commit_frame 等待它。
            sc->imageAvailableConsumed = true;
            sc->needsRebuild = true;
            return VK_NULL_HANDLE;
        }
        sc->currentImage = (int)idx;
        // After acquire the image's actual layout is PRESENT_SRC_KHR (or
        // UNDEFINED on the very first acquire of that image). We deliberately
        // record the upcoming acquire->attachment barrier with oldLayout =
        // UNDEFINED so the previous frame's contents are discarded — this is
        // both legal (UNDEFINED is always a valid source layout) and matches
        // the semantics of starting a new frame on a swapchain image whose
        // post-present contents are undefined anyway.
        sc->currentColorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        // imageAvailable was just signaled by vkAcquireNextImageKHR. Mark it
        // unconsumed so the next commit_frame()'s vkQueueSubmit waits on it
        // (at COLOR_ATTACHMENT_OUTPUT stage). Without that wait, the GPU
        // starts rendering before the presentation engine releases the image
        // -> MoltenVK black screen. See MobileGL FrameContext.cpp:234.
        sc->imageAvailableConsumed = false;
    }
    VkImageView view = (sc->currentImage >= 0 && sc->currentImage < (int)sc->views.size())
                       ? sc->views[sc->currentImage] : VK_NULL_HANDLE;
    return view;
}

VkImageView swapchain_acquire_depth(Swapchain* sc) {
    return sc ? sc->depthView : VK_NULL_HANDLE;
}

void swapchain_present_and_acquire(Swapchain* sc) {
    if (!sc || !sc->swapchain) return;
    Backend* b = backend();
    if (b->deviceLost) {
        return;  // 持久性故障已挂起，跳过 present
    }

    if (sc->currentImage >= 0) {
        // Copy the index into a real uint32_t before taking its address.
        // sc->currentImage is int; casting int* to uint32_t* violates strict
        // aliasing and is UB, even though it works on every ABI we target.
        uint32_t idx = (uint32_t)sc->currentImage;
        VkPresentInfoKHR pi{};
        pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        // Wait on renderFinishedPerImage[currentImage] ONLY (NOT imageAvailable).
        // This mirrors MobileGL's GetPresentInfo (FrameContext.cpp:208), which
        // sets waitSemaphoreCount=1 on the per-image renderFinished semaphore.
        //
        // Why NOT imageAvailable: imageAvailable is the acquire semaphore,
        // signaled by vkAcquireNextImageKHR and consumed by commit_frame()'s
        // vkQueueSubmit (at COLOR_ATTACHMENT_OUTPUT stage). It expresses the
        // acquire->render dependency. The render->present dependency is a
        // SEPARATE edge expressed by renderFinished. If present also waited
        // on imageAvailable, we would (a) double-consume the acquire signal
        // (UB: a binary semaphore can only be waited on once per signal), and
        // (b) NOT actually guarantee rendering is complete — imageAvailable
        // was signaled at acquire time, long before any draw commands ran.
        //
        // Only wait on renderFinished if commit_frame() actually signaled it
        // for THIS image this frame (renderFinishedSignaledPerImage[idx]). If
        // commit_frame skipped submit (no commands, no layout transition,
        // imageAvailable already consumed by a previous mid-frame flush), then
        // there is no render work to wait on and present proceeds without a
        // wait — which is correct because the image is already in
        // PRESENT_SRC_KHR and no GPU work touches it.
        VkSemaphore waitSemaphore = VK_NULL_HANDLE;
        if (idx < sc->renderFinishedSignaledPerImage.size() &&
            sc->renderFinishedSignaledPerImage[idx] &&
            idx < sc->renderFinishedPerImage.size() &&
            sc->renderFinishedPerImage[idx] != VK_NULL_HANDLE) {
            waitSemaphore = sc->renderFinishedPerImage[idx];
            pi.waitSemaphoreCount = 1;
            pi.pWaitSemaphores = &waitSemaphore;
        }
        pi.swapchainCount = 1;
        pi.pSwapchains = &sc->swapchain;
        pi.pImageIndices = &idx;
        VkResult r = vkQueuePresentKHR(b->graphicsQueue, &pi);
        if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR) {
            b->consecutiveSubmitFailures = 0;
        } else if (r == VK_ERROR_OUT_OF_DATE_KHR) {
            // VK_ERROR_OUT_OF_DATE_KHR 表示 swapchain 需要重建，是正常路径，
            // 不计入 consecutiveSubmitFailures 计数器。
            sc->needsRebuild = true;
        } else if (r == VK_ERROR_DEVICE_LOST) {
            // 真正的设备丢失：设置 deviceLost，让 EGL 恢复路径处理
            sc->needsRebuild = true;
            b->deviceLost = true;
            static int presentDeviceLostCount = 0;
            presentDeviceLostCount++;
            if (presentDeviceLostCount <= 3 || presentDeviceLostCount % 100 == 0) {
                MITHRIL_LOG_ERROR("vk", "vkQueuePresentKHR returned "
                                  "VK_ERROR_DEVICE_LOST (occurrence #%d) — "
                                  "deviceLost set, EGL will attempt recovery",
                                  presentDeviceLostCount);
            }
        } else {
            // FIX (rendering suspended 根因): OOM 或其他 present 错误
            // 不再设置 deviceLost。标记 swapchain 重建 + 触发 OOM GC，
            // 跳过当前帧继续渲染。
            sc->needsRebuild = true;
            static int presentFailCount = 0;
            presentFailCount++;
            if (presentFailCount <= 3 || presentFailCount % 100 == 0) {
                MITHRIL_LOG_ERROR("vk", "vkQueuePresentKHR failed (rc=%d, "
                                  "occurrence #%d) — marking swapchain for "
                                  "rebuild, triggering OOM GC",
                                  (int)r, presentFailCount);
            }
            // OOM 主动 GC：释放延迟资源
            if (b->device) {
                vkDeviceWaitIdle(b->device);
            }
            drain_all_disposal_queues();
        }
        // The render-finished signal for THIS image has now been consumed by
        // present (or, on failure, will never be consumed — but we clear the
        // flag either way so the next commit_frame() rendering into this image
        // can signal again). Without this clear, a failed present would leave
        // renderFinishedSignaledPerImage[idx]=true forever, and the next
        // commit_frame would skip signaling (UB: present waits on a semaphore
        // that was never signaled).
        if (idx < sc->renderFinishedSignaledPerImage.size()) {
            sc->renderFinishedSignaledPerImage[idx] = false;
        }
        sc->currentImage = -1;
    }

    // Acquire the next image so the GLState default color view is valid.
    // If the swapchain was just marked needsRebuild, this returns null
    // (swapchain_acquire_color checks the flag first); EGL will detect the
    // null and rebuild.
    swapchain_acquire_color(sc);
}

} // namespace vk
} // namespace mithril
