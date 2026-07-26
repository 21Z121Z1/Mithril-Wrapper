// Mithril-Wrapper - egl/SurfaceX11.cpp
// Linux-only platform dispatch for EGL surface creation.
//
// Implements the surface_create() / surface_get_size() entry points declared
// in egl/egl.cpp. Compiled into the Linux build (CMake UNIX AND NOT APPLE
// guard); the native_window passed in is an X11 Window (XID). We use dlopen
// to resolve XGetGeometry lazily so the EGL core has no hard build-time
// dependency on X11 headers (the running host only needs libX11.so.6 to be
// reachable via dlopen). The X11 Window is passed through unchanged — the
// Vulkan backend wraps it via vkCreateXlibSurfaceKHR.
#include "../MG_Impl/Log.h"

#include <dlfcn.h>

namespace {

// Function-pointer types for the libX11 entry points we need. We avoid
// including <X11/Xlib.h> so the egl/ directory has no hard X11 header
// dependency; the symbols are resolved at runtime via dlopen.
using XOpenDisplayFn  = void* (*)(const char*);
using XGetGeometryFn  = int (*)(void*, unsigned long, void*, int*, int*,
                                unsigned int*, unsigned int*,
                                unsigned int*, unsigned int*);
using XCloseDisplayFn = int (*)(void*);

// Lazily dlopen libX11.so.6 and resolve the three entry points we use.
// Returns true on success and writes the function pointers to the out params.
// The dlopen handle is intentionally leaked (RTLD_LAZY | RTLD_GLOBAL would
// also work; we keep it simple — the host process lifetime owns it).
bool resolve_x11(XOpenDisplayFn* out_open, XGetGeometryFn* out_geom,
                 XCloseDisplayFn* out_close) {
    void* h = dlopen("libX11.so.6", RTLD_LAZY);
    if (!h) {
        // Try unversioned fallback (common on minimal Linux distros).
        h = dlopen("libX11.so", RTLD_LAZY);
    }
    if (!h) return false;
    *out_open  = reinterpret_cast<XOpenDisplayFn>(dlsym(h, "XOpenDisplay"));
    *out_geom  = reinterpret_cast<XGetGeometryFn>(dlsym(h, "XGetGeometry"));
    *out_close = reinterpret_cast<XCloseDisplayFn>(dlsym(h, "XCloseDisplay"));
    return *out_open && *out_geom;
}

// Query the geometry of an X11 window. Returns true on success and writes the
// window's width/height (px) to out_w/out_h.
bool query_window_size(unsigned long window, int* out_w, int* out_h) {
    XOpenDisplayFn  XOpenDisplay  = nullptr;
    XGetGeometryFn  XGetGeometry  = nullptr;
    XCloseDisplayFn XCloseDisplay = nullptr;
    if (!resolve_x11(&XOpenDisplay, &XGetGeometry, &XCloseDisplay)) return false;

    void* display = XOpenDisplay(nullptr);
    if (!display) return false;

    void*         root = nullptr;
    int           x = 0, y = 0;
    unsigned int  w = 0, h = 0, bw = 0, depth = 0;
    int ok = XGetGeometry(display, window, &root, &x, &y, &w, &h, &bw, &depth);
    if (XCloseDisplay) XCloseDisplay(display);
    if (!ok) return false;
    if (out_w) *out_w = (int)w;
    if (out_h) *out_h = (int)h;
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// surface_create: pass the X11 Window through; record its current size.
// Returns native_window unchanged (X11 path: backend_create_swapchain uses
// vkCreateXlibSurfaceKHR to wrap it). Returns nullptr on failure.
// ---------------------------------------------------------------------------
extern "C" void* surface_create(void* native_window, int* out_w, int* out_h) {
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    if (!native_window) return nullptr;

    if (!query_window_size((unsigned long)native_window, out_w, out_h)) {
        MITHRIL_LOG_WARN("egl", "SurfaceX11: XGetGeometry failed for window %p", native_window);
        return nullptr;
    }
    return native_window;
}

// ---------------------------------------------------------------------------
// surface_get_size: re-query the X11 window's current size via XGetGeometry.
// ---------------------------------------------------------------------------
extern "C" bool surface_get_size(void* native_window, int* out_w, int* out_h) {
    if (!native_window) return false;
    return query_window_size((unsigned long)native_window, out_w, out_h);
}
