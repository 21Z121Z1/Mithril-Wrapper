#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <EGL/egl.h>
#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

#include <dlfcn.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

extern "C" void mithril_e2e_capture_before_present(int width, int height, void* mithril_handle);

namespace {
struct ContextState {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
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
    const GLubyte* (*getString)(GLenum) = nullptr;
};

std::mutex g_mutex;
std::unordered_map<GLFWwindow*, ContextState> g_contexts;
thread_local GLFWwindow* g_current = nullptr;
std::atomic<unsigned long long> g_context_count{0};
std::atomic<unsigned long long> g_swap_count{0};
std::atomic<unsigned long long> g_proc_count{0};
std::mutex g_identity_mutex;
std::string g_identity_vendor;
std::string g_identity_renderer;
std::string g_identity_version;

std::string getenv_string(const char* name) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

std::string json_escape(const char* s) {
    if (!s) return "";
    std::string out;
    for (; *s; ++s) {
        switch (*s) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += *s; break;
        }
    }
    return out;
}

void* delegate_handle() {
    static void* handle = [] {
        std::string path = getenv_string("MITHRIL_E2E_GLFW_DELEGATE");
        void* h = nullptr;
        if (!path.empty()) h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!h) h = dlopen("libglfw.3.dylib", RTLD_NOW | RTLD_LOCAL);
        if (!h) std::fprintf(stderr, "[mithril-e2e] failed to load delegate GLFW: %s\n", dlerror());
        return h;
    }();
    return handle;
}

template <typename T>
T real_glfw(const char* name) {
    void* h = delegate_handle();
    return h ? reinterpret_cast<T>(dlsym(h, name)) : nullptr;
}

MithrilApi& mithril() {
    static MithrilApi api = [] {
        MithrilApi m;
        std::string path = getenv_string("MITHRIL_E2E_MITHRIL_DYLIB");
        if (path.empty()) {
            std::fprintf(stderr, "[mithril-e2e] MITHRIL_E2E_MITHRIL_DYLIB is unset\n");
            return m;
        }
        m.handle = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
        if (!m.handle) {
            std::fprintf(stderr, "[mithril-e2e] dlopen(%s) failed: %s\n", path.c_str(), dlerror());
            return m;
        }
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
        m.getString = reinterpret_cast<decltype(m.getString)>(dlsym(m.handle, "glGetString"));
        return m;
    }();
    return api;
}

bool mithril_ready() {
    auto& m = mithril();
    return m.handle && m.getDisplay && m.initialize && m.chooseConfig && m.bindAPI &&
           m.createWindowSurface && m.createContext && m.makeCurrent && m.swapBuffers &&
           m.swapInterval && m.destroySurface && m.destroyContext && m.getString;
}

const char* loaded_mithril_path() {
    Dl_info info{};
    auto& m = mithril();
    if (m.getDisplay && dladdr(reinterpret_cast<void*>(m.getDisplay), &info) && info.dli_fname) {
        return info.dli_fname;
    }
    return "";
}

void write_state(const char* stage) {
    std::string root = getenv_string("MITHRIL_E2E_ROOT");
    if (root.empty()) return;
    std::string path = root + "/bridge-state.json";
    std::string tmp = path + ".tmp";
    std::string vendor;
    std::string renderer;
    std::string version;
    auto& m = mithril();
    {
        std::lock_guard<std::mutex> identityLock(g_identity_mutex);
        if (g_current && m.getString) {
            const char* v = reinterpret_cast<const char*>(m.getString(GL_VENDOR));
            const char* r = reinterpret_cast<const char*>(m.getString(GL_RENDERER));
            const char* ver = reinterpret_cast<const char*>(m.getString(GL_VERSION));
            if (v && *v) g_identity_vendor = v;
            if (r && *r) g_identity_renderer = r;
            if (ver && *ver) g_identity_version = ver;
        }
        /* Keep the last authoritative identity after glfwMakeContextCurrent(NULL)
         * and glfwDestroyWindow().  The post-process oracle runs after JVM exit. */
        vendor = g_identity_vendor;
        renderer = g_identity_renderer;
        version = g_identity_version;
    }
    FILE* f = std::fopen(tmp.c_str(), "w");
    if (!f) return;
    std::fprintf(f,
        "{\n"
        "  \"schema_version\": \"1.0\",\n"
        "  \"stage\": \"%s\",\n"
        "  \"mithril_path\": \"%s\",\n"
        "  \"context_count\": %llu,\n"
        "  \"swap_count\": %llu,\n"
        "  \"get_proc_address_count\": %llu,\n"
        "  \"current_context\": %s,\n"
        "  \"gl_vendor\": \"%s\",\n"
        "  \"gl_renderer\": \"%s\",\n"
        "  \"gl_version\": \"%s\"\n"
        "}\n",
        json_escape(stage).c_str(), json_escape(loaded_mithril_path()).c_str(),
        g_context_count.load(), g_swap_count.load(), g_proc_count.load(),
        g_current ? "true" : "false",
        json_escape(vendor.c_str()).c_str(), json_escape(renderer.c_str()).c_str(), json_escape(version.c_str()).c_str());
    std::fclose(f);
    std::rename(tmp.c_str(), path.c_str());
}

