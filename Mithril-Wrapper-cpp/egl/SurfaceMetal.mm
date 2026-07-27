// Mithril-Wrapper - egl/SurfaceMetal.mm
// Apple-only platform dispatch for EGL surface creation.
//
// Implements the surface_create() / surface_get_size() entry points declared
// in egl/egl.cpp. Compiled into the Apple build (CMake APPLE guard); the
// native_window passed in is a host CALayer* (typically a UIView's backing
// layer). We coerce it to CAMetalLayer, pin the pixel format to
// MTLPixelFormatBGRA8Unorm (matching the swapchain's VK_FORMAT_B8G8R8A8_UNORM),
// and hand the CAMetalLayer* back to egl.cpp as a void* so the Vulkan
// backend can wrap it via VK_EXT_metal_surface (MoltenVK).
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#import <Foundation/Foundation.h>
#import <objc/runtime.h>   // object_setClass() for layer coercion

#include "../MG_Impl/Log.h"

// ---------------------------------------------------------------------------
// surface_create: coerce CALayer -> CAMetalLayer and pin format/attrs.
// Returns (__bridge void*)mtlLayer on success, nullptr on failure.
// out_w/out_h receive the layer's current drawableSize (0 if undetermined).
// ---------------------------------------------------------------------------
extern "C" void* surface_create(void* native_window, int* out_w, int* out_h) {
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    if (!native_window) return nullptr;

    CALayer* layer = (__bridge CALayer*)native_window;
    CAMetalLayer* mtlLayer = nil;
    bool coerced = false;
    if ([layer isKindOfClass:[CAMetalLayer class]]) {
        mtlLayer = (CAMetalLayer*)layer;
        MITHRIL_LOG_INFO("egl", "SurfaceMetal: layer is already CAMetalLayer");
    } else {
        // Coerce: replace the layer's class with CAMetalLayer.
        // NOTE: object_setClass on a CALayer to make it a CAMetalLayer is
        // fundamentally unsafe — CAMetalLayer may add ivars (e.g. device
        // storage) that a plain CALayer doesn't have, causing memory
        // corruption when those ivars are accessed. This path should ideally
        // never be taken; the host app MUST provide a CAMetalLayer.
        MITHRIL_LOG_WARN("egl", "SurfaceMetal: coercing CALayer -> CAMetalLayer "
                          "(unsafe, may corrupt memory!)");
        object_setClass(layer, [CAMetalLayer class]);
        mtlLayer = (CAMetalLayer*)layer;
        coerced = true;
    }
    if (!mtlLayer) {
        MITHRIL_LOG_WARN("egl", "SurfaceMetal: CAMetalLayer coercion failed");
        return nullptr;
    }
    // The layer MUST have its device property set to a valid MTLDevice before
    // MoltenVK creates a VkSurfaceKHR from it (vkCreateMetalSurfaceEXT).
    // The Metal spec (and vkCreateMetalSurfaceEXT documentation) require this.
    // On iOS there is exactly one GPU, so MTLCreateSystemDefaultDevice returns
    // the same MTLDevice that MoltenVK selects internally for its VkPhysicalDevice.
    // Without this, the layer's drawable textures have no IOSurface backing,
    // causing IOSurfaceClientBindAccel to crash in the GPU driver when MoltenVK
    // tries to bind the drawable's texture during command encoding.
    mtlLayer.device = MTLCreateSystemDefaultDevice();
    // The pixel format must be BGRA8Unorm to match the swapchain's
    // VK_FORMAT_B8G8R8A8_UNORM.
    //
    // framebufferOnly MUST be NO when using MoltenVK (Vulkan-on-Metal), because
    // the swapchain images need VK_IMAGE_USAGE_TRANSFER_DST_BIT and potentially
    // other non-color-attachment usage (MSAA resolve, display transform).
    // Setting it to YES restricts the Metal texture to render-target-only,
    // causing IOSurfaceBindAccel to crash in the GPU driver when MoltenVK
    // tries to use the image for non-attachment operations.
    mtlLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    mtlLayer.framebufferOnly = NO;
    // presentsWithTransaction MUST be NO. When YES, CAMetalLayer blocks on
    // -[CAMetalLayer present:] until the compositor commits the drawable,
    // which serializes the render thread against the UI thread and — under
    // the high frame rates seen in the field (260 FPS) — interacts badly
    // with IOSurface lifetime: the drawable's IOSurface can be recycled by
    // the compositor while the next vkQueueSubmit is still encoding against
    // it, producing the IOSurfaceBindAccel SIGSEGV (UAF) reported on
    // iPadOS 16.1.1. NO (the default) uses the asynchronous path that
    // MoltenVK and Apple's own GLKView expect.
    mtlLayer.presentsWithTransaction = NO;
    // opaque = YES skips alpha compositing in the iOS compositor. Besides
    // being a small perf win, it prevents the compositor from holding an
    // extra reference to the drawable's IOSurface across the frame boundary,
    // which on some iPadOS 16.x builds delays IOSurface recycling long
    // enough to race with the next present.
    mtlLayer.opaque = YES;
    if (mtlLayer.drawableSize.width == 0 || mtlLayer.drawableSize.height == 0) {
        mtlLayer.drawableSize = layer.bounds.size;
    }

    if (out_w) *out_w = (int)mtlLayer.drawableSize.width;
    if (out_h) *out_h = (int)mtlLayer.drawableSize.height;
    return (__bridge void*)mtlLayer;
}

// ---------------------------------------------------------------------------
// surface_get_size: read the CAMetalLayer's current drawableSize.
// Returns false if native_window is null or not a CAMetalLayer.
// ---------------------------------------------------------------------------
extern "C" bool surface_get_size(void* native_window, int* out_w, int* out_h) {
    if (!native_window) return false;
    CALayer* layer = (__bridge CALayer*)native_window;
    if (![layer isKindOfClass:[CAMetalLayer class]]) return false;
    CAMetalLayer* mtlLayer = (CAMetalLayer*)layer;
    CGSize sz = mtlLayer.drawableSize;
    if (out_w) *out_w = (int)sz.width;
    if (out_h) *out_h = (int)sz.height;
    return true;
}
