// Mithril-Wrapper - MG_Backend/DirectVulkan/SwapchainX11.cpp
// Linux/X11 platform entry: creates the VkSurfaceKHR via
// VK_KHR_xlib_surface (vkCreateXlibSurfaceKHR) and then delegates to
// create_swapchain_post_surface() (in SwapchainCommon.cpp) for the rest of
// the swapchain pipeline. libX11 is loaded dynamically via dlopen() so the
// backend binary does not gain a hard link-time dependency on libX11 (the
// host application is responsible for providing an X11 window — typically
// the Window handle backing an EGLSurface).
//
// VK_USE_PLATFORM_XLIB_KHR MUST be defined before #include <vulkan/vulkan.h>
// (transitively via Swapchain.h) so vulkan_xlib.h is visible and the
// VkXlibSurfaceCreateInfoKHR / PFN_vkCreateXlibSurfaceKHR declarations are
// available. Defining the macro causes vulkan_xlib.h to #include
// <X11/Xlib.h>, which is what brings the `Display` and `Window` typedefs
// into scope; the host must therefore have the X11 dev headers installed
// (the loader resolves libX11.so.6 at runtime via dlopen).
//
// The public C API (backend_* wrappers) is defined here on Linux platforms;
// on Apple the same set of wrappers is provided by SwapchainMetal.mm.
//
// NOTE: vkCreateXlibSurfaceKHR is resolved via the createXlibSurfaceKHR
// field on the Backend struct (mirroring createMetalSurfaceEXT). Device.cpp
// resolves it once at instance creation; we cast it to PFN_vkCreateXlibSurfaceKHR
// here at the call site, where VK_USE_PLATFORM_XLIB_KHR is active.
#define VK_USE_PLATFORM_XLIB_KHR 1
#include "Swapchain.h"
#include "Device.h"
#include "../../MG_Impl/Log.h"

#include <dlfcn.h>
#include <cstdio>

namespace mithril {
namespace vk {

Swapchain* create_swapchain(void* native_window, int width, int height,
                            int want_depth_stencil, int platform_hint) {
    Backend* b = backend();
    if (!b->initialized || !native_window || width <= 0 || height <= 0) return nullptr;
    // platform_hint is taken as a hint; the X11 path is the only one compiled
    // into this TU, so explicit tokens other than EGL_PLATFORM_X11_KHR are
    // still routed through X11 (the host is responsible for passing a valid
    // X11 Window handle in native_window).
    (void)platform_hint;

    // Resolve XOpenDisplay / XDefaultScreen via dlopen(libX11.so.6). The
    // handle is leaked intentionally (process-lifetime cache) so repeated
    // swapchain creations don't pay the dlopen cost. The X11 function
    // pointer types are spelled against the standard Xlib prototypes
    // (Display* in / int return); vulkan_xlib.h, pulled in by
    // VK_USE_PLATFORM_XLIB_KHR, transitively includes <X11/Xlib.h> so the
    // types are visible here.
    static void* libx11 = nullptr;
    static Display* (*XOpenDisplayFn)(const char*) = nullptr;
    static int (*XDefaultScreenFn)(Display*) = nullptr;
    if (!libx11) {
        libx11 = dlopen("libX11.so.6", RTLD_LAZY);
        if (!libx11) {
            MITHRIL_LOG_ERROR("vk", "dlopen libX11.so.6 failed: %s", dlerror());
            return nullptr;
        }
        XOpenDisplayFn = (Display* (*)(const char*))dlsym(libx11, "XOpenDisplay");
        XDefaultScreenFn = (int (*)(Display*))dlsym(libx11, "DefaultScreen");
        if (!XOpenDisplayFn || !XDefaultScreenFn) {
            MITHRIL_LOG_ERROR("vk", "dlsym XOpenDisplay/DefaultScreen failed");
            return nullptr;
        }
    }

    Display* display = XOpenDisplayFn(nullptr);
    if (!display) {
        MITHRIL_LOG_ERROR("vk", "XOpenDisplay failed");
        return nullptr;
    }
    // XDefaultScreen is resolved for symmetry / future use; surface creation
    // itself does not need the screen number.
    (void)XDefaultScreenFn;

    if (!b->createXlibSurfaceKHR) {
        MITHRIL_LOG_ERROR("vk", "vkCreateXlibSurfaceKHR not resolved on Backend");
        return nullptr;
    }
    auto createXlibSurfaceKHR = (PFN_vkCreateXlibSurfaceKHR)b->createXlibSurfaceKHR;

    VkXlibSurfaceCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    sci.dpy = display;
    // native_window arrives as void* from the host (the X11 Window handle,
    // which is a typedef for unsigned long). The cast is well-defined on
    // every X11 ABI we target.
    sci.window = (Window)native_window;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (createXlibSurfaceKHR(b->instance, &sci, nullptr, &surface) != VK_SUCCESS) {
        MITHRIL_LOG_ERROR("vk", "vkCreateXlibSurfaceKHR failed");
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
