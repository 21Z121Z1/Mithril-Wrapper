// Mithril-Wrapper - MG_Backend/DirectMetal/MetalQueries.mm
// GL query objects on raw Metal (behaviour reference: DirectVulkan/Queries.cpp;
// the dmt_query_* C entry points generated from Backend.h live in
// MetalBackend.mm and forward to the dmt functions implemented here).
//
// MAPPING (kind -> Metal primitive):
//   * OCCLUSION    -> per-pool 8-byte Shared MTLBuffer driven by the render
//                     encoder's visibility-result machinery
//                     (setVisibilityResultBuffer: + setVisibilityResultMode:).
//                     GPU accumulates directly into CPU-mapped memory, so
//                     reads need no resolve round-trip.
//   * TIMESTAMP    -> per-pool MTLCounterSampleBuffer (counterSet =
//                     MTLCommonCounterTimestamp, 64 slots, Shared storage).
//                     glQueryCounter samples one slot; the last written slot
//                     is the live result.
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
//   * OCCLUSION needs a live render encoder (visibility mode is encoder
//     state, not command-buffer state); see the trade-off in query_begin.
#ifdef __APPLE__

#include "MetalQueries.h"
#include "MetalCommandStream.h"   // current_encoder / ensure_command_buffer /
                                  // note_non_render_commands / one-shot cb
#include "MetalResources.h"       // buffer_table_get (copy_results dst)
#include "../BackendTypes.h"      // MITHRIL_QUERY_*
#include "../../MG_Impl/Log.h"

#include <mach/mach_time.h>

#include <TargetConditionals.h>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace mithril {
namespace dmt {
namespace {

constexpr uint32_t kCounterSlots = 64;   // samples per timestamp pool
constexpr uint32_t kVisibilitySlots = 16384; // 128 KiB shared result arena

struct QueryPool {
    int kind = -1;                          // MITHRIL_QUERY_*
    NSUInteger visibilityOffset = NSNotFound; // occlusion byte offset
    id<MTLCounterSampleBuffer> counters = nil; // timestamp/elapsed slots
    uint64_t cpuT0 = 0, cpuT1 = 0;          // CPU fallback (ns)
    bool cpuFallback = false;               // sample on the CPU clock now on
    bool begun = false, ended = false;
    uint32_t counterIndex = 0;              // next free slot (circular)

