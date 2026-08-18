#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MITHRIL_DRAW_LOWERING_STATS_VERSION 1u

typedef struct MithrilDrawLoweringStatsV1 {
    uint32_t version;
    uint32_t struct_size;
    uint64_t shared_state_resolves;
    uint64_t geometry_lowerings;
    uint64_t multi_draw_calls;
    uint64_t multi_draw_subdraws;
} MithrilDrawLoweringStatsV1;

void mithrilResetDrawLoweringStats(void);
int mithrilGetDrawLoweringStatsV1(
    MithrilDrawLoweringStatsV1* output, size_t output_size);

#ifdef __cplusplus
}
#endif
