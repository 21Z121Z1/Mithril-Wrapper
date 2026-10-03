// Mithril-Wrapper - MG_Backend/DirectVulkan/AndroidVkHook.cpp
//
// Built as a SEPARATE shared object (libmithril_vkhook.so), not as part of
// libmithril.so.
//
// Why it has to be separate
// -------------------------
// On Android the platform loader - libvulkan.so - is the component that owns
// the window system integration. A Mesa driver such as Turnip, built with
// -Dandroid-stub=true, reports eight instance extensions and none of them are
// VK_KHR_surface or VK_KHR_android_surface, and its 156 device extensions do
// not include VK_KHR_swapchain. Those are supplied by the loader, which is why
// Zink and ANGLE only ever observe WSI through libvulkan.so. Driving the HAL
// module directly therefore gets a working device and no way to present:
// vkCreateAndroidSurfaceKHR does not resolve, and the process dies on the
// first surface.
//
// The loader reaches a driver through android_load_sphal_library(), which
// searches fixed system directories. A driver plugin under
// /data/app/.../lib/arm64 is unreachable from there, so the loader cannot be
// pointed at Turnip by path or by environment variable.
//
// The remaining route is the one libadrenotools uses: put a library exporting
// android_dlopen_ext and android_load_sphal_library into the same linker
// namespace as libvulkan.so, loaded before it, so those are the definitions
// libvulkan.so binds to. When the loader then asks for the vendor HAL module,
// the request is answered with the driver of our choosing. It has to be a
// distinct object because a library dlopen'd into a fresh namespace needs a
// real mapping in that namespace, and dlopen of one's own path returns the
// already-loaded instance rather than a second one.
//
// Everything is best effort: if the real functions cannot be found, or the
// driver cannot be loaded, the real implementation is used instead and the
// platform driver stays in play.

#include <android/dlext.h>
#include <dlfcn.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

typedef void* (*dlopen_ext_fn)(const char*, int, const android_dlextinfo*);
typedef void* (*load_sphal_fn)(const char*, int);
typedef struct android_namespace_t* (*create_ns_fn)(const char*, const char*,
                                                     const char*, uint64_t,
                                                     const char*,
                                                     struct android_namespace_t*,
                                                     const void*);
typedef bool (*link_ns_fn)(struct android_namespace_t*, struct android_namespace_t*, const char*);

dlopen_ext_fn   g_real_dlopen_ext = nullptr;
load_sphal_fn   g_real_load_sphal = nullptr;
create_ns_fn    g_real_create_ns  = nullptr;
link_ns_fn      g_real_link_ns    = nullptr;

struct android_namespace_t* g_driver_ns = nullptr;
const void*          g_caller    = nullptr;

char g_driver_dir[512] = {0};
char g_driver_name[256] = {0};

bool g_trace = false;
int  g_redirects = 0;

void trace(const char* fmt, ...) {
    if (!g_trace) return;
    va_list ap;
    va_start(ap, fmt);
    std::fprintf(stderr, "[mithril vkhook] ");
    std::vfprintf(stderr, fmt, ap);
    std::fprintf(stderr, "\n");
    va_end(ap);
}

void resolve_real() {
    if (g_real_dlopen_ext) return;

    // libdl.so carries the real implementations. An explicit handle is used
    // rather than RTLD_DEFAULT because this object exports the same names and
    // would otherwise find itself.
    void* libdl = dlopen("libdl.so", RTLD_NOW | RTLD_LOCAL);
    if (!libdl) {
        trace("dlopen(\"libdl.so\") failed: %s", dlerror());
        return;
    }
    g_real_dlopen_ext = (dlopen_ext_fn)dlsym(libdl, "android_dlopen_ext");
    g_real_load_sphal = (load_sphal_fn)dlsym(libdl, "android_load_sphal_library");
    // The exported wrappers validate the caller address against the caller's
    // namespace; the internal entry points accept it as a plain argument, which
    // is the only way to create a namespace from a dlopen'd library.
    g_real_create_ns = (create_ns_fn)dlsym(libdl, "__loader_android_create_namespace");
    g_real_link_ns = (link_ns_fn)dlsym(libdl, "__loader_android_link_namespaces");

    // A caller address inside libdl.so itself: the public create-namespace
    // entry would instead pass our own return address, and there is no
    // namespace whose accessible list covers a driver plugin directory.
    // dli_fbase is the mapping address of the object that defines dlopen,
    // i.e. libdl.so - an address inside it satisfies the check.
    Dl_info info{};
    if (dladdr((void*)&dlopen, &info) && info.dli_fbase) g_caller = info.dli_fbase;

    trace("real functions resolved: dlopen_ext=%s load_sphal=%s create_ns=%s",
          g_real_dlopen_ext ? "yes" : "no",
          g_real_load_sphal ? "yes" : "no",
          g_real_create_ns ? "yes" : "no");
}

const char* base_name(const char* path) {
    const char* slash = std::strrchr(path, '/');
    return slash ? slash + 1 : path;
}

