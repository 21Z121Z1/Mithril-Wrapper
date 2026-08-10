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

bool EnsurePresentPipeline() {
    if (g_present_pipeline) return true;

    auto& engine = GetEngine();
    static NSString* const source = @R"MSL(
#include <metal_stdlib>
using namespace metal;

vertex float4 mithril_present_vertex(uint vertex_id [[vertex_id]]) {
    const float2 positions[3] = {
        float2(-1.0, -1.0),
        float2( 3.0, -1.0),
        float2(-1.0,  3.0)
    };
    return float4(positions[vertex_id], 0.0, 1.0);
}

fragment float4 mithril_present_fragment(
    float4 position [[position]],
    texture2d<float, access::read> source [[texture(0)]]) {
    uint2 pixel = uint2(position.xy);
    pixel.x = min(pixel.x, source.get_width() - 1);
    pixel.y = min(pixel.y, source.get_height() - 1);
    return source.read(pixel);
}
)MSL";

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
        if (!SubmitInternal(false, false, nullptr)) return false;
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

        id<MTLCommandBuffer> command = [engine.queue commandBuffer];
        if (!command) {
            ML_LOG_ERROR("metal: failed to allocate presentation command buffer");
            return false;
        }
        command.label = @"Mithril present";

        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = drawable.texture;
        pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;

        id<MTLRenderCommandEncoder> encoder =
            [command renderCommandEncoderWithDescriptor:pass];
        if (!encoder) {
            ML_LOG_ERROR("metal: failed to create presentation render encoder");
            return false;
        }
        encoder.label = @"Mithril RGBA-to-BGRA presentation";
        [encoder setRenderPipelineState:g_present_pipeline];
        [encoder setFragmentTexture:engine.color atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                    vertexStart:0
                    vertexCount:3];
        [encoder endEncoding];

        [command presentDrawable:drawable];
        if (!CommitCommandBuffer(command)) return false;
        return true;
    }
}

} // namespace mithril::metal
