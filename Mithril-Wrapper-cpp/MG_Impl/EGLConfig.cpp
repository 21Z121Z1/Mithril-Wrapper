// Mithril-Wrapper - MG_Impl/EGLConfig.cpp
// Pure-C++ extraction of the EGL config descriptor + attribute matching /
// lookup logic. Extracted from egl.mm so the pure-logic helpers can be
// unit-tested without an Objective-C++ toolchain.
//
// This translation unit MUST be free of Objective-C / Objective-C++ usage so
// it compiles as plain C++ on Linux CI (egl.mm stays .mm and continues to own
// the CAMetalLayer / Vulkan swapchain glue).
#include "EGLConfig.h"

namespace mithril {
namespace egl {

// Pre-baked configs. Indexed by EGLConfig (we hand out &g_configs[i]).
//
// renderableType declares BOTH EGL_OPENGL_BIT and EGL_OPENGL_ES3_BIT:
//   - EGL_OPENGL_BIT    — the wrapper truly implements desktop GL 3.3 Core
//                         Profile (GL_VERSION = "3.3.0 Mithril-Wrapper").
//   - EGL_OPENGL_ES3_BIT — advertised so host EGL clients that probe for an
//                         ES3 config (e.g. Amethyst's gl_bridge.m in Mithril
//                         mode, where angleDesktopGL==NO) match successfully.
//                         eglBindAPI accepts both EGL_OPENGL_API and
//                         EGL_OPENGL_ES_API (egl.cpp::eglBindAPI), and the GL
//                         frontend exposes the desktop Core Profile entry
//                         points either way; the host sees GL_VERSION 3.3.0
//                         regardless of which client API bit it bound.
// EGL_RENDERABLE_TYPE is a bitmask (EGL 1.5 §3.4.1.2), so advertising both is
// spec-compliant and lets the same config satisfy ANGLE-style desktop-GL
// queries (EGL_OPENGL_BIT) and ES3 queries (EGL_OPENGL_ES3_BIT) from
// different host bridges without needing two config tables.
EglConfig g_configs[kNumConfigs] = {
    // id=1: RGBA8 + D24S8 (the config Amethyst requests for MC Java)
    { 8, 8, 8, 8, 24, 8,  EGL_WINDOW_BIT | EGL_PBUFFER_BIT, EGL_OPENGL_BIT | EGL_OPENGL_ES3_BIT, 1 },
    // id=2: RGBA8 + D24 (no stencil)
    { 8, 8, 8, 8, 24, 0,  EGL_WINDOW_BIT | EGL_PBUFFER_BIT, EGL_OPENGL_BIT | EGL_OPENGL_ES3_BIT, 2 },
    // id=3: RGBA8 + S8 (no depth)
    { 8, 8, 8, 8, 0,  8,  EGL_WINDOW_BIT | EGL_PBUFFER_BIT, EGL_OPENGL_BIT | EGL_OPENGL_ES3_BIT, 3 },
    // id=4: RGBA8 only
    { 8, 8, 8, 8, 0,  0,  EGL_WINDOW_BIT | EGL_PBUFFER_BIT, EGL_OPENGL_BIT | EGL_OPENGL_ES3_BIT, 4 },
};

// Match `cfg` against an EGL attribute list (a sequence of {name, value}
// pairs terminated by EGL_NONE). Returns true if the config satisfies every
// non-EGL_DONT_CARE constraint. `attribs` may be null (treated as "match
// all"). Used by eglChooseConfig.
bool config_matches(const EglConfig* cfg, const EGLint* attribs) {
    if (!attribs) return true;
    for (const EGLint* a = attribs; *a != EGL_NONE; a += 2) {
        EGLint name  = a[0];
        EGLint value = a[1];
        if (value == EGL_DONT_CARE) continue;
        switch (name) {
            case EGL_RED_SIZE:        if (cfg->redSize       < value) return false; break;
            case EGL_GREEN_SIZE:      if (cfg->greenSize     < value) return false; break;
            case EGL_BLUE_SIZE:       if (cfg->blueSize      < value) return false; break;
            case EGL_ALPHA_SIZE:      if (cfg->alphaSize     < value) return false; break;
            case EGL_DEPTH_SIZE:      if (cfg->depthSize     < value) return false; break;
            case EGL_STENCIL_SIZE:    if (cfg->stencilSize   < value) return false; break;
            case EGL_SURFACE_TYPE:    if ((cfg->surfaceType & value) != value) return false; break;
            case EGL_RENDERABLE_TYPE: if ((cfg->renderableType & value) != value) return false; break;
            case EGL_COLOR_BUFFER_TYPE: if (value != EGL_RGB_BUFFER) return false; break;
            // All pre-baked configs are non-transparent RGB buffers, so a
            // request for EGL_TRANSPARENT_RGB must reject them (config_get_attr
            // reports EGL_NONE for EGL_TRANSPARENT_TYPE). Without this case
            // the token fell through to `default` and the constraint was
            // silently ignored — a semantic mismatch with config_get_attr.
            case EGL_TRANSPARENT_TYPE: if (value != EGL_NONE) return false; break;
            // No pre-baked config carries a luminance buffer; only 0 matches.
            case EGL_LUMINANCE_SIZE:   if (value != 0) return false; break;
            case EGL_CONFIG_ID:       if (cfg->configId != value) return false; break;
            case EGL_LEVEL:           break; // ignored
            case EGL_NATIVE_RENDERABLE: break; // ignored
            case EGL_NATIVE_VISUAL_ID: break; // ignored
            case EGL_BIND_TO_TEXTURE_RGB:
            case EGL_BIND_TO_TEXTURE_RGBA:
                // We always permit texturing; ignore the constraint.
                break;
            default:
                // Unknown attribute — EGL says this is EGL_BAD_ATTRIBUTE,
                // but to be tolerant of extension tokens we ignore it.
                break;
        }
    }
    return true;
}

EGLint config_get_attr(const EglConfig* cfg, EGLint attr) {
    switch (attr) {
        case EGL_RED_SIZE:        return cfg->redSize;
        case EGL_GREEN_SIZE:      return cfg->greenSize;
        case EGL_BLUE_SIZE:       return cfg->blueSize;
        case EGL_ALPHA_SIZE:      return cfg->alphaSize;
        case EGL_DEPTH_SIZE:      return cfg->depthSize;
        case EGL_STENCIL_SIZE:    return cfg->stencilSize;
        case EGL_SURFACE_TYPE:    return cfg->surfaceType;
        case EGL_RENDERABLE_TYPE: return cfg->renderableType;
        case EGL_CONFORMANT:      return cfg->renderableType;
        case EGL_CONFIG_ID:       return cfg->configId;
        case EGL_COLOR_BUFFER_TYPE: return EGL_RGB_BUFFER;
        case EGL_BUFFER_SIZE:     return cfg->redSize + cfg->greenSize + cfg->blueSize;
        case EGL_LUMINANCE_SIZE:  return 0;
        case EGL_ALPHA_MASK_SIZE: return 0;
        case EGL_CONFIG_CAVEAT:   return EGL_NONE;
        case EGL_LEVEL:           return 0;
        case EGL_MAX_PBUFFER_WIDTH:  return 16384;
        case EGL_MAX_PBUFFER_PIXELS: return 16384 * 16384;
        case EGL_NATIVE_RENDERABLE:  return EGL_FALSE;
        // EGL_NATIVE_VISUAL_ID and EGL_MAX_PBUFFER_HEIGHT are the same token
        // (0x3030) in the Khronos EGL spec; EGL_NATIVE_VISUAL_TYPE and
        // EGL_SAMPLES share 0x3031. A config query at 0x3030 returns the
        // native visual id (0 — gl_bridge.m tolerates this), and 0x3031
        // returns the sample count (0 == no MSAA). One case label per value.
        case EGL_NATIVE_VISUAL_ID:   return 0;
        case EGL_SAMPLES:            return 0;
        case EGL_SAMPLE_BUFFERS:     return 0;
        case EGL_TRANSPARENT_TYPE:   return EGL_NONE;
        case EGL_MIN_SWAP_INTERVAL:  return 0;
        case EGL_MAX_SWAP_INTERVAL:  return 1;
        default:                     return 0;
    }
}

} // namespace egl
} // namespace mithril
