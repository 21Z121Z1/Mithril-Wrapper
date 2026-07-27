// Mithril-Wrapper - MG_Backend/DirectVulkan/Swapchain.h
// Per-EGLSurface Vulkan swapchain: VkSurfaceKHR (via VK_EXT_metal_surface) +
// VkSwapchainKHR + swapchain images + depth VkImage/View. EGL owns one of
// these per EGLSurface; the backend never creates swapchains itself.
//
// The CAMetalLayer is passed in as an opaque void* (bridged from Objective-C)
// so this translation unit stays pure C++ — only egl/egl.mm touches Metal.
#ifndef MITHRIL_DIRECTVULKAN_SWAPCHAIN_H
#define MITHRIL_DIRECTVULKAN_SWAPCHAIN_H

#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

namespace mithril {
namespace vk {

// Per-swapchain state. Allocated by create_swapchain(), freed by
// destroy_swapchain(). The EGL layer holds the returned pointer as the
// EGLSurface's native handle.
struct Swapchain {
    VkSurfaceKHR    surface = VK_NULL_HANDLE;
    VkSwapchainKHR  swapchain = VK_NULL_HANDLE;
    VkFormat        format = VK_FORMAT_UNDEFINED;
    int             width = 0;
    int             height = 0;

    // Swapchain images + per-image views.
    std::vector<VkImage>     images;
    std::vector<VkImageView> views;

    // Depth/stencil image (VK_FORMAT_D32_SFLOAT_S8_UINT) + view. Allocated
    // when the EGLConfig requests depth/stencil.
    VkImage        depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView    depthView = VK_NULL_HANDLE;

    // Index of the currently-acquired image. -1 when none acquired.
    int             currentImage = -1;

    // Semaphore signaled by vkAcquireNextImageKHR; the next vkQueueSubmit
    // waits on it (at COLOR_ATTACHMENT_OUTPUT stage) before the recorded
    // layout-transition barrier + draw commands execute.
    VkSemaphore     imageAvailable = VK_NULL_HANDLE;

    // Render-finished semaphore signaled by the submit that runs the frame's
    // command buffer. vkQueuePresentKHR waits on this (not imageAvailable) so
    // the present only proceeds once rendering is complete. Set by
    // commit_frame() each frame; read by swapchain_present_and_acquire().
    VkSemaphore     pendingRenderFinished = VK_NULL_HANDLE;

    // Tracks whether pendingRenderFinished has been signaled but not yet
    // waited on. vkQueueSubmit signals a binary semaphore; signaling one that
    // is already signaled is spec-violating and accumulates unconsumed
    // signals. commit_frame() only signals when this is false; sets it true.
    // swapchain_present_and_acquire() clears it after vkQueuePresentKHR
    // consumes the signal. Also cleared by destroy_swapchain().
    bool            renderFinishedSignaled = false;

    // Tracked layout of the currently-acquired color image. After acquire it
    // is PRESENT_SRC_KHR (or UNDEFINED on the very first acquire). The
    // acquire->attachment barrier transitions it to COLOR_ATTACHMENT_OPTIMAL;
    // the attachment->present barrier transitions it back to PRESENT_SRC_KHR.
    // Without this tracking, dynamic rendering hard-codes
    // COLOR_ATTACHMENT_OPTIMAL and MoltenVK behaviour is undefined -> black screen.
    VkImageLayout   currentColorLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    // One-shot flag: depth image transitions UNDEFINED ->
    // DEPTH_STENCIL_ATTACHMENT_OPTIMAL on first use, then stays there for the
    // swapchain's lifetime (the depth image is never presented).
    bool            depthLayoutInitialized = false;
};

// Create the surface + swapchain (+ optional depth image) for a native window.
// Returns a heap-owned Swapchain* (caller frees with destroy_swapchain()).
// On Apple this is defined in SwapchainMetal.mm (native_window is a
// CAMetalLayer*). The platform_hint parameter is taken as
// a hint and may be ignored by the implementation (the CMake-selected TU
// already determines the active platform path). 0 = auto-detect.
Swapchain* create_swapchain(void* native_window, int width, int height,
                            int want_depth_stencil, int platform_hint);

// Create the swapchain given an already-created VkSurfaceKHR. Used by
// platform-specific files (SwapchainMetal.mm) after
// they create the surface via the platform-specific Vulkan extension.
// On success, the returned Swapchain takes ownership of `surface` and will
// destroy it in destroy_swapchain(). On failure (returns nullptr), ownership
// stays with the caller, which must call vkDestroySurfaceKHR itself.
Swapchain* create_swapchain_post_surface(VkSurfaceKHR surface, int width, int height,
                                         int want_depth_stencil);

// Tear down everything created by create_swapchain().
void destroy_swapchain(Swapchain* sc);

// Acquire the next swapchain image. Returns the color VkImageView for the
// acquired image (or VK_NULL_HANDLE on failure).
VkImageView swapchain_acquire_color(Swapchain* sc);
// Returns the depth VkImageView (VK_NULL_HANDLE if none allocated).
VkImageView swapchain_acquire_depth(Swapchain* sc);

// Present the current image to the queue and acquire the next one. Called by
// backend_present_and_acquire().
void swapchain_present_and_acquire(Swapchain* sc);

} // namespace vk
} // namespace mithril

#endif // MITHRIL_DIRECTVULKAN_SWAPCHAIN_H
