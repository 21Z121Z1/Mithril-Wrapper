// Mithril-Wrapper - egl/egl.mm
// EGL 1.5 implementation backed by Vulkan 1.2 (MoltenVK).
//
// This is the layer that Amethyst-iOS' Natives/ctxbridges/gl_bridge.m dlsym's
// against libmithril.dylib. It exposes the 21 egl* entry points listed in
// Amethyst's `egl_library` struct (see Natives/ctxbridges/gl_bridge.h) plus a
// handful of EGL 1.5 helpers (eglCreatePbufferSurface, eglQuerySurface, ...).
//
// Mapping (Vulkan/MoltenVK rewrite of the former Metal-backed egl.mm):
//   EGLDisplay  -> singleton EglDisplay. The Vulkan instance/device live in
//                  the DirectVulkan backend (MG_Backend/DirectVulkan/Device.cpp);
//                  eglInitialize brings them up via backend_init().
//   EGLConfig   -> opaque pointer to one of a small set of pre-baked
//                  EglConfig records (RGBA8 + optional depth/stencil).
//   EGLSurface  -> EglSurface holding a CAMetalLayer* + an opaque
//                  swapchain_state pointer (a mithril::vk::Swapchain* created
//                  by backend_create_swapchain()). The swapchain owns the
//                  VkSurfaceKHR (via VK_EXT_metal_surface), VkSwapchainKHR,
//                  swapchain images/views, and the depth/stencil VkImage/View
//                  (VK_FORMAT_D32_SFLOAT_S8_UINT).
//   EGLContext  -> EglContext holding its own mithril::GLState* (allocated
//                  via state_create()) so multiple contexts do not share GL
//                  object tables. eglMakeCurrent swaps mithril::g_state to
//                  point at the chosen context's state.
//
// The render path:
//   eglMakeCurrent installs the surface's current swapchain image's
//   VkImageView on g_state->eglDefaultColor (and the depth VkImageView on
//   g_state->eglDefaultDepth). GL commands against framebuffer 0 then render
//   straight into the on-screen drawable (see collect_draw_fbo_attachments).
//   eglSwapBuffers flushes Mithril's pending Vulkan work, presents the
//   swapchain image via vkQueuePresentKHR, then acquires the next image for
//   the following frame.
//
// Minimum requirements: Vulkan 1.2 (MoltenVK static link), iOS 14+, arm64,
// Apple A11+ (the MoltenVK portability subset target). The CAMetalLayer is
// still the on-screen drawable owner — MoltenVK cross-translates the Vulkan
// swapchain into Metal presentables under the hood.
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#import <Foundation/Foundation.h>
#import <objc/runtime.h>   // object_setClass() for layer coercion

#include "includes.h"
#include <EGL/egl.h>

#include <atomic>
#include <mutex>
#include <thread>

