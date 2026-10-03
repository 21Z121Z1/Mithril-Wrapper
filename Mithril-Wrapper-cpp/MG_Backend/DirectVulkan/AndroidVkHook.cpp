// Mithril-Wrapper - MG_Backend/DirectVulkan/AndroidVkHook.cpp
//
// Separate namespace-global hook used only on Android.
//
// The Android Vulkan loader owns VK_KHR_surface / VK_KHR_android_surface and
// VK_KHR_swapchain. Turnip is a Vulkan HAL module, so loading it directly gives
// us the GPU driver but loses Android WSI. The proven route used by FCL and
// libadrenotools is:
//
//   hook namespace
//       -> preload this DF_1_GLOBAL hook
//       -> load a unique copy of /system/.../libvulkan.so
//       -> loader asks for vulkan.<soc>.so
//       -> hook creates a driver namespace whose parent is the loader-supplied
//          SP-HAL/vendor namespace
//       -> load libvulkan_freedreno.so from DRIVER_PATH
//
// Keeping the Turnip namespace parented to the namespace supplied by the
// platform loader matters: the parent already contains the loader/HAL
// dependency set expected by Android's Vulkan stack. A single broad "escape"
// namespace is useful for diagnostics/direct fallback, but is not the correct
// parent for the loader-mediated driver path.

#include <android/dlext.h>
#include <dlfcn.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

constexpr uint64_t kNsSharedIsolated = 3; // ISOLATED(1) | SHARED(2), private bionic ABI

using dlopen_ext_fn = void* (*)(const char*, int, const android_dlextinfo*);
using load_sphal_fn = void* (*)(const char*, int);
using create_ns_fn = struct android_namespace_t* (*)(
    const char*, const char*, const char*, uint64_t, const char*,
    struct android_namespace_t*, const void*);
using get_exported_ns_fn = struct android_namespace_t* (*)(const char*);

dlopen_ext_fn g_real_dlopen_ext = nullptr;
load_sphal_fn g_real_load_sphal = nullptr;
create_ns_fn g_create_ns = nullptr;
get_exported_ns_fn g_get_exported_ns = nullptr;

struct android_namespace_t* g_turnip_ns = nullptr;

char g_driver_dir[512] = {0};
char g_driver_name[256] = {0};

bool g_trace = false;
int g_redirects = 0;
int g_intercepts = 0;

void trace(const char* fmt, ...) {
    if (!g_trace) return;
    va_list ap;
    va_start(ap, fmt);
    std::fprintf(stderr, "[mithril vkhook] ");
    std::vfprintf(stderr, fmt, ap);
    std::fprintf(stderr, "\n");
    va_end(ap);
}

const char* base_name(const char* path) {
    if (!path) return "";
    const char* slash = std::strrchr(path, '/');
    return slash ? slash + 1 : path;
}

std::string parent_dir(const char* path) {
    if (!path || !path[0]) return {};
    const char* slash = std::strrchr(path, '/');
    if (!slash) return {};
    return std::string(path, static_cast<size_t>(slash - path));
}

void resolve_real() {
    if (g_real_dlopen_ext) return;

    // Resolve through explicit library handles rather than RTLD_DEFAULT: this
    // DSO intentionally exports the same symbols and must never resolve its own
    // interposed entry points as the fallback implementation.
    void* libdl = dlopen("libdl.so", RTLD_NOW | RTLD_LOCAL);
    if (!libdl) {
        trace("dlopen(libdl.so) failed: %s", dlerror());
        return;
    }
    g_real_dlopen_ext =
        reinterpret_cast<dlopen_ext_fn>(dlsym(libdl, "android_dlopen_ext"));

    // android_load_sphal_library lives in libvndksupport.
    void* vndksupport = dlopen("libvndksupport.so", RTLD_NOW | RTLD_LOCAL);
    if (vndksupport) {
        g_real_load_sphal =
            reinterpret_cast<load_sphal_fn>(
                dlsym(vndksupport, "android_load_sphal_library"));
    }

    trace("real functions: dlopen_ext=%s load_sphal=%s create_ns=%s get_exported_ns=%s",
          g_real_dlopen_ext ? "yes" : "no",
          g_real_load_sphal ? "yes" : "no",
          g_create_ns ? "yes" : "no",
          g_get_exported_ns ? "yes" : "no");
}

