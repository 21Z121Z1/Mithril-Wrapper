// Mithril-Wrapper - MG_Backend/DirectVulkan/SwapchainAndroid.cpp
// Android platform entry: creates the VkSurfaceKHR via VK_KHR_android_surface
// (vkCreateAndroidSurfaceKHR) and then delegates to
// create_swapchain_post_surface() (in SwapchainCommon.cpp) for the rest of the
// swapchain pipeline - identical to what SwapchainMetal.mm does on Apple, just
// with a different surface creation call.
//
// VK_USE_PLATFORM_ANDROID_KHR must be defined before <vulkan/vulkan.h> so
// vulkan_android.h is visible. As with the Metal TU, it is deliberately not a
// global CMake compile-definition.
#if defined(__ANDROID__)

#define VK_USE_PLATFORM_ANDROID_KHR 1

#include "Swapchain.h"
#include "Device.h"
#include "../../MG_Impl/Log.h"

#include <android/native_window.h>

namespace mithril {
namespace vk {

Swapchain* create_swapchain(void* native_window, int width, int height,
                            int want_depth_stencil, int platform_hint) {
    Backend* b = backend();
    if (!b->initialized || !native_window || width <= 0 || height <= 0) return nullptr;
    // Only one surface path is compiled into this TU, so an explicit hint for
    // another platform is simply ignored.
    (void)platform_hint;

    ANativeWindow* win = static_cast<ANativeWindow*>(native_window);
    if (!win) {
        MITHRIL_LOG_ERROR("vk", "create_swapchain: null ANativeWindow");
        return nullptr;
    }

    // Resolved on demand rather than cached on Backend: this is the only place
    // that needs it, and keeping it out of Device.h avoids making every other
    // TU depend on the Android platform header.
    auto createAndroidSurfaceKHR = reinterpret_cast<PFN_vkCreateAndroidSurfaceKHR>(
        vkGetInstanceProcAddr(b->instance, "vkCreateAndroidSurfaceKHR"));
    if (!createAndroidSurfaceKHR) {
        MITHRIL_LOG_ERROR("vk", "vkCreateAndroidSurfaceKHR not resolved");
        return nullptr;
    }

    VkAndroidSurfaceCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    sci.pNext = nullptr;
    sci.flags = 0;
    sci.window = win;

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    const VkResult r = createAndroidSurfaceKHR(b->instance, &sci, nullptr, &surface);
    if (r != VK_SUCCESS) {
        MITHRIL_LOG_ERROR("vk", "vkCreateAndroidSurfaceKHR failed (%d)", (int)r);
        return nullptr;
    }

    // Delegate the rest (format query / vkCreateSwapchainKHR / image views /
    // depth image / acquire semaphore) to the platform-independent path. On
    // failure post_surface does NOT destroy the surface - we own it here until
    // post_surface signals success by returning a non-null Swapchain.
    Swapchain* sc = create_swapchain_post_surface(surface, width, height, want_depth_stencil);
    if (!sc) {
        vkDestroySurfaceKHR(b->instance, surface, nullptr);
    }
    return sc;
}

} // namespace vk
} // namespace mithril

// ===========================================================================
// Public C API wrappers (declared in MG_Backend/Backend.h)
// ===========================================================================
extern "C" {

void* backend_create_swapchain(void* native_window, int width, int height,
                               int want_depth_stencil, int platform_hint) {
    return mithril::vk::create_swapchain(native_window, width, height,
                                         want_depth_stencil, platform_hint);
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

int backend_swapchain_needs_rebuild(void* swapchain_state) {
    auto* sc = (mithril::vk::Swapchain*)swapchain_state;
    return sc && sc->needsRebuild ? 1 : 0;
}

void backend_swapchain_set_drawable_size(void* swapchain_state, int w, int h) {
    auto* sc = (mithril::vk::Swapchain*)swapchain_state;
    if (!sc) return;
    sc->actualDrawableWidth = w;
    sc->actualDrawableHeight = h;
}

void backend_swapchain_mark_rebuild(void* swapchain_state) {
    auto* sc = (mithril::vk::Swapchain*)swapchain_state;
    if (!sc) return;
    sc->needsRebuild = true;
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
    // create_swapchain_post_surface(); there is no per-swapchain field
    // tracking it.
    (void)sc;
    return VK_FORMAT_D32_SFLOAT_S8_UINT;
}

} // extern "C"

#endif // __ANDROID__
