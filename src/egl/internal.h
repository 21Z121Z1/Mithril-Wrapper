#pragma once

#include <EGL/egl.h>
#include <EGL/eglext.h>

#ifndef EGL_NO_CONFIG
#define EGL_NO_CONFIG EGL_CAST(EGLConfig, 0)
#endif

namespace mithril::egl {

// The single display object Mithril manages. Handles handed out to the app
// are opaque pointers into the singleton.
struct Display {
    bool initialized = false;
    EGLint major = 0;
    EGLint minor = 0;
};

struct Context {
    EGLConfig config = EGL_NO_CONFIG;
    bool drawable_state_initialized = false;
};

struct Surface {
    EGLConfig config = EGL_NO_CONFIG;
    void* native_window = nullptr;
    bool is_window = false;
    EGLint swap_interval = 1;
};

// Global registry: one display, one config, shareable objects.
struct Globals {
    Display display;
    Context context;
    Surface surface;
    EGLint error = EGL_SUCCESS;
};

Globals& globals();

// --- config model --------------------------------------------------------
// We expose exactly one config satisfying the Amethyst contract:
//   RGBA8 + depth24 + WINDOW|PBUFFER + OPENGL_BIT
bool ConfigMatches(const EGLint* attrib_list);
EGLConfig ConfigToken();

// --- internal helpers ----------------------------------------------------
void SetError(EGLenum err);
EGLint GetNativeVisualId();

} // namespace mithril::egl
