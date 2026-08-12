// Apple presentation seam for DirectMetal.
//
// The GL default framebuffer intentionally remains RGBA8Unorm. CAMetalLayer
// does not accept RGBA8Unorm as a drawable pixel format, so presentation must
// not be a raw texture blit. This translation unit reuses the existing Metal
// engine implementation while replacing only SetNativeWindow/Present with a
// BGRA8 CAMetalLayer + render-pass presentation path.
//
// Keep this wrapper narrow: the renderer/state/resource implementation remains
// in engine.mm, and desktop-GL observable framebuffer semantics stay unchanged.

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "engine.h"

// Compile the existing implementation in this translation unit, but retain its
// old presentation functions under private integration names. engine.h has
// already been included above, so its public declarations are not macro-renamed.
#define SetNativeWindow MithrilLegacySetNativeWindow
#define Present MithrilLegacyPresent
#include "engine.mm"
#undef Present
#undef SetNativeWindow

namespace mithril::metal {
namespace {

id<MTLRenderPipelineState> g_present_pipeline = nil;
id<MTLLibrary> g_present_library = nil;

#if defined(MITHRIL_PRESENTATION_TEST_HOOKS)
struct PresentationPixelCapture {
    bool armed = false;
    NSUInteger x = 0;
    NSUInteger y = 0;
    id<MTLTexture> reference = nil;
    id<MTLBuffer> readback = nil;
    id<MTLCommandBuffer> command = nil;
};

PresentationPixelCapture g_presentation_capture;

bool EncodePresentationPixelCapture(id<CAMetalDrawable> drawable,
                                    id<MTLCommandBuffer> command) {
    if (!g_presentation_capture.armed) return true;
    g_presentation_capture.armed = false;

    id<MTLTexture> texture = drawable.texture;
    id<MTLTexture> reference = g_presentation_capture.reference;
    if (!texture || texture.framebufferOnly || !reference ||
        g_presentation_capture.x >= texture.width ||
        g_presentation_capture.y >= texture.height ||
        g_presentation_capture.x >= reference.width ||
        g_presentation_capture.y >= reference.height) {
        g_presentation_capture.reference = nil;
        g_presentation_capture.readback = nil;
        g_presentation_capture.command = nil;
        return false;
    }

    auto& engine = GetEngine();
    id<MTLBuffer> readback = [engine.device
        newBufferWithLength:512 options:MTLResourceStorageModeShared];
    id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
    if (!readback || !blit) {
        g_presentation_capture.readback = nil;
        g_presentation_capture.command = nil;
        return false;
    }
    [blit copyFromTexture:texture
              sourceSlice:0
              sourceLevel:0
             sourceOrigin:MTLOriginMake(g_presentation_capture.x,
                                        g_presentation_capture.y, 0)
               sourceSize:MTLSizeMake(1, 1, 1)
                 toBuffer:readback
        destinationOffset:0
   destinationBytesPerRow:256
 destinationBytesPerImage:256];
    [blit copyFromTexture:reference
              sourceSlice:0
              sourceLevel:0
             sourceOrigin:MTLOriginMake(g_presentation_capture.x,
                                        g_presentation_capture.y, 0)
               sourceSize:MTLSizeMake(1, 1, 1)
                 toBuffer:readback
        destinationOffset:256
   destinationBytesPerRow:256
 destinationBytesPerImage:256];
    [blit endEncoding];
    g_presentation_capture.readback = readback;
    g_presentation_capture.command = command;
    return true;
}
#endif

bool EncodePresentationRender(id<MTLCommandBuffer> command,
                              id<MTLTexture> target,
                              NSString* label) {
    if (!command || !target) return false;
    auto& engine = GetEngine();
    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = target;
    pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;

    id<MTLRenderCommandEncoder> encoder =
        [command renderCommandEncoderWithDescriptor:pass];
    if (!encoder) return false;
    encoder.label = label;
    [encoder setRenderPipelineState:g_present_pipeline];
    // Define the presentation raster bounds explicitly. Apple Paravirtual can
    // otherwise accept and complete this encoder while producing no fragments.
    [encoder setViewport:MTLViewport{0.0, 0.0,
                                     static_cast<double>(target.width),
                                     static_cast<double>(target.height),
                                     0.0, 1.0}];
    [encoder setScissorRect:MTLScissorRect{0, 0, target.width, target.height}];
    [encoder setFragmentTexture:engine.color atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:0
                vertexCount:3];
    [encoder endEncoding];
    return true;
}

#if defined(MITHRIL_PRESENTATION_TEST_HOOKS)
bool EncodePresentationReference(id<MTLCommandBuffer> command,
                                 id<MTLTexture> drawable_texture) {
    if (!g_presentation_capture.armed) return true;
    MTLTextureDescriptor* descriptor = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                     width:drawable_texture.width
                                    height:drawable_texture.height
                                 mipmapped:NO];
    descriptor.storageMode = MTLStorageModePrivate;
    descriptor.usage = MTLTextureUsageRenderTarget;
    g_presentation_capture.reference =
        [GetEngine().device newTextureWithDescriptor:descriptor];
    if (!g_presentation_capture.reference) return false;
    g_presentation_capture.reference.label =
        @"Mithril presentation test reference";
    return EncodePresentationRender(
        command, g_presentation_capture.reference,
        @"Mithril RGBA-to-BGRA presentation reference");
}
#endif

bool EnsurePresentPipeline() {
    if (g_present_pipeline) return true;

    auto& engine = GetEngine();
    static NSString* const source =
        @"#include <metal_stdlib>\n"
        @"using namespace metal;\n"
        @"\n"
        @"vertex float4 mithril_present_vertex(uint vertex_id [[vertex_id]]) {\n"
        @"    const float2 positions[3] = {\n"
        @"        float2(-1.0, -1.0),\n"
        @"        float2( 3.0, -1.0),\n"
        @"        float2(-1.0,  3.0)\n"
        @"    };\n"
        @"    return float4(positions[vertex_id], 0.0, 1.0);\n"
        @"}\n"
        @"\n"
        @"fragment float4 mithril_present_fragment(\n"
        @"    float4 position [[position]],\n"
        @"    texture2d<float, access::read> source [[texture(0)]]) {\n"
        @"    uint2 pixel = uint2(position.xy);\n"
        @"    pixel.x = min(pixel.x, source.get_width() - 1);\n"
        @"    pixel.y = min(pixel.y, source.get_height() - 1);\n"
        @"    return source.read(pixel);\n"
        @"}\n";

    NSError* library_error = nil;
    g_present_library = [engine.device newLibraryWithSource:source
                                                    options:nil
                                                      error:&library_error];
    if (!g_present_library) {
        ML_LOG_ERROR("metal: failed to compile drawable presentation shader: %s",
                     library_error.localizedDescription.UTF8String ?: "unknown error");
        return false;
    }

    id<MTLFunction> vertex =
        [g_present_library newFunctionWithName:@"mithril_present_vertex"];
    id<MTLFunction> fragment =
        [g_present_library newFunctionWithName:@"mithril_present_fragment"];
    if (!vertex || !fragment) {
        ML_LOG_ERROR("metal: drawable presentation shader entry point missing");
        g_present_library = nil;
        return false;
    }

    MTLRenderPipelineDescriptor* descriptor =
        [[MTLRenderPipelineDescriptor alloc] init];
    descriptor.label = @"Mithril drawable presentation pipeline";
    descriptor.vertexFunction = vertex;
    descriptor.fragmentFunction = fragment;
    descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

    NSError* pipeline_error = nil;
    g_present_pipeline =
        [engine.device newRenderPipelineStateWithDescriptor:descriptor
                                                     error:&pipeline_error];
    if (!g_present_pipeline) {
        ML_LOG_ERROR("metal: failed to create drawable presentation pipeline: %s",
                     pipeline_error.localizedDescription.UTF8String ?: "unknown error");
        g_present_library = nil;
        return false;
    }
    return true;
}

struct PresentationSubmission {
    id<CAMetalDrawable> drawable = nil;
};

bool EncodePresentationTail(id<MTLCommandBuffer> command, void* context) {
    auto* submission = static_cast<PresentationSubmission*>(context);
    if (!submission || !submission->drawable) return false;
    command.label = @"Mithril DirectMetal frame + present";

#if defined(MITHRIL_PRESENTATION_TEST_HOOKS)
    if (!EncodePresentationReference(command, submission->drawable.texture)) {
        ML_LOG_ERROR("metal: failed to encode presentation test reference");
        return false;
    }
#endif
    if (!EncodePresentationRender(command, submission->drawable.texture,
                                  @"Mithril RGBA-to-BGRA presentation")) {
        ML_LOG_ERROR("metal: failed to create presentation render encoder");
        return false;
    }

#if defined(MITHRIL_PRESENTATION_TEST_HOOKS)
    if (!EncodePresentationPixelCapture(submission->drawable, command)) {
        ML_LOG_ERROR("metal: failed to capture presentation test pixel");
        return false;
    }
#endif

    [command presentDrawable:submission->drawable];
    return true;
}

} // namespace

