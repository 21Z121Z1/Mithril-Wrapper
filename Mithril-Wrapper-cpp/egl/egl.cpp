// Mithril-Wrapper - egl/egl.cpp
// EGL 1.5 cross-platform core implementation backed by Vulkan 1.2.
//
// Platform-specific surface creation (CAMetalLayer coercion on Apple) is
// isolated in egl/Surface<Platform>.cpp/mm, selected by CMake based on the
// APPLE guard. This file stays pure C++ with no platform-specific includes.
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
//   EGLSurface  -> EglSurface holding a void* native_window + an opaque
//                  swapchain_state pointer (a mithril::vk::Swapchain* created
//                  by backend_create_swapchain()). The swapchain owns the
//                  VkSurfaceKHR (via VK_EXT_metal_surface), VkSwapchainKHR,
//                  swapchain
//                  images/views, and the depth/stencil VkImage/View
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

// includes.h lives in MG_Impl/ (sibling of egl/); use a relative path since
// the egl/ directory is not on the include search path and the quote-include
// lookup only checks the current file's directory + -I dirs.
#include "../MG_Impl/includes.h"
#include "../MG_Impl/EGLConfig.h"
#include "../MG_Impl/Log.h"
#include <EGL/egl.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_map>

// ---------------------------------------------------------------------------
// Platform dispatch (defined in egl/Surface<Platform>.cpp/mm, selected by CMake)
// ---------------------------------------------------------------------------
// surface_create() prepares the native window for use as a Vulkan surface and
// returns a void* native_window suitable for backend_create_swapchain():
//   - Apple:   CAMetalLayer* (after CALayer -> CAMetalLayer coercion)
// Returns nullptr on failure. out_w / out_h receive the window's current size
// (0 if undetermined).
//
// surface_get_size() queries the current size of a native_window previously
// returned by surface_create(). Returns false if the window is invalid or the
// size cannot be determined.
extern "C" void* surface_create(void* native_window, int* out_w, int* out_h);
extern "C" bool  surface_get_size(void* native_window, int* out_w, int* out_h);

