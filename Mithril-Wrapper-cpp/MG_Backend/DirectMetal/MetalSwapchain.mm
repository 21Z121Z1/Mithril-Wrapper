// Mithril-Wrapper - MG_Backend/DirectMetal/MetalSwapchain.mm
// Per-EGLSurface Metal swapchain: CAMetalLayer + nextDrawable management +
// the persistent depth/stencil MTLTexture (behaviour reference:
// DirectVulkan/SwapchainCommon.cpp + SwapchainMetal.mm).
//
// WHY THIS SHAPE: Metal has no VkSwapchainKHR equivalent — no image array,
// no acquire semaphores, no OUT_OF_DATE rebuilds. [CAMetalLayer nextDrawable]
// hands out one CAMetalDrawable per frame whose texture IS the color
// attachment, and the layer's drawableSize change takes effect on the very
// next drawable without any recreation. The only Vulkan-side duties that
// survive translation are therefore:
//   * pacing the producer (nextDrawable blocks while the compositor holds
//     every drawable — the exact role of vkAcquireNextImageKHR on FIFO),
//   * keeping a stable color "view" cookie for the frontend (the wrapper is
//     reused every frame; only its MTLTexture pointer is swapped),
//   * hand-allocating the depth texture (Vulkan rebuilds it with the
//     swapchain; Metal must do it explicitly on resize).
//
// LAYER PROPERTY OWNERSHIP: EGL/SurfaceMetal.mm configures the layer
// (drawableSize / framebufferOnly / maximumDrawableCount /
// presentsWithTransaction). This file deliberately touches ONLY pixelFormat —
// setting any of the others here would race EGL's resize path (two writers,
// different values, undefined winner) and reintroduce the
// maximumDrawableCount-vs-in-flight mismatch class of bugs documented in
// SwapchainMetal.mm.
//
// LIFETIME: the swapchain struct holds the ONLY strong refs the backend owns
// (layer / drawable / two MTLTextures via wrappers). destroy_swapchain waits
// the in-flight command buffers first — mirroring shutdown_device() — then
// deletes the struct and lets ARC drop the refs. Objects still referenced by
// an executing command buffer are retained by Metal itself until completion
// (see MetalResources.mm LIFETIME NOTE), so no deferred-disposal queue is
// needed, unlike the Vulkan path's disposalQueue.
#ifdef __APPLE__

#include "MetalSwapchain.h"
#include "../../MG_Impl/Log.h"

#include <TargetConditionals.h>

