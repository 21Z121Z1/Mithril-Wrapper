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

namespace mithril {
// Records the first call to each GL entry point that originated outside the
// Mithril dylib. Calls made by one Mithril GL entry point delegating to another
// are filtered using the caller image, so the production trace reflects the
// application/LWJGL surface rather than wrapper implementation details.
void semantic_trace_external_api_call(const char* api, const void* caller);
}

#ifdef __cplusplus
extern "C" {
#endif

void proc_init(void);

/* Conditional-render 门控（Stubs.cpp 实现，复用 query_get_result64 取遮挡
 * 查询结果）。draw / glClear / glClearBuffer* 入口先问它：false = 按
 * glBeginConditionalRender 语义丢弃本次操作。无条件渲染时恒 true。 */
bool mg_conditional_render_allows(void);

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
//
// FIX (root cause - headless backend never brought up):
// The load-time initialiser (static_block_t in init.cpp) calls state_init(),
// which allocates g_state. That makes the `!g_state` branch below false on the
// very first GL entry point, so proc_init() — the headless backend bring-up
// that owns backend_init() → init_device() → b->initialized=true — was never
// reached. The MoltenVK backend stayed uninitialized, so every backend_* call
// (glClear/glDrawArrays/glReadPixels) bailed on its `!b->initialized` guard and
// became a silent no-op → offscreen render read back all-black.
//
// Fix: on the headless path (EGL not in use) also run proc_init() when g_state
// already exists but the backend is not yet available. proc_init() is
// idempotent (static `done` + backend_init() idempotence), so this is safe.
// On the EGL/iOS path g_eglInitialized is true, this branch is skipped, and
// eglInitialize() owns backend bring-up exactly as before.
#if defined(__clang__) || defined(__GNUC__)
#define MITHRIL_CALLER_ADDRESS() __builtin_return_address(0)
#else
#define MITHRIL_CALLER_ADDRESS() nullptr
#endif

#define MITHRIL_ENSURE_INIT() \
    do { \
        if (!::mithril::g_state) { \
            if (!::mithril::g_eglInitialized) ::proc_init(); \
        } else if (!::mithril::g_eglInitialized && !backend_available()) { \
            ::proc_init(); \
        } \
        ::mithril::semantic_trace_external_api_call(__func__, MITHRIL_CALLER_ADDRESS()); \
    } while (0)

#endif // MITHRIL_INCLUDES_H