void emit_event(const char* event, const char* message) {
    std::string root = getenv_string("MITHRIL_E2E_ROOT");
    if (root.empty()) return;
    FILE* f = std::fopen((root + "/native-events.jsonl").c_str(), "a");
    if (!f) return;
    std::fprintf(f,
        "{\"schema_version\":\"1.0\",\"producer\":\"glfw-mithril-bridge\","
        "\"event\":\"%s\",\"message\":\"%s\",\"mithril_path\":\"%s\"}\n",
        json_escape(event).c_str(), json_escape(message).c_str(),
        json_escape(loaded_mithril_path()).c_str());
    std::fclose(f);
}

bool ignored_context_hint(int hint) {
    switch (hint) {
        case GLFW_CLIENT_API:
        case GLFW_CONTEXT_VERSION_MAJOR:
        case GLFW_CONTEXT_VERSION_MINOR:
        case GLFW_CONTEXT_ROBUSTNESS:
        case GLFW_OPENGL_FORWARD_COMPAT:
        case GLFW_OPENGL_DEBUG_CONTEXT:
        case GLFW_OPENGL_PROFILE:
        case GLFW_CONTEXT_RELEASE_BEHAVIOR:
        case GLFW_CONTEXT_NO_ERROR:
        case GLFW_CONTEXT_CREATION_API:
            return true;
        default:
            return false;
    }
}
} // namespace

extern "C" {

void glfwWindowHint(int hint, int value) {
    using Fn = void (*)(int, int);
    Fn fn = real_glfw<Fn>("glfwWindowHint");
    if (!fn) return;
    if (ignored_context_hint(hint)) {
        if (hint == GLFW_CLIENT_API) fn(GLFW_CLIENT_API, GLFW_NO_API);
        return;
    }
    fn(hint, value);
}

GLFWwindow* glfwCreateWindow(int width, int height, const char* title,
                             GLFWmonitor* monitor, GLFWwindow* share) {
    using CreateFn = GLFWwindow* (*)(int, int, const char*, GLFWmonitor*, GLFWwindow*);
    using HintFn = void (*)(int, int);
    using CocoaFn = id (*)(GLFWwindow*);
    CreateFn create = real_glfw<CreateFn>("glfwCreateWindow");
    HintFn hint = real_glfw<HintFn>("glfwWindowHint");
    CocoaFn getCocoa = real_glfw<CocoaFn>("glfwGetCocoaWindow");
    if (!create || !hint || !getCocoa || !mithril_ready()) {
        emit_event("bridge_error", "missing delegate GLFW or Mithril EGL symbols");
        return nullptr;
    }
    hint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = create(width, height, title, monitor, nullptr);
    if (!window) {
        emit_event("bridge_error", "delegate GLFW could not create NO_API Cocoa window");
        return nullptr;
    }

    NSWindow* cocoa = (NSWindow*)getCocoa(window);
    NSView* view = cocoa.contentView;
    view.wantsLayer = YES;
    [view layoutSubtreeIfNeeded];
    if (!view.layer) {
        auto destroy = real_glfw<void (*)(GLFWwindow*)>("glfwDestroyWindow");
        if (destroy) destroy(window);
        emit_event("bridge_error", "Cocoa contentView has no backing layer");
        return nullptr;
    }
    CAMetalLayer* layer = [CAMetalLayer layer];
    layer.frame = view.bounds;
    view.layer = layer;

    auto& m = mithril();
    EGLDisplay display = m.getDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY || !m.initialize(display, nullptr, nullptr)) {
        emit_event("bridge_error", "Mithril eglInitialize failed");
        return nullptr;
    }
    EGLint attributes[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_NONE
    };
    EGLConfig config = nullptr;
    EGLint count = 0;
    if (!m.chooseConfig(display, attributes, &config, 1, &count) || count < 1 || !config) {
        emit_event("bridge_error", "Mithril eglChooseConfig failed");
        return nullptr;
    }
    if (!m.bindAPI(EGL_OPENGL_API)) {
        emit_event("bridge_error", "Mithril eglBindAPI(EGL_OPENGL_API) failed");
        return nullptr;
    }
    EGLSurface surface = m.createWindowSurface(display, config,
        reinterpret_cast<EGLNativeWindowType>(layer), nullptr);
    if (surface == EGL_NO_SURFACE) {
        emit_event("bridge_error", "Mithril eglCreateWindowSurface(CAMetalLayer) failed");
        return nullptr;
    }
    EGLContext context = m.createContext(display, config, EGL_NO_CONTEXT, nullptr);
    if (context == EGL_NO_CONTEXT) {
        emit_event("bridge_error", "Mithril eglCreateContext failed");
        return nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_contexts[window] = ContextState{display, context, surface};
    }
    g_context_count.fetch_add(1);
    emit_event("bridge_context_created", "Cocoa CAMetalLayer -> Mithril EGL context ready");
    write_state("context_created");
    (void)share;
    return window;
}

