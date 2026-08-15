// Mithril-Wrapper - MG_Backend/DirectMetal/MetalDevice.mm
// Device bring-up / teardown, per-frame UBO arena, sync serials, limits.
// The dmt_init / dmt_shutdown / dmt_available lifecycle entry points live in
// MetalEntry.mm; this TU provides the internals.
#ifdef __APPLE__

#include "MetalDevice.h"
#include "../BackendTypes.h"   // MITHRIL_LIMIT_* selectors
#include "../../MG_Impl/Log.h"

#include <mach/mach_time.h>
#include <sys/time.h>

namespace mithril {
namespace dmt {

static Backend g_backend;

Backend* backend() { return &g_backend; }

// ---- Lifecycle ----------------------------------------------------------

bool init_device() {
    Backend* b = backend();
    if (b->initialized) return true;

    b->device = MTLCreateSystemDefaultDevice();
    if (b->device == nil) {
        MITHRIL_LOG_ERROR("mtl", "MTLCreateSystemDefaultDevice returned nil");
        return false;
    }
    b->queue = [b->device newCommandQueue];
    if (b->queue == nil) {
        MITHRIL_LOG_ERROR("mtl", "newCommandQueue failed");
        return false;
    }

    if (@available(macOS 10.15, iOS 13.0, *)) {
        b->unifiedMemory = b->device.hasUnifiedMemory;
    }
#ifdef __MAC_OS_X_VERSION_MIN_REQUIRED
    b->unifiedMemory = b->device.hasUnifiedMemory;
#else
    // iOS is always unified.
    b->unifiedMemory = YES;
#endif
    if (@available(macOS 11.0, iOS 14.0, *)) {
        b->supportsFamilyApple7 = [b->device supportsFamily:MTLGPUFamilyApple7];
    }

    b->initialized = true;
    MITHRIL_LOG_INFO("mtl", "DirectMetal device up: %s (unified=%d)",
                     b->device.name.UTF8String, (int)b->unifiedMemory);
    return true;
}

void shutdown_device() {
    Backend* b = backend();
    if (!b->initialized) return;
    // Any in-flight command buffer must complete before releasing resources.
    for (int i = 0; i < MITHRIL_DMT_MAX_FRAMES_IN_FLIGHT; ++i) {
        @autoreleasepool {
            if (b->slotCmd[i] != nil) { [b->slotCmd[i] waitUntilCompleted]; b->slotCmd[i] = nil; }
        }
    }
    if (b->cmd != nil) { [b->cmd waitUntilCompleted]; b->cmd = nil; }
    ubo_shutdown_all();
    b->queue = nil;
    b->device = nil;
    b->initialized = false;
}

// ---- MSL version --------------------------------------------------------

uint32_t msl_version_for_device() {
    // Metal 3 devices (Apple7+/Mac2) can compile MSL 2.4; everything else in
    // the supported range gets 2.2 which covers every feature the SPIR-V we
    // emit exercises (buffer/texture/sampler bindings, frag depth, arrays).
    if (@available(macOS 11.0, iOS 14.0, *)) {
        if (backend()->supportsFamilyApple7) {
            if (@available(macOS 13.0, iOS 16.0, *)) return MTLLanguageVersion3_0;
        }
    }
    return MTLLanguageVersion2_2;
}

// ---- UBO arena ----------------------------------------------------------

static NSUInteger next_block_size(NSUInteger prev) {
    NSUInteger sz = prev ? prev * 2 : 256 * 1024;
    return sz > (4u * 1024 * 1024) ? (4u * 1024 * 1024) : sz;
}

static id<MTLBuffer> arena_new_block(Backend* b, NSUInteger len) {
    // iOS uses MTLResourceStorageModeShared (unified memory); macOS discrete
    // GPU uses MTLResourceStorageModeManaged (requires didModifyRange sync).
    return [b->device newBufferWithLength:len
                                  options:MITHRIL_DMT_STORAGE(!b->unifiedMemory)];
}

bool ubo_allocate(int slot, NSUInteger size, UboSliceDmt& out) {
    Backend* b = backend();
    if (!b->initialized) return false;
    size = (size + ubo_alignment() - 1) & ~(ubo_alignment() - 1);
    if (size == 0) size = ubo_alignment();

    UboArenaSlot& s = b->uboArena[slot & (MITHRIL_DMT_MAX_FRAMES_IN_FLIGHT - 1)];
    if (!s.blocks.empty()) {
        UboBlock& blk = s.blocks.back();
        if (blk.capacity - blk.used >= size) {
            out.buf = blk.buf;
            out.offset = blk.used;
            out.size = size;
            blk.used += size;
            return true;
        }
    }
    NSUInteger len = next_block_size(s.blocks.empty() ? 0
                                                      : s.blocks.back().capacity);
    if (len < size) len = size;
    id<MTLBuffer> nb = arena_new_block(b, len);
    if (nb == nil) return false;
    UboBlock blk; blk.buf = nb; blk.capacity = len; blk.used = size;
    s.blocks.push_back(blk);
    out.buf = nb; out.offset = 0; out.size = size;
    return true;
}

bool ubo_upload(int slot, const void* data, NSUInteger size, UboSliceDmt& out) {
    if (!ubo_allocate(slot, size, out)) return false;
    if (data && out.buf.contents) {
        std::memcpy((uint8_t*)out.buf.contents + out.offset, data, size);
        // Sync managed buffer on macOS; no-op on iOS (Shared storage).
        // MITHRIL_DMT_SYNC needs a MetalBuffer*, but out.buf is id<MTLBuffer>.
        // For UBO arena we can call didModifyRange directly since the arena
        // is always Shared on iOS (compiles out via #if TARGET_OS_OSX).
#if TARGET_OS_OSX
        if (!backend()->unifiedMemory) [out.buf didModifyRange:NSMakeRange(out.offset, size)];
#endif
    }
    return true;
}

void ubo_rewind(int slot) {
    Backend* b = backend();
    UboArenaSlot& s = b->uboArena[slot & (MITHRIL_DMT_MAX_FRAMES_IN_FLIGHT - 1)];
    for (auto& blk : s.blocks) blk.used = 0;
}

void ubo_shutdown_all() {
    Backend* b = backend();
    for (int i = 0; i < MITHRIL_DMT_MAX_FRAMES_IN_FLIGHT; ++i)
        b->uboArena[i].blocks.clear();
}

// ---- Limits -------------------------------------------------------------

int device_limit(int which, int fallback) {
    Backend* b = backend();
    if (!b->initialized) return fallback;
    // "宁可少报，绝不能多报" — never report more than the device can do.
    const int tex2d = b->supportsFamilyApple7 ? 16384 : 8192;
    switch (which) {
        case MITHRIL_LIMIT_MAX_TEXTURE_SIZE:          return tex2d;
        case MITHRIL_LIMIT_MAX_3D_TEXTURE_SIZE:       return 2048;
        case MITHRIL_LIMIT_MAX_CUBE_MAP_TEXTURE_SIZE: return tex2d;
        case MITHRIL_LIMIT_MAX_ARRAY_TEXTURE_LAYERS:  return 2048;
        case MITHRIL_LIMIT_MAX_RENDERBUFFER_SIZE:     return tex2d;
        case MITHRIL_LIMIT_MAX_VIEWPORT_WIDTH:        return tex2d;
        case MITHRIL_LIMIT_MAX_VIEWPORT_HEIGHT:       return tex2d;
        case MITHRIL_LIMIT_MAX_TEXTURE_IMAGE_UNITS:   return 16;
        case MITHRIL_LIMIT_MAX_COMBINED_TEX_UNITS:    return 80;
        case MITHRIL_LIMIT_MAX_UNIFORM_BLOCK_SIZE:    return 64 * 1024;
        case MITHRIL_LIMIT_UNIFORM_BUFFER_ALIGNMENT:  return (int)ubo_alignment();
        case MITHRIL_LIMIT_MAX_UNIFORM_BUFFER_BINDINGS: return 14; // vertex slots 16..29
        case MITHRIL_LIMIT_MAX_COLOR_ATTACHMENTS:     return 8;
        case MITHRIL_LIMIT_MAX_SAMPLES:               return 4;
        case MITHRIL_LIMIT_MAX_VERTEX_ATTRIBS:        return 16;
        case MITHRIL_LIMIT_MAX_SSBO_BINDINGS:         return 8;
        case MITHRIL_LIMIT_MAX_SSBO_SIZE:             return 128 * 1024 * 1024;
        case MITHRIL_LIMIT_MAX_COMPUTE_WG_INVOCATIONS: return 1024;
        case MITHRIL_LIMIT_MAX_COMPUTE_WG_COUNT_X:    return 65535;
        case MITHRIL_LIMIT_MAX_COMPUTE_WG_SIZE_X:     return 1024;
        default: return fallback;
    }
}

} // namespace dmt
} // namespace mithril

#endif // __APPLE__
