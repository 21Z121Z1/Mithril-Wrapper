// Mithril-Wrapper - MG_Backend/DirectMetal/MetalQueries.mm
// GL query objects on raw Metal (behaviour reference: DirectVulkan/Queries.cpp;
// the dmt_query_* C entry points generated from Backend.h live in
// MetalBackend.mm and forward to the dmt functions implemented here).
//
// MAPPING (kind -> Metal primitive):
//   * OCCLUSION    -> per-pool 8-byte Shared MTLBuffer driven by the render
//                     pass visibilityResultBuffer + encoder visibility mode.
//                     GPU accumulates directly into CPU-mapped memory, so
//                     reads need no resolve round-trip.
//   * TIMESTAMP    -> per-pool MTLCounterSampleBuffer (counterSet =
//                     MTLCommonCounterTimestamp, 64 slots) resolved into a
//                     CPU-visible MTLBuffer. glQueryCounter samples one slot;
//                     the last written slot is the live result.
//   * TIME_ELAPSED -> two counter samples (begin slot + end slot) in the same
//                     buffer; result = t1 - t0. Same shape as the Vulkan
//                     backend's 2-slot timestamp pool (the radv-style GL
//                     mapping) — Metal likewise has no begin/end timestamps.
//
// CAPABILITY PROBE + FALLBACK CHAIN (first link that holds wins):
//   1. [device.counterSets] contains MTLCommonCounterTimestamp
//        -> per-pool counter sample buffers: real GPU timestamps (64 valid
//           bits reported via query_timestamp_valid_bits).
//   2. counter buffer creation fails, or a pool's 64 slots wrap around
//        -> the pool degrades to the CPU clock (mach_absolute_time scaled by
//           mach_timebase_info to ns); one warn per degradation.
//   3. no timestamp counter set at all
//        -> query_pool_create returns false for timestamp/elapsed kinds and
//           valid_bits reports 0, so the GL frontend itself uses the CPU
//           clock (identical to timestampValidBits == 0 in DirectVulkan).
//
// KNOWN DEVIATIONS vs VkGetQueryPoolResults (deliberate, kept narrow):
//   * Availability is the host-side `ended` flag. Metal gives no per-sample
//     GPU-side availability word; a read racing the GPU returns the slot's
//     previous (zero-initialised) value instead of VK_NOT_READY.
//   * wait=true blocks only on the COMMITTED slot command buffers. Samples
//     still queued in the uncommitted backend()->cmd cannot be waited on
//     from here without hijacking commit_frame's serial bookkeeping.
//   * OCCLUSION binds its result buffer through the next render-pass
//     descriptor; changing query buffers closes the current render encoder.
#ifdef __APPLE__

#include "MetalQueries.h"
#include "MetalCommandStream.h"   // current_encoder / ensure_command_buffer /
                                  // note_non_render_commands / one-shot cb
#include "MetalResources.h"       // buffer_table_get (copy_results dst)
#include "../BackendTypes.h"      // MITHRIL_QUERY_*
#include "../../MG_Impl/Log.h"

#include <mach/mach_time.h>

#include <cstring>
#include <unordered_map>

namespace mithril {
namespace dmt {
namespace {

constexpr uint32_t kCounterSlots = 64;   // samples per pool (8 B each)
constexpr NSUInteger kCounterResultStride = 256; // resolve offset alignment

struct QueryPool {
    int kind = -1;                          // MITHRIL_QUERY_*
    id<MTLBuffer> visibility = nil;         // occlusion: shared 8-byte counter
    id<MTLCounterSampleBuffer> counters = nil; // timestamp/elapsed slots
    id<MTLBuffer> counterResults = nil;      // resolved, CPU-visible values
    uint64_t cpuT0 = 0, cpuT1 = 0;          // CPU fallback (ns)
    bool cpuFallback = false;               // sample on the CPU clock now on
    bool begun = false, ended = false;
    uint32_t counterIndex = 0;              // next free slot (circular)

