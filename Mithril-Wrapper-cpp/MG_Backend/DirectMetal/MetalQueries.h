// Mithril-Wrapper - MG_Backend/DirectMetal/MetalQueries.h
// GL query-object backing on raw Metal: occlusion via per-pool
// visibility-result buffers, timestamps / time-elapsed via
// MTLCounterSampleBuffer, with a mach_absolute_time CPU fallback when the
// device has no MTLCommonCounterTimestamp counter set.
//
// The public C entry points (dmt_query_*, generated from Backend.h's
// backend_query_* family by the dispatcher's backend_ -> dmt_ renaming) live
// in MetalBackend.mm and simply forward to the functions below. Semantics
// mirror the backend_query_* contract comments in MG_Backend/Backend.h
// (query-family block) — same call sequence, same GL-visible behaviour as
// DirectVulkan/Queries.cpp.
#ifndef MITHRIL_DIRECTMETAL_QUERIES_H
#define MITHRIL_DIRECTMETAL_QUERIES_H

#ifdef __APPLE__

#include "MetalDevice.h"

namespace mithril {
namespace dmt {

/* backend_query_pool_create: create the backing for a GL query object.
 * Idempotent for an existing (query_id, kind); a kind change retires the old
 * backing first. Returns false when the kind is unsupported by this device
 * (no timestamp counter set) — the GL layer then keeps its CPU-clock
 * fallback, mirroring timestampValidBits == 0 in DirectVulkan. */
bool     query_pool_create(uint64_t query_id, int kind);

/* backend_query_pool_destroy: erase the pool; ARC releases the Metal
 * objects (no deferred disposal needed — Metal retains objects referenced
 * by in-flight command buffers itself). */
void     query_pool_destroy(uint64_t query_id);

/* backend_query_begin / backend_query_end: record the GL begin/end pair.
 * OCCLUSION: encoder visibility-result Counting/Disabled (+ buffer bind).
 * TIME_ELAPSED: begin/end counter samples (slots 0/1 of the pair). */
void     query_begin(uint64_t query_id);
void     query_end(uint64_t query_id);

/* backend_query_write_timestamp: glQueryCounter — one counter sample. */
void     query_write_timestamp(uint64_t query_id);

/* backend_query_get_results: read the result. wait=true blocks on the
 * committed command buffers first (GL_QUERY_RESULT); !ended => not
 * available. TIME_ELAPSED returns t1 - t0. Returns false = no such pool. */
bool     query_get_results(uint64_t query_id, bool wait,
                           uint64_t* out, bool* available);

/* backend_query_copy_results: glGetQueryBufferObject — memcpy the result
 * [u64, and availability u64 when with_availability] into the GL buffer's
 * CPU-mapped MTLBuffer at `offset`. */
void     query_copy_results(uint64_t query_id, uint32_t gl_buffer_id,
                            VkDeviceSize offset, bool with_availability);

/* backend_query_timestamp_valid_bits: 64 when GPU timestamps are supported,
 * else 0 (frontend falls back to the CPU clock before creating pools). */
uint32_t query_timestamp_valid_bits();

/* backend_query_timestamp_now_ns: glGetInteger64v(GL_TIMESTAMP) — sampled on
 * the GPU counter timebase via a one-shot command buffer when available,
 * CPU monotonic clock otherwise. */
uint64_t query_timestamp_now_ns();

} // namespace dmt
} // namespace mithril

#endif // __APPLE__
#endif // MITHRIL_DIRECTMETAL_QUERIES_H