bool SetNativeWindow(void* native_window) {
    if (!native_window) {
        GetEngine().layer = nil;
        return true;
    }
    if ((reinterpret_cast<uintptr_t>(native_window) & (alignof(void*) - 1)) != 0)
        return false;
    if (!EnsureInit()) return false;

    id object = (__bridge id)native_window;
    if (![object isKindOfClass:[CAMetalLayer class]]) {
        ML_LOG_ERROR("metal: EGL native window is not a CAMetalLayer");
        return false;
    }

    auto& engine = GetEngine();
    CAMetalLayer* layer = (CAMetalLayer*)object;
    layer.device = engine.device;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    // The drawable is now a render target rather than a blit destination.
    layer.framebufferOnly = YES;
    if (layer.drawableSize.width <= 0 || layer.drawableSize.height <= 0)
        layer.drawableSize = CGSizeMake(engine.width, engine.height);

    engine.layer = layer;
    if (!SyncLayerTargetSize() || !EnsurePresentPipeline()) {
        engine.layer = nil;
        return false;
    }

    ML_LOG_INFO("metal: CAMetalLayer presentation configured as BGRA8Unorm");
    return true;
}

bool Present() {
    auto& engine = GetEngine();
    if (!engine.initialized || !engine.layer) {
        ML_LOG_ERROR("metal: eglSwapBuffers has no CAMetalLayer surface");
        return false;
    }

    @autoreleasepool {
        // Draws are recorded until submit, so resizing here re-targets the
        // complete pending GL frame without rendering an intermediate size.
        if (!SyncLayerTargetSize()) return false;
        if (!EnsurePresentPipeline()) return false;

        id<CAMetalDrawable> drawable = [engine.layer nextDrawable];
        if (!drawable) {
            ML_LOG_ERROR("metal: CAMetalLayer returned no drawable");
            return false;
        }
        if (drawable.texture.width != engine.width ||
            drawable.texture.height != engine.height) {
            ML_LOG_ERROR("metal: drawable size changed without a surface resize");
            return false;
        }
        if (drawable.texture.pixelFormat != MTLPixelFormatBGRA8Unorm) {
            ML_LOG_ERROR("metal: unexpected CAMetalDrawable pixel format %lu",
                         (unsigned long)drawable.texture.pixelFormat);
            return false;
        }

        PresentationSubmission submission{drawable};
        return SubmitInternal(false, false, nullptr, EncodePresentationTail,
                              &submission);
    }
}