namespace mithril {
namespace dmt {

// ---- Create / destroy ------------------------------------------------------

MetalSwapchain* create_swapchain(void* native_window, int width, int height,
                                 int want_depth_stencil, int platform_hint) {
    Backend* b = backend();
    if (!b || !b->initialized || native_window == nullptr ||
        width <= 0 || height <= 0) {
        MITHRIL_LOG_ERROR("mtl", "create_swapchain: bad args "
                          "(init=%d layer=%p %dx%d)",
                          b ? (int)b->initialized : 0, native_window,
                          width, height);
        return nullptr;
    }
    // platform_hint is only a hint; this TU is the sole Apple surface path,
    // so any value routes here identically (same stance as SwapchainMetal.mm).
    (void)platform_hint;

    // The EGL layer hands us the CAMetalLayer it created for the host view.
    // __bridge borrow: the view owns the layer; our struct takes its own
    // strong reference below, keeping the layer alive independent of the view.
    CAMetalLayer* layer = (__bridge CAMetalLayer*)native_window;
    if (layer == nil) {
        MITHRIL_LOG_ERROR("mtl", "create_swapchain: native_window is not a "
                          "CAMetalLayer");
        return nullptr;
    }

    // Pin the pixel format to BGRA8Unorm so the drawable textures match
    // colorVkFormat = VK_FORMAT_B8G8R8A8_UNORM the frontend is told about.
    // This is also CAMetalLayer's default, but stating it keeps the contract
    // explicit instead of incidental. Everything else about the layer stays
    // under EGL/SurfaceMetal.mm's control (see file-header note).
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;

    MetalSwapchain* sc = new MetalSwapchain();
    sc->layer = layer;              // ARC strong assign — retains the layer
    sc->width = width;
    sc->height = height;
    sc->colorVkFormat = VK_FORMAT_B8G8R8A8_UNORM;
    sc->depthVkFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;

    // Depth/stencil is allocated up-front when requested; on failure the
    // realloc helper leaves depthTex->tex == nil and passes become color-only
    // (same recovery as the Vulkan path's depthView == VK_NULL_HANDLE).
    if (want_depth_stencil)
        swapchain_realloc_depth(sc, width, height);

    MITHRIL_LOG_INFO("mtl", "Metal swapchain created: %dx%d, depth=%d",
                     width, height, (int)(want_depth_stencil != 0));
    return sc;
}

void destroy_swapchain(MetalSwapchain* sc) {
    if (!sc) return;
    Backend* b = backend();
    if (b) {
        // Wait every in-flight command buffer before dropping the ARC refs:
        // the drawable's texture and the depth texture may be attached to a
        // committed (slotCmd[]) or still-recording (cmd) buffer. Mirrors
        // shutdown_device()'s drain exactly. EGL only destroys surfaces
        // outside the frame loop, so the current cmd is quiescent here.
        for (int i = 0; i < MITHRIL_DMT_MAX_FRAMES_IN_FLIGHT; ++i) {
            @autoreleasepool {
                if (b->slotCmd[i] != nil) [b->slotCmd[i] waitUntilCompleted];
            }
        }
        if (b->cmd != nil) {
            [b->cmd waitUntilCompleted];
            // A waited-out command buffer must never be appended to again
            // (Metal asserts on encoding into a completed buffer); clearing
            // the slot makes ensure_command_buffer() build a fresh one.
            b->cmd = nil;
        }
    }
    // delete (not ARC) for the wrapper structs — they are plain C++ news —
    // and their id<> members release the MTLTexture refs under ARC. The
    // layer / drawable strong refs drop with the struct.
    delete sc->colorTex;
    delete sc->depthTex;
    delete sc;
}

// ---- Depth/stencil ---------------------------------------------------------

void swapchain_realloc_depth(MetalSwapchain* sc, int w, int h) {
    if (!sc || w <= 0 || h <= 0) return;
    Backend* b = backend();
    if (!b || !b->initialized) return;

    MTLTextureDescriptor* desc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8
                                                           width:(NSUInteger)w
                                                          height:(NSUInteger)h
                                                       mipmapped:NO];
    // RenderTarget for the pass attachment; ShaderRead so depth readback /
    // debug sampling never forces a format-compatible realloc later.
    desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    // Unified memory: Shared is GPU-and-CPU-coherent with zero maintenance.
    // Discrete: Managed gives the GPU a private fast copy; GPU->CPU sync
    // happens automatically at command-buffer completion, which is all the
    // depth path ever needs (it is written by GPU, read only for debug).
#if TARGET_OS_OSX
    desc.storageMode = b->unifiedMemory ? MTLStorageModeShared : MTLStorageModeManaged;
#else
    desc.storageMode = MTLStorageModeShared;
#endif

    id<MTLTexture> tex = [b->device newTextureWithDescriptor:desc];
    if (tex == nil) {
        // Why null the old texture instead of keeping it: Metal validates
        // that the depth attachment extent covers the color attachment's; a
        // stale-sized depth texture aborts encoder creation. nil makes
        // begin_render_pass treat the pass as color-only — degraded but
        // alive, exactly like the Vulkan path's failed-depth recovery.
        MITHRIL_LOG_WARN("mtl", "swapchain depth alloc failed (%dx%d) — "
                          "rendering continues without depth", w, h);
        if (sc->depthTex) sc->depthTex->tex = nil;
        return;
    }