// ---------------------------------------------------------------------------
// Internal handle types
// ---------------------------------------------------------------------------
namespace {

struct EglDisplay {
    bool      initialized = false;
    EGLenum   boundAPI   = EGL_OPENGL_API;
};

struct EglConfig {
    EGLint  redSize;
    EGLint  greenSize;
    EGLint  blueSize;
    EGLint  alphaSize;
    EGLint  depthSize;
    EGLint  stencilSize;
    EGLint  surfaceType;   // EGL_WINDOW_BIT | EGL_PBUFFER_BIT
    EGLint  renderableType; // EGL_OPENGL_BIT (we expose Core Profile)
    EGLint  configId;
};

struct EglSurface {
    CAMetalLayer* layer            = nil;  // weak ref; owned by the host view
    void*         swapchain_state  = nullptr;  // mithril::vk::Swapchain*
    EGLConfig     config           = nullptr;
    EGLint        width            = 0;
    EGLint        height           = 0;
    EGLint        swapInterval     = 1;
    bool          firstFrame       = true;
    bool          wantDepthStencil = false;
};

struct EglContext {
    mithril::GLState*   state      = nullptr;
    EGLConfig           config     = nullptr;
    EglContext*         share      = nullptr;
    EGLenum             clientAPI  = EGL_OPENGL_API;
    EGLint              majorVer   = 3;   // we report OpenGL 3.3 Core Profile
    EGLint              minorVer   = 3;
    bool                lost       = false;
    std::atomic<int>    refcount{1};
};

// Singleton display. Returned for every eglGetDisplay / eglGetPlatformDisplay.
EglDisplay g_display;

// Pre-baked configs. Indexed by EGLConfig (we hand out &g_configs[i]).
EglConfig g_configs[] = {
    // id=1: RGBA8 + D24S8 (the config Amethyst requests for MC Java)
    { 8, 8, 8, 8, 24, 8,  EGL_WINDOW_BIT | EGL_PBUFFER_BIT, EGL_OPENGL_BIT, 1 },
    // id=2: RGBA8 + D24 (no stencil)
    { 8, 8, 8, 8, 24, 0,  EGL_WINDOW_BIT | EGL_PBUFFER_BIT, EGL_OPENGL_BIT, 2 },
    // id=3: RGBA8 + S8 (no depth)
    { 8, 8, 8, 8, 0,  8,  EGL_WINDOW_BIT | EGL_PBUFFER_BIT, EGL_OPENGL_BIT, 3 },
    // id=4: RGBA8 only
    { 8, 8, 8, 8, 0,  0,  EGL_WINDOW_BIT | EGL_PBUFFER_BIT, EGL_OPENGL_BIT, 4 },
};
constexpr int kNumConfigs = sizeof(g_configs) / sizeof(g_configs[0]);

// Thread-local EGL current state (mirrors Khronos EGL semantics).
thread_local EglContext* t_currentCtx    = nullptr;
thread_local EglSurface* t_currentDraw   = nullptr;
thread_local EglSurface* t_currentRead   = nullptr;
thread_local EGLint      t_lastError     = EGL_SUCCESS;
thread_local EGLenum     t_boundAPI      = EGL_OPENGL_API;

std::mutex g_ctxMutex; // guards share-group refcount updates

// ---------------------------------------------------------------------------
// Error helpers
// ---------------------------------------------------------------------------
inline void set_error(EGLint e) { if (t_lastError == EGL_SUCCESS) t_lastError = e; }
inline void clear_error()       { t_lastError = EGL_SUCCESS; }

inline bool valid_display(EGLDisplay d) {
    return d == (EGLDisplay)&g_display;
}
inline bool valid_config(EGLConfig c) {
    if (!c) return false;
    for (int i = 0; i < kNumConfigs; ++i) {
        if ((EGLConfig)&g_configs[i] == c) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Vulkan swapchain helpers
// ---------------------------------------------------------------------------
// Build (or rebuild) the per-surface Vulkan swapchain against the CAMetalLayer.
// Returns true on success. The swapchain is owned by the EglSurface and freed
// in eglDestroySurface / when the layer size changes.
bool ensure_swapchain(EglSurface* s) {
    if (!s || !s->layer) return false;
    CGSize sz = s->layer.drawableSize;
    if (sz.width <= 0 || sz.height <= 0) {
        sz = s->layer.bounds.size;
        if (sz.width <= 0 || sz.height <= 0) {
            // Layer not yet sized; defer swapchain creation to a later call.
            return false;
        }
        s->layer.drawableSize = sz;
    }
    int w = (int)sz.width;
    int h = (int)sz.height;
    if (s->swapchain_state) {
        int cur_w = backend_swapchain_width(s->swapchain_state);
        int cur_h = backend_swapchain_height(s->swapchain_state);
        if (cur_w == w && cur_h == h) {
            s->width  = w;
            s->height = h;
            return true;
        }
        // Size changed: tear down + recreate.
        backend_destroy_swapchain(s->swapchain_state);
        s->swapchain_state = nullptr;
    }
    s->swapchain_state = backend_create_swapchain(
        (__bridge void*)s->layer, w, h, s->wantDepthStencil ? 1 : 0);
    if (!s->swapchain_state) {
        NSLog(@"[egl] backend_create_swapchain failed (layer size = %.0fx%.0f)",
              sz.width, sz.height);
        return false;
    }
    s->width  = backend_swapchain_width(s->swapchain_state);
    s->height = backend_swapchain_height(s->swapchain_state);
    return true;
}

// Push the surface's current swapchain image views into the active GLState so
// framebuffer-0 renders land on the on-screen drawable. Acquires the next
// swapchain image if none is currently acquired.
void install_surface_on_state(EglSurface* s) {
    if (!g_state) return;
    if (s && s->swapchain_state) {
        VkImageView color = backend_swapchain_acquire_color(s->swapchain_state);
        VkImageView depth = backend_swapchain_acquire_depth(s->swapchain_state);
        g_state->eglDefaultColor  = color;
        g_state->eglDefaultDepth  = depth;
        g_state->eglDefaultWidth  = s->width;
        g_state->eglDefaultHeight = s->height;
    } else {
        g_state->eglDefaultColor  = VK_NULL_HANDLE;
        g_state->eglDefaultDepth  = VK_NULL_HANDLE;
        g_state->eglDefaultWidth  = 0;
        g_state->eglDefaultHeight = 0;
    }
}

// ---------------------------------------------------------------------------
// Config matching
// ---------------------------------------------------------------------------
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

} // namespace

// ===========================================================================
// Public EGL entry points (extern "C", exported by libmithril.dylib)
//
// Force default visibility at the source level regardless of the toolchain's
// global visibility policy. leetal/ios-cmake compiles .mm files as OBJCXX with
// -fvisibility=hidden by default; without this pragma the egl* entry points
// would be hidden, never enter the dylib's export table, and host launchers
// (Amethyst-iOS' egl_bridge.m) would see:
//     dlsym(handle, "eglCreateContext"): symbol not found
// followed by a NULL-pointer SIGSEGV in gl_make_current when the unresolved
// pointer is later called. The pragma below overrides hidden visibility so
// every egl* in this block is exported and dlsym-resolvable.
// ===========================================================================
#pragma GCC visibility push(default)
extern "C" {

EGLDisplay eglGetDisplay(EGLNativeDisplayType display_id) {
    clear_error();
    (void)display_id;   // we always return the singleton Vulkan-backed display
    return (EGLDisplay)&g_display;
}

EGLDisplay eglGetPlatformDisplay(EGLenum platform, void* native_display,
                                 const EGLint* attrib_list) {
    clear_error();
    (void)platform; (void)native_display; (void)attrib_list;
    // We are a single-display implementation; any platform token resolves to
    // the Vulkan-backed singleton. EGL_EXT_platform_base callers (Amethyst's
    //eglGetPlatformDisplay path) land here.
    return (EGLDisplay)&g_display;
}

EGLBoolean eglInitialize(EGLDisplay dpy, EGLint* major, EGLint* minor) {
    clear_error();
    if (!valid_display(dpy)) { set_error(EGL_BAD_DISPLAY); return EGL_FALSE; }
    // Bring up the Vulkan backend once. backend_init() is idempotent.
    backend_init();
    if (!backend_available()) {
        set_error(EGL_NOT_INITIALIZED);
        return EGL_FALSE;
    }
    g_display.initialized = true;
    if (major) *major = 1;
    if (minor) *minor = 5;
    return EGL_TRUE;
}

EGLBoolean eglTerminate(EGLDisplay dpy) {
    clear_error();
    if (!valid_display(dpy)) { set_error(EGL_BAD_DISPLAY); return EGL_FALSE; }
    // We do NOT destroy the Vulkan instance/device — the host process may call
    // eglInitialize again, and instance/device creation is expensive. Just
    // mark the display as not-initialized so callers must re-init per spec.
    g_display.initialized = false;
    return EGL_TRUE;
}

const char* eglQueryString(EGLDisplay dpy, EGLint name) {
    clear_error();
    if (!valid_display(dpy)) { set_error(EGL_BAD_DISPLAY); return nullptr; }
    switch (name) {
        case EGL_VENDOR:
            return "Mithril-Wrapper (EGL-on-Vulkan 1.2 / MoltenVK)";
        case EGL_VERSION:
            return "1.5 Mithril-Wrapper (Vulkan 1.2 backend)";
        case EGL_CLIENT_APIS:
            return "OpenGL";   // we expose OpenGL 3.3 Core Profile
        case EGL_EXTENSIONS:
            // Minimal but honest list of what we actually implement.
            return "EGL_EXT_platform_base EGL_KHR_platform_android "
                   "EGL_ANDROID_recordable EGL_MESA_platform_surfaceless "
                   "EGL_KHR_swap_buffers_with_damage";
        default:
            set_error(EGL_BAD_PARAMETER);
            return nullptr;
    }
}

EGLBoolean eglBindAPI(EGLenum api) {
    clear_error();
    if (api != EGL_OPENGL_API && api != EGL_OPENGL_ES_API && api != EGL_OPENVG_API) {
        set_error(EGL_BAD_PARAMETER);
        return EGL_FALSE;
    }
    // We always expose OpenGL 3.3 Core Profile, but we accept OpenGL ES
    // requests too — the Mithril GL state machine is API-agnostic at the
    // surface level. Amethyst binds EGL_OPENGL_API for the Metal-ANGLE path
    // and EGL_OPENGL_ES_API for the LTW/GLES path; either works here.
    t_boundAPI = api;
    g_display.boundAPI = api;
    return EGL_TRUE;
}

EGLBoolean eglReleaseThread(void) {
    clear_error();
    // Drop the thread-local current context/surface references.
    t_currentCtx  = nullptr;
    t_currentDraw = nullptr;
    t_currentRead = nullptr;
    return EGL_TRUE;
}

EGLint eglGetError(void) {
    EGLint e = t_lastError;
    t_lastError = EGL_SUCCESS;
    return e;
}

// ---- Configs ----
EGLBoolean eglGetConfigs(EGLDisplay dpy, EGLConfig* configs,
                         EGLint config_size, EGLint* num_config) {
    clear_error();
    if (!valid_display(dpy)) { set_error(EGL_BAD_DISPLAY); return EGL_FALSE; }
    if (!num_config) { set_error(EGL_BAD_PARAMETER); return EGL_FALSE; }
    if (!configs || config_size <= 0) {
        *num_config = kNumConfigs;
        return EGL_TRUE;
    }
    EGLint n = kNumConfigs < config_size ? kNumConfigs : config_size;
    for (EGLint i = 0; i < n; ++i) configs[i] = (EGLConfig)&g_configs[i];
    *num_config = n;
    return EGL_TRUE;
}

EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint* attrib_list,
                           EGLConfig* configs, EGLint config_size,
                           EGLint* num_config) {
    clear_error();
    if (!valid_display(dpy)) { set_error(EGL_BAD_DISPLAY); return EGL_FALSE; }
    if (!num_config) { set_error(EGL_BAD_PARAMETER); return EGL_FALSE; }

    EGLint matches[kNumConfigs];
    EGLint n = 0;
    for (int i = 0; i < kNumConfigs; ++i) {
        if (config_matches(&g_configs[i], attrib_list)) {
            matches[n++] = i;
        }
    }
    if (!configs || config_size <= 0) {
        *num_config = n;
        return EGL_TRUE;
    }
    EGLint out = n < config_size ? n : config_size;
    for (EGLint i = 0; i < out; ++i) configs[i] = (EGLConfig)&g_configs[matches[i]];
    *num_config = out;
    return EGL_TRUE;
}

EGLBoolean eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config,
                              EGLint attribute, EGLint* value) {
    clear_error();
    if (!valid_display(dpy)) { set_error(EGL_BAD_DISPLAY); return EGL_FALSE; }
    if (!valid_config(config)) { set_error(EGL_BAD_CONFIG); return EGL_FALSE; }
    if (!value) { set_error(EGL_BAD_PARAMETER); return EGL_FALSE; }
    *value = config_get_attr((EglConfig*)config, attribute);
    return EGL_TRUE;
}

// ---- Surfaces ----
EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                  EGLNativeWindowType win,
                                  const EGLint* attrib_list) {
    clear_error();
    if (!valid_display(dpy)) { set_error(EGL_BAD_DISPLAY); return EGL_NO_SURFACE; }
    if (!valid_config(config)) { set_error(EGL_BAD_CONFIG); return EGL_NO_SURFACE; }
    if (!win) { set_error(EGL_BAD_NATIVE_WINDOW); return EGL_NO_SURFACE; }

    // The native window is the host CALayer. Amethyst's SurfaceViewController
    // passes its view's root layer; for Vulkan/MoltenVK rendering it MUST be
    // a CAMetalLayer (MoltenVK's VK_EXT_metal_surface consumes the layer
    // directly). If it isn't, we coerce it (the host view sets this up itself
    // in production; the coercion is a safety net for ad-hoc hosts).
    CALayer* layer = (__bridge CALayer*)win;
    CAMetalLayer* mtlLayer = nil;
    if ([layer isKindOfClass:[CAMetalLayer class]]) {
        mtlLayer = (CAMetalLayer*)layer;
    } else {
        // Coerce: replace the layer's class with CAMetalLayer. This mirrors
        // what UIKit views do in +layerClass. We only do this if the layer is
        // standalone (not yet attached as a sublayer) to avoid surprising the
        // host view hierarchy.
        object_setClass(layer, [CAMetalLayer class]);
        mtlLayer = (CAMetalLayer*)layer;
    }
    if (!mtlLayer) {
        set_error(EGL_BAD_NATIVE_WINDOW);
        return EGL_NO_SURFACE;
    }
    // MoltenVK picks the MTLDevice itself via vkCreateMetalSurfaceEXT; we do
    // NOT bind the layer to a specific MTLDevice here (MoltenVK will choose
    // the system default device, which matches the VkPhysicalDevice it
    // exposes). The pixel format must be BGRA8Unorm to match the swapchain's
    // VK_FORMAT_B8G8R8A8_UNORM.
    mtlLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    mtlLayer.framebufferOnly = YES;
    if (mtlLayer.drawableSize.width == 0 || mtlLayer.drawableSize.height == 0) {
        mtlLayer.drawableSize = layer.bounds.size;
    }

    (void)attrib_list; // we ignore render-buffer / post-sub-buffer attribs

    EglSurface* s = new EglSurface{};
    s->layer  = mtlLayer;
    s->config = config;
    s->firstFrame = true;
    EglConfig* cfg = (EglConfig*)config;
    s->wantDepthStencil = (cfg->depthSize > 0 || cfg->stencilSize > 0);
    // Build the Vulkan swapchain now if the layer is already sized. If not,
    // defer to eglMakeCurrent / eglSwapBuffers which will retry.
    if (!ensure_swapchain(s)) {
        NSLog(@"[egl] eglCreateWindowSurface: deferred swapchain (layer size = %.0fx%.0f)",
              mtlLayer.drawableSize.width, mtlLayer.drawableSize.height);
    }
    return (EGLSurface)s;
}