void glfwMakeContextCurrent(GLFWwindow* window) {
    auto& m = mithril();
    if (!window) {
        if (g_current) {
            ContextState state{};
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                auto it = g_contexts.find(g_current);
                if (it != g_contexts.end()) state = it->second;
            }
            if (state.display != EGL_NO_DISPLAY) {
                m.makeCurrent(state.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            }
        }
        g_current = nullptr;
        write_state("context_cleared");
        return;
    }
    ContextState state{};
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_contexts.find(window);
        if (it == g_contexts.end()) return;
        state = it->second;
    }
    if (m.makeCurrent(state.display, state.surface, state.surface, state.context)) {
        g_current = window;
        emit_event("bridge_context_current", "Mithril EGL context made current");
        write_state("context_current");
    }
}

GLFWwindow* glfwGetCurrentContext(void) {
    return g_current;
}

void glfwSwapInterval(int interval) {
    if (!g_current) return;
    ContextState state{};
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_contexts.find(g_current);
        if (it == g_contexts.end()) return;
        state = it->second;
    }
    mithril().swapInterval(state.display, interval);
}

void glfwSwapBuffers(GLFWwindow* window) {
    ContextState state{};
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_contexts.find(window);
        if (it == g_contexts.end()) return;
        state = it->second;
    }
    auto [width, height] = std::pair<int, int>{0, 0};
    using FbSizeFn = void (*)(GLFWwindow*, int*, int*);
    FbSizeFn fbsize = real_glfw<FbSizeFn>("glfwGetFramebufferSize");
    if (fbsize) fbsize(window, &width, &height);
    mithril_e2e_capture_before_present(width, height, mithril().handle);
    if (!mithril().swapBuffers(state.display, state.surface)) {
        emit_event("bridge_error", "Mithril eglSwapBuffers failed");
        return;
    }
    g_swap_count.fetch_add(1);
    emit_event("bridge_swap_present", "eglSwapBuffers submitted DirectMetal present");
    write_state("swap_presented");
}

GLFWglproc glfwGetProcAddress(const char* name) {
    g_proc_count.fetch_add(1);
    void* p = mithril().handle ? dlsym(mithril().handle, name) : nullptr;
    if (!p) {
        emit_event("bridge_symbol_missing", name ? name : "<null>");
    }
    return reinterpret_cast<GLFWglproc>(p);
}

void glfwDestroyWindow(GLFWwindow* window) {
    ContextState state{};
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_contexts.find(window);
        if (it != g_contexts.end()) {
            state = it->second;
            g_contexts.erase(it);
        }
    }
    auto& m = mithril();
    if (state.display != EGL_NO_DISPLAY) {
        m.makeCurrent(state.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (state.surface != EGL_NO_SURFACE) m.destroySurface(state.display, state.surface);
        if (state.context != EGL_NO_CONTEXT) m.destroyContext(state.display, state.context);
    }
    if (g_current == window) g_current = nullptr;
    using Fn = void (*)(GLFWwindow*);
    Fn fn = real_glfw<Fn>("glfwDestroyWindow");
    if (fn) fn(window);
    write_state("window_destroyed");
}

} // extern "C"
