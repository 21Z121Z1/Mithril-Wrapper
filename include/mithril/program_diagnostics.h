#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MITHRIL_PROGRAM_PREWARM_STATS_VERSION 1u

typedef struct MithrilProgramPrewarmStatsV1 {
    uint32_t version;
    uint32_t struct_size;
    uint64_t frontend_program_bindings;
    uint64_t link_prewarms;
    uint64_t use_prewarms;
    uint64_t draw_fallbacks;
    uint64_t create_failures;
} MithrilProgramPrewarmStatsV1;

void mithrilResetProgramPrewarmStats(void);
int mithrilGetProgramPrewarmStatsV1(
    MithrilProgramPrewarmStatsV1* output, size_t output_size);

#ifdef __cplusplus
}
#endif
