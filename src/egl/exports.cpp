// Mithril-Wrapper EGL entry points.
// Export set: the 18 symbols Amethyst dlsyms + remaining EGL 1.5 core.
// See docs/egl_list.md.

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <cstring>

#include <backend/backend.h>
#include <egl/internal.h>
#include <state/state.h>
#include <util/log.h>

using namespace mithril::egl;

extern "C" {

// ---- Display / error ----------------------------------------------------

EGLDisplay eglGetDisplay(EGLNativeDisplayType display_id) {
    ML_LOG_DEBUG("eglGetDisplay(%p)", (void*)(intptr_t)display_id);
    if (display_id != EGL_DEFAULT_DISPLAY) {
        SetError(EGL_BAD_PARAMETER);
        return EGL_NO_DISPLAY;
    }
    SetError(EGL_SUCCESS);
    return reinterpret_cast<EGLDisplay>(&globals().display);
}

EGLDisplay eglGetPlatformDisplay(EGLenum platform, void* native_display,
                                 const EGLAttrib* attrib_list) {
    (void)platform;
    (void)attrib_list;
    ML_LOG_DEBUG("eglGetPlatformDisplay(0x%x, %p)", (unsigned)platform, native_display);
    // EGL_DEFAULT_DISPLAY == ((EGLNativeDisplayType)0) on every platform, so a
    // non-null native_display is always an error here.
    if (native_display != nullptr) {
        SetError(EGL_BAD_PARAMETER);
        return EGL_NO_DISPLAY;
    }
    SetError(EGL_SUCCESS);
    return reinterpret_cast<EGLDisplay>(&globals().display);
}

EGLBoolean eglInitialize(EGLDisplay dpy, EGLint* major, EGLint* minor) {
    if (dpy != reinterpret_cast<EGLDisplay>(&globals().display)) {
        SetError(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }
    if (major) *major = 1;
    if (minor) *minor = 5;
    globals().display.initialized = true;
    globals().display.major = 1;
    globals().display.minor = 5;
    SetError(EGL_SUCCESS);
    return EGL_TRUE;
}

EGLBoolean eglTerminate(EGLDisplay dpy) {
    if (dpy != reinterpret_cast<EGLDisplay>(&globals().display)) {
        SetError(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }
    globals().display.initialized = false;
    SetError(EGL_SUCCESS);
    return EGL_TRUE;
}

EGLint eglGetError() {
    return TakeError();
}

// ---- Configs -------------------------------------------------------------

EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint* attrib_list, EGLConfig* configs,
                           EGLint config_size, EGLint* num_config) {
    if (!dpy || num_config == nullptr) {
        SetError(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }
    if (!ConfigMatches(attrib_list)) {
        *num_config = 0;
        SetError(EGL_SUCCESS);
        return EGL_TRUE;
    }
    *num_config = 1;
    if (configs && config_size > 0) {
        configs[0] = ConfigToken();
    }
    SetError(EGL_SUCCESS);
    return EGL_TRUE;
}

EGLBoolean eglGetConfigs(EGLDisplay dpy, EGLConfig* configs, EGLint config_size,
                         EGLint* num_config) {
    if (!dpy) {
        SetError(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }
    if (configs && config_size > 0) configs[0] = ConfigToken();
    *num_config = 1;
    SetError(EGL_SUCCESS);
    return EGL_TRUE;
}

EGLBoolean eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config, EGLint attribute, EGLint* value) {
    if (config != ConfigToken()) {
        SetError(EGL_BAD_CONFIG);
        return EGL_FALSE;
    }
    switch (attribute) {
        case EGL_NATIVE_VISUAL_ID: *value = GetNativeVisualId(); break;
        case EGL_RED_SIZE: case EGL_GREEN_SIZE: case EGL_BLUE_SIZE:
        case EGL_ALPHA_SIZE: *value = 8; break;
        case EGL_DEPTH_SIZE: *value = 24; break;
        case EGL_STENCIL_SIZE: *value = 0; break;
        case EGL_SURFACE_TYPE: *value = EGL_WINDOW_BIT | EGL_PBUFFER_BIT; break;
        case EGL_RENDERABLE_TYPE: *value = SupportedRenderableTypes(); break;
        case EGL_COLOR_BUFFER_TYPE: *value = EGL_RGB_BUFFER; break;
        case EGL_NATIVE_RENDERABLE: case EGL_TRANSPARENT_TYPE: *value = EGL_TRUE; break;
        case EGL_CONFIG_CAVEAT: *value = EGL_NONE; break;
        default:
            SetError(EGL_BAD_ATTRIBUTE);
            return EGL_FALSE;
    }
    SetError(EGL_SUCCESS);
    return EGL_TRUE;
}

// ---- API binding ----------------------------------------------------------

EGLBoolean eglBindAPI(EGLenum api) {
    if (api != EGL_OPENGL_API && api != EGL_OPENGL_ES_API) {
        SetError(EGL_BAD_PARAMETER);
        return EGL_FALSE;
    }
    thread_state().bound_api = api;
    ML_LOG_DEBUG("eglBindAPI(%s)",
                 api == EGL_OPENGL_API ? "EGL_OPENGL_API"
                                      : "EGL_OPENGL_ES_API (Amethyst alias)");
    SetError(EGL_SUCCESS);
    return EGL_TRUE;
}

EGLenum eglQueryAPI() {
    return thread_state().bound_api;
}

EGLBoolean eglQueryContext(EGLDisplay dpy, EGLContext ctx, EGLint attribute, EGLint* value) {
    if (dpy != reinterpret_cast<EGLDisplay>(&globals().display)) {
        SetError(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }
    if (ctx != reinterpret_cast<EGLContext>(&globals().context)) {
        SetError(EGL_BAD_CONTEXT);
        return EGL_FALSE;
    }
    if (!value) {
        SetError(EGL_BAD_PARAMETER);
        return EGL_FALSE;
    }
    switch (attribute) {
        case EGL_CONTEXT_CLIENT_TYPE: *value = globals().context.client_api; break;
        case EGL_CONTEXT_CLIENT_VERSION: *value = globals().context.client_version; break;
        default:
            SetError(EGL_BAD_ATTRIBUTE);
            return EGL_FALSE;
    }
    SetError(EGL_SUCCESS);
    return EGL_TRUE;
}

// ---- Context / current ----------------------------------------------------

EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig config, EGLContext share_context,
                            const EGLint* attrib_list) {
    if (dpy != reinterpret_cast<EGLDisplay>(&globals().display)) {
        SetError(EGL_BAD_DISPLAY);
        return EGL_NO_CONTEXT;
    }
    if (config != ConfigToken()) {
        SetError(EGL_BAD_CONFIG);
        return EGL_NO_CONTEXT;
    }
    if (share_context != EGL_NO_CONTEXT &&
        share_context != reinterpret_cast<EGLContext>(&globals().context)) {
        SetError(EGL_BAD_CONTEXT);
        return EGL_NO_CONTEXT;
    }

    EGLint client_version =
        thread_state().bound_api == EGL_OPENGL_ES_API ? 1 : 3;
    if (attrib_list) {
        for (const EGLint* attribute = attrib_list;
             *attribute != EGL_NONE; attribute += 2) {
            if (attribute[0] == EGL_CONTEXT_CLIENT_VERSION) {
                client_version = attribute[1];
            }
        }
    }
    if (thread_state().bound_api == EGL_OPENGL_ES_API &&
        client_version != 3) {
        SetError(EGL_BAD_MATCH);
        return EGL_NO_CONTEXT;
    }

    globals().context.config = config;
    globals().context.client_api = thread_state().bound_api;
    globals().context.client_version = client_version;
    globals().context.drawable_state_initialized = false;
    ML_LOG_DEBUG("eglCreateContext(config=%p, share=%p, api=0x%x, version=%d)",
                 config, share_context,
                 static_cast<unsigned>(globals().context.client_api),
                 globals().context.client_version);
    SetError(EGL_SUCCESS);
    return reinterpret_cast<EGLContext>(&globals().context);
}

EGLBoolean eglDestroyContext(EGLDisplay dpy, EGLContext ctx) {
    if (ctx != reinterpret_cast<EGLContext>(&globals().context)) {
        SetError(EGL_BAD_CONTEXT);
        return EGL_FALSE;
    }
    globals().context.drawable_state_initialized = false;
    SetError(EGL_SUCCESS);
    return EGL_TRUE;
}

EGLContext eglGetCurrentContext() {
    return thread_state().current_context;
}

EGLDisplay eglGetCurrentDisplay() {
    return thread_state().current_display;
}

EGLSurface eglGetCurrentSurface(EGLint readdraw) {
    if (readdraw == EGL_DRAW) return thread_state().current_draw;
    if (readdraw == EGL_READ) return thread_state().current_read;
    SetError(EGL_BAD_PARAMETER);
    return EGL_NO_SURFACE;
}

EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx) {
    if (dpy != reinterpret_cast<EGLDisplay>(&globals().display)) {
        SetError(EGL_BAD_DISPLAY);
        return EGL_FALSE;
    }
    if (ctx == EGL_NO_CONTEXT && draw == EGL_NO_SURFACE &&
        read == EGL_NO_SURFACE) {
        thread_state().current_display = EGL_NO_DISPLAY;
        thread_state().current_context = EGL_NO_CONTEXT;
        thread_state().current_draw = EGL_NO_SURFACE;
        thread_state().current_read = EGL_NO_SURFACE;
        SetError(EGL_SUCCESS);
        return EGL_TRUE;
    }
    if (ctx != reinterpret_cast<EGLContext>(&globals().context)) {
        SetError(EGL_BAD_CONTEXT);
        return EGL_FALSE;
    }
    if (draw != reinterpret_cast<EGLSurface>(&globals().surface) ||
        read != reinterpret_cast<EGLSurface>(&globals().surface)) {
        SetError(EGL_BAD_SURFACE);
        return EGL_FALSE;
    }
    if (!globals().context.drawable_state_initialized) {
        const GLsizei width = static_cast<GLsizei>(mithril::backend::TargetWidth());
        const GLsizei height = static_cast<GLsizei>(mithril::backend::TargetHeight());
        auto& state = mithril::state::GetState();
        state.viewport = {0, 0, width, height, true};
        state.scissor = {0, 0, width, height, true};
        globals().context.drawable_state_initialized = true;
    }
    thread_state().current_display = dpy;
    thread_state().current_context = ctx;
    thread_state().current_draw = draw;
    thread_state().current_read = read;
    SetError(EGL_SUCCESS);
    return EGL_TRUE;
}

EGLBoolean eglReleaseThread() {
    ResetThreadState();
    SetError(EGL_SUCCESS);
    return EGL_TRUE;
}

// ---- Surfaces --------------------------------------------------------------

EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                  EGLNativeWindowType win, const EGLint* attrib_list) {
    globals().surface.config = config;
    globals().surface.native_window = (void*)win;
    globals().surface.is_window = true;
    globals().surface.swap_interval = 1;
    ML_LOG_DEBUG("eglCreateWindowSurface(win=%p)", (void*)win);
    if (!mithril::backend::SetNativeWindow((void*)win)) {
        globals().surface.native_window = nullptr;
        globals().surface.is_window = false;
        SetError(EGL_BAD_NATIVE_WINDOW);
        return EGL_NO_SURFACE;
    }
    SetError(EGL_SUCCESS);
    return reinterpret_cast<EGLSurface>(&globals().surface);
}

EGLSurface eglCreatePlatformWindowSurface(EGLDisplay dpy, EGLConfig config, void* native_window,
                                          const EGLAttrib* attrib_list) {
    globals().surface.config = config;
    globals().surface.native_window = native_window;
    globals().surface.is_window = true;
    globals().surface.swap_interval = 1;
    if (!mithril::backend::SetNativeWindow(native_window)) {
        globals().surface.native_window = nullptr;
        globals().surface.is_window = false;
        SetError(EGL_BAD_NATIVE_WINDOW);
        return EGL_NO_SURFACE;
    }
    SetError(EGL_SUCCESS);
    return reinterpret_cast<EGLSurface>(&globals().surface);
}

EGLSurface eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config, const EGLint* attrib_list) {
    globals().surface.config = config;
    globals().surface.native_window = nullptr;
    globals().surface.is_window = false;
    mithril::backend::SetNativeWindow(nullptr);
    SetError(EGL_SUCCESS);
    return reinterpret_cast<EGLSurface>(&globals().surface);
}

