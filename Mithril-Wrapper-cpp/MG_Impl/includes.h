// Mithril-Wrapper - MG_Impl/includes.h
// Central internal header pulled in by every GL-entry-point translation unit.
// Replaces the former gl/includes.h — Metal headers are gone; the backend is
// the Vulkan C API in MG_Backend/Backend.h.
#ifndef MITHRIL_INCLUDES_H
#define MITHRIL_INCLUDES_H

#include <GL/gl.h>

#include "Log.h"
#include "../MG_State/State.h"
#include "Framebuffer.h"
#include "../MG_Backend/Backend.h"

// Bring the global GL state pointer into the global namespace so that
// `extern "C"` GL entry points (which live in the global namespace) can refer
// to it as `g_state` without a `mithril::` qualifier.
using mithril::g_state;

#ifdef __cplusplus
extern "C" {
#endif

void proc_init(void);

#ifdef __cplusplus
}
#endif

// Lightweight guard placed at the top of each GL entry point.
//
// P0-1 fix: g_state is now thread_local.  If EGL has been initialised but the
// current thread has no current context, we must NOT create a phantom global
// state — GL calls in that situation should produce GL_INVALID_OPERATION per
// the spec.  Only create the implicit global state when EGL is not in use
// (e.g. headless unit tests that call GL directly without EGL).
#define MITHRIL_ENSURE_INIT() \
    do { \
        if (!::mithril::g_state) { \
            if (!::mithril::g_eglInitialized) ::proc_init(); \
        } \
    } while (0)

#endif // MITHRIL_INCLUDES_H
