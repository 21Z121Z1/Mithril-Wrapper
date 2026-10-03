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
// EGL_OPENGL_BIT | EGL_OPENGL_ES2_BIT | EGL_OPENGL_ES3_BIT.
//
// ES2 is not optional. Launchers that drive us through their GL bridge ask for
// it explicitly: FCL's gl_bridge.c gl_init_context() builds
//   { ..., EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE }
// and bails out with "eglChooseConfig_p() found no matching config" when
// nothing matches, which kills context creation before any GL call happens.
// Advertising only ES3 left that request unsatisfiable even though an ES3
// config can serve an ES2 client.
//
// EGL_RENDERABLE_TYPE is a bitmask (EGL 1.5 3.4.1.2), so advertising all three
// is spec-compliant and lets one config table satisfy desktop-GL, ES2 and ES3
// host bridges alike.
constexpr EGLint kRenderableTypes = EGL_OPENGL_BIT | EGL_OPENGL_ES2_BIT | EGL_OPENGL_ES3_BIT;

// FCL queries EGL_NATIVE_VISUAL_ID and passes it directly to
// ANativeWindow_setBuffersGeometry(). Android EGL implementations map an
// RGBA8 config to HAL/AHardwareBuffer RGBA_8888 (numeric value 1). Returning
// zero asks ANativeWindow to restore its default format, which disconnects the
// selected EGL config from the actual window format.
#if defined(__ANDROID__)
constexpr EGLint kNativeVisualId = 1; // AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM
constexpr EGLint kNativeVisualType = EGL_NONE;
#else
constexpr EGLint kNativeVisualId = 0;
constexpr EGLint kNativeVisualType = 0;
#endif

EglConfig g_configs[kNumConfigs] = {
    // id=1: RGBA8 + D24S8 (the config Amethyst requests for MC Java)
    { 8, 8, 8, 8, 24, 8,  EGL_WINDOW_BIT | EGL_PBUFFER_BIT, kRenderableTypes, 1 },
    // id=2: RGBA8 + D24 (no stencil)
    { 8, 8, 8, 8, 24, 0,  EGL_WINDOW_BIT | EGL_PBUFFER_BIT, kRenderableTypes, 2 },
    // id=3: RGBA8 + S8 (no depth)
    { 8, 8, 8, 8, 0,  8,  EGL_WINDOW_BIT | EGL_PBUFFER_BIT, kRenderableTypes, 3 },
    // id=4: RGBA8 only
    { 8, 8, 8, 8, 0,  0,  EGL_WINDOW_BIT | EGL_PBUFFER_BIT, kRenderableTypes, 4 },
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
            case EGL_BUFFER_SIZE:
                if (cfg->redSize + cfg->greenSize + cfg->blueSize + cfg->alphaSize < value)
                    return false;
                break;
            case EGL_RED_SIZE:        if (cfg->redSize       < value) return false; break;
            case EGL_GREEN_SIZE:      if (cfg->greenSize     < value) return false; break;
            case EGL_BLUE_SIZE:       if (cfg->blueSize      < value) return false; break;
            case EGL_ALPHA_SIZE:      if (cfg->alphaSize     < value) return false; break;
            case EGL_DEPTH_SIZE:      if (cfg->depthSize     < value) return false; break;
            case EGL_STENCIL_SIZE:    if (cfg->stencilSize   < value) return false; break;
            case EGL_SURFACE_TYPE:    if ((cfg->surfaceType & value) != value) return false; break;
            case EGL_RENDERABLE_TYPE:
            case EGL_CONFORMANT:
                if ((cfg->renderableType & value) != value) return false;
                break;
            case EGL_COLOR_BUFFER_TYPE: if (value != EGL_RGB_BUFFER) return false; break;
            case EGL_CONFIG_CAVEAT:     if (value != EGL_NONE) return false; break;
            case EGL_TRANSPARENT_TYPE:  if (value != EGL_NONE) return false; break;
            case EGL_LUMINANCE_SIZE:    if (value != 0) return false; break;
            case EGL_SAMPLE_BUFFERS:    if (value != 0) return false; break;
            case EGL_SAMPLES:           if (value != 0) return false; break;
            case EGL_CONFIG_ID:         if (cfg->configId != value) return false; break;
            case EGL_LEVEL:             if (value != 0) return false; break;
            case EGL_NATIVE_RENDERABLE: if (value != EGL_FALSE) return false; break;
            case EGL_NATIVE_VISUAL_ID:  if (value != kNativeVisualId) return false; break;
            case EGL_NATIVE_VISUAL_TYPE: if (value != kNativeVisualType) return false; break;
            case EGL_MAX_PBUFFER_WIDTH:  if (value > 16384) return false; break;
            case EGL_MAX_PBUFFER_HEIGHT: if (value > 16384) return false; break;
            case EGL_MAX_PBUFFER_PIXELS:
                if (value > 16384 * 16384) return false;
                break;
            case EGL_BIND_TO_TEXTURE_RGB:
            case EGL_BIND_TO_TEXTURE_RGBA:
                // Pbuffer texture binding is only a stub today. Report the
                // capability honestly so clients do not select a config based
                // on an operation that cannot actually be performed.
                if (value != EGL_FALSE) return false;
                break;
            default:
                // Unknown/extension attributes are tolerated for launcher
                // compatibility. Core attributes above are matched exactly.
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
        case EGL_BUFFER_SIZE:     return cfg->redSize + cfg->greenSize + cfg->blueSize + cfg->alphaSize;
        case EGL_LUMINANCE_SIZE:  return 0;
        case EGL_ALPHA_MASK_SIZE: return 0;
        case EGL_CONFIG_CAVEAT:   return EGL_NONE;
        case EGL_LEVEL:           return 0;
        case EGL_MAX_PBUFFER_WIDTH:  return 16384;
        case EGL_MAX_PBUFFER_HEIGHT: return 16384;
        case EGL_MAX_PBUFFER_PIXELS: return 16384 * 16384;
        case EGL_NATIVE_RENDERABLE:  return EGL_FALSE;
        case EGL_NATIVE_VISUAL_ID:   return kNativeVisualId;
        case EGL_NATIVE_VISUAL_TYPE: return kNativeVisualType;
        case EGL_SAMPLES:            return 0;
        case EGL_SAMPLE_BUFFERS:     return 0;
        case EGL_BIND_TO_TEXTURE_RGB:
        case EGL_BIND_TO_TEXTURE_RGBA:
            return EGL_FALSE;
        case EGL_TRANSPARENT_TYPE:   return EGL_NONE;
        case EGL_MIN_SWAP_INTERVAL:  return 0;
        case EGL_MAX_SWAP_INTERVAL:  return 1;
        default:                     return 0;
    }
}

} // namespace egl
} // namespace mithril