EGLBoolean eglDestroySurface(EGLDisplay dpy, EGLSurface surface) {
    if (surface != reinterpret_cast<EGLSurface>(&globals().surface)) {
        SetError(EGL_BAD_SURFACE);
        return EGL_FALSE;
    }
    mithril::backend::SetNativeWindow(nullptr);
    globals().surface.native_window = nullptr;
    globals().surface.is_window = false;
    SetError(EGL_SUCCESS);
    return EGL_TRUE;
}

EGLBoolean eglQuerySurface(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint* value) {
    if (surface != reinterpret_cast<EGLSurface>(&globals().surface) || !value) {
        SetError(EGL_BAD_SURFACE);
        return EGL_FALSE;
    }
    switch (attribute) {
        case EGL_WIDTH: *value = static_cast<EGLint>(mithril::backend::TargetWidth()); break;
        case EGL_HEIGHT: *value = static_cast<EGLint>(mithril::backend::TargetHeight()); break;
        default:
            SetError(EGL_BAD_ATTRIBUTE);
            return EGL_FALSE;
    }
    SetError(EGL_SUCCESS);
    return EGL_TRUE;
}

EGLBoolean eglSurfaceAttrib(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint value) {
    SetError(EGL_SUCCESS);
    return EGL_TRUE;
}

