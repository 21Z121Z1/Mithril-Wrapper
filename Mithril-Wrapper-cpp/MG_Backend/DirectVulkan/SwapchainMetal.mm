// Mithril-Wrapper - MG_Backend/DirectVulkan/SwapchainMetal.mm
// Apple platform entry: creates the VkSurfaceKHR via VK_EXT_metal_surface
// (vkCreateMetalSurfaceEXT) and then delegates to create_swapchain_post_surface()
// (in SwapchainCommon.cpp) for the rest of the swapchain pipeline. This TU is
// compiled as Objective-C++ so it can define VK_USE_PLATFORM_METAL_EXT (which
// pulls in <Metal/Metal.h> for the VkMetalSurfaceCreateInfoEXT /
// PFN_vkCreateMetalSurfaceEXT declarations).
//
// VK_USE_PLATFORM_METAL_EXT MUST be defined before #include <vulkan/vulkan.h>
// (transitively via Swapchain.h / Device.h) so vulkan_metal.h is visible. It
// is intentionally NOT a global CMake compile-definition: doing so would force
// every .cpp in the backend to be compiled as .mm (the Metal system header is
// Objective-C only). Device.h stores the function pointer as PFN_vkVoidFunction
// to stay metal-free; this file casts it to PFN_vkCreateMetalSurfaceEXT here.
//
// The public C API (backend_* wrappers) is defined here on Apple platforms.
#define VK_USE_PLATFORM_METAL_EXT 1
#include "Swapchain.h"
#include "Device.h"
#include "../../MG_Impl/Log.h"

namespace mithril {
namespace vk {

Swapchain* create_swapchain(void* native_window, int width, int height,
                            int want_depth_stencil, int platform_hint) {
    Backend* b = backend();
    if (!b->initialized || !native_window || width <= 0 || height <= 0) return nullptr;
    // platform_hint is taken as a hint; the Metal path is the only one
    // compiled into this TU, so any explicit non-Metal token is
    // honoured by simply proceeding with the Metal surface creation path.
    (void)platform_hint;

    // VkSurfaceKHR via VK_EXT_metal_surface. The layer pointer is bridged from
    // egl.cpp as void*; cast to CAMetalLayer* (the type vulkan_metal.h
    // expects). createMetalSurfaceEXT is stored as PFN_vkVoidFunction on the
    // Backend (see Device.h) - cast to the real type here.
    VkMetalSurfaceCreateInfoEXT sci{};
    sci.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
    // __bridge: the CAMetalLayer is owned by the host view (EglSurface's
    // native_window, a weak ARC reference). We borrow the pointer for Vulkan
    // surface creation without transferring ownership. A plain C cast is
    // rejected under -fobjc-arc.
    sci.pLayer = (__bridge const CAMetalLayer*)native_window;
    if (!b->createMetalSurfaceEXT) {
        MITHRIL_LOG_ERROR("vk", "vkCreateMetalSurfaceEXT not resolved");
        return nullptr;
    }
    auto createMetalSurfaceEXT = (PFN_vkCreateMetalSurfaceEXT)b->createMetalSurfaceEXT;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (createMetalSurfaceEXT(b->instance, &sci, nullptr, &surface) != VK_SUCCESS) {
        MITHRIL_LOG_ERROR("vk", "vkCreateMetalSurfaceEXT failed");
        return nullptr;
    }

    // Delegate the rest (format query / vkCreateSwapchainKHR / image views /
    // depth image / acquire semaphore) to the platform-independent path. On
    // failure post_surface does NOT destroy the surface — we own it here
    // until post_surface signals success by returning a non-null Swapchain.
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
