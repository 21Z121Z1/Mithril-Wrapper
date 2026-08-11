// Versioned, read-only DirectMetal diagnostic counters.
//
// Callers must serialize these functions with their GL context. Reset before
// issuing the workload, establish completion with glFinish, then read the
// counters. The API is intentionally independent of Objective-C/Metal types so
// standalone host and Simulator regression apps can consume it through dlsym.

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MITHRIL_DIRECT_METAL_BINDING_STATS_VERSION 1u

typedef struct MithrilDirectMetalBindingStatsV1 {
    uint32_t version;
    uint32_t struct_size;
    uint64_t draws_encoded;
    uint64_t vertex_texture_bind_calls;
    uint64_t fragment_texture_bind_calls;
    uint64_t vertex_sampler_bind_calls;
    uint64_t fragment_sampler_bind_calls;
    uint64_t texture_bind_calls_elided;
    uint64_t sampler_bind_calls_elided;
    uint64_t inactive_stage_texture_bind_calls_avoided;
    uint64_t inactive_stage_sampler_bind_calls_avoided;
} MithrilDirectMetalBindingStatsV1;

void mithrilResetDirectMetalBindingStats(void);
int mithrilGetDirectMetalBindingStatsV1(
    MithrilDirectMetalBindingStatsV1* output, size_t output_size);

#ifdef __cplusplus
}
#endif