EGLBoolean eglSwapInterval(EGLDisplay dpy, EGLint interval) {
    globals().surface.swap_interval = interval;
    SetError(EGL_SUCCESS);
    return EGL_TRUE;
}

EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    if (surface != reinterpret_cast<EGLSurface>(&globals().surface)) {
        SetError(EGL_BAD_SURFACE);
        return EGL_FALSE;
    }
    if (!globals().surface.is_window) {
        mithril::backend::SubmitFlush(false);
        SetError(EGL_SUCCESS);
        return EGL_TRUE;
    }
    if (!mithril::backend::SwapBuffers()) {
        SetError(EGL_BAD_SURFACE);
        return EGL_FALSE;
    }
    SetError(EGL_SUCCESS);
    return EGL_TRUE;
}

// ---- String / proc -----------------------------------------------------------

const char* eglQueryString(EGLDisplay dpy, EGLint name) {
    switch (name) {
        case EGL_VENDOR: return "MithrilWrapper";
        case EGL_VERSION: return "1.5";
        case EGL_CLIENT_APIS: return "OpenGL OpenGL_ES";
        case EGL_EXTENSIONS: return "";
        default:
            SetError(EGL_BAD_PARAMETER);
            return nullptr;
    }
}

typedef void (*FnPtrType)();
FnPtrType eglGetProcAddress(const char* procname) {
    ML_LOG_DEBUG("eglGetProcAddress(%s)", procname);
    // GL proc resolution lands in M1/M2; null forces callers to fall back to
    // direct dlsym of the exported gl* symbol table.
    return nullptr;
}

