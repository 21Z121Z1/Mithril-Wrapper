// Mithril-Wrapper - MG_Impl/lookup.cpp
// glXGetProcAddress / glXGetProcAddressARB: resolve GL/GLX symbol names to
// function pointers exported by this very dylib. Mirrors the dlsym-based
// lookup used by MobileGlues' glx/lookup.cpp, but resolves against ourselves
// (RTLD_DEFAULT on Apple) so clients probing for any GL Core Profile entry
// point receive a real implementation pointer rather than NULL.
//
// Only OpenGL Core Profile functions are exposed. Pure GLX functions
// (glXCreateContext, glXMakeCurrent, etc.) are intentionally NOT exported —
// on iOS the host application drives the Vulkan-backed EGL layer directly.
//
// This is the Vulkan/MoltenVK rewrite of the former glx/lookup.cpp; the
// resolution mechanism is unchanged because it depends only on the dynamic
// linker, not on the backend.
#include "includes.h"

#include <cstdint>
#include <dlfcn.h>

extern "C" {

/*
 * Resolve `name` to a function pointer. On Apple, resolve against the image
 * that owns this resolver. RTLD_DEFAULT is not ownership-preserving: iOS may
 * already have OpenGLES loaded, and a same-named system symbol can win the
 * global lookup. Unknown names return NULL, which is the GLX spec behaviour.
 */
static void* lookup_symbol(const char* name) {
    if (!name) return nullptr;
    void* p = nullptr;
#if defined(__APPLE__)
    static void* selfHandle = []() -> void* {
        Dl_info info = {};
        const void* resolver = reinterpret_cast<const void*>(
            reinterpret_cast<uintptr_t>(&lookup_symbol));
        if (!dladdr(resolver, &info) || !info.dli_fname) return nullptr;
        return dlopen(info.dli_fname, RTLD_NOW | RTLD_LOCAL);
    }();
    if (selfHandle) p = dlsym(selfHandle, name);
#else
    p = dlsym(RTLD_DEFAULT, name);
#endif
    if (p) return p;
    // Some hosts probe for glX* entry points. We don't implement them, but
    // returning a generic no-op stub would mislead the caller into thinking
    // they have a working GLX. Spec-correct behaviour is to return NULL.
    return nullptr;
}

void* glXGetProcAddress(const char* name) {
    return lookup_symbol(name);
}

void* glXGetProcAddressARB(const char* name) {
    return lookup_symbol(name);
}

} // extern "C"