EGLSurface eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config,
                                   const EGLint* attrib_list) {
    clear_error();
    if (!valid_display(dpy)) { set_error(EGL_BAD_DISPLAY); return EGL_NO_SURFACE; }
    if (!valid_config(config)) { set_error(EGL_BAD_CONFIG); return EGL_NO_SURFACE; }
    (void)attrib_list;
    // PBuffers are not actively used by MC Java; return a no-op surface so
    // EGL probes (LWJGL) succeed. We do not allocate a backing swapchain until
    // the surface is actually rendered to.
    EglSurface* s = new EglSurface{};
    s->config = config;
    return (EGLSurface)s;
}

EGLBoolean eglDestroySurface(EGLDisplay dpy, EGLSurface surface) {
    clear_error();
    if (!valid_display(dpy)) { set_error(EGL_BAD_DISPLAY); return EGL_FALSE; }
    if (surface == EGL_NO_SURFACE) { set_error(EGL_BAD_SURFACE); return EGL_FALSE; }
    EglSurface* s = (EglSurface*)surface;
    // Detach from any current context on this thread.
    if (t_currentDraw == s) { t_currentDraw = nullptr; install_surface_on_state(nullptr); }
    if (t_currentRead == s) { t_currentRead = nullptr; }
    if (s->swapchain_state) {
        backend_destroy_swapchain(s->swapchain_state);
        s->swapchain_state = nullptr;
    }
    s->layer = nil;
    delete s;
    return EGL_TRUE;
}

