// Linux headless EGL surface stub — pairs with SwapchainHeadless.cpp so the
// full dylib/so links on non-Apple CI boxes. surface_create() returns a
// dummy native window (a 1-byte allocation is enough; the headless swapchain
// ignores it), surface_get_size() reports the passed-in size.
#include <cstdlib>

extern "C" void* surface_create(void* native_window, int* out_w, int* out_h) {
    if (out_w) *out_w = 64;
    if (out_h) *out_h = 64;
    // Return a non-null token so egl.cpp treats the surface as valid.
    return (void*)0x1;
}

extern "C" bool surface_get_size(void* native_window, int* out_w, int* out_h) {
    if (out_w) *out_w = 64;
    if (out_h) *out_h = 64;
    return true;
}

extern "C" void surface_destroy(void* native_window) { (void)native_window; }
