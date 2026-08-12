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
    EGLenum client_api = EGL_OPENGL_ES_API;
    EGLint client_version = 3;
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
};

// EGL client-API binding, error state and current handles are thread state.
// The renderer still owns one backend context, but reporting a context as
// current after eglMakeCurrent(..., EGL_NO_CONTEXT) breaks Amethyst's context
// lifecycle even in that single-context model.
struct ThreadState {
    EGLenum bound_api = EGL_OPENGL_ES_API;
    EGLDisplay current_display = EGL_NO_DISPLAY;
    EGLContext current_context = EGL_NO_CONTEXT;
    EGLSurface current_draw = EGL_NO_SURFACE;
    EGLSurface current_read = EGL_NO_SURFACE;
    EGLint error = EGL_SUCCESS;
};

Globals& globals();
ThreadState& thread_state();

// --- config model --------------------------------------------------------
// We expose exactly one config satisfying both direct desktop-GL callers and
// Amethyst's host negotiation alias:
//   RGBA8 + depth24 + WINDOW|PBUFFER + OPENGL_BIT|OPENGL_ES3_BIT
bool ConfigMatches(const EGLint* attrib_list);
EGLConfig ConfigToken();
EGLint SupportedRenderableTypes();

// --- internal helpers ----------------------------------------------------
void SetError(EGLenum err);
EGLint TakeError();
void ResetThreadState();
EGLint GetNativeVisualId();

} // namespace mithril::egl