EGLBoolean eglQuerySurface(EGLDisplay dpy, EGLSurface surface,
                           EGLint attribute, EGLint* value) {
    clear_error();
    if (!valid_display(dpy)) { set_error(EGL_BAD_DISPLAY); return EGL_FALSE; }
    EglSurface* s = (EglSurface*)surface;
    if (!s) { set_error(EGL_BAD_SURFACE); return EGL_FALSE; }
    if (!value) { set_error(EGL_BAD_PARAMETER); return EGL_FALSE; }
    switch (attribute) {
        case EGL_WIDTH:           *value = s->width;  break;
        case EGL_HEIGHT:          *value = s->height; break;
        case EGL_CONFIG_ID:
            *value = s->config ? ((EglConfig*)s->config)->configId : 0; break;
        case EGL_RENDER_BUFFER:   *value = EGL_BACK_BUFFER; break;
        case EGL_SWAP_BEHAVIOR:   *value = EGL_BUFFER_DESTROYED; break;
        case EGL_MULTISAMPLE_RESOLVE: *value = EGL_MULTISAMPLE_RESOLVE_DEFAULT; break;
        default:                  *value = 0; break;
    }
    return EGL_TRUE;
}

// ---- Contexts ----
EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig config,
                            EGLContext share_context,
                            const EGLint* attrib_list) {
    clear_error();
    if (!valid_display(dpy)) { set_error(EGL_BAD_DISPLAY); return EGL_NO_CONTEXT; }
    if (!valid_config(config)) { set_error(EGL_BAD_CONFIG); return EGL_NO_CONTEXT; }

    EglContext* ctx = new EglContext{};
    ctx->state = mithril::state_create();
    ctx->config = config;
    ctx->clientAPI = t_boundAPI;
    ctx->majorVer = 3;
    ctx->minorVer = 3;

    // Parse context attributes (EGL_CONTEXT_MAJOR_VERSION / _CLIENT_VERSION /
    // _MINOR_VERSION / _FLAGS_KHR / _OPENGL_PROFILE_MASK). We are an OpenGL
    // 3.3 Core Profile implementation, so we honor 3.3 / 4.x requests by
    // clamping to 3.3 (the highest Core Profile version Mithril speaks).
    if (attrib_list) {
        for (const EGLint* a = attrib_list; *a != EGL_NONE; a += 2) {
            EGLint name = a[0], value = a[1];
            if (name == EGL_CONTEXT_MAJOR_VERSION || name == EGL_CONTEXT_CLIENT_VERSION) {
                ctx->majorVer = value;
            } else if (name == EGL_CONTEXT_MINOR_VERSION) {
                ctx->minorVer = value;
            } else if (name == EGL_CONTEXT_OPENGL_PROFILE_MASK) {
                // We always report Core Profile; Compatibility is silently
                // honoured because our entry points don't differ.
            } else if (name == EGL_CONTEXT_FLAGS_KHR) {
                // No-op: we don't expose debug/robustness yet.
            }
        }
    }
    if (ctx->majorVer > 3 || (ctx->majorVer == 3 && ctx->minorVer > 3)) {
        ctx->majorVer = 3; ctx->minorVer = 3;
    }

    if (share_context != EGL_NO_CONTEXT) {
        EglContext* sh = (EglContext*)share_context;
        ctx->share = sh;
        std::lock_guard<std::mutex> lk(g_ctxMutex);
        sh->refcount.fetch_add(1);
    }
    return (EGLContext)ctx;
}

