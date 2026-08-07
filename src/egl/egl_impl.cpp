#include "egl/egl_internal.h"

#include <util/log.h>

namespace mithril::egl {

Globals& globals() {
    static Globals g;
    return g;
}

static const EGLint kNativeVisualId = 0x4D52; // "MR"

EGLConfig ConfigToken() {
    return reinterpret_cast<EGLConfig>(&globals().display);
}

void SetError(EGLenum err) {
    globals().error = err;
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
                if ((a[1] & EGL_OPENGL_BIT) == 0) return false;
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