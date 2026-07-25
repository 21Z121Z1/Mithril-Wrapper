// Mithril-Wrapper - MG_Impl/EGLConfig.h
// Pure-C++ extraction of the EGL config descriptor + attribute matching /
// lookup logic. Lives outside egl.mm so it can be unit-tested without an
// Objective-C++ toolchain.
//
// egl.mm #includes this header and treats `mithril::egl::g_configs` as its
// pre-baked config table; `EGLConfig` opaque pointers handed back to the host
// are pointers into `g_configs`. The matching + attribute-lookup helpers are
// pure data-table logic that does not touch any EGL display / surface / Vulkan
// state, so a unit test can exercise them directly.
#ifndef MITHRIL_EGLCONFIG_H
#define MITHRIL_EGLCONFIG_H

#include <EGL/egl.h>

namespace mithril {
namespace egl {

// Internal EGL config descriptor. One entry per pre-baked config in
// `g_configs`. The EGLConfig opaque pointer handed back to the host is a
// pointer to one of these records (`(EGLConfig)&g_configs[i]`).
struct EglConfig {
    EGLint  redSize;
    EGLint  greenSize;
    EGLint  blueSize;
    EGLint  alphaSize;
    EGLint  depthSize;
    EGLint  stencilSize;
    EGLint  surfaceType;    // EGL_WINDOW_BIT | EGL_PBUFFER_BIT
    EGLint  renderableType; // EGL_OPENGL_BIT (we expose Core Profile)
    EGLint  configId;
};

// Number of pre-baked configs in `g_configs`. Exposed as a constexpr so call
// sites that need a fixed-size local array (eglChooseConfig's match list) can
// size it at compile time without depending on sizeof(g_configs).
constexpr int kNumConfigs = 4;

// Pre-baked config table. Indexed by EGLConfig (we hand out &g_configs[i]).
// Defined in EGLConfig.cpp.
extern EglConfig g_configs[kNumConfigs];

// Match `cfg` against an EGL attribute list (a sequence of {name, value}
// pairs terminated by EGL_NONE). Returns true if the config satisfies every
// non-EGL_DONT_CARE constraint. `attribs` may be null (treated as "match
// all"). Used by eglChooseConfig.
bool config_matches(const EglConfig* cfg, const EGLint* attribs);

// Look up a single attribute value for `cfg`. Returns 0 for unknown tokens
// (mirrors EGL's tolerant query semantics). Used by eglGetConfigAttrib.
EGLint config_get_attr(const EglConfig* cfg, EGLint attr);

} // namespace egl
} // namespace mithril

#endif // MITHRIL_EGLCONFIG_H