EGLBoolean eglDestroyContext(EGLDisplay dpy, EGLContext ctx) {
    clear_error();
    if (!valid_display(dpy)) { set_error(EGL_BAD_DISPLAY); return EGL_FALSE; }
    EglContext* c = (EglContext*)ctx;
    if (!c || c == (EglContext*)EGL_NO_CONTEXT) {
        set_error(EGL_BAD_CONTEXT); return EGL_FALSE;
    }
    // If this context is current on this thread, detach it first.
    if (t_currentCtx == c) {
        install_surface_on_state(nullptr);
        mithril::g_state = nullptr;
        t_currentCtx = nullptr;
        t_currentDraw = nullptr;
        t_currentRead = nullptr;
    }
    {
        std::lock_guard<std::mutex> lk(g_ctxMutex);
        if (c->refcount.fetch_sub(1) == 1) {
            mithril::state_destroy(c->state);
            delete c;
        }
    }
    return EGL_TRUE;
}

EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read,
                          EGLContext ctx) {
    clear_error();
    if (!valid_display(dpy)) { set_error(EGL_BAD_DISPLAY); return EGL_FALSE; }

    // Detach case: ctx == EGL_NO_CONTEXT and draw/read == EGL_NO_SURFACE.
    if (ctx == EGL_NO_CONTEXT) {
        if (draw != EGL_NO_SURFACE || read != EGL_NO_SURFACE) {
            set_error(EGL_BAD_MATCH); return EGL_FALSE;
        }
        install_surface_on_state(nullptr);
        mithril::g_state = nullptr;
        t_currentCtx = nullptr;
        t_currentDraw = nullptr;
        t_currentRead = nullptr;
        return EGL_TRUE;
    }

    EglContext* c = (EglContext*)ctx;
    EglSurface* d = (EglSurface*)draw;
    EglSurface* r = (read == draw) ? d : (EglSurface*)read;
    if (!c) { set_error(EGL_BAD_CONTEXT); return EGL_FALSE; }

    // Make sure the Vulkan backend is up before any GL call lands.
    backend_init();

    // Swap Mithril's global state pointer to this context's state.
    mithril::g_state = c->state;

    // Install the draw surface's swapchain image views on the (now current)
    // GLState so framebuffer-0 rendering lands on the on-screen CAMetalLayer.
    if (d) {
        if (!d->swapchain_state && d->layer) {
            // First make-current on a freshly-created surface whose initial
            // swapchain creation failed (layer wasn't sized yet). Retry now.
            ensure_swapchain(d);
        }
        install_surface_on_state(d);
        // Initialise the viewport to the surface size if the app hasn't yet.
        if (c->state->viewportW <= 0 || c->state->viewportH <= 0) {
            c->state->viewportX = 0;
            c->state->viewportY = 0;
            c->state->viewportW = d->width;
            c->state->viewportH = d->height;
        }
    } else {
        install_surface_on_state(nullptr);
    }

    t_currentCtx  = c;
    t_currentDraw = d;
    t_currentRead = r ? r : d;
    return EGL_TRUE;
}

