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
    MITHRIL_LOG_WARN("vk", "Swapchain: compositeAlpha=0x%x, supported=0x%x",
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
            VkMemoryAllocateInfo ai{};
            ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            ai.allocationSize = req.size;
            ai.memoryTypeIndex = mt;
            if (vkAllocateMemory(b->device, &ai, nullptr, &sc->depthMemory) == VK_SUCCESS) {
                vkBindImageMemory(b->device, sc->depthImage, sc->depthMemory, 0);
            }
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
            vkCreateImageView(b->device, &dvci, nullptr, &sc->depthView);
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
    if (sc->swapchain) { vkDestroySwapchainKHR(b->device, sc->swapchain, nullptr); sc->swapchain = VK_NULL_HANDLE; }
    if (sc->surface)   { vkDestroySurfaceKHR(b->instance, sc->surface, nullptr); sc->surface = VK_NULL_HANDLE; }
    delete sc;
}

VkImageView swapchain_acquire_color(Swapchain* sc) {
    if (!sc || !sc->swapchain) return VK_NULL_HANDLE;
    Backend* b = backend();
    if (sc->currentImage < 0) {
        uint32_t idx = 0;
        VkResult r = vkAcquireNextImageKHR(b->device, sc->swapchain, UINT64_MAX,
                                           sc->imageAvailable, VK_NULL_HANDLE, &idx);
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
            MITHRIL_LOG_WARN("vk", "vkAcquireNextImageKHR failed (rc=%d)", (int)r);
            return VK_NULL_HANDLE;
        }
        sc->currentImage = (int)idx;
    }
    return sc->currentImage >= 0 ? sc->views[sc->currentImage] : VK_NULL_HANDLE;
}

VkImageView swapchain_acquire_depth(Swapchain* sc) {
    return sc ? sc->depthView : VK_NULL_HANDLE;
}

void swapchain_present_and_acquire(Swapchain* sc) {
    if (!sc || !sc->swapchain) return;
    Backend* b = backend();

    if (sc->currentImage >= 0) {
        // Copy the index into a real uint32_t before taking its address.
        // sc->currentImage is int; casting int* to uint32_t* violates strict
        // aliasing and is UB, even though it works on every ABI we target.
        uint32_t idx = (uint32_t)sc->currentImage;
        VkPresentInfoKHR pi{};
        pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        // Wait on the acquire semaphore so the present only proceeds once the
        // image is actually available, and so the semaphore is consumed.
        // Each vkAcquireNextImageKHR signal MUST be paired with exactly one
        // wait, otherwise the semaphore accumulates signals and the next
        // acquire behaves as already-signalled (undefined behaviour).
        // NOTE: full submit-waits-acquire synchronisation is deferred; the
        // per-frame vkWaitForFences in commit_frame() currently serialises
        // CPU against GPU completion, which keeps this correct in practice.
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &sc->imageAvailable;
        pi.swapchainCount = 1;
        pi.pSwapchains = &sc->swapchain;
        pi.pImageIndices = &idx;
        VkResult r = vkQueuePresentKHR(b->graphicsQueue, &pi);
        if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
            MITHRIL_LOG_WARN("vk", "vkQueuePresentKHR failed (rc=%d)", (int)r);
        }
        sc->currentImage = -1;
    }

    // Acquire the next image so the GLState default color view is valid.
    swapchain_acquire_color(sc);
}

} // namespace vk
} // namespace mithril
