// Mithril-Wrapper - MG_Backend/DirectMetal/MetalSwapchain.h
// Per-EGLSurface Metal swapchain: CAMetalLayer + nextDrawable management +
// the persistent depth/stencil MTLTexture. EGL owns one of these per
// EGLSurface exactly like the Vulkan path; the backend never creates
// swapchains itself.
//
// COMPARED TO VULKAN: Metal has no swapchain object, no acquire semaphores
// and no image arrays — [CAMetalLayer nextDrawable] hands out one
// CAMetalDrawable per frame (backed by an IOSurface the compositor presents).
// The struct therefore collapses to: the layer, the current drawable's
// texture wrapper, and the depth texture.
//
// DRAWABLE MODEL: acquire_color() must be called exactly once per frame,
// BEFORE any rendering (it caches nextDrawable). commit_frame() presents the
// cached drawable, after which acquire must run again. EGL drives this with
// backend_present_and_acquire() at eglSwapBuffers time.
#ifndef MITHRIL_DIRECTMETAL_SWAPCHAIN_H
#define MITHRIL_DIRECTMETAL_SWAPCHAIN_H

#ifdef __APPLE__

#import <QuartzCore/CAMetalLayer.h>

#include "MetalDevice.h"

namespace mithril {
namespace dmt {

struct MetalSwapchain {
    CAMetalLayer*   layer = nil;        // unretained? NO — retained (strong)
    int             width = 0;          // creation-time extent
    int             height = 0;
    int             actualDrawableWidth = 0;   // layer.drawableSize at acquire
    int             actualDrawableHeight = 0;

    // Current-frame drawable + its color texture wrapper (the VkImageView
    // cookie handed to the frontend as eglDefaultColor).
    id<CAMetalDrawable> drawable = nil;
    MetalTexture*   colorTex = nullptr; // wrapper around drawable.texture

    // Persistent depth/stencil texture (Depth32Float_Stencil8) + wrapper.
    MetalTexture*   depthTex = nullptr;
    VkFormat        depthVkFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;

    VkFormat        colorVkFormat = VK_FORMAT_B8G8R8A8_UNORM;

    // Set when nextDrawable returns nil (compositor stall > 1s) or the layer
    // is destroyed; EGL rebuilds the swapchain when it observes this.
    bool            needsRebuild = false;

    // True between acquire and present — commit_frame() uses this to decide
    // whether [cmd presentDrawable:] is legal.
    bool            frameAcquired = false;
};

// Create for a CAMetalLayer (native_window is a bridged CAMetalLayer*).
// want_depth_stencil allocates the persistent depth texture.
MetalSwapchain* create_swapchain(void* native_window, int width, int height,
                                 int want_depth_stencil, int platform_hint);
void            destroy_swapchain(MetalSwapchain* sc);

// One drawable per frame. Returns the color texture wrapper (or nullptr).
MetalTexture* swapchain_acquire_color(MetalSwapchain* sc);
MetalTexture* swapchain_acquire_depth(MetalSwapchain* sc);

// Present the current drawable + clear frameAcquired. Called by
// dmt_present_and_acquire AFTER commit_frame has encoded [presentDrawable:].
void swapchain_present(MetalSwapchain* sc);

// EGL-driven resize hooks (see Backend.h contract comments).
void swapchain_set_drawable_size(MetalSwapchain* sc, int w, int h);
void swapchain_mark_rebuild(MetalSwapchain* sc);
bool swapchain_needs_rebuild(MetalSwapchain* sc);

// Re-allocate the depth texture after a size change (called from
// swapchain_set_drawable_size when the extent actually changed).
void swapchain_realloc_depth(MetalSwapchain* sc, int w, int h);

} // namespace dmt
} // namespace mithril

#endif // __APPLE__
#endif // MITHRIL_DIRECTMETAL_SWAPCHAIN_H
