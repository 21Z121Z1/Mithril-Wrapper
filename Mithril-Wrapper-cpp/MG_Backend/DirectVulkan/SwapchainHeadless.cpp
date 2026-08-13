// Linux headless swapchain stub — enables running tests/render_smoke.c
// on a Linux CI box with lavapipe (Mesa Vulkan software rasterizer).
//
// The Apple Metal swapchain (SwapchainMetal.mm) creates a VkSurfaceKHR from
// a CAMetalLayer. On Linux there is no such surface; instead we create a
// memory-backed VkImage (R8G8B8A8_UNORM) that stands in for the swapchain
// color, and a depth image. backend_swapchain_acquire_color returns the
// color view; present is a no-op (there is no display). This is enough for
// the offscreen render smoke test to exercise the full GL -> Vulkan draw
// path (render pass, pipeline, descriptors, readback).
#include "../Backend.h"
#include "Device.h"
#include "Swapchain.h"

#include <cstring>
#include <vulkan/vulkan.h>

namespace mithril {
namespace vk {

namespace {

struct HeadlessSwapchain {
    VkImage colorImage = VK_NULL_HANDLE;
    VkDeviceMemory colorMemory = VK_NULL_HANDLE;
    VkImageView colorView = VK_NULL_HANDLE;
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
    VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
    int width = 0;
    int height = 0;
    int currentImage = -1;
    bool needsRebuild = false;
    int actualDrawableWidth = 0;
    int actualDrawableHeight = 0;
};

VkDeviceMemory allocate_image_memory(VkImage image) {
    Backend* b = backend();
    if (!b->device) return VK_NULL_HANDLE;
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(b->device, image, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    for (uint32_t i = 0; i < 32; ++i) {
        if ((req.memoryTypeBits >> i) & 1) {
            VkMemoryType mt = b->memProps.memoryTypes[i];
            if (mt.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
                ai.memoryTypeIndex = i;
                VkDeviceMemory mem = VK_NULL_HANDLE;
                if (vkAllocateMemory(b->device, &ai, nullptr, &mem) == VK_SUCCESS) return mem;
            }
        }
    }
    return VK_NULL_HANDLE;
}

VkImage create_image(VkFormat fmt, int w, int h, VkImageUsageFlags usage) {
    Backend* b = backend();
    if (!b->device) return VK_NULL_HANDLE;
    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = fmt;
    ici.extent = {(uint32_t)w, (uint32_t)h, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_LINEAR;  // host-visible readback
    ici.usage = usage;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImage img = VK_NULL_HANDLE;
    if (vkCreateImage(b->device, &ici, nullptr, &img) != VK_SUCCESS) return VK_NULL_HANDLE;
    return img;
}

}  // namespace

}  // namespace vk
}  // namespace mithril

// ---- exported backend swapchain API (Linux headless) ----

void* backend_create_swapchain(void* native_window, int width, int height,
                               int want_depth_stencil, int platform_hint) {
    (void)native_window; (void)platform_hint;
    auto* sc = new mithril::vk::HeadlessSwapchain();
    sc->width = width > 0 ? width : 64;
    sc->height = height > 0 ? height : 64;
    sc->actualDrawableWidth = sc->width;
    sc->actualDrawableHeight = sc->height;
    sc->colorImage = mithril::vk::create_image(sc->colorFormat, sc->width, sc->height,
                                               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                               VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    sc->colorMemory = mithril::vk::allocate_image_memory(sc->colorImage);
    if (sc->colorMemory) {
        mithril::vk::backend()->device;
        vkBindImageMemory(mithril::vk::backend()->device, sc->colorImage, sc->colorMemory, 0);
    }
    if (want_depth_stencil) {
        sc->depthImage = mithril::vk::create_image(sc->depthFormat, sc->width, sc->height,
                                                   VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
        sc->depthMemory = mithril::vk::allocate_image_memory(sc->depthImage);
        if (sc->depthMemory) {
            vkBindImageMemory(mithril::vk::backend()->device, sc->depthImage, sc->depthMemory, 0);
        }
    }
    return sc;
}

void backend_destroy_swapchain(void* swapchain_state) {
    auto* sc = (mithril::vk::HeadlessSwapchain*)swapchain_state;
    if (!sc) return;
    VkDevice dev = mithril::vk::backend()->device;
    if (dev) {
        if (sc->colorView) vkDestroyImageView(dev, sc->colorView, nullptr);
        if (sc->depthView) vkDestroyImageView(dev, sc->depthView, nullptr);
        if (sc->colorImage) vkDestroyImage(dev, sc->colorImage, nullptr);
        if (sc->depthImage) vkDestroyImage(dev, sc->depthImage, nullptr);
        if (sc->colorMemory) vkFreeMemory(dev, sc->colorMemory, nullptr);
        if (sc->depthMemory) vkFreeMemory(dev, sc->depthMemory, nullptr);
    }
    delete sc;
}

VkImageView backend_swapchain_acquire_color(void* swapchain_state) {
    auto* sc = (mithril::vk::HeadlessSwapchain*)swapchain_state;
    if (!sc) return VK_NULL_HANDLE;
    VkDevice dev = mithril::vk::backend()->device;
    if (!dev) return VK_NULL_HANDLE;
    if (sc->colorView == VK_NULL_HANDLE) {
        VkImageViewCreateInfo ici{};
        ici.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ici.image = sc->colorImage;
        ici.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ici.format = sc->colorFormat;
        ici.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCreateImageView(dev, &ici, nullptr, &sc->colorView);
    }
    sc->currentImage = 0;
    return sc->colorView;
}

VkImageView backend_swapchain_acquire_depth(void* swapchain_state) {
    auto* sc = (mithril::vk::HeadlessSwapchain*)swapchain_state;
    if (!sc) return VK_NULL_HANDLE;
    VkDevice dev = mithril::vk::backend()->device;
    if (!dev) return VK_NULL_HANDLE;
    if (sc->depthView == VK_NULL_HANDLE && sc->depthImage) {
        VkImageViewCreateInfo ici{};
        ici.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ici.image = sc->depthImage;
        ici.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ici.format = sc->depthFormat;
        ici.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
        vkCreateImageView(dev, &ici, nullptr, &sc->depthView);
    }
    return sc->depthView;
}

int backend_swapchain_width(void* swapchain_state) {
    auto* sc = (mithril::vk::HeadlessSwapchain*)swapchain_state;
    return sc ? sc->width : 0;
}

int backend_swapchain_height(void* swapchain_state) {
    auto* sc = (mithril::vk::HeadlessSwapchain*)swapchain_state;
    return sc ? sc->height : 0;
}

void backend_present_and_acquire(void* swapchain_state) {
    // No display on headless Linux; nothing to present.
    backend_swapchain_acquire_color(swapchain_state);
}

int backend_swapchain_needs_rebuild(void* swapchain_state) {
    auto* sc = (mithril::vk::HeadlessSwapchain*)swapchain_state;
    int r = sc ? sc->needsRebuild : 0;
    if (sc) sc->needsRebuild = false;
    return r;
}

void backend_swapchain_set_drawable_size(void* swapchain_state, int w, int h) {
    auto* sc = (mithril::vk::HeadlessSwapchain*)swapchain_state;
    if (sc) { sc->actualDrawableWidth = w; sc->actualDrawableHeight = h; }
}

void backend_swapchain_mark_rebuild(void* swapchain_state) {
    auto* sc = (mithril::vk::HeadlessSwapchain*)swapchain_state;
    if (sc) sc->needsRebuild = true;
}

VkImage backend_swapchain_current_color_image(void* swapchain_state) {
    auto* sc = (mithril::vk::HeadlessSwapchain*)swapchain_state;
    return sc ? sc->colorImage : VK_NULL_HANDLE;
}

VkFormat backend_swapchain_color_format(void* swapchain_state) {
    auto* sc = (mithril::vk::HeadlessSwapchain*)swapchain_state;
    return sc ? sc->colorFormat : VK_FORMAT_UNDEFINED;
}

VkImage backend_swapchain_current_depth_image(void* swapchain_state) {
    auto* sc = (mithril::vk::HeadlessSwapchain*)swapchain_state;
    return sc ? sc->depthImage : VK_NULL_HANDLE;
}

VkFormat backend_swapchain_depth_format(void* swapchain_state) {
    auto* sc = (mithril::vk::HeadlessSwapchain*)swapchain_state;
    return sc ? sc->depthFormat : VK_FORMAT_UNDEFINED;
}
