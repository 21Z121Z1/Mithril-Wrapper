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

#include <cstdlib>

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

enum class PresentationTransferMode {
    Unknown,
    Direct,
    MaterializedCopy,
};

PresentationTransferMode g_present_transfer_mode =
    PresentationTransferMode::Unknown;
id<MTLTexture> g_materialized_present_source = nil;

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
        newBufferWithLength:768 options:MTLResourceStorageModeShared];
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
    [blit copyFromTexture:engine.color
              sourceSlice:0
              sourceLevel:0
             sourceOrigin:MTLOriginMake(g_presentation_capture.x,
                                        g_presentation_capture.y, 0)
               sourceSize:MTLSizeMake(1, 1, 1)
                 toBuffer:readback
        destinationOffset:512
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
                              id<MTLTexture> source,
                              NSString* label) {
    if (!command || !target || !source) return false;
    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = target;
    pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;

    id<MTLRenderCommandEncoder> encoder =
        [command renderCommandEncoderWithDescriptor:pass];
    if (!encoder) return false;
    encoder.label = label;
    [encoder setRenderPipelineState:g_present_pipeline];
    // Keep the standalone presentation pass independent of inherited/default
    // raster state, including after a non-square drawable replacement.
    [encoder setViewport:MTLViewport{0.0, 0.0,
                                     static_cast<double>(target.width),
                                     static_cast<double>(target.height),
                                     0.0, 1.0}];
    [encoder setScissorRect:MTLScissorRect{0, 0, target.width, target.height}];
    [encoder setFragmentTexture:source atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:0
                vertexCount:3];
    [encoder endEncoding];
    return true;
}

#if defined(MITHRIL_PRESENTATION_TEST_HOOKS)
bool EncodePresentationReference(id<MTLCommandBuffer> command,
                                 id<MTLTexture> drawable_texture,
                                 id<MTLTexture> source) {
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
        command, g_presentation_capture.reference, source,
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

bool PresentationProbePixelMatches(const uint8_t pixel[4]) {
    const uint8_t expected[4] = {128, 64, 32, 255};
    for (size_t i = 0; i < 4; ++i) {
        const int delta = static_cast<int>(pixel[i]) - expected[i];
        if (delta < -2 || delta > 2) return false;
    }
    return true;
}

bool RunPresentationTransferProbe(bool materialize, uint8_t pixel[4]) {
    auto& engine = GetEngine();
    const NSUInteger width = engine.width;
    const NSUInteger height = engine.height;
    if (!width || !height) return false;

    MTLTextureDescriptor* descriptor = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                     width:width
                                    height:height
                                 mipmapped:NO];
    descriptor.storageMode = MTLStorageModePrivate;
    descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    id<MTLTexture> source = [engine.device newTextureWithDescriptor:descriptor];

    descriptor.pixelFormat = MTLPixelFormatBGRA8Unorm;
    descriptor.usage = MTLTextureUsageRenderTarget;
    id<MTLTexture> target = [engine.device newTextureWithDescriptor:descriptor];

    id<MTLTexture> copied_source = nil;
    if (materialize) {
        descriptor.pixelFormat = MTLPixelFormatRGBA8Unorm;
        descriptor.usage = MTLTextureUsageShaderRead;
        copied_source = [engine.device newTextureWithDescriptor:descriptor];
    }
    id<MTLBuffer> readback = [engine.device
        newBufferWithLength:256 options:MTLResourceStorageModeShared];
    if (!source || !target || (materialize && !copied_source) || !readback)
        return false;
    source.label = @"Mithril presentation transfer probe source";
    target.label = @"Mithril presentation transfer probe target";
    if (copied_source)
        copied_source.label = @"Mithril materialized presentation probe source";

    id<MTLCommandBuffer> command = [engine.queue commandBuffer];
    if (!command) return false;
    command.label = materialize
        ? @"Mithril materialized presentation transfer probe"
        : @"Mithril direct presentation transfer probe";

    MTLRenderPassDescriptor* clear_pass =
        [MTLRenderPassDescriptor renderPassDescriptor];
    clear_pass.colorAttachments[0].texture = source;
    clear_pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    clear_pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    clear_pass.colorAttachments[0].clearColor =
        MTLClearColorMake(0.125, 0.25, 0.5, 1.0);
    id<MTLRenderCommandEncoder> clear_encoder =
        [command renderCommandEncoderWithDescriptor:clear_pass];
    if (!clear_encoder) return false;
    [clear_encoder endEncoding];

    id<MTLTexture> sampled_source = source;
    if (materialize) {
        id<MTLBlitCommandEncoder> copy = [command blitCommandEncoder];
        if (!copy) return false;
        [copy copyFromTexture:source
                 sourceSlice:0
                 sourceLevel:0
                sourceOrigin:MTLOriginMake(0, 0, 0)
                  sourceSize:MTLSizeMake(width, height, 1)
                   toTexture:copied_source
            destinationSlice:0
            destinationLevel:0
           destinationOrigin:MTLOriginMake(0, 0, 0)];
        [copy endEncoding];
        sampled_source = copied_source;
    }

    if (!EncodePresentationRender(command, target, sampled_source,
                                  @"Mithril presentation transfer probe"))
        return false;

    id<MTLBlitCommandEncoder> capture = [command blitCommandEncoder];
    if (!capture) return false;
    [capture copyFromTexture:target
                 sourceSlice:0
                 sourceLevel:0
                sourceOrigin:MTLOriginMake(width / 2, height / 2, 0)
                  sourceSize:MTLSizeMake(1, 1, 1)
                    toBuffer:readback
           destinationOffset:0
      destinationBytesPerRow:256
    destinationBytesPerImage:256];
    [capture endEncoding];

    [command commit];
    [command waitUntilCompleted];
    if (command.status != MTLCommandBufferStatusCompleted) {
        ML_LOG_ERROR("metal: presentation transfer probe failed: %s",
                     command.error.localizedDescription.UTF8String ?: "unknown error");
        return false;
    }
    std::memcpy(pixel, readback.contents, 4);
    return true;
}