// ---------------------------------------------------------------------------
// Internal handle types
// ---------------------------------------------------------------------------
namespace {

// Bring the extracted config table + matching helpers from
// mithril::egl (see MG_Impl/EGLConfig.h) into this TU's anonymous namespace
// so egl.cpp can keep referring to EglConfig / g_configs / kNumConfigs /
// config_matches / config_get_attr unqualified, exactly as it did before the
// extraction.
using mithril::egl::EglConfig;
using mithril::egl::g_configs;
using mithril::egl::kNumConfigs;
using mithril::egl::config_matches;
using mithril::egl::config_get_attr;

struct EglDisplay {
    bool      initialized = false;
    EGLenum   boundAPI   = EGL_OPENGL_API;
};

struct EglSurface {
    void*         native_window    = nullptr;  // CAMetalLayer* (weak ref; owned by host)
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

// EGL 1.5 sync object (shadow implementation: always signaled, no real GPU
// fence). Backed by a process-local handle so eglClientWaitSync/eglWaitSync
// can validate the handle without touching the Vulkan backend.
struct EglSync {
    EGLDisplay dpy       = EGL_NO_DISPLAY;
    EGLenum    type      = 0;
    EGLenum    condition = 0;
    EGLenum    status    = EGL_SIGNALED;
};

// EGL 1.5 image object (shadow implementation: records target + buffer only,
// no real VkImage import). Real interop will land with the Vulkan Image bind.
struct EglImage {
    EGLDisplay      dpy    = EGL_NO_DISPLAY;
    EGLenum         target = 0;
    EGLClientBuffer buffer = nullptr;
};

// Singleton display. Returned for every eglGetDisplay / eglGetPlatformDisplay.
EglDisplay g_display;

// Thread-local EGL current state (mirrors Khronos EGL semantics).
thread_local EglContext* t_currentCtx    = nullptr;
thread_local EglSurface* t_currentDraw   = nullptr;
thread_local EglSurface* t_currentRead   = nullptr;
thread_local EGLint      t_lastError     = EGL_SUCCESS;
thread_local EGLenum     t_boundAPI      = EGL_OPENGL_ES_API;

std::mutex g_ctxMutex; // guards share-group refcount updates

// EGL 1.5 sync/image handle tables (shadow implementations). Handles are
// process-local integers cast to EGLSync/EGLImage; 0 is reserved for
// EGL_NO_SYNC / EGL_NO_IMAGE.
static std::unordered_map<EGLSync, EglSync> g_syncs;
static uintptr_t g_nextSyncHandle = 1;
static std::unordered_map<EGLImage, EglImage> g_images;
static uintptr_t g_nextImageHandle = 1;

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
// Build (or rebuild) the per-surface Vulkan swapchain against the native
// window. Returns true on success. The swapchain is owned by the EglSurface
// and freed in eglDestroySurface / when the window size changes.
//
// On rebuild (size changed), this drains GPU work that references the old
// swapchain BEFORE destroying it, so the Metal driver is no longer reading
// the old IOSurface-backed images when they are torn down. Without this
// drain, vkDestroySwapchainKHR frees IOSurfaces that the GPU is still
// accessing, and the next IOSurfaceBindAccel call crashes with SIGSEGV (UAF).
bool ensure_swapchain(EglSurface* s) {
    if (!s || !s->native_window) return false;
    int w = 0, h = 0;
    if (!surface_get_size(s->native_window, &w, &h)) return false;
    if (w <= 0 || h <= 0) {
        // Window not yet sized; defer swapchain creation to a later call.
        return false;
    }
    if (s->swapchain_state) {
        int cur_w = backend_swapchain_width(s->swapchain_state);
        int cur_h = backend_swapchain_height(s->swapchain_state);
        if (cur_w == w && cur_h == h) {
            s->width  = w;
            s->height = h;
            return true;
        }
        // Size changed: drain GPU work referencing the old swapchain, detach
        // it from the encoder, THEN tear down + recreate. The drain is
        // critical: it ensures the Metal driver has released the old
        // IOSurface-backed drawables before vkDestroySwapchainKHR frees them.
        // Skipping the drain causes IOSurfaceBindAccel UAF crashes on the
        // next present.
        if (t_currentDraw == s) {
            backend_drain_and_detach_swapchain();
        }
        backend_destroy_swapchain(s->swapchain_state);
        s->swapchain_state = nullptr;
    }
    s->swapchain_state = backend_create_swapchain(
        s->native_window, w, h, s->wantDepthStencil ? 1 : 0, /*platform_hint=*/0);
    if (!s->swapchain_state) {
        MITHRIL_LOG_WARN("egl", "backend_create_swapchain failed (window size = %dx%d)", w, h);
        return false;
    }
    s->width  = backend_swapchain_width(s->swapchain_state);
    s->height = backend_swapchain_height(s->swapchain_state);
    return true;
}

// Push the surface's current swapchain image views into the active GLState so
// framebuffer-0 renders land on the on-screen drawable. Acquires the next
// swapchain image if none is currently acquired.
//
// Also registers the swapchain with the backend encoder (via
// backend_set_active_swapchain) so begin_render_pass()/commit_frame() can
// record the PRESENT_SRC/UNDEFINED <-> COLOR_ATTACHMENT_OPTIMAL layout barriers
// on the swapchain color image, the one-shot UNDEFINED ->
// DEPTH_STENCIL_ATTACHMENT_OPTIMAL barrier on the depth image, and signal the
// swapchain's pendingRenderFinished semaphore on submit. Without this
// registration, dynamic rendering would hard-code COLOR_ATTACHMENT_OPTIMAL on
// an image that is actually in PRESENT_SRC_KHR (or UNDEFINED on first use),
// which MoltenVK treats as an illegal layout and renders nothing (black screen).
void install_surface_on_state(EglSurface* s) {
    if (!g_state) return;
    if (s && s->swapchain_state) {
        VkImageView color = backend_swapchain_acquire_color(s->swapchain_state);
        VkImageView depth = backend_swapchain_acquire_depth(s->swapchain_state);
        MITHRIL_LOG_WARN("egl", "install_surface_on_state: color=%p depth=%p "
                          "(swapchain_state=%p)", (void*)color, (void*)depth,
                          (void*)s->swapchain_state);
        g_state->eglDefaultColor  = color;
        g_state->eglDefaultDepth  = depth;
        // Also expose the underlying VkImage handles + formats so image-level
        // operations (glBlitFramebuffer / glReadPixels involving FBO 0) can
        // reference the on-screen drawable directly.
        g_state->eglDefaultColorImage   = backend_swapchain_current_color_image(s->swapchain_state);
        g_state->eglDefaultColorFormat  = backend_swapchain_color_format(s->swapchain_state);
        g_state->eglDefaultDepthImage   = backend_swapchain_current_depth_image(s->swapchain_state);
        g_state->eglDefaultDepthFormat  = backend_swapchain_depth_format(s->swapchain_state);
        g_state->eglDefaultWidth  = s->width;
        g_state->eglDefaultHeight = s->height;
        // Register the swapchain with the encoder so it can record layout
        // barriers and signal pendingRenderFinished. Only register when the
        // surface actually has an acquired color view (color != VK_NULL_HANDLE)
        // — passing a swapchain whose acquire failed would crash the barrier
        // recorder, which dereferences sc->images[sc->currentImage].
        backend_set_active_swapchain(color != VK_NULL_HANDLE ? s->swapchain_state : nullptr);
    } else {
        MITHRIL_LOG_WARN("egl", "install_surface_on_state: no swapchain or surface "
                          "(s=%p, swapchain_state=%p)", (void*)s,
                          (void*)(s ? s->swapchain_state : nullptr));
        g_state->eglDefaultColor  = VK_NULL_HANDLE;
        g_state->eglDefaultDepth  = VK_NULL_HANDLE;
        g_state->eglDefaultColorImage  = VK_NULL_HANDLE;
        g_state->eglDefaultColorFormat = VK_FORMAT_UNDEFINED;
        g_state->eglDefaultDepthImage  = VK_NULL_HANDLE;
        g_state->eglDefaultDepthFormat = VK_FORMAT_UNDEFINED;
        g_state->eglDefaultWidth  = 0;
        g_state->eglDefaultHeight = 0;
        // Detach the swapchain from the encoder so a headless / surfaceless
        // frame (or a frame against a user FBO) does not try to record layout
        // barriers against a destroyed swapchain.
        backend_set_active_swapchain(nullptr);
    }
}

// ---------------------------------------------------------------------------
// Config matching
// ---------------------------------------------------------------------------
// config_matches / config_get_attr live in mithril::egl (see
// MG_Impl/EGLConfig.{h,cpp}). The using-declarations above alias them into
// this anonymous namespace so call sites in egl.cpp can refer to them
// unqualified, exactly as before the extraction.

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
            return "EGL_EXT_platform_base "
                   "EGL_MESA_platform_surfaceless "
                   "EGL_KHR_swap_buffers_with_damage "
                   "EGL_KHR_fence_sync EGL_KHR_wait_sync EGL_KHR_image "
                   "EGL_KHR_image_base EGL_KHR_create_context "
                   "EGL_KHR_platform_base";
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