bool is_vulkan_hal(const char* name) {
    if (!name) return false;
    const char* base = base_name(name);
    if (std::strncmp(base, "vulkan.", 7) != 0) return false;
    const size_t len = std::strlen(base);
    return len > 3 && std::strcmp(base + len - 3, ".so") == 0;
}

struct android_namespace_t* exported_driver_parent() {
    if (!g_get_exported_ns) return nullptr;
    for (const char* name : {"sphal", "vendor", "default"}) {
        if (auto* ns = g_get_exported_ns(name)) {
            trace("using exported parent namespace %s=%p", name, (void*)ns);
            return ns;
        }
    }
    return nullptr;
}

std::string driver_search_dir() {
    if (g_driver_dir[0]) return std::string(g_driver_dir);
    if (g_driver_name[0]) return parent_dir(g_driver_name);
    return {};
}

struct android_namespace_t* ensure_turnip_namespace(
    struct android_namespace_t* parent,
    const void* caller_addr) {
    if (g_turnip_ns) return g_turnip_ns;
    if (!g_create_ns || !parent) {
        trace("cannot create Turnip namespace: create_ns=%s parent=%p",
              g_create_ns ? "yes" : "no", (void*)parent);
        return nullptr;
    }

    const std::string search = driver_search_dir();
    if (search.empty()) {
        trace("cannot create Turnip namespace: no driver search directory");
        return nullptr;
    }

    // Mirrors FCL's linkerhook: the custom-driver namespace is created lazily
    // from the namespace supplied by libvulkan's HAL load request. The custom
    // driver directory is the namespace search/permitted path; dependencies
    // already loaded by the parent stay visible because the namespace is
    // SHARED_ISOLATED.
    g_turnip_ns = g_create_ns(
        "mithril-turnip-driver",
        nullptr,
        search.c_str(),
        kNsSharedIsolated,
        search.c_str(),
        parent,
        caller_addr);

    std::fprintf(stderr,
                 "[mithril vkhook] Turnip namespace %s (parent=%p path=%s)\n",
                 g_turnip_ns ? "created" : "FAILED",
                 (void*)parent, search.c_str());
    return g_turnip_ns;
}

std::string driver_load_name() {
    if (g_driver_name[0]) {
        // Loading by SONAME/base name lets the child namespace's search path do
        // the lookup, exactly like FCL's linkerhook. This also avoids absolute
        // path accessibility checks from the parent namespace.
        return std::string(base_name(g_driver_name));
    }
    return "libvulkan_freedreno.so";
}

