#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <string>

extern "C" void mithril_e2e_capture_before_present(int width, int height, void* gl_handle);

namespace {
std::string env_string(const char* name) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

void* delegate_handle() {
    static void* handle = [] {
        std::string path = env_string("MITHRIL_E2E_GLFW_DELEGATE");
        void* h = nullptr;
        if (!path.empty()) h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!h) h = dlopen("libglfw.3.dylib", RTLD_NOW | RTLD_LOCAL);
        if (!h) std::fprintf(stderr, "[native-e2e] failed to load delegate GLFW: %s\n", dlerror());
        return h;
    }();
    return handle;
}

template <typename T>
T real_glfw(const char* name) {
    void* h = delegate_handle();
    return h ? reinterpret_cast<T>(dlsym(h, name)) : nullptr;
}

void* native_gl_handle() {
    static void* handle = [] {
        void* h = dlopen("/System/Library/Frameworks/OpenGL.framework/OpenGL",
                         RTLD_NOW | RTLD_LOCAL);
        if (!h) std::fprintf(stderr, "[native-e2e] failed to load OpenGL.framework: %s\n", dlerror());
        return h;
    }();
    return handle;
}
}

extern "C" void glfwSwapBuffers(GLFWwindow* window) {
    using GetFramebufferSize = void (*)(GLFWwindow*, int*, int*);
    using SwapBuffers = void (*)(GLFWwindow*);
    auto getSize = real_glfw<GetFramebufferSize>("glfwGetFramebufferSize");
    auto swap = real_glfw<SwapBuffers>("glfwSwapBuffers");
    if (!swap) {
        std::fprintf(stderr, "[native-e2e] delegate glfwSwapBuffers missing\n");
        return;
    }
    if (getSize) {
        int width = 0, height = 0;
        getSize(window, &width, &height);
        if (width > 1 && height > 1) {
            mithril_e2e_capture_before_present(width, height, native_gl_handle());
        }
    }
    swap(window);
}