bool EnsurePresentationTransferMode() {
    if (g_present_transfer_mode != PresentationTransferMode::Unknown)
        return true;

    uint8_t direct_pixel[4] = {0, 0, 0, 0};
#if defined(MITHRIL_PRESENTATION_TEST_HOOKS)
    const char* force_copy =
        std::getenv("MITHRIL_TEST_FORCE_MATERIALIZED_PRESENTATION");
    const bool force_materialized =
        force_copy && std::strcmp(force_copy, "1") == 0;
#else
    constexpr bool force_materialized = false;
#endif
    if (!force_materialized &&
        RunPresentationTransferProbe(false, direct_pixel) &&
        PresentationProbePixelMatches(direct_pixel)) {
        g_present_transfer_mode = PresentationTransferMode::Direct;
        ML_LOG_INFO("metal: presentation transfer probe selected direct texture read");
        return true;
    }

    uint8_t copied_pixel[4] = {0, 0, 0, 0};
    if (RunPresentationTransferProbe(true, copied_pixel) &&
        PresentationProbePixelMatches(copied_pixel)) {
        g_present_transfer_mode = PresentationTransferMode::MaterializedCopy;
        if (force_materialized) {
            ML_LOG_INFO("metal: presentation test hook forced the materialized "
                        "GPU-copy path");
        } else {
            ML_LOG_WARN("metal: direct presentation texture read produced "
                        "BGRA=(%u,%u,%u,%u); using an explicit GPU copy",
                        direct_pixel[0], direct_pixel[1], direct_pixel[2],
                        direct_pixel[3]);
        }
        return true;
    }

    ML_LOG_ERROR("metal: presentation transfer probe failed closed; "
                 "direct BGRA=(%u,%u,%u,%u), copied BGRA=(%u,%u,%u,%u)",
                 direct_pixel[0], direct_pixel[1], direct_pixel[2],
                 direct_pixel[3], copied_pixel[0], copied_pixel[1],
                 copied_pixel[2], copied_pixel[3]);
    return false;
}

id<MTLTexture> SelectPresentationSource(id<MTLCommandBuffer> command) {
    auto& engine = GetEngine();
    if (g_present_transfer_mode == PresentationTransferMode::Direct)
        return engine.color;
    if (g_present_transfer_mode != PresentationTransferMode::MaterializedCopy ||
        !command || !engine.color)
        return nil;

    if (!g_materialized_present_source ||
        g_materialized_present_source.width != engine.color.width ||
        g_materialized_present_source.height != engine.color.height) {
        MTLTextureDescriptor* descriptor = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                         width:engine.color.width
                                        height:engine.color.height
                                     mipmapped:NO];
        descriptor.storageMode = MTLStorageModePrivate;
        descriptor.usage = MTLTextureUsageShaderRead;
        g_materialized_present_source =
            [engine.device newTextureWithDescriptor:descriptor];
        if (!g_materialized_present_source) return nil;
        g_materialized_present_source.label =
            @"Mithril materialized presentation source";
    }

    id<MTLBlitCommandEncoder> copy = [command blitCommandEncoder];
    if (!copy) return nil;
    [copy copyFromTexture:engine.color
              sourceSlice:0
              sourceLevel:0
             sourceOrigin:MTLOriginMake(0, 0, 0)
               sourceSize:MTLSizeMake(engine.color.width, engine.color.height, 1)
                toTexture:g_materialized_present_source
         destinationSlice:0
         destinationLevel:0
        destinationOrigin:MTLOriginMake(0, 0, 0)];
    [copy endEncoding];
    return g_materialized_present_source;
}

struct PresentationSubmission {
    id<CAMetalDrawable> drawable = nil;
};

bool EncodePresentationTail(id<MTLCommandBuffer> command, void* context) {
    auto* submission = static_cast<PresentationSubmission*>(context);
    if (!submission || !submission->drawable) return false;
    command.label = @"Mithril DirectMetal frame + present";
    id<MTLTexture> source = SelectPresentationSource(command);
    if (!source) {
        ML_LOG_ERROR("metal: failed to materialize the presentation source");
        return false;
    }

#if defined(MITHRIL_PRESENTATION_TEST_HOOKS)
    if (!EncodePresentationReference(command, submission->drawable.texture,
                                     source)) {
        ML_LOG_ERROR("metal: failed to encode presentation test reference");
        return false;
    }
#endif
    if (!EncodePresentationRender(command, submission->drawable.texture, source,
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
    if (!SyncLayerTargetSize() || !EnsurePresentPipeline() ||
        !EnsurePresentationTransferMode()) {
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
                                    uint8_t reference_pixel[4],
                                    uint8_t source_pixel[4]) {
    if (!drawable_pixel || !reference_pixel || !source_pixel ||
        g_presentation_capture.armed ||
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
    std::memcpy(source_pixel,
                static_cast<uint8_t*>(g_presentation_capture.readback.contents) +
                    512,
                4);
    g_presentation_capture.reference = nil;
    g_presentation_capture.readback = nil;
    g_presentation_capture.command = nil;
    return true;
}
#endif

} // namespace mithril::metal
