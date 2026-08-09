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

    // Actual drawable size at acquire time (from CAMetalLayer.drawableSize).
    // Updated by EGL's install_surface_on_state() after each acquire. On
    // MoltenVK/iOS, the swapchain image is backed by a CAMetalLayer drawable
    // whose size = drawableSize at acquire time, NOT the swapchain's
    // imageExtent at creation time. If the drawableSize changed after swapchain
    // creation (e.g. GLFW resized the window), these fields reflect the ACTUAL
    // IOSurface dimensions, while width/height still hold the creation-time
    // extent. begin_render_pass() clamps the render area to min(these, width/height)
    // to prevent IOSurfaceBindAccel SIGSEGV from out-of-bounds access.
    int             actualDrawableWidth = 0;
    int             actualDrawableHeight = 0;

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

    // Per-swapchain-image render-finished semaphores. vkQueueSubmit signals
    // renderFinishedPerImage[currentImage] when the frame's command buffer
    // completes; vkQueuePresentKHR waits on the SAME per-image semaphore.
    //
    // This mirrors MobileGL's FrameContext
    // (m_swapchainImageRenderFinishedSemaphores, FrameContext.h:122), which
    // indexes renderFinished by SWAPCHAIN IMAGE (not by frame slot). The
    // per-image design is essential because present must wait on the
    // semaphore that was signaled by the submit which rendered into THIS
    // specific image. A single per-swapchain (or per-frame-slot) semaphore
    // races: if image A's submit signals it and image B's submit re-signals
    // before present consumes A's signal, present of A reads stale pixels
    // (black screen) or hits a spec violation (signaling an already-signaled
    // binary semaphore). One semaphore per swapchain image guarantees the
    // submit->present dependency is always expressed on the correct edge.
    std::vector<VkSemaphore> renderFinishedPerImage;

    // Per-image flag: whether renderFinishedPerImage[i] has been signaled but
    // not yet waited on by present. vkQueueSubmit signals a binary semaphore;
    // signaling one that is already signaled is spec-violating. commit_frame()
    // sets this true when it signals renderFinishedPerImage[currentImage];
    // swapchain_present_and_acquire() clears it after vkQueuePresentKHR
    // consumes the signal.
    std::vector<bool> renderFinishedSignaledPerImage;

    // Tracks whether imageAvailable has been consumed by a vkQueueSubmit wait
    // this frame. vkAcquireNextImageKHR signals imageAvailable; the FIRST
    // vkQueueSubmit of the frame MUST wait on it (at COLOR_ATTACHMENT_OUTPUT
    // stage) so the GPU does not start writing to the swapchain image before
    // acquire completes. Without this wait, the GPU races ahead of the
    // presentation engine and renders into an image it does not yet own —
    // under MoltenVK this manifests as a black screen (the rendered contents
    // land in a stale/owned-by-presenter image and never reach the display).
    //
    // Mid-frame flushes (eglWaitClient -> backend_commit) call commit_frame
    // multiple times per frame; only the first submit waits on imageAvailable.
    // Subsequent submits must NOT wait (the semaphore was already consumed;
    // waiting again would deadlock). This mirrors MobileGL's
    // imageAvailableSemaphoreConsumed flag (FrameContext.cpp:191).
    //
    // Reset to false by swapchain_acquire_color() right after acquire.
    bool            imageAvailableConsumed = false;

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

    // Sticky error flag: set when vkAcquireNextImageKHR or vkQueuePresentKHR
    // returns a fatal error (VK_ERROR_OUT_OF_DEVICE_MEMORY,
    // VK_ERROR_SURFACE_LOST_KHR, VK_ERROR_DEVICE_LOST, etc.). Once set, the
    // swapchain is considered dead — install_surface_on_state() detaches it
    // from the encoder, and eglSwapBuffers triggers a full rebuild via
    // ensure_swapchain(). Without this flag, a dead swapchain would keep
    // returning VK_NULL_HANDLE from acquire, and the render thread would spin
    // in a no-op loop (begin_render_pass returns early, commit_frame returns
    // early on !hasCommands, present is skipped) burning CPU forever instead
    // of either recovering or surfacing the error.
    bool            needsRebuild = false;
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
