// Mithril-Wrapper - egl/SurfaceAndroid.cpp
// Android platform entry for the surface_create() / surface_get_size() /
// surface_destroy() triple declared in EglInternal.h.
//
// On Android the native window handed to eglCreateWindowSurface() is an
// ANativeWindow* already, so unlike the Apple path there is no layer coercion
// to do: the host (a SurfaceView/SurfaceTexture backed Surface) owns the
// buffer queue and the window can be handed to Vulkan as-is. The only
// contract that matters is lifetime, because ANativeWindow is reference
// counted and the Java Surface can be released while we still hold the
// pointer - hence the acquire/release pairing below.
#if defined(__ANDROID__)

#include "EglInternal.h"

#include <android/native_window.h>

extern "C" void* surface_create(void* native_window, int* out_w, int* out_h) {
    ANativeWindow* win = static_cast<ANativeWindow*>(native_window);
    if (!win) return nullptr;

    // Take a reference so the buffer queue stays alive for as long as the
    // EGLSurface holds this pointer, independent of the Java Surface's own
    // lifetime.
    ANativeWindow_acquire(win);

    if (out_w) *out_w = static_cast<int>(ANativeWindow_getWidth(win));
    if (out_h) *out_h = static_cast<int>(ANativeWindow_getHeight(win));
    return static_cast<void*>(win);
}

extern "C" bool surface_get_size(void* native_window, int* out_w, int* out_h) {
    ANativeWindow* win = static_cast<ANativeWindow*>(native_window);
    if (!win || !out_w || !out_h) return false;

    const int32_t w = ANativeWindow_getWidth(win);
    const int32_t h = ANativeWindow_getHeight(win);
    // A window that has not been sized yet reports 0x0. Treat that as "not
    // ready" so the EGL layer defers swapchain creation instead of building
    // one with a degenerate extent.
    if (w <= 0 || h <= 0) return false;

    *out_w = static_cast<int>(w);
    *out_h = static_cast<int>(h);
    return true;
}

extern "C" void surface_destroy(void* native_window) {
    if (!native_window) return;
    ANativeWindow_release(static_cast<ANativeWindow*>(native_window));
}

#endif // __ANDROID__
