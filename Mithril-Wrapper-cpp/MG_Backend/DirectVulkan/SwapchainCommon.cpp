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
    MITHRIL_LOG_WARN("vk", "Swapchain format selected: 0x%x (BGRA8%s) from %zu formats",
                     (unsigned)sc->format,
                     sc->format == VK_FORMAT_B8G8R8A8_SRGB ? "_SRGB" : "_UNORM",
                     fmts.size());

    // Surface capabilities.
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(b->physicalDevice, sc->surface, &caps);
    uint32_t imgCount = caps.minImageCount > 0 ? caps.minImageCount : 2;
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

    // Swapchain.
    VkSwapchainCreateInfoKHR scci{};
    scci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    scci.surface = sc->surface;
    scci.minImageCount = imgCount;
    scci.imageFormat = sc->format;
    scci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    scci.imageExtent = extent;
    scci.imageArrayLayers = 1;
    scci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    scci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    scci.preTransform = caps.currentTransform;
    scci.compositeAlpha = compAlpha;
    scci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    scci.clipped = VK_TRUE;
    if (vkCreateSwapchainKHR(b->device, &scci, nullptr, &sc->swapchain) != VK_SUCCESS) {
        MITHRIL_LOG_ERROR("vk", "vkCreateSwapchainKHR failed");
        // Surface ownership stays with the caller (see contract above).
        sc->surface = VK_NULL_HANDLE;
        delete sc;
        return nullptr;
    }
    sc->width = (int)extent.width;
    sc->height = (int)extent.height;
    MITHRIL_LOG_WARN("vk", "Swapchain created: %dx%d images=%u fmt=0x%x presentMode=FIFO",
                     sc->width, sc->height, imgCount, (unsigned)sc->format);

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

    // Render-finished semaphore signaled by commit_frame()'s vkQueueSubmit
    // and waited on by swapchain_present_and_acquire()'s vkQueuePresentKHR.
    // Pairs the submit→present dependency that imageAvailable alone cannot
    // express (imageAvailable is signaled by acquire, not by render).
    vkCreateSemaphore(b->device, &semi, nullptr, &sc->pendingRenderFinished);

    // Depth/stencil image (VK_FORMAT_D32_SFLOAT_S8_UINT).
    if (want_depth_stencil) {
        VkFormat depthFmt = VK_FORMAT_D32_SFLOAT_S8_UINT;
        sc->depthImages.resize(imgCount2);
        sc->depthMemories.resize(imgCount2);
        sc->depthViews.resize(imgCount2);
        sc->depthLayoutInitialized.assign(imgCount2, false);
        for (uint32_t i = 0; i < imgCount2; ++i) {
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
            if (vkCreateImage(b->device, &dici, nullptr, &sc->depthImages[i]) != VK_SUCCESS) {
                MITHRIL_LOG_WARN("vk", "swapchain depth[%u]: vkCreateImage failed", i);
                sc->depthImages[i] = VK_NULL_HANDLE;
                continue;
            }
            VkMemoryRequirements req{};
            vkGetImageMemoryRequirements(b->device, sc->depthImages[i], &req);
            uint32_t mt = find_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            // find_memory_type failure → skip this slot (no view). Without
            // this, vkAllocateMemory would be called with
            // memoryTypeIndex=0xFFFFFFFFu and likely fail, but the previous
            // code still created a view on the unbound image — the root
            // cause of kIOGPUCommandBufferCallbackErrorInvalidResource.
            if (mt == 0xFFFFFFFFu) {
                MITHRIL_LOG_WARN("vk", "swapchain depth[%u]: find_memory_type invalid "
                                 "(memoryTypeBits=0x%x) — skipping depth attachment",
                                 i, (unsigned)req.memoryTypeBits);
                vkDestroyImage(b->device, sc->depthImages[i], nullptr);
                sc->depthImages[i] = VK_NULL_HANDLE;
                continue;
            }
            VkMemoryAllocateInfo ai{};
            ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            ai.allocationSize = req.size;
            ai.memoryTypeIndex = mt;
            // Only create the view after BOTH vkAllocateMemory and
            // vkBindImageMemory succeed — a view on an unbound VkImage has
            // no backing storage and triggers InvalidResource at submit.
            if (vkAllocateMemory(b->device, &ai, nullptr, &sc->depthMemories[i]) != VK_SUCCESS) {
                MITHRIL_LOG_WARN("vk", "swapchain depth[%u]: vkAllocateMemory failed — "
                                 "skipping depth view (image has no memory)", i);
                vkDestroyImage(b->device, sc->depthImages[i], nullptr);
                sc->depthImages[i] = VK_NULL_HANDLE;
                continue;
            }
            if (vkBindImageMemory(b->device, sc->depthImages[i], sc->depthMemories[i], 0) != VK_SUCCESS) {
                MITHRIL_LOG_WARN("vk", "swapchain depth[%u]: vkBindImageMemory failed — "
                                 "skipping depth view (image has no memory)", i);
                vkFreeMemory(b->device, sc->depthMemories[i], nullptr);
                sc->depthMemories[i] = VK_NULL_HANDLE;
                vkDestroyImage(b->device, sc->depthImages[i], nullptr);
                sc->depthImages[i] = VK_NULL_HANDLE;
                continue;
            }
            VkImageViewCreateInfo dvci{};
            dvci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            dvci.image = sc->depthImages[i];
            dvci.viewType = VK_IMAGE_VIEW_TYPE_2D;
            dvci.format = depthFmt;
            dvci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            dvci.subresourceRange.baseMipLevel = 0;
            dvci.subresourceRange.levelCount = 1;
            dvci.subresourceRange.baseArrayLayer = 0;
            dvci.subresourceRange.layerCount = 1;
            // Check view creation; on failure keep depthViews[i] =
            // VK_NULL_HANDLE so begin_render_pass omits the depth attachment
            // for this slot and renders color-only.
            if (vkCreateImageView(b->device, &dvci, nullptr, &sc->depthViews[i]) != VK_SUCCESS) {
                MITHRIL_LOG_WARN("vk", "swapchain depth[%u]: vkCreateImageView failed — "
                                 "depthViews[%u] stays VK_NULL_HANDLE", i, i);
                sc->depthViews[i] = VK_NULL_HANDLE;
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
    for (auto& v : sc->depthViews)    if (v) vkDestroyImageView(b->device, v, nullptr);
    for (auto& img : sc->depthImages) if (img) vkDestroyImage(b->device, img, nullptr);
    for (auto& m : sc->depthMemories) if (m) vkFreeMemory(b->device, m, nullptr);
    sc->depthViews.clear();
    sc->depthImages.clear();
    sc->depthMemories.clear();
    sc->depthLayoutInitialized.clear();
    for (auto& v : sc->views) if (v) vkDestroyImageView(b->device, v, nullptr);
    sc->views.clear();
    sc->images.clear();
    if (sc->imageAvailable) { vkDestroySemaphore(b->device, sc->imageAvailable, nullptr); sc->imageAvailable = VK_NULL_HANDLE; }
    if (sc->pendingRenderFinished) { vkDestroySemaphore(b->device, sc->pendingRenderFinished, nullptr); sc->pendingRenderFinished = VK_NULL_HANDLE; }
    sc->renderFinishedSignaled = false;
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
            MITHRIL_LOG_ERROR("vk", "swapchain_acquire_color: vkAcquireNextImageKHR "
                              "failed (rc=%d, idx=%u) — marking swapchain for rebuild",
                              (int)r, (unsigned)idx);
            // Fatal acquire errors: VK_ERROR_OUT_OF_DEVICE_MEMORY (-4),
            // VK_ERROR_SURFACE_LOST_KHR (-7), VK_ERROR_DEVICE_LOST (-4).
            // Mark the swapchain dead so EGL rebuilds it; otherwise the next
            // eglSwapBuffers would call acquire again on the same dead
            // swapchain and spin forever.
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
    if (!sc) return VK_NULL_HANDLE;
    if (sc->currentImage < 0 || sc->currentImage >= (int)sc->depthViews.size()) return VK_NULL_HANDLE;
    return sc->depthViews[sc->currentImage];
}

void swapchain_present_and_acquire(Swapchain* sc) {
    if (!sc || !sc->swapchain) return;
    Backend* b = backend();
    if (b->deviceLost) {
        return;  // Persistent fault suspended, skip present
    }

    // Log first few presents so developers can trace the frame lifecycle.
    {
        static int present_count = 0;
        if (present_count < 10) {
            MITHRIL_LOG_WARN("vk", "swapchain_present_and_acquire #%d: "
                              "currentImage=%d renderFinishedSignaled=%d "
                              "hasCommandsInFlight=%s",
                              present_count + 1, sc->currentImage,
                              (int)sc->renderFinishedSignaled,
                              sc->renderFinishedSignaled ? "yes" : "no");
            present_count++;
        }
    }

    if (sc->currentImage >= 0) {
        // Copy the index into a real uint32_t before taking its address.
        // sc->currentImage is int; casting int* to uint32_t* violates strict
        // aliasing and is UB, even though it works on every ABI we target.
        uint32_t idx = (uint32_t)sc->currentImage;
        VkPresentInfoKHR pi{};
        pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        // Wait on renderFinished ONLY (NOT imageAvailable). This mirrors
        // MobileGL's GetPresentInfo (FrameContext.cpp:208), which sets
        // waitSemaphoreCount=1 on the per-image renderFinished semaphore.
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
        // this frame (renderFinishedSignaled). If commit_frame skipped submit
        // (no commands, no layout transition, imageAvailable already consumed
        // by a previous mid-frame flush), then there is no render work to wait
        // on and present proceeds without a wait — which is correct because
        // the image is already in PRESENT_SRC_KHR and no GPU work touches it.
        VkSemaphore waitSemaphore = VK_NULL_HANDLE;
        if (sc->renderFinishedSignaled && sc->pendingRenderFinished != VK_NULL_HANDLE) {
            waitSemaphore = sc->pendingRenderFinished;
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
        } else {
            // 致命 present 错误（VK_ERROR_OUT_OF_DEVICE_MEMORY /
            // VK_ERROR_SURFACE_LOST_KHR / VK_ERROR_OUT_OF_HOST_MEMORY 等）：
            // swapchain 不可用，标记重建；否则下一帧 present 会以同样方式失败，
            // 渲染线程会在日志风暴中空转。
            sc->needsRebuild = true;
            b->consecutiveSubmitFailures++;
            if (b->consecutiveSubmitFailures >= 3 && !b->deviceLost) {
                b->deviceLost = true;
                MITHRIL_LOG_ERROR("vk", "Persistent GPU fault detected after %d "
                                  "consecutive present failures — rendering suspended",
                                  b->consecutiveSubmitFailures);
            }
            // 日志限流：首次失败 + 每 100 次各打印一条，避免日志风暴。
            if (b->consecutiveSubmitFailures == 1 || b->consecutiveSubmitFailures % 100 == 0) {
                MITHRIL_LOG_ERROR("vk", "vkQueuePresentKHR failed (rc=%d) — marking "
                                  "swapchain for rebuild", (int)r);
            }
        }
        // The render-finished signal has now been consumed by present (or, on
        // failure, will never be consumed — but we clear the flag either way
        // so the next commit_frame() can signal again). Without this clear,
        // a failed present would leave renderFinishedSignaled=true forever,
        // and the next commit_frame would skip signaling (UB: present waits
        // on a semaphore that was never signaled).
        sc->renderFinishedSignaled = false;
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
