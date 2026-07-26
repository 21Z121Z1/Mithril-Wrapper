// Mithril-Wrapper - egl/SurfaceAndroid.cpp
// Android-only platform dispatch for EGL surface creation (stub).
//
// Implements the surface_create() / surface_get_size() entry points declared
// in egl/egl.cpp. Compiled into the Android build (CMake ANDROID guard); the
// native_window passed in is an ANativeWindow* ( jobject obtained from a
// Surface / SurfaceHolder / SurfaceTexture via ANativeWindow_fromSurface()).
// We pass it through unchanged — the Vulkan backend wraps it via
// vkCreateAndroidSurfaceKHR.
#include <android/native_window.h>

// ---------------------------------------------------------------------------
// surface_create: pass the ANativeWindow through; record its current size.
// Returns native_window unchanged. Returns nullptr on failure.
// ---------------------------------------------------------------------------
extern "C" void* surface_create(void* native_window, int* out_w, int* out_h) {
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    if (!native_window) return nullptr;

    ANativeWindow* win = (ANativeWindow*)native_window;
    if (out_w) *out_w = ANativeWindow_getWidth(win);
    if (out_h) *out_h = ANativeWindow_getHeight(win);
    return native_window;
}

// ---------------------------------------------------------------------------
// surface_get_size: re-query the ANativeWindow's current width/height.
// ---------------------------------------------------------------------------
extern "C" bool surface_get_size(void* native_window, int* out_w, int* out_h) {
    if (!native_window) return false;
    ANativeWindow* win = (ANativeWindow*)native_window;
    if (out_w) *out_w = ANativeWindow_getWidth(win);
    if (out_h) *out_h = ANativeWindow_getHeight(win);
    return true;
}