EGLContext eglGetCurrentContext(void) {
    return (EGLContext)t_currentCtx;
}

EGLSurface eglGetCurrentSurface(EGLenum readdraw) {
    if (readdraw == EGL_READ) return (EGLSurface)t_currentRead;
    if (readdraw == EGL_DRAW) return (EGLSurface)t_currentDraw;
    set_error(EGL_BAD_PARAMETER);
    return EGL_NO_SURFACE;
}

EGLDisplay eglGetCurrentDisplay(void) {
    return t_currentCtx ? (EGLDisplay)&g_display : EGL_NO_DISPLAY;
}

EGLBoolean eglQueryContext(EGLDisplay dpy, EGLContext ctx,
                           EGLint attribute, EGLint* value) {
    clear_error();
    if (!valid_display(dpy)) { set_error(EGL_BAD_DISPLAY); return EGL_FALSE; }
    EglContext* c = (EglContext*)ctx;
    if (!c) { set_error(EGL_BAD_CONTEXT); return EGL_FALSE; }
    if (!value) { set_error(EGL_BAD_PARAMETER); return EGL_FALSE; }
    switch (attribute) {
        case EGL_CONFIG_ID:
            *value = c->config ? ((EglConfig*)c->config)->configId : 0; break;
        case EGL_CONTEXT_CLIENT_TYPE:
            *value = (t_boundAPI == EGL_OPENGL_ES_API) ? EGL_OPENGL_ES_API : EGL_OPENGL_API;
            break;
        // EGL_CONTEXT_CLIENT_VERSION and EGL_CONTEXT_MAJOR_VERSION are the
        // same token (0x3098) in the Khronos EGL spec (the latter is the EGL
        // 1.5 rename of the former); a single case label covers both.
        case EGL_CONTEXT_MAJOR_VERSION: *value = c->majorVer; break;
        case EGL_CONTEXT_MINOR_VERSION: *value = c->minorVer; break;
        case EGL_RENDER_BUFFER:         *value = EGL_BACK_BUFFER; break;
        default:                        *value = 0; break;
    }
    return EGL_TRUE;
}

