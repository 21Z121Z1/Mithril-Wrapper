// Mithril-Wrapper - MG_Impl/init.cpp
// Static initialisation: ensures the state machine + Vulkan (MoltenVK) backend
// come up before any GL call is serviced (mirrors MobileGlues' static-block
// pattern). This is the Vulkan/MoltenVK rewrite of the former init.cpp which
// brought up the Metal backend.
#include "includes.h"

namespace {
struct static_block_t {
    static_block_t() { proc_init(); }
};
static static_block_t g_static_block;
}

extern "C" {

void proc_init(void) {
    static bool done = false;
    if (done) return;
    done = true;

    ::mithril::state_init();
    backend_init();

    if (backend_available()) {
        MITHRIL_LOG_INFO("init", "Mithril-Wrapper initialised (Vulkan 1.2 backend, MoltenVK static link)");
        const char* gpu = backend_physical_device_name();
        if (gpu) {
            MITHRIL_LOG_INFO("renderer", "GPU: %s", gpu);
        }
        uint64_t vram = backend_vram_bytes();
        if (vram > 0) {
            MITHRIL_LOG_INFO("renderer", "VRAM (device-local heaps): %llu MB",
                             (unsigned long long)(vram / (1024ULL * 1024ULL)));
        }
    } else {
        MITHRIL_LOG_WARN("init", "Vulkan backend did not come up; GL calls will be no-ops");
    }
}

} // extern "C"