// ---- Sync / wait (EGL 1.5, minimal) ----------------------------------------

EGLSync eglCreateSync(EGLDisplay dpy, EGLenum type, const EGLAttrib* attrib_list) {
    (void)type;
    (void)attrib_list;
    SetError(EGL_SUCCESS);
    return reinterpret_cast<EGLSync>(&globals().display);
}

EGLBoolean eglDestroySync(EGLDisplay dpy, EGLSync sync) {
    if (sync != reinterpret_cast<EGLSync>(&globals().display)) {
        SetError(EGL_BAD_PARAMETER);
        return EGL_FALSE;
    }
    SetError(EGL_SUCCESS);
    return EGL_TRUE;
}

EGLint eglClientWaitSync(EGLDisplay dpy, EGLSync sync, EGLint flags, EGLTime timeout) {
    (void)flags; (void)timeout;
    if (sync != reinterpret_cast<EGLSync>(&globals().display)) {
        SetError(EGL_BAD_PARAMETER);
        return EGL_FALSE;
    }
    SetError(EGL_SUCCESS);
    return EGL_CONDITION_SATISFIED;
}

EGLBoolean eglWaitSync(EGLDisplay dpy, EGLSync sync, EGLint flags) {
    SetError(EGL_SUCCESS);
    return EGL_TRUE;
}

