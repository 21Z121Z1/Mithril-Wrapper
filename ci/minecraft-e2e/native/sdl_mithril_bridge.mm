// SDL3 -> Mithril bridge.
//
// Minecraft 26.3 creates its window through LWJGL's SDL3 bindings and uses
// SDL_GL_* for the OpenGL context + present. During GPU capability detection
// and window (re)creation it may create and tear down several SDL windows in
// quick succession, then keep rendering on the final one.
//
// Mithril's Vulkan/MoltenVK backend is process-global, but its GL object
// namespace (GLState, including the name allocator) is per-EGLContext. If we
// created a fresh EGLContext per SDL window, a recreated window would restart
// the name allocator and collide with the previous context's still-cached
// persistent mapped pointers -> SIGSEGV (SEGV_ACCERR) on the first frame.
//
// Real GL drivers solve window recreation through a single context (or a share
// group). This wrapper therefore keeps ONE shared Mithril EGLContext/GLState
// for the whole process and only the EGLWindowSurface (tied to each window's
// CAMetalLayer) changes per SDL window:
//   - attaches a CAMetalLayer to each SDL window's Cocoa content view,
//   - lazily creates one Mithril EGL display + shared context,
//   - creates an EGL window surface per SDL window,
//   - routes SDL_GL_CreateContext to the shared context,
//   - routes SDL_GL_MakeCurrent to (that window's surface, shared context),
//   - routes SDL_GL_SwapWindow through the prepresent capture seam +
//     eglSwapBuffers.
//
// It is built as a libSDL3 replacement that re-exports every real SDL symbol
// and overrides only the handful of functions above.
#include <EGL/egl.h>
#include <GL/gl.h>

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#import <QuartzCore/CAMetalLayer.h>

#include <dlfcn.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

extern "C" void mithril_e2e_capture_before_present(int width, int height, void* mithril_handle);

struct SDL_Window;
typedef void* SDL_GLContext;
typedef unsigned int Uint32;

namespace {

struct WindowState {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    int width = 0;
    int height = 0;
};

struct MithrilApi {
    void* handle = nullptr;
    decltype(&eglGetDisplay) getDisplay = nullptr;
    decltype(&eglInitialize) initialize = nullptr;
    decltype(&eglChooseConfig) chooseConfig = nullptr;
    decltype(&eglBindAPI) bindAPI = nullptr;
    decltype(&eglCreateWindowSurface) createWindowSurface = nullptr;
    decltype(&eglCreateContext) createContext = nullptr;
    decltype(&eglMakeCurrent) makeCurrent = nullptr;
    decltype(&eglSwapBuffers) swapBuffers = nullptr;
    decltype(&eglSwapInterval) swapInterval = nullptr;
    decltype(&eglDestroySurface) destroySurface = nullptr;
    decltype(&eglDestroyContext) destroyContext = nullptr;
};

std::mutex g_mutex;
std::unordered_map<SDL_Window*, WindowState> g_windows;
std::atomic<unsigned long long> g_swap_count{0};

// Process-wide shared EGL pieces.
EGLDisplay g_display = EGL_NO_DISPLAY;
EGLContext g_shared_context = EGL_NO_CONTEXT;
EGLConfig  g_config = nullptr;

std::string getenv_string(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

// Real SDL3 handle (for calling the genuine implementations).
void* real_sdl() {
    static void* h = [] {
        std::string path = getenv_string("MITHRIL_REAL_SDL");
        if (!path.empty()) {
            void* x = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
            if (x) return x;
        }
        return (void*)nullptr;
    }();
    return h;
}

template <typename T>
T real_sdl_fn(const char* name) {
    void* h = real_sdl();
    return h ? reinterpret_cast<T>(dlsym(h, name)) : nullptr;
}

MithrilApi& mithril() {
    static MithrilApi m = [] {
        MithrilApi m;
        std::string path = getenv_string("MITHRIL_E2E_MITHRIL_DYLIB");
        if (!path.empty()) m.handle = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
        if (!m.handle) m.handle = dlopen(nullptr, RTLD_NOW | RTLD_GLOBAL);
#define LOAD_EGL(field, name) m.field = reinterpret_cast<decltype(m.field)>(dlsym(m.handle, #name))
        LOAD_EGL(getDisplay, eglGetDisplay);
        LOAD_EGL(initialize, eglInitialize);
        LOAD_EGL(chooseConfig, eglChooseConfig);
        LOAD_EGL(bindAPI, eglBindAPI);
        LOAD_EGL(createWindowSurface, eglCreateWindowSurface);
        LOAD_EGL(createContext, eglCreateContext);
        LOAD_EGL(makeCurrent, eglMakeCurrent);
        LOAD_EGL(swapBuffers, eglSwapBuffers);
        LOAD_EGL(swapInterval, eglSwapInterval);
        LOAD_EGL(destroySurface, eglDestroySurface);
        LOAD_EGL(destroyContext, eglDestroyContext);
#undef LOAD_EGL
        return m;
    }();
    return m;
}

NSWindow* find_nswindow(const char* title) {
    NSString* want = title ? [NSString stringWithUTF8String:title] : nil;
    NSWindow* fallback = NSApp.keyWindow;
    for (NSWindow* w in NSApp.windows) {
        if (want && [w.title isEqualToString:want]) return w;
        if (!fallback) fallback = w;
    }
    return fallback;
}

// Lazily initialise the shared display + config + context (once).
bool ensure_shared(EGLDisplay& out_display) {
    auto& m = mithril();
    if (g_shared_context != EGL_NO_CONTEXT) { out_display = g_display; return true; }

    EGLDisplay display = m.getDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0, minor = 0;
    if (display == EGL_NO_DISPLAY ||
        m.initialize(display, &major, &minor) != EGL_TRUE ||
        m.bindAPI(EGL_OPENGL_API) != EGL_TRUE) {
        std::fprintf(stderr, "[sdl-mithril] EGL init failed\n");
        return false;
    }
    const EGLint configAttribs[] = {
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_NONE
    };
    EGLConfig config = nullptr;
    EGLint count = 0;
    if (m.chooseConfig(display, configAttribs, &config, 1, &count) != EGL_TRUE ||
        count != 1 || !config) {
        std::fprintf(stderr, "[sdl-mithril] chooseConfig failed\n");
        return false;
    }
    const EGLint contextAttribs[] = {
        EGL_CONTEXT_MAJOR_VERSION, 3,
        EGL_CONTEXT_MINOR_VERSION, 3,
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
        EGL_NONE
    };
    EGLContext context = m.createContext(display, config, EGL_NO_CONTEXT, contextAttribs);
    if (context == EGL_NO_CONTEXT) {
        std::fprintf(stderr, "[sdl-mithril] createContext failed\n");
        return false;
    }
    g_display = display;
    g_config = config;
    g_shared_context = context;
    out_display = display;
    return true;
}

bool setup_window(SDL_Window* sw, const char* title, int fbw, int fbh) {
    NSWindow* nsw = find_nswindow(title);
    if (!nsw || !nsw.contentView) {
        std::fprintf(stderr, "[sdl-mithril] no Cocoa content view\n");
        return false;
    }
    NSView* view = nsw.contentView;

    CAMetalLayer* metalLayer = [CAMetalLayer layer];
    if (!metalLayer) return false;
    CGFloat scale = nsw.backingScaleFactor > 0.0 ? nsw.backingScaleFactor : 1.0;
    metalLayer.contentsScale = scale;
    metalLayer.opaque = YES;
    view.layer = metalLayer;
    view.wantsLayer = YES;
    [view layoutSubtreeIfNeeded];

    EGLDisplay display = EGL_NO_DISPLAY;
    if (!ensure_shared(display)) return false;

    auto& m = mithril();
    EGLSurface surface = m.createWindowSurface(display, g_config,
                                               (__bridge void*)metalLayer, nullptr);
    if (surface == EGL_NO_SURFACE) {
        std::fprintf(stderr, "[sdl-mithril] createWindowSurface failed\n");
        return false;
    }
    WindowState st;
    st.display = display; st.surface = surface;
    st.width = fbw; st.height = fbh;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_windows[sw] = st;
    }
    return true;
}

}  // namespace