    // Bookkeeping VkQueryPool provides for free but a bare slot cursor does
    // not (additions to the original design sketch, each load-bearing):
    bool     pendingVisibility = false; // occlusion begun with no encoder
    uint32_t elapsedSlot0 = 0;          // TIME_ELAPSED begin slot (end = +1)
    uint32_t samplesWritten = 0;        // wrap detection at kCounterSlots
};

std::unordered_map<uint64_t, QueryPool>& pool_table() {
    static std::unordered_map<uint64_t, QueryPool> t;
    return t;
}

id<MTLBuffer> g_visibilityResults = nil;
uint32_t g_nextVisibilitySlot = 0;
std::vector<uint32_t> g_freeVisibilitySlots;

id<MTLBuffer> ensure_visibility_result_buffer() {
    Backend* b = backend();
    if (!b || !b->initialized || b->device == nil) return nil;
    if (g_visibilityResults != nil && g_visibilityResults.device == b->device)
        return g_visibilityResults;
    g_visibilityResults = [b->device newBufferWithLength:(NSUInteger)kVisibilitySlots * 8
                                                  options:MTLResourceStorageModeShared];
    g_nextVisibilitySlot = 0;
    g_freeVisibilitySlots.clear();
    if (g_visibilityResults != nil && g_visibilityResults.contents != nullptr)
        std::memset(g_visibilityResults.contents, 0, (size_t)kVisibilitySlots * 8);
    return g_visibilityResults;
}

bool allocate_visibility_offset(NSUInteger& offset) {
    if (ensure_visibility_result_buffer() == nil) return false;
    uint32_t slot;
    if (!g_freeVisibilitySlots.empty()) {
        slot = g_freeVisibilitySlots.back();
        g_freeVisibilitySlots.pop_back();
    } else {
        if (g_nextVisibilitySlot >= kVisibilitySlots) return false;
        slot = g_nextVisibilitySlot++;
    }
    offset = (NSUInteger)slot * 8;
    return true;
}

void free_visibility_offset(NSUInteger offset) {
    if (offset == NSNotFound || (offset & 7u) != 0) return;
    NSUInteger slot = offset / 8;
    if (slot < kVisibilitySlots) g_freeVisibilitySlots.push_back((uint32_t)slot);
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
        d.storageMode = MTLStorageModeShared;   // CPU-mapped contents
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

// Counter samples use a vendor-private representation; resolve to Metal's
// standard timestamp result before CPU access.
static uint64_t read_slot(const QueryPool& p, uint32_t slot) {
    if (p.counters == nil) return 0;
    if (@available(macOS 10.15, iOS 14.0, *)) {
        NSData* data = [p.counters resolveCounterRange:NSMakeRange(slot, 1)];
        if (data == nil || data.length < sizeof(MTLCounterResultTimestamp)) return 0;
        auto* ts = static_cast<const MTLCounterResultTimestamp*>(data.bytes);
        return ts[0].timestamp == MTLCounterErrorValue ? 0 : ts[0].timestamp;
    }
    return 0;
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
    if (p.counters == nil) return false;
    if (!ensure_command_buffer()) return false;
    if (@available(macOS 10.15, iOS 14.0, *)) {
        id<MTLRenderCommandEncoder> renc = current_encoder();
        if (renc != nil) {
            [renc sampleCountersInBuffer:p.counters atSampleIndex:slot
                             withBarrier:YES];
            return true;
        }
        id<MTLBlitCommandEncoder> benc = [backend()->cmd blitCommandEncoder];
        if (benc == nil) return false;
        [benc sampleCountersInBuffer:p.counters atSampleIndex:slot
                         withBarrier:YES];
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

id<MTLBuffer> visibility_result_buffer() { return g_visibilityResults; }

void replay_pending_visibility(id<MTLRenderCommandEncoder> encoder) {
    if (encoder == nil || g_visibilityResults == nil) return;
    for (auto& kv : pool_table()) {
        QueryPool& p = kv.second;
        if (p.kind != MITHRIL_QUERY_OCCLUSION || !p.begun ||
            p.ended || !p.pendingVisibility || p.visibilityOffset == NSNotFound)
            continue;
        [encoder setVisibilityResultMode:MTLVisibilityResultModeCounting
                                  offset:p.visibilityOffset];
        p.pendingVisibility = false;
        // OpenGL permits at most one active query per target in a context;
        // Metal likewise has one visibility mode/offset per encoder.
        break;
    }
}

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
        const bool firstVisibilityUse = (g_visibilityResults == nil);
        if (!allocate_visibility_offset(p.visibilityOffset)) {
            MITHRIL_LOG_WARN("mtl", "occlusion query visibility arena exhausted for %llu",
                             (unsigned long long)query_id);
            return false;
        }
        id<MTLBuffer> vb = ensure_visibility_result_buffer();
        std::memset((uint8_t*)vb.contents + p.visibilityOffset, 0, 8);
        // A pass created before the first occlusion pool has no visibility
        // result buffer by design. Split it now; query_begin will mark the
        // query pending and the next pass replays the visibility mode.
        if (firstVisibilityUse && render_pass_active()) end_render_pass();
    } else {
        // No timestamp counter set: report the pool as unsupported so the GL
        // layer keeps its CPU-clock fallback (mirrors validBits == 0).
        if (timestamp_counter_set() == nil) return false;
        p.counters = new_counter_buffer(kCounterSlots);
        if (p.counters == nil) return false;
    }

    table[query_id] = p;
    return true;
}

void query_pool_destroy(uint64_t query_id) {
    auto& table = pool_table();
    auto it = table.find(query_id);
    if (it == table.end()) return;
    if (it->second.kind == MITHRIL_QUERY_OCCLUSION)
        free_visibility_offset(it->second.visibilityOffset);
    table.erase(it);
}

// ---- Begin / end -----------------------------------------------------------

void query_begin(uint64_t query_id) {
    Backend* b = backend();
    if (!b || !b->initialized) return;
    auto it = pool_table().find(query_id);
    if (it == pool_table().end()) return;
    QueryPool& p = it->second;

    if (p.kind == MITHRIL_QUERY_OCCLUSION) {
        id<MTLBuffer> visibility = ensure_visibility_result_buffer();
        if (visibility == nil || visibility.contents == nullptr || p.visibilityOffset == NSNotFound) return;
        // GL semantics: a fresh begin discards the previous result. Vulkan
        // records vkCmdResetQueryPool on the command buffer; Metal has no
        // reset command, so zero the 8 bytes from the CPU. This races only
        // with a STILL-INFLIGHT previous frame using the same pool — the
        // Vulkan path carries the identical caveat.
        std::memset((uint8_t*)visibility.contents + p.visibilityOffset, 0, 8);
        p.begun = true;
        p.ended = false;
        p.pendingVisibility = false;

        id<MTLRenderCommandEncoder> enc = current_encoder();
        if (enc != nil) {
            [enc setVisibilityResultMode:MTLVisibilityResultModeCounting
                                   offset:p.visibilityOffset];
        } else {
            // TRADE-OFF (no live pass): the visibility mode is ENCODER state
            // — there is nowhere to record it yet, and MetalCommandStream
            // deliberately does not know about queries (no replay support).
            // Record a pending flag and try again at query_end; draws made
            // before then are NOT counted, so the result undercounts. GL
            // occlusion in Minecraft is a debug aid (F3 chunk-border style
            // overlays), never gameplay logic — an undercount is acceptable
            // where a silent no-op or a stale count would not be obviously
            // worse. The Vulkan version depends on recording state in the
            // same way (begin outside a command buffer is silently lost).
            p.pendingVisibility = true;
        }
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
        id<MTLRenderCommandEncoder> enc = current_encoder();
        if (enc != nil) {
            if (p.pendingVisibility) {
                // Begin had no encoder: bind + enable now, then disable
                // immediately below. Zero draws happen in between, so the
                // count stays at the memset zero — but the encoder's
                // visibility state ends up well-defined instead of stale.
                    [enc setVisibilityResultMode:MTLVisibilityResultModeCounting
                                       offset:p.visibilityOffset];
                p.pendingVisibility = false;
            }
            [enc setVisibilityResultMode:MTLVisibilityResultModeDisabled
                                   offset:p.visibilityOffset];
        } else {
            // No encoder at end either: the pending mode is dropped; the
            // result remains the memset zero and availability is still true.
            p.pendingVisibility = false;
        }
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
            if (out) {
                id<MTLBuffer> visibility = visibility_result_buffer();
                if (visibility != nil && visibility.contents != nullptr && p.visibilityOffset != NSNotFound) {
                    uint64_t v = 0;
                    std::memcpy(&v, (const uint8_t*)visibility.contents + p.visibilityOffset, 8);
                    *out = v;
                }
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
#if TARGET_OS_OSX
    if (mb->managed) {
        [mb->buf didModifyRange:NSMakeRange((NSUInteger)offset, need)];
    }
#endif
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
        static bool tried = false;
        if (!tried) {
            tried = true;
            csb = new_counter_buffer(1);
        }
        if (csb != nil) {
            id<MTLCommandBuffer> cb = new_oneshot_command_buffer();
            if (cb != nil) {
                id<MTLBlitCommandEncoder> enc = [cb blitCommandEncoder];
                if (enc != nil) {
                    [enc sampleCountersInBuffer:csb atSampleIndex:0
                                     withBarrier:YES];
                    [enc endEncoding];
                    [cb commit];
                    [cb waitUntilCompleted];
                    NSData* data = [csb resolveCounterRange:NSMakeRange(0, 1)];
                    if (data != nil && data.length >= sizeof(MTLCounterResultTimestamp)) {
                        auto* ts = static_cast<const MTLCounterResultTimestamp*>(data.bytes);
                        uint64_t v = ts[0].timestamp;
                        if (v != 0 && v != MTLCounterErrorValue) return v;
                    }
                }
            }
        }
    }
    return mach_now_ns();
}

} // namespace dmt
} // namespace mithril

#endif // __APPLE__
