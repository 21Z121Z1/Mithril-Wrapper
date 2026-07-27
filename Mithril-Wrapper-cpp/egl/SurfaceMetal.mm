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
    if ([layer isKindOfClass:[CAMetalLayer class]]) {
        mtlLayer = (CAMetalLayer*)layer;
    } else {
        // Coerce: replace the layer's class with CAMetalLayer. This mirrors
        // what UIKit views do in +layerClass. We only do this if the layer is
        // standalone (not yet attached as a sublayer) to avoid surprising the
        // host view hierarchy.
        object_setClass(layer, [CAMetalLayer class]);
        mtlLayer = (CAMetalLayer*)layer;
    }
    if (!mtlLayer) {
        MITHRIL_LOG_WARN("egl", "SurfaceMetal: CAMetalLayer coercion failed");
        return nullptr;
    }
    // MoltenVK picks the MTLDevice itself via vkCreateMetalSurfaceEXT; we do
    // NOT bind the layer to a specific MTLDevice here (MoltenVK will choose
    // the system default device, which matches the VkPhysicalDevice it
    // exposes). The pixel format must be BGRA8Unorm to match the swapchain's
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