    // Bookkeeping VkQueryPool provides for free but a bare slot cursor does
    // not (additions to the original design sketch, each load-bearing):
    uint32_t elapsedSlot0 = 0;          // TIME_ELAPSED begin slot (end = +1)
    uint32_t samplesWritten = 0;        // wrap detection at kCounterSlots
};

std::unordered_map<uint64_t, QueryPool>& pool_table() {
    static std::unordered_map<uint64_t, QueryPool> t;
    return t;
}

// ---- CPU clock (fallback timebase) ----------------------------------------

static uint64_t mach_now_ns() {
    static mach_timebase_info_data_t tb = {0, 0};
    if (tb.denom == 0) (void)mach_timebase_info(&tb);
    // 128-bit intermediate: Intel Macs tick at ~24 MHz (numer/denom ~ 1/24),
    // ARM64 ticks at 1 Hz/ns. A 64-bit multiply-first would overflow after a
    // few years of uptime on the Intel scale; dividing first loses precision
    // on the ARM scale only trivially (numer==denom==1 there anyway).
    return (uint64_t)(((unsigned __int128)mach_absolute_time() * tb.numer)
                      / tb.denom);
}

// ---- Capability probe (once per process; the device never changes) --------

static id<MTLCounterSet> timestamp_counter_set() {
    static id<MTLCounterSet> cached = nil;
    static bool probed = false;
    if (!probed) {
        probed = true;
        Backend* b = backend();
        if (b && b->initialized) {
            if (@available(macOS 10.15, iOS 14.0, *)) {
                for (id<MTLCounterSet> cs in b->device.counterSets) {
                    if ([cs.name isEqualToString:MTLCommonCounterTimestamp]) {
                        cached = cs;
                        break;
                    }
                }
            }
        }
        if (cached == nil) {
            MITHRIL_LOG_INFO("mtl", "no MTLCommonCounterTimestamp counter "
                             "set — GPU timestamps unavailable, CPU clock "
                             "fallback in effect");
        }
    }
    return cached;
}

static id<MTLCounterSampleBuffer> new_counter_buffer(NSUInteger samples) {
    Backend* b = backend();
    id<MTLCounterSet> cs = timestamp_counter_set();
    if (b == nil || !b->initialized || cs == nil) return nil;
    if (@available(macOS 10.15, iOS 14.0, *)) {
        MTLCounterSampleBufferDescriptor* d =
            [[MTLCounterSampleBufferDescriptor alloc] init];
        d.counterSet = cs;             // single-counter set: 8 B per sample
        d.sampleCount = samples;
        d.storageMode = MTLStorageModeShared;
        NSError* err = nil;
        id<MTLCounterSampleBuffer> csb =
            [b->device newCounterSampleBufferWithDescriptor:d error:&err];
        if (csb == nil) {
            const char* why = err.localizedDescription.UTF8String;
            MITHRIL_LOG_WARN("mtl", "newCounterSampleBuffer failed: %s",
                             why ? why : "unknown error");
        }
        return csb;
    }
    return nil;
}

// The counter set holds exactly one counter (timestamp) -> stride is 8 bytes.
static uint64_t read_slot(const QueryPool& p, uint32_t slot) {
    if (p.counterResults == nil || p.counterResults.contents == nullptr) return 0;
    uint64_t v = 0;
    std::memcpy(&v, (const uint8_t*)p.counterResults.contents +
                        (size_t)slot * kCounterResultStride,
                8);
    return v;
}

// Move the circular cursor one slot forward; degrade the pool to the CPU
// clock when the 64 slots are exhausted (a single GL query object taking
// >64 samples is pathological, but the wrap would clobber unread results).
static bool advance_slot(QueryPool& p) {
    if (p.samplesWritten >= kCounterSlots) {
        if (!p.cpuFallback) {
            p.cpuFallback = true;
            MITHRIL_LOG_WARN("mtl", "query pool counter slots exhausted "
                             "(%u) — degrading this pool to the CPU clock",
                             (unsigned)kCounterSlots);
        }
        return false;
    }
    p.samplesWritten++;
    p.counterIndex = (p.counterIndex + 1) % kCounterSlots;
    return true;
}

// Sample one counter slot without disturbing a live render pass. Priority:
// the live render encoder (current_encoder()); when none is live, spin a
// TRANSIENT blit encoder on the current command buffer, sample, end it.
// The blit path is legal precisely because current_encoder() == nil means no
// encoder is open on backend()->cmd (encoders must be strictly sequential).
// This keeps TIME_ELAPSED/TIMESTAMP working when glBeginQuery runs before
// the first draw has opened a pass — the case where Vulkan would simply
// record vkCmdWriteTimestamp on the bare command buffer.
static bool sample_counter(QueryPool& p, uint32_t slot) {
    if (p.counters == nil || p.counterResults == nil) return false;
    if (!ensure_command_buffer()) return false;
    if (@available(macOS 10.15, iOS 14.0, *)) {
        id<MTLRenderCommandEncoder> renc = current_encoder();
        if (renc != nil) {
            [renc sampleCountersInBuffer:p.counters atSampleIndex:slot
                             withBarrier:YES];
            end_render_pass();
        }
        id<MTLBlitCommandEncoder> benc = [backend()->cmd blitCommandEncoder];
        if (benc == nil) return false;
        if (renc == nil) {
            [benc sampleCountersInBuffer:p.counters atSampleIndex:slot
                             withBarrier:YES];
        }
        [benc resolveCounters:p.counters
                      inRange:NSMakeRange(slot, 1)
            destinationBuffer:p.counterResults
          destinationOffset:(NSUInteger)slot * kCounterResultStride];
        [benc endEncoding];
        note_non_render_commands();   // hasCommands accounting stays honest
        return true;
    }
    return false;
}

// Degrade helper: keeps every subsequent sample of this pool on one
// consistent (CPU) timebase — a GPU begin mixed with a CPU end would be
// meaningless garbage rather than a slightly-off measurement.
static void degrade_to_cpu(QueryPool& p) {
    if (!p.cpuFallback) {
        p.cpuFallback = true;
        MITHRIL_LOG_WARN("mtl", "query pool fell back to CPU-clock sampling");
    }
}

} // namespace

// ---- Pool lifecycle --------------------------------------------------------

bool query_pool_create(uint64_t query_id, int kind) {
    Backend* b = backend();
    if (!b || !b->initialized) return false;
    if (kind != MITHRIL_QUERY_OCCLUSION && kind != MITHRIL_QUERY_TIMESTAMP &&
        kind != MITHRIL_QUERY_TIME_ELAPSED)
        return false;

    auto& table = pool_table();
    auto it = table.find(query_id);
    if (it != table.end()) {
        if (it->second.kind == kind) return true;   // idempotent
        table.erase(it);   // kind change: retire + rebuild (GL forbids the
                           // target change upstream; stay defensive anyway)
    }

    QueryPool p;
    p.kind = kind;
    if (kind == MITHRIL_QUERY_OCCLUSION) {
        // Per-pool buffer => no cross-frame/cross-query aliasing. Shared on
        // every platform: buffers accept Shared even on discrete GPUs, and
        // then GPU-writes are directly CPU-coherent with zero maintenance
        // (Managed would need didModifyRange for the wrong direction only).
        p.visibility = [b->device newBufferWithLength:8
                                              options:MTLResourceStorageModeShared];
        if (p.visibility == nil) {
            MITHRIL_LOG_WARN("mtl", "occlusion query: visibility buffer "
                             "alloc failed for query %llu",
                             (unsigned long long)query_id);
            return false;
        }
        std::memset(p.visibility.contents, 0, 8);
    } else {
        // No timestamp counter set: report the pool as unsupported so the GL
        // layer keeps its CPU-clock fallback (mirrors validBits == 0).
        if (timestamp_counter_set() == nil) return false;
        p.counters = new_counter_buffer(kCounterSlots);
        if (p.counters == nil) return false;
        p.counterResults = [b->device
            newBufferWithLength:kCounterSlots * kCounterResultStride
                        options:MTLResourceStorageModeShared];
        if (p.counterResults == nil) return false;
        std::memset(p.counterResults.contents, 0,
                    kCounterSlots * kCounterResultStride);
    }

    table[query_id] = p;
    return true;
}

void query_pool_destroy(uint64_t query_id) {
    // Why no deferred disposal (unlike Vulkan's disposalQueue): Metal
    // retains every object an encoded command buffer references until that
    // buffer completes (MetalResources.mm LIFETIME NOTE), so erasing the
    // entry and letting ARC drop the id<> refs is safe with queries in
    // flight. VkQueryPool needed the queue only because vkDestroyQueryPool
    // is immediate and the driver does not hold references for you.
    auto& table = pool_table();
    auto it = table.find(query_id);
    if (it != table.end() && it->second.visibility != nil)
        clear_visibility_query(it->second.visibility);
    table.erase(query_id);
}

// ---- Begin / end -----------------------------------------------------------

void query_begin(uint64_t query_id) {
    Backend* b = backend();
    if (!b || !b->initialized) return;
    auto it = pool_table().find(query_id);
    if (it == pool_table().end()) return;
    QueryPool& p = it->second;

    if (p.kind == MITHRIL_QUERY_OCCLUSION) {
        if (p.visibility == nil || p.visibility.contents == nullptr) return;
        // GL semantics: a fresh begin discards the previous result. Vulkan
        // records vkCmdResetQueryPool on the command buffer; Metal has no
        // reset command, so zero the 8 bytes from the CPU. This races only
        // with a STILL-INFLIGHT previous frame using the same pool — the
        // Vulkan path carries the identical caveat.
        std::memset(p.visibility.contents, 0, 8);
        p.begun = true;
        p.ended = false;
        set_visibility_query(p.visibility, true);
        return;
    }

    if (p.kind == MITHRIL_QUERY_TIME_ELAPSED) {
        p.begun = true;
        p.ended = false;
        if (p.cpuFallback) {
            p.cpuT0 = mach_now_ns();
            return;
        }
        // Reserve BOTH pair slots up front so a wrap never splits a pair.
        if (p.samplesWritten + 2 > kCounterSlots) {
            degrade_to_cpu(p);
            p.cpuT0 = mach_now_ns();
            return;
        }
        p.elapsedSlot0 = p.counterIndex;
        if (!sample_counter(p, p.elapsedSlot0)) {
            degrade_to_cpu(p);
            p.cpuT0 = mach_now_ns();
            return;
        }
        advance_slot(p);
        return;
    }

    // TIMESTAMP pools have no begin/end (glQueryCounter is write-only).
}

void query_end(uint64_t query_id) {
    Backend* b = backend();
    if (!b || !b->initialized) return;
    auto it = pool_table().find(query_id);
    if (it == pool_table().end()) return;
    QueryPool& p = it->second;

    if (p.kind == MITHRIL_QUERY_OCCLUSION) {
        p.begun = false;
        p.ended = true;
        set_visibility_query(p.visibility, false);
        return;
    }

    if (p.kind == MITHRIL_QUERY_TIME_ELAPSED) {
        p.begun = false;
        p.ended = true;
        if (p.cpuFallback) {
            p.cpuT1 = mach_now_ns();
            return;
        }
        const uint32_t endSlot = (p.elapsedSlot0 + 1) % kCounterSlots;
        if (!sample_counter(p, endSlot)) {
            // End sample failed (device lost / encoder unavailable). Leave
            // the end slot untouched: a zero/unwritten t1 clamps the result
            // to 0 in get_results rather than producing garbage.
            MITHRIL_LOG_WARN("mtl", "TIME_ELAPSED end sample failed — "
                             "result clamped to 0");
            return;
        }
        advance_slot(p);
        return;
    }
}

void query_write_timestamp(uint64_t query_id) {
    Backend* b = backend();
    if (!b || !b->initialized) return;
    auto it = pool_table().find(query_id);
    if (it == pool_table().end()) return;
    QueryPool& p = it->second;
    if (p.kind != MITHRIL_QUERY_TIMESTAMP) return;   // glQueryCounter only

    if (!p.cpuFallback && p.samplesWritten < kCounterSlots &&
        sample_counter(p, p.counterIndex)) {
        advance_slot(p);   // last written slot = counterIndex - 1 (mod 64)
    } else {
        // No command buffer / no encoder / slots exhausted: CPU clock. The
        // value stays on a monotonic ns timebase, so GL consumers see a
        // plausible timestamp rather than nothing.
        degrade_to_cpu(p);
        p.cpuT1 = mach_now_ns();
    }
    p.ended = true;   // a write makes the pool readable from now on
}

// ---- Results ---------------------------------------------------------------

bool query_get_results(uint64_t query_id, bool wait,
                       uint64_t* out, bool* available) {
    Backend* b = backend();
    if (!b || !b->initialized) return false;
    auto it = pool_table().find(query_id);
    if (it == pool_table().end()) return false;
    const QueryPool& p = it->second;

    if (out) *out = 0;
    if (available) *available = false;
    if (!p.ended) return true;   // never ended: unavailable, result 0

    if (wait) {
        // GL_QUERY_RESULT blocking semantics. Only COMMITTED buffers can be
        // waited on: slotCmd[] are committed by construction, while
        // backend()->cmd may still be recording (waitUntilCompleted on an
        // uncommitted buffer would block forever) — flushing it belongs to
        // commit_frame, and the queries layer does not cross that boundary.
        // Waiting the slots covers the dominant read pattern (fetch last
        // frame's queries at the start of this one).
        for (int i = 0; i < MITHRIL_DMT_MAX_FRAMES_IN_FLIGHT; ++i) {
            @autoreleasepool {
                if (b->slotCmd[i] != nil) [b->slotCmd[i] waitUntilCompleted];
            }
        }
    }

    switch (p.kind) {
        case MITHRIL_QUERY_OCCLUSION:
            // Shared storage: the GPU's accumulation lands directly in
            // CPU-mapped memory — this plain read IS the "resolve". That is
            // the one systematic win over VkGetQueryPoolResults, at the cost
            // of no GPU-side availability word (see file header).
            if (out && p.visibility != nil && p.visibility.contents != nullptr) {
                uint64_t v = 0;
                std::memcpy(&v, p.visibility.contents, 8);
                *out = v;
            }
            break;

        case MITHRIL_QUERY_TIME_ELAPSED:
            if (p.cpuFallback) {
                if (out) *out = (p.cpuT1 >= p.cpuT0) ? (p.cpuT1 - p.cpuT0) : 0;
            } else {
                const uint64_t t0 = read_slot(p, p.elapsedSlot0);
                const uint64_t t1 =
                    read_slot(p, (p.elapsedSlot0 + 1) % kCounterSlots);
                // Clamp: an unwritten/failed end slot reads 0 -> report 0
                // instead of a 2^64-underflow nonsense span.
                if (out) *out = (t1 >= t0) ? (t1 - t0) : 0;
            }
            break;

        case MITHRIL_QUERY_TIMESTAMP:
            if (p.cpuFallback) {
                if (out) *out = p.cpuT1;
            } else if (out) {
                // The slot written by the most recent glQueryCounter
                // (counterIndex points at the NEXT free slot).
                *out = read_slot(p, (p.counterIndex + kCounterSlots - 1) %
                                   kCounterSlots);
            }
            break;
    }

    if (available) *available = true;
    return true;
}

void query_copy_results(uint64_t query_id, uint32_t gl_buffer_id,
                        VkDeviceSize offset, bool with_availability) {
    Backend* b = backend();
    if (!b || !b->initialized) return;

    MetalBuffer* mb = buffer_table_get(gl_buffer_id);
    if (mb == nil || mb->buf == nil || mb->contents == nullptr) {
        MITHRIL_LOG_WARN("mtl", "glGetQueryBufferObject: buffer %u has no "
                          "MTLBuffer backend", gl_buffer_id);
        return;
    }

    // NO_WAIT semantics ride on with_availability (GL spec: the availability
    // word lets the app poll without blocking) — read non-blocking here.
    uint64_t value = 0;
    bool avail = false;
    if (!query_get_results(query_id, false, &value, &avail)) return;

    const NSUInteger need = with_availability ? 16 : 8;   // [u64] / [u64,u64]
    if ((NSUInteger)offset + need > mb->capacity) {
        MITHRIL_LOG_WARN("mtl", "glGetQueryBufferObject: offset %llu + %llu "
                          "exceeds buffer %u capacity %llu",
                          (unsigned long long)offset,
                          (unsigned long long)need, gl_buffer_id,
                          (unsigned long long)mb->capacity);
        return;
    }

    // GL semantics are a GPU-side write (vkCmdCopyQueryPoolResults recorded
    // into the command stream). DirectMetal reaches the same OBSERVABLE
    // state with a CPU memcpy into the shared staging buffer: the bytes are
    // in place before the next draw consumes them, which is all a correct
    // GL program can rely on. Note this copy path can even produce the
    // TIME_ELAPSED delta (the Vulkan version must copy the raw end slot —
    // vkCmdCopyQueryPoolResults cannot subtract).
    uint8_t* dst = (uint8_t*)mb->contents + (NSUInteger)offset;
    std::memcpy(dst, &value, 8);
    if (with_availability) {
        const uint64_t a = avail ? 1ull : 0ull;
        std::memcpy(dst + 8, &a, 8);
    }
    MITHRIL_DMT_SYNC(mb, (NSUInteger)offset, need);
}

// ---- Clock -----------------------------------------------------------------

uint32_t query_timestamp_valid_bits() {
    return timestamp_counter_set() != nil ? 64 : 0;
}

uint64_t query_timestamp_now_ns() {
    Backend* b = backend();
    if (!b || !b->initialized || query_timestamp_valid_bits() == 0)
        return mach_now_ns();

    if (@available(macOS 10.15, iOS 14.0, *)) {
        // Keep GL_TIMESTAMP on the SAME GPU timebase as glQueryCounter
        // results (GL only requires a shared monotonic source): sample a
        // dedicated 1-slot counter buffer through a one-shot command buffer,
        // commit, block, read. Mirrors the Vulkan backend's temp-pool +
        // commit + blocking-read; rare path (perf-hud init), cost is fine.
        static id<MTLCounterSampleBuffer> csb = nil;
        static id<MTLBuffer> result = nil;
        static bool tried = false;
        if (!tried) {
            tried = true;
            csb = new_counter_buffer(1);
            if (csb != nil) {
                result = [b->device newBufferWithLength:kCounterResultStride
                                                options:MTLResourceStorageModeShared];
            }
        }
        if (csb != nil && result != nil) {
            id<MTLCommandBuffer> cb = new_oneshot_command_buffer();
            if (cb != nil) {
                id<MTLBlitCommandEncoder> enc = [cb blitCommandEncoder];
                if (enc != nil) {
                    [enc sampleCountersInBuffer:csb atSampleIndex:0
                                     withBarrier:YES];
                    [enc resolveCounters:csb
                                 inRange:NSMakeRange(0, 1)
                       destinationBuffer:result
                     destinationOffset:0];
                    [enc endEncoding];
                    [cb commit];
                    [cb waitUntilCompleted];
                    uint64_t v = 0;
                    std::memcpy(&v, result.contents, 8);
                    if (v != 0) return v;   // 0 = never written -> fallback
                }
            }
        }
    }
    return mach_now_ns();
}

} // namespace dmt
} // namespace mithril

#endif // __APPLE__