#if defined(MITHRIL_PRESENTATION_TEST_HOOKS)
extern "C" __attribute__((visibility("default")))
bool mithrilTestArmNextPresentedPixel(uint32_t x, uint32_t y) {
    if (g_presentation_capture.armed) return false;
    g_presentation_capture.x = x;
    g_presentation_capture.y = y;
    g_presentation_capture.reference = nil;
    g_presentation_capture.readback = nil;
    g_presentation_capture.command = nil;
    g_presentation_capture.armed = true;
    return true;
}

extern "C" __attribute__((visibility("default")))
bool mithrilTestReadPresentedPixels(uint8_t drawable_pixel[4],
                                    uint8_t reference_pixel[4]) {
    if (!drawable_pixel || !reference_pixel || g_presentation_capture.armed ||
        !g_presentation_capture.readback ||
        !g_presentation_capture.command)
        return false;
    [g_presentation_capture.command waitUntilCompleted];
    if (g_presentation_capture.command.status !=
        MTLCommandBufferStatusCompleted)
        return false;
    std::memcpy(drawable_pixel, g_presentation_capture.readback.contents, 4);
    std::memcpy(reference_pixel,
                static_cast<uint8_t*>(g_presentation_capture.readback.contents) +
                    256,
                4);
    g_presentation_capture.reference = nil;
    g_presentation_capture.readback = nil;
    g_presentation_capture.command = nil;
    return true;
}
#endif

} // namespace mithril::metal