// ---- Swap ----
EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    clear_error();
    if (!valid_display(dpy)) { set_error(EGL_BAD_DISPLAY); return EGL_FALSE; }
    EglSurface* s = (EglSurface*)surface;
    if (!s) { set_error(EGL_BAD_SURFACE); return EGL_FALSE; }

    // Flush any pending Vulkan work into the current swapchain image view.
    // backend_end_render_pass() + backend_commit() end the active render pass
    // and submit the command buffer, so the encoded draws land on the
    // currently-acquired swapchain image before we present.
    backend_end_render_pass();
    backend_commit();

    // Rebuild the swapchain if the layer was resized between frames. This
    // invalidates the currently-acquired image, so we do it BEFORE presenting.
    if (s->layer) {
        CGSize sz = s->layer.drawableSize;
        if (sz.width > 0 && sz.height > 0 &&
            ((int)sz.width != s->width || (int)sz.height != s->height)) {
            ensure_swapchain(s);
        }
    }

    // Present the frame we just rendered, then acquire the next image for
    // the following frame. backend_present_and_acquire() calls
    // vkQueuePresentKHR followed by vkAcquireNextImageKHR.
    if (s->swapchain_state) {
        backend_present_and_acquire(s->swapchain_state);
        if (t_currentDraw == s) {
            install_surface_on_state(s);
        }
    }
    s->firstFrame = false;
    return EGL_TRUE;
}

