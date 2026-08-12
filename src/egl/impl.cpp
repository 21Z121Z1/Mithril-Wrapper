#include "egl/internal.h"

#include <util/log.h>

namespace mithril::egl {

Globals& globals() {
    static Globals g;
    return g;
}

ThreadState& thread_state() {
    static thread_local ThreadState state;
    return state;
}

static const EGLint kNativeVisualId = 0x4D52; // "MR"

EGLConfig ConfigToken() {
    return reinterpret_cast<EGLConfig>(&globals().display);
}

void SetError(EGLenum err) {
    thread_state().error = err;
}

EGLint TakeError() {
    EGLint error = thread_state().error;
    thread_state().error = EGL_SUCCESS;
    return error;
}

void ResetThreadState() {
    thread_state() = ThreadState{};
}

EGLint SupportedRenderableTypes() {
    // Amethyst's Mithril bridge binds EGL_OPENGL_ES_API and requests the ES3
    // config bit before loading Mithril's desktop OpenGL 3.3 exports.  Treat
    // that EGL negotiation as an explicit host compatibility alias; the GL
    // profile exposed to LWJGL remains OpenGL 3.3 Core.
    return EGL_OPENGL_BIT | EGL_OPENGL_ES3_BIT;
}

bool ConfigMatches(const EGLint* attrib_list) {
    if (!attrib_list) return true;
    for (const EGLint* a = attrib_list; *a != EGL_NONE; a += 2) {
        switch (a[0]) {
            case EGL_RED_SIZE:   if (a[1] > 8) return false; break;
            case EGL_GREEN_SIZE: if (a[1] > 8) return false; break;
            case EGL_BLUE_SIZE:  if (a[1] > 8) return false; break;
            case EGL_ALPHA_SIZE: if (a[1] > 8) return false; break;
            case EGL_DEPTH_SIZE: if (a[1] > 24) return false; break;
            case EGL_STENCIL_SIZE:
                if (a[1] > 0) return false;
                break;
            case EGL_SURFACE_TYPE:
                if ((a[1] & (EGL_WINDOW_BIT | EGL_PBUFFER_BIT)) == 0) return false;
                break;
            case EGL_RENDERABLE_TYPE:
                if (a[1] == 0 ||
                    (a[1] & ~SupportedRenderableTypes()) != 0)
                    return false;
                break;
            case EGL_COLOR_BUFFER_TYPE:
                if (a[1] != EGL_RGB_BUFFER) return false;
                break;
            default:
                break; // ignore unknown attributes (best-effort match)
        }
    }
    return true;
}

EGLint GetNativeVisualId() {
    return kNativeVisualId;
}

} // namespace mithril::egl
