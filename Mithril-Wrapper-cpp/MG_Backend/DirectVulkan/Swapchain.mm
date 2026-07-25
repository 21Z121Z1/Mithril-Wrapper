// Mithril-Wrapper - MG_Backend/DirectVulkan/Swapchain.mm
// Per-EGLSurface Vulkan swapchain: VkSurfaceKHR (via VK_EXT_metal_surface) +
// VkSwapchainKHR + swapchain images/views + depth VkImage. The CAMetalLayer is
// bridged in as void* from egl.mm; this TU is compiled as Objective-C++ so it
// can define VK_USE_PLATFORM_METAL_EXT (which pulls in <Metal/Metal.h> for the
// VkMetalSurfaceCreateInfoEXT / PFN_vkCreateMetalSurfaceEXT declarations).
//
// VK_USE_PLATFORM_METAL_EXT MUST be defined before #include <vulkan/vulkan.h>
// (transitively via Swapchain.h / Device.h) so vulkan_metal.h is visible. It
// is intentionally NOT a global CMake compile-definition: doing so would force
// every .cpp in the backend to be compiled as .mm (the Metal system header is
// Objective-C only). Device.h stores the function pointer as PFN_vkVoidFunction
// to stay metal-free; this file casts it to PFN_vkCreateMetalSurfaceEXT here.
#define VK_USE_PLATFORM_METAL_EXT 1
#include "Swapchain.h"
#include "Device.h"
#include "Resources.h"
#include "../../MG_Impl/Log.h"

#include <cstring>
#include <vector>

namespace mithril {
namespace vk {

Swapchain* create_swapchain(void* cametal_layer, int width, int height,
                            int want_depth_stencil) {
    Backend* b = backend();
    if (!b->initialized || !cametal_layer || width <= 0 || height <= 0) return nullptr;

    Swapchain* sc = new Swapchain{};
    sc->width = width;
    sc->height = height;

    // VkSurfaceKHR via VK_EXT_metal_surface. The layer pointer is bridged from
    // egl.mm as void*; cast to CAMetalLayer* (the type vulkan_metal.h expects).
    // createMetalSurfaceEXT is stored as PFN_vkVoidFunction on the Backend
    // (see Device.h) — cast to the real type here.
    VkMetalSurfaceCreateInfoEXT sci{};
    sci.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
    sci.pLayer = (const CAMetalLayer*)cametal_layer;
    if (!b->createMetalSurfaceEXT) {
        MITHRIL_LOG_ERROR("vk", "vkCreateMetalSurfaceEXT not resolved");
        delete sc;
        return nullptr;
    }
    auto createMetalSurfaceEXT = (PFN_vkCreateMetalSurfaceEXT)b->createMetalSurfaceEXT;
    if (createMetalSurfaceEXT(b->instance, &sci, nullptr, &sc->surface) != VK_SUCCESS) {
        MITHRIL_LOG_ERROR("vk", "vkCreateMetalSurfaceEXT failed");
        delete sc;
        return nullptr;
    }

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
    scci.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    scci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    scci.clipped = VK_TRUE;
    if (vkCreateSwapchainKHR(b->device, &scci, nullptr, &sc->swapchain) != VK_SUCCESS) {
        MITHRIL_LOG_ERROR("vk", "vkCreateSwapchainKHR failed");
        vkDestroySurfaceKHR(b->instance, sc->surface, nullptr);
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

// ===========================================================================
// Public C API wrappers (declared in MG_Backend/Backend.h)
// ===========================================================================
extern "C" {

void* backend_create_swapchain(void* cametal_layer, int width, int height,
                               int want_depth_stencil) {
    return mithril::vk::create_swapchain(cametal_layer, width, height, want_depth_stencil);
}

void backend_destroy_swapchain(void* swapchain_state) {
    mithril::vk::destroy_swapchain((mithril::vk::Swapchain*)swapchain_state);
}

VkImageView backend_swapchain_acquire_color(void* swapchain_state) {
    return mithril::vk::swapchain_acquire_color((mithril::vk::Swapchain*)swapchain_state);
}

VkImageView backend_swapchain_acquire_depth(void* swapchain_state) {
    return mithril::vk::swapchain_acquire_depth((mithril::vk::Swapchain*)swapchain_state);
}

int backend_swapchain_width(void* swapchain_state) {
    auto* sc = (mithril::vk::Swapchain*)swapchain_state;
    return sc ? sc->width : 0;
}

int backend_swapchain_height(void* swapchain_state) {
    auto* sc = (mithril::vk::Swapchain*)swapchain_state;
    return sc ? sc->height : 0;
}

void backend_present_and_acquire(void* swapchain_state) {
    mithril::vk::swapchain_present_and_acquire((mithril::vk::Swapchain*)swapchain_state);
}

VkImage backend_swapchain_current_color_image(void* swapchain_state) {
    auto* sc = (mithril::vk::Swapchain*)swapchain_state;
    if (!sc || sc->currentImage < 0 || sc->currentImage >= (int)sc->images.size())
        return VK_NULL_HANDLE;
    return sc->images[sc->currentImage];
}

VkFormat backend_swapchain_color_format(void* swapchain_state) {
    auto* sc = (mithril::vk::Swapchain*)swapchain_state;
    return sc ? sc->format : VK_FORMAT_UNDEFINED;
}

VkImage backend_swapchain_current_depth_image(void* swapchain_state) {
    auto* sc = (mithril::vk::Swapchain*)swapchain_state;
    return sc ? sc->depthImage : VK_NULL_HANDLE;
}

VkFormat backend_swapchain_depth_format(void* swapchain_state) {
    auto* sc = (mithril::vk::Swapchain*)swapchain_state;
    // The depth image is always created as VK_FORMAT_D32_SFLOAT_S8_UINT in
    // create_swapchain(); there is no per-swapchain field tracking it.
    (void)sc;
    return VK_FORMAT_D32_SFLOAT_S8_UINT;
}

} // extern "C"
