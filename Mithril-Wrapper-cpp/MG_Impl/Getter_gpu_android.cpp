// Mithril-Wrapper - MG_Impl/Getter_gpu_android.cpp
// Non-Apple counterpart of Getter_gpu.mm.
//
// The Apple TU is .mm purely so it can include <Foundation/Foundation.h>; there
// is no real Objective-C in it, but Foundation does not exist off Apple, so the
// Android build needs its own translation unit rather than a reinterpretation
// of that one. Everything here is plain C++ over the same backend_* queries.
//
// The strings differ from the Apple version only where they would otherwise
// name Apple-specific machinery: there is no MoltenVK and no CAMetalLayer on
// Android, and claiming either in GL_RENDERER or the F3 dump would be wrong.
#if !defined(__APPLE__)

#include "includes.h"

#include <cstdint>
#include <sstream>
#include <string>

#ifndef MITHRIL_COMMIT_ID
#define MITHRIL_COMMIT_ID "unknown"
#endif

static std::string friendly_gpu_name(const char* vk_name) {
    if (!vk_name || !*vk_name) return "Vulkan GPU";
    return std::string(vk_name);
}

extern "C" const char* mithril_get_vulkan_device_name(void) {
    static std::string name;
    if (!name.empty()) return name.c_str();
    name = friendly_gpu_name(backend_physical_device_name());
    return name.c_str();
}

extern "C" const char* mithril_get_vulkan_api_string(void) {
    return "Vulkan 1.2";
}

extern "C" uint64_t mithril_get_vram_bytes(void) {
    return backend_vram_bytes();
}

extern "C" const char* mithril_get_gpu_renderer_string(void) {
    static std::string cached;
    if (!cached.empty()) return cached.c_str();

    if (!backend_available()) {
        cached = "Mithril-Wrapper (Mithril-Wrapper Core) (Vulkan backend, no device)";
        return cached.c_str();
    }

    // MobileGL-style three-part GL_RENDERER:
    //   {RendererName} ({CoreName}) ({backendApiVersionString})
    const std::string gpuName = friendly_gpu_name(backend_physical_device_name());
    const std::string api = mithril_get_vulkan_api_string();
    cached = gpuName + " (Mithril-Wrapper Core) (" + gpuName + ", " + api + ")";
    return cached.c_str();
}

extern "C" const char* mithril_get_settings_dump(void) {
    static std::string dump;
    if (!dump.empty()) return dump.c_str();

    std::ostringstream ss;
    ss << "Mithril-Wrapper 1.0 (OpenGL 3.3 -> Vulkan 1.2)\n";
    ss << "  Backend: Vulkan 1.2 (native loader)\n";
    ss << "  Build: GIT@" MITHRIL_COMMIT_ID "\n";

    if (backend_available()) {
        ss << "  Renderer: " << mithril_get_gpu_renderer_string() << "\n";
        ss << "  GPU: " << backend_physical_device_name() << "\n";
        ss << "  API: " << mithril_get_vulkan_api_string() << "\n";
        const uint64_t vram = backend_vram_bytes();
        if (vram > 0) {
            ss << "  VRAM: " << (vram / (1024ULL * 1024ULL)) << " MB (device-local heaps)\n";
        }
    } else {
        ss << "  GPU: (no Vulkan device)\n";
    }

    ss << "  Shader pipeline: GLSL -> SPIR-V (glslang)\n";
    ss << "  Depth/stencil: VK_FORMAT_D32_SFLOAT_S8_UINT\n";
    ss << "  Surface: VK_KHR_android_surface (vkCreateAndroidSurfaceKHR)\n";
    ss << "  EGL: 1.5 (Vulkan-backed)\n";
    ss << "  GL version: 3.3 Core Profile\n";
    ss << "  GLSL version: 3.30\n";

    dump = ss.str();
    return dump.c_str();
}

#endif // !__APPLE__