    // Platform-specific surface preparation. Returns a void* native_window
    // suitable for backend_create_swapchain (CAMetalLayer* on Apple),
    // or nullptr on failure.
    int w = 0, h = 0;
    void* native_window = surface_create((void*)win, &w, &h);
    if (!native_window) {
        set_error(EGL_BAD_NATIVE_WINDOW);
        return EGL_NO_SURFACE;
    }

    (void)attrib_list; // we ignore render-buffer / post-sub-buffer attribs

    EglSurface* s = new EglSurface{};
    s->native_window = native_window;
    s->config = config;
    s->firstFrame = true;
    EglConfig* cfg = (EglConfig*)config;
    s->wantDepthStencil = (cfg->depthSize > 0 || cfg->stencilSize > 0);
    // Build the Vulkan swapchain now if the window is already sized. If not,
    // defer to eglMakeCurrent / eglSwapBuffers which will retry.
    if (!ensure_swapchain(s)) {
        MITHRIL_LOG_WARN("egl", "eglCreateWindowSurface: deferred swapchain (window size = %dx%d)", w, h);
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
    // If this surface is current on this thread, drain GPU work referencing
    // its swapchain and detach the swapchain from the encoder BEFORE we tear
    // it down. Without the drain, vkDestroySwapchainKHR frees IOSurfaces that
    // the GPU may still be reading, and the next IOSurfaceBindAccel call in
    // the Metal driver crashes with SIGSEGV (UAF). The drain also calls
    // set_active_swapchain(nullptr), so the encoder never records against
    // the dying swapchain again.
    if (t_currentDraw == s) {
        backend_drain_and_detach_swapchain();
        t_currentDraw = nullptr;
        install_surface_on_state(nullptr);
    }
    if (t_currentRead == s) { t_currentRead = nullptr; }
    if (s->swapchain_state) {
        backend_destroy_swapchain(s->swapchain_state);
        s->swapchain_state = nullptr;
    }
    s->native_window = nullptr;
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
    // GLState so framebuffer-0 rendering lands on the on-screen surface.
    if (d) {
        if (!d->swapchain_state && d->native_window) {
            // First make-current on a freshly-created surface whose initial
            // swapchain creation failed (window wasn't sized yet). Retry now.
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

    // First-frame / deferred-swapchain retry: if the swapchain wasn't created
    // at eglCreateWindowSurface time (because the native window wasn't sized
    // yet — common on iOS where the CAMetalLayer gets its size asynchronously
    // after the view is laid out), retry now. Without this, the host would
    // call eglSwapBuffers on a surface with no swapchain, present nothing,
    // and the screen would stay black forever (the swapchain would never be
    // created because eglMakeCurrent's retry only fires on the very first
    // make-current). We retry on every swap until the swapchain comes up.
    if (s->native_window && !s->swapchain_state) {
        ensure_swapchain(s);
        if (s->swapchain_state && t_currentDraw == s) {
            // New swapchain just came up: install it on the current GLState so
            // the next frame's draws land on the on-screen drawable.
            install_surface_on_state(s);
        }
    }

    // Flush any pending Vulkan work into the current swapchain image view.
    // backend_end_render_pass() + backend_commit() end the active render pass
    // and submit the command buffer, so the encoded draws land on the
    // currently-acquired swapchain image before we present. commit_frame()'s
    // empty-submit defense skips the submit if no commands were recorded
    // since the last commit (e.g. eglWaitClient already flushed this frame).
    backend_end_render_pass();
    backend_commit();

    // Present the frame we just rendered, then acquire the next image for
    // the following frame. backend_present_and_acquire() calls
    // vkQueuePresentKHR followed by vkAcquireNextImageKHR.
    //
    // IMPORTANT: present happens BEFORE the resize/rebuild check below. If we
    // rebuilt the swapchain before presenting, the vkQueuePresentKHR would
    // reference a just-destroyed swapchain (UAF) — exactly the
    // IOSurfaceBindAccel SIGSEGV seen in the field. The correct order is:
    // present the already-rendered frame against the current (still-valid)
    // swapchain, THEN tear down + recreate for the next frame.
    if (s->swapchain_state) {
        backend_present_and_acquire(s->swapchain_state);
    }

    // Rebuild the swapchain if (a) the native window was resized between
    // frames, or (b) the backend marked the swapchain dead via
    // backend_swapchain_needs_rebuild (fatal Vulkan error: GPU OOM, surface
    // lost, device lost). Case (b) is the recovery path for the VK_NOT_READY
    // death spiral: without rebuilding, the dead swapchain would keep
    // returning null from acquire and the render thread would spin forever.
    // ensure_swapchain() drains GPU work and detaches the old swapchain from
    // the encoder before destroying it, so this is safe even under OOM.
    if (s->native_window && s->swapchain_state) {
        int w = 0, h = 0;
        bool size_changed = (surface_get_size(s->native_window, &w, &h) &&
                             w > 0 && h > 0 &&
                             (w != s->width || h != s->height));
        bool needs_rebuild = (backend_swapchain_needs_rebuild(s->swapchain_state) != 0);
        if (size_changed || needs_rebuild) {
            if (needs_rebuild) {
                MITHRIL_LOG_WARN("egl", "eglSwapBuffers: swapchain marked dead by "
                                  "backend, rebuilding (GPU OOM / surface lost)");
            }
            ensure_swapchain(s);
        }
    }

    // Re-install the (possibly new) swapchain's current image on the GLState
    // so the next frame's draws land on a valid drawable. This also re-
    // registers the swapchain with the encoder (backend_set_active_swapchain)
    // so layout barriers + pendingRenderFinished signaling work next frame.
    if (s->swapchain_state && t_currentDraw == s) {
        install_surface_on_state(s);
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

// ---- EGL 1.5 Sync (shadow implementation) ----
EGLSync eglCreateSync(EGLDisplay dpy, EGLenum type, const EGLAttrib* attrib_list) {
    clear_error();
    if (!valid_display(dpy)) { set_error(EGL_BAD_DISPLAY); return EGL_NO_SYNC; }
    if (type != EGL_SYNC_FENCE) { set_error(EGL_BAD_ATTRIBUTE); return EGL_NO_SYNC; }
    (void)attrib_list;  // EGL_SYNC_FENCE ignores attrib_list per spec

    EglSync sync{};
    sync.dpy = dpy;
    sync.type = EGL_SYNC_FENCE;
    sync.condition = EGL_SYNC_PRIOR_COMMANDS_COMPLETE;
    sync.status = EGL_SIGNALED;

    EGLSync handle = reinterpret_cast<EGLSync>(g_nextSyncHandle++);
    g_syncs[handle] = sync;
    return handle;
}

EGLBoolean eglDestroySync(EGLDisplay dpy, EGLSync sync) {
    clear_error();
    if (!valid_display(dpy)) { set_error(EGL_BAD_DISPLAY); return EGL_FALSE; }
    auto it = g_syncs.find(sync);
    if (it == g_syncs.end()) { set_error(EGL_BAD_SYNC_KHR); return EGL_FALSE; }
    g_syncs.erase(it);
    return EGL_TRUE;
}

EGLint eglClientWaitSync(EGLDisplay dpy, EGLSync sync, EGLint flags, EGLTime timeout) {
    clear_error();
    (void)flags; (void)timeout;
    if (!valid_display(dpy)) { set_error(EGL_BAD_DISPLAY); return EGL_FALSE; }
    auto it = g_syncs.find(sync);
    if (it == g_syncs.end()) { set_error(EGL_BAD_SYNC_KHR); return EGL_FALSE; }
    // Shadow implementation: always signaled, return immediately.
    return EGL_CONDITION_SATISFIED;
}

EGLBoolean eglWaitSync(EGLDisplay dpy, EGLSync sync, EGLint flags) {
    clear_error();
    (void)flags;
    if (!valid_display(dpy)) { set_error(EGL_BAD_DISPLAY); return EGL_FALSE; }
    auto it = g_syncs.find(sync);
    if (it == g_syncs.end()) { set_error(EGL_BAD_SYNC_KHR); return EGL_FALSE; }
    // Shadow implementation: no real GPU-side wait.
    return EGL_TRUE;
}

EGLBoolean eglGetSyncAttrib(EGLDisplay dpy, EGLSync sync, EGLint attribute, EGLAttrib* value) {
    clear_error();
    if (!valid_display(dpy)) { set_error(EGL_BAD_DISPLAY); return EGL_FALSE; }
    auto it = g_syncs.find(sync);
    if (it == g_syncs.end()) { set_error(EGL_BAD_SYNC_KHR); return EGL_FALSE; }
    if (!value) { set_error(EGL_BAD_PARAMETER); return EGL_FALSE; }
    const EglSync& s = it->second;
    switch (attribute) {
        case EGL_SYNC_TYPE:      *value = s.type;      break;
        case EGL_SYNC_STATUS:    *value = s.status;    break;
        case EGL_SYNC_CONDITION: *value = s.condition; break;
        default:                 set_error(EGL_BAD_ATTRIBUTE); return EGL_FALSE;
    }
    return EGL_TRUE;
}

// ---- EGL 1.5 Image (shadow implementation) ----
EGLImage eglCreateImage(EGLDisplay dpy, EGLContext ctx, EGLenum target,
                        EGLClientBuffer buffer, const EGLAttrib* attrib_list) {
    clear_error();
    (void)ctx; (void)attrib_list;
    if (!valid_display(dpy)) { set_error(EGL_BAD_DISPLAY); return EGL_NO_IMAGE; }
    // Accept known targets; reject unknown.
    switch (target) {
        case EGL_GL_TEXTURE_2D:
        case EGL_GL_TEXTURE_3D:
        case EGL_GL_TEXTURE_CUBE_MAP_POSITIVE_X:
        case EGL_GL_RENDERBUFFER:
            break;
        default:
            set_error(EGL_BAD_PARAMETER);
            return EGL_NO_IMAGE;
    }

    EglImage img{};
    img.dpy = dpy;
    img.target = target;
    img.buffer = buffer;

    EGLImage handle = reinterpret_cast<EGLImage>(g_nextImageHandle++);
    g_images[handle] = img;
    return handle;
}

EGLBoolean eglDestroyImage(EGLDisplay dpy, EGLImage image) {
    clear_error();
    if (!valid_display(dpy)) { set_error(EGL_BAD_DISPLAY); return EGL_FALSE; }
    auto it = g_images.find(image);
    if (it == g_images.end()) { set_error(EGL_BAD_PARAMETER); return EGL_FALSE; }
    g_images.erase(it);
    return EGL_TRUE;
}

// ---- EGL 1.5 Platform Surface ----
EGLSurface eglCreatePlatformWindowSurface(EGLDisplay dpy, EGLConfig config,
                                          void* native_window,
                                          const EGLAttrib* attrib_list) {
    clear_error();
    (void)attrib_list;

    // Surfaceless / headless mode: return a placeholder surface with no
    // swapchain. Used by EGL_MESA_platform_surfaceless with a null window.
    if (native_window == nullptr) {
        if (!valid_display(dpy)) { set_error(EGL_BAD_DISPLAY); return EGL_NO_SURFACE; }
        if (!valid_config(config)) { set_error(EGL_BAD_CONFIG); return EGL_NO_SURFACE; }
        EglSurface* s = new EglSurface{};
        s->config = config;
        s->width = 0;
        s->height = 0;
        s->swapchain_state = nullptr;
        s->wantDepthStencil = false;
        s->swapInterval = 0;
        return (EGLSurface)s;
    }

    // Default: delegate to eglCreateWindowSurface (handles platform-specific
    // surface preparation via surface_create()).
    return eglCreateWindowSurface(dpy, config, (EGLNativeWindowType)native_window, nullptr);
}

EGLSurface eglCreatePlatformPixmapSurface(EGLDisplay dpy, EGLConfig config,
                                          void* native_pixmap,
                                          const EGLAttrib* attrib_list) {
    clear_error();
    (void)native_pixmap; (void)attrib_list;
    if (!valid_display(dpy)) { set_error(EGL_BAD_DISPLAY); return EGL_NO_SURFACE; }
    if (!valid_config(config)) { set_error(EGL_BAD_CONFIG); return EGL_NO_SURFACE; }
    // Pixmap surfaces are recorded in the state layer only, no swapchain.
    EglSurface* s = new EglSurface{};
    s->config = config;
    s->width = 0;
    s->height = 0;
    s->swapchain_state = nullptr;
    s->wantDepthStencil = false;
    s->swapInterval = 0;
    return (EGLSurface)s;
}

EGLSurface eglCreatePixmapSurface(EGLDisplay dpy, EGLConfig config,
                                  EGLNativePixmapType pixmap,
                                  const EGLint* attrib_list) {
    clear_error();
    (void)pixmap; (void)attrib_list;
    if (!valid_display(dpy)) { set_error(EGL_BAD_DISPLAY); return EGL_NO_SURFACE; }
    if (!valid_config(config)) { set_error(EGL_BAD_CONFIG); return EGL_NO_SURFACE; }
    // Pixmap surfaces are recorded in the state layer only, no swapchain.
    EglSurface* s = new EglSurface{};
    s->config = config;
    s->width = 0;
    s->height = 0;
    s->swapchain_state = nullptr;
    s->wantDepthStencil = false;
    s->swapInterval = 0;
    return (EGLSurface)s;
}

EGLSurface eglCreatePbufferFromClientBuffer(EGLDisplay dpy, EGLenum buftype,
                                            EGLClientBuffer buffer, EGLConfig config,
                                            const EGLint* attrib_list) {
    clear_error();
    (void)buffer; (void)config; (void)attrib_list;
    if (!valid_display(dpy)) { set_error(EGL_BAD_DISPLAY); return EGL_NO_SURFACE; }
    // OpenVG is not supported.
    if (buftype == EGL_OPENVG_IMAGE) { set_error(EGL_BAD_MATCH); return EGL_NO_SURFACE; }
    set_error(EGL_BAD_PARAMETER);
    return EGL_NO_SURFACE;
}

EGLenum eglQueryAPI(void) {
    return t_boundAPI;  // defaults to EGL_OPENGL_ES_API per EGL 1.5 spec
}

} // extern "C"
#pragma GCC visibility pop