void* load_custom_driver(const char* requested,
                         const android_dlextinfo* loader_info,
                         const void* caller_addr) {
    if (g_driver_dir[0] == '\0' && g_driver_name[0] == '\0') return nullptr;
    resolve_real();
    if (!g_real_dlopen_ext) return nullptr;

    struct android_namespace_t* parent = nullptr;
    if (loader_info &&
        (loader_info->flags & ANDROID_DLEXT_USE_NAMESPACE) &&
        loader_info->library_namespace) {
        parent = loader_info->library_namespace;
    }
    if (!parent) parent = exported_driver_parent();

    auto* ns = ensure_turnip_namespace(parent, caller_addr);
    if (!ns) return nullptr;

    android_dlextinfo ext{};
    ext.flags = ANDROID_DLEXT_USE_NAMESPACE;
    ext.library_namespace = ns;

    const std::string name = driver_load_name();
    void* h = g_real_dlopen_ext(name.c_str(), RTLD_NOW | RTLD_LOCAL, &ext);

    // Some driver plugins publish an absolute path/name that differs from the
    // expected Turnip SONAME. Keep a second attempt through the same child
    // namespace, never through the app/default namespace.
    if (!h && g_driver_name[0] && std::strchr(g_driver_name, '/')) {
        dlerror();
        h = g_real_dlopen_ext(g_driver_name, RTLD_NOW | RTLD_LOCAL, &ext);
    }

    if (!h) {
        const char* err = dlerror();
        std::fprintf(stderr,
                     "[mithril vkhook] Turnip redirect FAILED: request=\"%s\" driver=\"%s\" err=%s\n",
                     requested ? requested : "(null)", name.c_str(),
                     err ? err : "unknown");
        return nullptr;
    }

    ++g_redirects;
    std::fprintf(stderr,
                 "[mithril vkhook] redirected \"%s\" -> \"%s\" (ns=%p)\n",
                 requested ? requested : "(null)", name.c_str(), (void*)ns);
    return h;
}

} // namespace

extern "C" {

__attribute__((visibility("default")))
void mithril_vkhook_init(const char* driver_dir,
                         const char* driver_name,
                         int trace_on,
                         void* create_namespace_fn,
                         void* get_exported_namespace_fn) {
    g_trace = trace_on != 0;
    g_create_ns = reinterpret_cast<create_ns_fn>(create_namespace_fn);
    g_get_exported_ns =
        reinterpret_cast<get_exported_ns_fn>(get_exported_namespace_fn);

    g_driver_dir[0] = '\0';
    g_driver_name[0] = '\0';
    if (driver_dir) {
        std::strncpy(g_driver_dir, driver_dir, sizeof(g_driver_dir) - 1);
        g_driver_dir[sizeof(g_driver_dir) - 1] = '\0';
    }
    if (driver_name) {
        std::strncpy(g_driver_name, driver_name, sizeof(g_driver_name) - 1);
        g_driver_name[sizeof(g_driver_name) - 1] = '\0';
    }

    resolve_real();
    trace("init: dir=%s name=%s", g_driver_dir, g_driver_name);
}

__attribute__((visibility("default")))
int mithril_vkhook_redirects(void) {
    return g_redirects;
}

__attribute__((visibility("default")))
int mithril_vkhook_intercepts(void) {
    return g_intercepts;
}

__attribute__((visibility("default")))
void* android_dlopen_ext(const char* filename,
                         int flags,
                         const android_dlextinfo* info) {
    resolve_real();

    if (is_vulkan_hal(filename)) {
        ++g_intercepts;
        std::fprintf(stderr,
                     "[mithril vkhook] intercepted android_dlopen_ext(\"%s\", ns=%p)\n",
                     filename ? filename : "(null)",
                     (void*)(info ? info->library_namespace : nullptr));
        if (void* h =
                load_custom_driver(filename, info, __builtin_return_address(0))) {
            return h;
        }
    }

    if (g_real_dlopen_ext) return g_real_dlopen_ext(filename, flags, info);
    return dlopen(filename, flags);
}

__attribute__((visibility("default")))
void* android_load_sphal_library(const char* filename, int flags) {
    resolve_real();

    if (is_vulkan_hal(filename)) {
        ++g_intercepts;
        auto* parent = exported_driver_parent();
        android_dlextinfo info{};
        if (parent) {
            info.flags = ANDROID_DLEXT_USE_NAMESPACE;
            info.library_namespace = parent;
        }

        std::fprintf(stderr,
                     "[mithril vkhook] intercepted android_load_sphal_library(\"%s\", parent=%p)\n",
                     filename ? filename : "(null)", (void*)parent);

        if (void* h = load_custom_driver(
                filename, parent ? &info : nullptr, __builtin_return_address(0))) {
            return h;
        }
    }

    if (g_real_load_sphal) return g_real_load_sphal(filename, flags);
    return dlopen(filename, flags);
}

} // extern "C"
