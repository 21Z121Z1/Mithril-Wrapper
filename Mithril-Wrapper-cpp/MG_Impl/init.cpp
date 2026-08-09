// Mithril-Wrapper - MG_Impl/init.cpp
// Start-up sequencing. The load-time initialiser brings up only the pure-C++
// state machine; the Vulkan (MoltenVK) backend is deliberately NOT started
// there, because MoltenVK is statically linked into this dylib and its own
// globals are not constructed yet at that point (see the note on
// static_block_t). eglInitialize() owns backend bring-up on the EGL path, and
// proc_init() owns it on the headless path.
#include "includes.h"

namespace {
// Runs from __DATA,__mod_init_func while dyld is still loading the image.
//
// It must NOT call backend_init(). MoltenVK is statically linked into this same
// dylib, and the order in which dyld runs an image's initialisers follows link
// order — this translation unit's initialiser is scheduled ahead of MoltenVK's.
// Calling into Vulkan from here reaches a MoltenVK whose own globals have not
// been constructed yet, and it does not fail loudly: MVKExtensionList is a
// fixed-size array, so vkEnumerate*ExtensionProperties returns the right COUNT
// with every extensionName still zeroed. The device log from an iPhone X showed
// exactly that — eighteen blank extension names, followed by
//
//   vkCreateMetalSurfaceEXT not resolved
//   设备不支持 VK_KHR_dynamic_rendering（Apple A11 GPU，Vulkan 1.2.357）
//   Vulkan backend did not come up; GL calls will be no-ops
//
// None of which was true of the hardware. Every name comparison had simply been
// run against empty strings. eglInitialize() then called backend_init() a second
// time, by which point MoltenVK was constructed and the very same device
// enumerated the extensions correctly and came up — leaving the process with a
// leaked first VkInstance and a startup log that blamed the GPU.
//
// So the initialiser is limited to what is safe before dyld finishes: the build
// stamp and the pure-C++ state machine. Bringing the backend up is left to
// eglInitialize()/eglMakeCurrent() (egl.cpp), which run long after loading, and
// to proc_init() for the headless path that never touches EGL.
struct static_block_t {
    static_block_t() {
        MITHRIL_LOG_WARN("init", "Build commit: " MITHRIL_COMMIT_ID);
        ::mithril::state_init();
    }
};
static static_block_t g_static_block;
}

extern "C" {

// Headless bring-up: reached from MITHRIL_ENSURE_INIT() only when EGL is not in
// use (unit tests and any host that drives GL directly). Under EGL —
// which is the path Minecraft takes — g_eglInitialized is already set by the
// time a GL entry point runs, the guard deliberately does nothing, and the
// backend has been brought up by eglInitialize() instead.
//
// The build stamp is not printed here; the static initialiser above emits it
// while the image loads, so it appears even on runs that never reach this
// function.
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