extern "C" {

// SDL3 signature: SDL_CreateWindow(title, w, h, flags)
SDL_Window* SDL_CreateWindow(const char* title, int w, int h, Uint32 flags) {
    static auto real = real_sdl_fn<SDL_Window* (*)(const char*, int, int, Uint32)>("SDL_CreateWindow");
    if (!real) { std::fprintf(stderr, "[sdl-mithril] no real SDL_CreateWindow\n"); return nullptr; }
    SDL_Window* sw = real(title, w, h, flags);
    if (!sw) return nullptr;
    // The backing store is typically 2x on Retina; the EGL surface derives its
    // drawable size from the CAMetalLayer. Pixel dimensions are read at swap.
    setup_window(sw, title, w, h);
    return sw;
}

SDL_GLContext SDL_GL_CreateContext(SDL_Window* window) {
    EGLDisplay display = EGL_NO_DISPLAY;
    if (!ensure_shared(display)) return nullptr;
    return (SDL_GLContext)g_shared_context;
}

int SDL_GL_MakeCurrent(SDL_Window* window, SDL_GLContext context) {
    auto& m = mithril();
    if (!window) {
        m.makeCurrent(EGL_NO_DISPLAY, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        return 1;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_windows.find(window);
    if (it == g_windows.end()) { std::fprintf(stderr,"[sdl-mithril] MakeCurrent: no surface for window\n"); return 0; }
    int ok = m.makeCurrent(it->second.display, it->second.surface, it->second.surface,
                         g_shared_context) ? 1 : 0;
    return ok;
}

int SDL_GL_SwapWindow(SDL_Window* window) {
    WindowState st;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_windows.find(window);
        if (it == g_windows.end()) return 0;
        st = it->second;
    }
    // Determine current drawable size for the capture seam.
    int cw = st.width, ch = st.height;
    static auto getDrawableSize =
        real_sdl_fn<int (*)(SDL_Window*, int*, int*)>("SDL_GetWindowSizeInPixels");
    if (getDrawableSize) getDrawableSize(window, &cw, &ch);
    mithril_e2e_capture_before_present(cw, ch, mithril().handle);

    if (mithril().swapBuffers(st.display, st.surface) == EGL_TRUE) {
        g_swap_count.fetch_add(1);
        return 1;
    }
    std::fprintf(stderr, "[sdl-mithril] eglSwapBuffers failed\n");
    return 0;
}

int SDL_GL_SetSwapInterval(int interval) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_display != EGL_NO_DISPLAY)
        mithril().swapInterval(g_display, interval);
    return 1;
}

int SDL_GL_DestroyContext(SDL_GLContext context) {
    // The shared context lives for the whole process; never destroy it here,
    // otherwise a transient probe window's teardown would invalidate the GL
    // objects that survive on the real window.
    return 1;
}

void SDL_DestroyWindow(SDL_Window* window) {
    static auto real = real_sdl_fn<void (*)(SDL_Window*)>("SDL_DestroyWindow");
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_windows.find(window);
        if (it != g_windows.end()) {
            mithril().destroySurface(it->second.display, it->second.surface);
            g_windows.erase(it);
        }
    }
    if (real) real(window);
}

}  // extern "C"
