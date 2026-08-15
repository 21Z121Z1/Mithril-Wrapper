#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <string>

extern "C" void mithril_e2e_capture_before_present(int width, int height, void* gl_handle);

namespace {
std::string env_string(const char* name) { const char* v = std::getenv(name); return v ? std::string(v) : std::string(); }
void* delegate_handle() {
    static void* h = [] {
        std::string path = env_string("MITHRIL_E2E_GLFW_DELEGATE");
        void* p = !path.empty() ? dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL) : nullptr;
        if (!p) p = dlopen("libglfw.so.3", RTLD_NOW | RTLD_LOCAL);
        if (!p) std::fprintf(stderr, "[linux-native-e2e] GLFW delegate load failed: %s\n", dlerror());
        return p;
    }();
    return h;
}
template <typename T> T real_glfw(const char* name) { void* h = delegate_handle(); return h ? reinterpret_cast<T>(dlsym(h, name)) : nullptr; }
void* gl_handle() {
    static void* h = [] { void* p = dlopen("libGL.so.1", RTLD_NOW | RTLD_LOCAL); if (!p) std::fprintf(stderr, "[linux-native-e2e] libGL load failed: %s\n", dlerror()); return p; }();
    return h;
}
}

extern "C" void glfwSwapBuffers(GLFWwindow* window) {
    using GetFramebufferSize = void (*)(GLFWwindow*, int*, int*);
    using SwapBuffers = void (*)(GLFWwindow*);
    auto getSize = real_glfw<GetFramebufferSize>("glfwGetFramebufferSize");
    auto swap = real_glfw<SwapBuffers>("glfwSwapBuffers");
    if (!swap) return;
    int w = 0, h = 0;
    if (getSize) getSize(window, &w, &h);
    if (w > 1 && h > 1) mithril_e2e_capture_before_present(w, h, gl_handle());
    swap(window);
}