    // One wrapper lives for the swapchain's whole lifetime so the frontend's
    // cached depth cookie stays valid across resizes; only the MTLTexture
    // pointer (and extent) rotate. The strong assign releases the previous
    // texture immediately — if an in-flight buffer still references it,
    // Metal retains it until that buffer completes (no use-after-free).
    if (!sc->depthTex) sc->depthTex = new MetalTexture();
    sc->depthTex->tex = tex;
    sc->depthTex->width = w;
    sc->depthTex->height = h;
    sc->depthTex->depth = 1;
    sc->depthTex->levels = 1;
    sc->depthTex->samples = 1;
    sc->depthTex->vkFormat = sc->depthVkFormat;
    sc->depthTex->glTarget = GL_TEXTURE_2D;
}

// ---- Acquire ---------------------------------------------------------------

MetalTexture* swapchain_acquire_color(MetalSwapchain* sc) {
    if (!sc) return nullptr;
    // Dead swapchain (nil drawable last frame, EGL-flagged rebuild, device
    // lost): refuse to acquire so the render thread cannot spin — same gate
    // as the Vulkan path's needsRebuild / deviceLost checks.
    if (sc->needsRebuild) return nullptr;
    if (backend()->deviceLost.load(std::memory_order_acquire)) return nullptr;

    // Idempotent within a frame: EGL can re-enter acquire (eglMakeCurrent
    // after eglSwapBuffers in the same frame). One drawable per frame —
    // hand back the cached wrapper unchanged.
    if (sc->drawable != nil) return sc->colorTex;

    // nextDrawable BLOCKS while the compositor holds every drawable (up to
    // ~1s at the layer's maximumDrawableCount). That throttle is not a bug
    // to work around — it IS the present model, pacing the producer to the
    // display exactly like FIFO vkAcquireNextImageKHR. autoreleasepool so
    // the transient objects nextDrawable creates do not pile up per frame.
    id<CAMetalDrawable> d = nil;
    @autoreleasepool {
        d = [sc->layer nextDrawable];
    }
    if (d == nil) {
        // No drawable available: layer size is zero / offscreen / compositor
        // stall beyond the ~1s wait. Vulkan maps these to fatal acquire
        // errors; mirror that by flagging a rebuild for EGL to act on.
        sc->needsRebuild = true;
        MITHRIL_LOG_WARN("mtl", "nextDrawable returned nil — swapchain "
                          "marked for rebuild");
        return nullptr;
    }
    sc->drawable = d;

    id<MTLTexture> mt = d.texture;
    // Persistent wrapper: the frontend caches the cookie this returns as
    // eglDefaultColor, so the POINTER must remain stable across frames —
    // only its contents (tex / extent) are refreshed every acquire.
    if (!sc->colorTex) sc->colorTex = new MetalTexture();
    sc->colorTex->tex = mt;
    sc->colorTex->width = (int)mt.width;
    sc->colorTex->height = (int)mt.height;
    sc->colorTex->depth = 1;
    sc->colorTex->levels = 1;
    sc->colorTex->samples = 1;
    sc->colorTex->vkFormat = sc->colorVkFormat;
    sc->colorTex->glTarget = GL_TEXTURE_2D;

    // Record what the layer actually handed us: drawableSize may have
    // changed since our last acquire (resize between frames is applied by
    // the layer without telling anyone).
    sc->actualDrawableWidth = (int)mt.width;
    sc->actualDrawableHeight = (int)mt.height;
    sc->frameAcquired = true;
    return sc->colorTex;
}

MetalTexture* swapchain_acquire_depth(MetalSwapchain* sc) {
    if (!sc) return nullptr;
    // Contract: nullptr = no depth attachment for this swapchain. That is
    // either "never requested" (no wrapper) or "allocation failed / pending
    // resize" (wrapper with nil tex) — both render color-only.
    if (!sc->depthTex || sc->depthTex->tex == nil) return nullptr;
    return sc->depthTex;
}

// ---- Present / resize ------------------------------------------------------

void swapchain_present(MetalSwapchain* sc) {
    if (!sc) return;
    // commit_frame has ALREADY encoded [cmd presentDrawable:sc->drawable]
    // (the header contract: called by dmt_present_and_acquire after commit).
    // Presenting a drawable also releases it back to the layer's pool, so
    // dropping our ref here cannot strand a drawable.
    sc->frameAcquired = false;
    sc->drawable = nil;                 // ARC release; next acquire re-fetches
    if (sc->colorTex) sc->colorTex->tex = nil; // wrapper survives, texture does not
}

void swapchain_set_drawable_size(MetalSwapchain* sc, int w, int h) {
    if (!sc || w <= 0 || h <= 0) return;
    sc->actualDrawableWidth = w;
    sc->actualDrawableHeight = h;

    // Why no swapchain rebuild: Metal has no OUT_OF_DATE. The layer hands
    // out the next drawable at the new drawableSize automatically, so the
    // color side is already done. Only the depth texture needs a manual
    // re-allocation — including the failed-alloc case (tex == nil), so a
    // later resize retries the depth attachment. Swapchains created without
    // depth stay depth-less (wrapper never existed).
    if (sc->depthTex &&
        (sc->depthTex->width != w || sc->depthTex->height != h)) {
        swapchain_realloc_depth(sc, w, h);
    }
}

void swapchain_mark_rebuild(MetalSwapchain* sc) {
    if (sc) sc->needsRebuild = true;
}

bool swapchain_needs_rebuild(MetalSwapchain* sc) {
    return sc && sc->needsRebuild;
}

} // namespace dmt
} // namespace mithril

#endif // __APPLE__