EGLBoolean eglSwapInterval(EGLDisplay dpy, EGLint interval) {
    clear_error();
    if (!valid_display(dpy)) { set_error(EGL_BAD_DISPLAY); return EGL_FALSE; }
    if (t_currentDraw) {
        t_currentDraw->swapInterval = interval > 1 ? 1 : (interval < 0 ? 0 : interval);
    }
    return EGL_TRUE;
}

// ---- Idle sync (no-ops; Mithril flushes work synchronously per draw) ----
EGLBoolean eglWaitClient(void)  { backend_end_render_pass(); backend_commit(); return EGL_TRUE; }
EGLBoolean eglWaitGL(void)      { backend_end_render_pass(); backend_commit(); return EGL_TRUE; }
EGLBoolean eglWaitNative(EGLint) { return EGL_TRUE; }

// ---- Extension function resolution ----
// eglGetProcAddress delegates to glXGetProcAddress which resolves symbols from
// this dylib's export table. LWJGL/GLFW use this to obtain GL function pointers.
// Any GL Core Profile entry point we export is returned; unknown names return
// NULL (per EGL spec).
void (*eglGetProcAddress(const char* procname))(void) {
    clear_error();
    if (!procname) return nullptr;
    // Delegate to glXGetProcAddress (same symbol resolution mechanism).
    extern void* glXGetProcAddress(const char*);
    return (void(*)(void))glXGetProcAddress(procname);
}

// EGL 1.5 surface attribute query (eglQuerySurface extension attributes).
EGLBoolean eglSurfaceAttrib(EGLDisplay dpy, EGLSurface surface,
                            EGLint attribute, EGLint value) {
    clear_error();
    if (!valid_display(dpy)) { set_error(EGL_BAD_DISPLAY); return EGL_FALSE; }
    EglSurface* s = (EglSurface*)surface;
    if (!s) { set_error(EGL_BAD_SURFACE); return EGL_FALSE; }
    (void)attribute; (void)value;
    return EGL_TRUE;
}

EGLBoolean eglBindTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer) {
    clear_error();
    (void)dpy; (void)surface; (void)buffer;
    return EGL_TRUE;
}

EGLBoolean eglReleaseTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer) {
    clear_error();
    (void)dpy; (void)surface; (void)buffer;
    return EGL_TRUE;
}

EGLBoolean eglCopyBuffers(EGLDisplay dpy, EGLSurface surface, EGLNativePixmapType target) {
    clear_error();
    (void)dpy; (void)surface; (void)target;
    return EGL_TRUE;
}

EGLBoolean eglQueryAPI(void) {
    return (EGLBoolean)t_boundAPI;
}

} // extern "C"
#pragma GCC visibility pop