EGLBoolean eglGetSyncAttrib(EGLDisplay dpy, EGLSync sync, EGLint attribute, EGLAttrib* value) {
    if (attribute == EGL_SYNC_STATUS) *value = EGL_SIGNALED;
    else if (attribute == EGL_SYNC_TYPE) *value = EGL_SYNC_FENCE;
    else return EGL_FALSE;
    SetError(EGL_SUCCESS);
    return EGL_TRUE;
}

EGLBoolean eglWaitClient() {
    SetError(EGL_SUCCESS);
    return EGL_TRUE;
}

EGLBoolean eglWaitGL() {
    SetError(EGL_SUCCESS);
    return EGL_TRUE;
}

EGLBoolean eglWaitNative(EGLint engine) {
    SetError(EGL_SUCCESS);
    return EGL_TRUE;
}

// ---- Unsupported-but-exported (stubs returning spec-legal errors) -----------

EGLBoolean eglBindTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer) {
    SetError(EGL_BAD_SURFACE);
    return EGL_FALSE;
}

EGLBoolean eglReleaseTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer) {
    SetError(EGL_BAD_SURFACE);
    return EGL_FALSE;
}

EGLBoolean eglCopyBuffers(EGLDisplay dpy, EGLSurface surface, EGLNativePixmapType target) {
    SetError(EGL_BAD_SURFACE);
    return EGL_FALSE;
}

EGLSurface eglCreatePixmapSurface(EGLDisplay dpy, EGLConfig config, EGLNativePixmapType pixmap,
                                  const EGLint* attrib_list) {
    SetError(EGL_BAD_NATIVE_PIXMAP);
    return EGL_NO_SURFACE;
}

EGLSurface eglCreatePlatformPixmapSurface(EGLDisplay dpy, EGLConfig config, void* native_pixmap,
                                          const EGLAttrib* attrib_list) {
    SetError(EGL_BAD_NATIVE_PIXMAP);
    return EGL_NO_SURFACE;
}

EGLSurface eglCreatePbufferFromClientBuffer(EGLDisplay dpy, EGLenum buftype, EGLClientBuffer buffer,
                                            EGLConfig config, const EGLint* attrib_list) {
    SetError(EGL_BAD_PARAMETER);
    return EGL_NO_SURFACE;
}

EGLImage eglCreateImage(EGLDisplay dpy, EGLContext ctx, EGLenum target, EGLClientBuffer buffer,
                        const EGLAttrib* attrib_list) {
    SetError(EGL_BAD_PARAMETER);
    return EGL_NO_IMAGE;
}

EGLBoolean eglDestroyImage(EGLDisplay dpy, EGLImage image) {
    SetError(EGL_BAD_PARAMETER);
    return EGL_FALSE;
}

} // extern "C"