// The loader asks for the vendor HAL module by a board-specific name such as
// vulkan.adreno.so or vulkan.sm6375.so. Anything shaped like a Vulkan HAL
// module is a request we should answer; anything else - most importantly
// libvulkan.so's own dependencies - must pass through untouched, or the
// loader fails to initialise at all.
bool is_vulkan_hal(const char* name) {
    if (!name) return false;
    const char* base = base_name(name);
    if (std::strncmp(base, "vulkan.", 7) != 0) return false;
    const size_t len = std::strlen(base);
    return len > 3 && std::strcmp(base + len - 3, ".so") == 0;
}

struct android_namespace_t* driver_namespace() {
    if (g_driver_ns || !g_real_create_ns || !g_caller) return g_driver_ns;

    // The driver's own DT_NEEDED entries - libhardware.so, libcutils.so, the
    // ones that end at /dev/kgsl-3d0 - live in /system/lib64, which no
    // app-created namespace searches by default.
    const char* search = "/system/lib64:/vendor/lib64:/system/lib64/hw:"
                         "/vendor/lib64/hw:/apex/com.android.runtime/lib64/bionic";
    std::string ld;
    ld.reserve(1024);
    if (g_driver_dir[0]) {
        ld += g_driver_dir;
        ld += ":";
    }
    ld += search;

    g_driver_ns = (struct android_namespace_t*)g_real_create_ns(
        "mithril-driver", ld.c_str(), ld.c_str(),
        ANDROID_NAMESPACE_TYPE_ISOLATED | ANDROID_NAMESPACE_TYPE_SHARED,
        nullptr, nullptr, g_caller);

    // Without the link the driver cannot see the system libraries it needs, and
    // it fails with "library libhardware.so not found" - the failure seen
    // before any of this was in place.
    if (g_driver_ns && g_real_link_ns) {
        g_real_link_ns(g_driver_ns, nullptr, nullptr);
    }
    trace("driver namespace %s", g_driver_ns ? "created" : "NOT created");
    return g_driver_ns;
}

// Load the chosen driver in place of whatever HAL module was requested.
void* load_custom_driver(const char* requested) {
    if (g_driver_dir[0] == '\0' && g_driver_name[0] == '\0') return nullptr;

    std::string path;
    path.reserve(1024);
    if (g_driver_name[0]) {
        // An explicit name is used verbatim when it is already a path.
        if (std::strchr(g_driver_name, '/')) {
            path = g_driver_name;
        } else if (g_driver_dir[0]) {
            path = g_driver_dir;
            path += "/";
            path += g_driver_name;
        } else {
            path = g_driver_name;
        }
    } else if (g_driver_dir[0]) {
        path = g_driver_dir;
        path += "/libvulkan_freedreno.so";
    }

    android_dlextinfo ext{};
    ext.flags = ANDROID_DLEXT_USE_NAMESPACE;
    ext.library_namespace = driver_namespace();

    void* h = nullptr;
    if (g_real_dlopen_ext) {
        h = g_real_dlopen_ext(path.c_str(), RTLD_NOW | RTLD_LOCAL, &ext);
    }
    if (!h) {
        // Fall back to a plain dlopen so a driver reachable via
        // LD_LIBRARY_PATH still has a chance.
        h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    }
    trace("redirected \"%s\" -> \"%s\": %s", requested, path.c_str(),
          h ? "loaded" : (dlerror() ? dlerror() : "failed"));
    if (h) ++g_redirects;
    return h;
}

}  // namespace

extern "C" {

// Parameter hand-off from libmithril.so, which knows the driver directory and
// name but must not hold linker state of its own.
__attribute__((visibility("default")))
void mithril_vkhook_init(const char* driver_dir, const char* driver_name, int trace_on) {
    g_trace = trace_on != 0;
    if (driver_dir) {
        std::strncpy(g_driver_dir, driver_dir, sizeof(g_driver_dir) - 1);
    }
    if (driver_name) {
        std::strncpy(g_driver_name, driver_name, sizeof(g_driver_name) - 1);
    }
    resolve_real();
    trace("init: dir=%s name=%s", g_driver_dir, g_driver_name);
}

__attribute__((visibility("default")))
int mithril_vkhook_redirects(void) {
    return g_redirects;
}

__attribute__((visibility("default")))
void* android_dlopen_ext(const char* filename, int flags, const android_dlextinfo* info) {
    resolve_real();
    if (is_vulkan_hal(filename)) {
        void* h = load_custom_driver(filename);
        if (h) return h;
    }
    if (g_real_dlopen_ext) return g_real_dlopen_ext(filename, flags, info);
    return dlopen(filename, flags);
}

__attribute__((visibility("default")))
void* android_load_sphal_library(const char* filename, int flags) {
    resolve_real();
    if (is_vulkan_hal(filename)) {
        void* h = load_custom_driver(filename);
        if (h) return h;
    }
    if (g_real_load_sphal) return g_real_load_sphal(filename, flags);
    return dlopen(filename, flags);
}

}  // extern "C"
