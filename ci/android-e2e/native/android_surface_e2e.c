#include <jni.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <EGL/egl.h>
#include <GL/glcorearb.h>

#define TAG "MithrilE2E"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

typedef EGLDisplay (*eglGetDisplay_fn)(EGLNativeDisplayType);
typedef EGLBoolean (*eglInitialize_fn)(EGLDisplay, EGLint*, EGLint*);
typedef EGLBoolean (*eglChooseConfig_fn)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*);
typedef EGLBoolean (*eglGetConfigAttrib_fn)(EGLDisplay, EGLConfig, EGLint, EGLint*);
typedef EGLBoolean (*eglBindAPI_fn)(EGLenum);
typedef EGLSurface (*eglCreateWindowSurface_fn)(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint*);
typedef EGLContext (*eglCreateContext_fn)(EGLDisplay, EGLConfig, EGLContext, const EGLint*);
typedef EGLBoolean (*eglMakeCurrent_fn)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
typedef EGLBoolean (*eglQuerySurface_fn)(EGLDisplay, EGLSurface, EGLint, EGLint*);
typedef const char* (*eglQueryString_fn)(EGLDisplay, EGLint);
typedef EGLBoolean (*eglSwapInterval_fn)(EGLDisplay, EGLint);
typedef EGLBoolean (*eglSwapBuffers_fn)(EGLDisplay, EGLSurface);
typedef EGLint (*eglGetError_fn)(void);
typedef EGLBoolean (*eglDestroyContext_fn)(EGLDisplay, EGLContext);
typedef EGLBoolean (*eglDestroySurface_fn)(EGLDisplay, EGLSurface);
typedef EGLBoolean (*eglTerminate_fn)(EGLDisplay);

typedef const GLubyte* (*glGetString_fn)(GLenum);
typedef GLenum (*glGetError_fn)(void);
typedef void (*glViewport_fn)(GLint, GLint, GLsizei, GLsizei);
typedef void (*glClearColor_fn)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (*glClear_fn)(GLbitfield);
typedef GLuint (*glCreateShader_fn)(GLenum);
typedef void (*glShaderSource_fn)(GLuint, GLsizei, const GLchar* const*, const GLint*);
typedef void (*glCompileShader_fn)(GLuint);
typedef void (*glGetShaderiv_fn)(GLuint, GLenum, GLint*);
typedef void (*glGetShaderInfoLog_fn)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef GLuint (*glCreateProgram_fn)(void);
typedef void (*glAttachShader_fn)(GLuint, GLuint);
typedef void (*glLinkProgram_fn)(GLuint);
typedef void (*glGetProgramiv_fn)(GLuint, GLenum, GLint*);
typedef void (*glGetProgramInfoLog_fn)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef void (*glUseProgram_fn)(GLuint);
typedef void (*glGenVertexArrays_fn)(GLsizei, GLuint*);
typedef void (*glBindVertexArray_fn)(GLuint);
typedef void (*glDrawArrays_fn)(GLenum, GLint, GLsizei);
typedef void (*glFinish_fn)(void);
typedef void (*glReadPixels_fn)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
typedef void (*glDeleteShader_fn)(GLuint);
typedef void (*glDeleteProgram_fn)(GLuint);
typedef void (*glDeleteVertexArrays_fn)(GLsizei, const GLuint*);
typedef int (*hook_counter_fn)(void);

static void json_escape(FILE* f, const char* s) {
    if (!s) s = "";
    for (; *s; ++s) {
        unsigned char c = (unsigned char)*s;
        if (c == '\\' || c == '"') {
            fputc('\\', f);
            fputc(c, f);
        } else if (c == '\n') {
            fputs("\\n", f);
        } else if (c == '\r') {
            fputs("\\r", f);
        } else if (c == '\t') {
            fputs("\\t", f);
        } else if (c >= 0x20) {
            fputc(c, f);
        }
    }
}

static void write_failure(const char* path, const char* stage, const char* detail) {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fputs("{\n  \"status\": \"fail\",\n  \"stage\": \"", f);
    json_escape(f, stage);
    fputs("\",\n  \"detail\": \"", f);
    json_escape(f, detail);
    fputs("\"\n}\n", f);
    fclose(f);
}

static void log_shader_error(GLuint shader, glGetShaderInfoLog_fn get_log) {
    char buf[2048] = {0};
    GLsizei n = 0;
    get_log(shader, (GLsizei)(sizeof(buf) - 1), &n, buf);
    LOGE("shader compile log: %s", buf);
}

static void log_program_error(GLuint program, glGetProgramInfoLog_fn get_log) {
    char buf[2048] = {0};
    GLsizei n = 0;
    get_log(program, (GLsizei)(sizeof(buf) - 1), &n, buf);
    LOGE("program link log: %s", buf);
}

#define LOAD_SYM(handle, type, var, name) \
    type var = (type)dlsym((handle), (name)); \
    if (!(var)) { \
        LOGE("missing symbol %s: %s", (name), dlerror()); \
        write_failure(result_path, "resolve_symbol", (name)); \
        ANativeWindow_release(window); \
        dlclose(mithril); \
        return 20; \
    }

JNIEXPORT jint JNICALL
Java_com_mithril_wrapper_e2e_E2EActivity_nativeRun(
        JNIEnv* env,
        jclass clazz,
        jobject surface,
        jstring raw_frame_path,
        jstring result_json_path,
        jboolean turnip_probe,
        jstring native_lib_dir,
        jstring cache_dir) {
    (void)clazz;

    const char* raw_path = (*env)->GetStringUTFChars(env, raw_frame_path, NULL);
    const char* result_path = (*env)->GetStringUTFChars(env, result_json_path, NULL);
    const char* native_dir = (*env)->GetStringUTFChars(env, native_lib_dir, NULL);
    const char* cache_path = (*env)->GetStringUTFChars(env, cache_dir, NULL);

    if (!raw_path || !result_path || !native_dir || !cache_path) return 2;

    setenv("TMPDIR", cache_path, 1);
    setenv("MITHRIL_DEBUG", "1", 1);
    setenv("MITHRIL_GL_VERSION", "4.6", 1);
    setenv("LIBGL_ES", "3", 1);

    if (turnip_probe == JNI_TRUE) {
        setenv("MITHRIL_TURNIP", "1", 1);
        setenv("DRIVER_PATH", native_dir, 1);
        LOGI("mode=hook-fallback DRIVER_PATH=%s", native_dir);
    } else {
        unsetenv("MITHRIL_TURNIP");
        unsetenv("MITHRIL_VULKAN_LIBRARY");
        unsetenv("DRIVER_PATH");
        LOGI("mode=stock");
    }

    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (!window) {
        write_failure(result_path, "native_window", "ANativeWindow_fromSurface returned null");
        return 3;
    }

    void* mithril = dlopen("libmithril.so", RTLD_NOW | RTLD_GLOBAL);
    if (!mithril) {
        const char* err = dlerror();
        LOGE("dlopen libmithril.so failed: %s", err ? err : "unknown");
        write_failure(result_path, "dlopen_mithril", err ? err : "unknown");
        ANativeWindow_release(window);
        return 4;
    }

    LOAD_SYM(mithril, eglGetDisplay_fn, p_eglGetDisplay, "eglGetDisplay");
    LOAD_SYM(mithril, eglInitialize_fn, p_eglInitialize, "eglInitialize");
    LOAD_SYM(mithril, eglChooseConfig_fn, p_eglChooseConfig, "eglChooseConfig");
    LOAD_SYM(mithril, eglGetConfigAttrib_fn, p_eglGetConfigAttrib, "eglGetConfigAttrib");
    LOAD_SYM(mithril, eglBindAPI_fn, p_eglBindAPI, "eglBindAPI");
    LOAD_SYM(mithril, eglCreateWindowSurface_fn, p_eglCreateWindowSurface, "eglCreateWindowSurface");
    LOAD_SYM(mithril, eglCreateContext_fn, p_eglCreateContext, "eglCreateContext");
    LOAD_SYM(mithril, eglMakeCurrent_fn, p_eglMakeCurrent, "eglMakeCurrent");
    LOAD_SYM(mithril, eglQuerySurface_fn, p_eglQuerySurface, "eglQuerySurface");
    LOAD_SYM(mithril, eglQueryString_fn, p_eglQueryString, "eglQueryString");
    LOAD_SYM(mithril, eglSwapInterval_fn, p_eglSwapInterval, "eglSwapInterval");
    LOAD_SYM(mithril, eglSwapBuffers_fn, p_eglSwapBuffers, "eglSwapBuffers");
    LOAD_SYM(mithril, eglGetError_fn, p_eglGetError, "eglGetError");
    LOAD_SYM(mithril, eglDestroyContext_fn, p_eglDestroyContext, "eglDestroyContext");
    LOAD_SYM(mithril, eglDestroySurface_fn, p_eglDestroySurface, "eglDestroySurface");
    LOAD_SYM(mithril, eglTerminate_fn, p_eglTerminate, "eglTerminate");

    LOAD_SYM(mithril, glGetString_fn, p_glGetString, "glGetString");
    LOAD_SYM(mithril, glGetError_fn, p_glGetError, "glGetError");
    LOAD_SYM(mithril, glViewport_fn, p_glViewport, "glViewport");
    LOAD_SYM(mithril, glClearColor_fn, p_glClearColor, "glClearColor");
    LOAD_SYM(mithril, glClear_fn, p_glClear, "glClear");
    LOAD_SYM(mithril, glCreateShader_fn, p_glCreateShader, "glCreateShader");
    LOAD_SYM(mithril, glShaderSource_fn, p_glShaderSource, "glShaderSource");
    LOAD_SYM(mithril, glCompileShader_fn, p_glCompileShader, "glCompileShader");
    LOAD_SYM(mithril, glGetShaderiv_fn, p_glGetShaderiv, "glGetShaderiv");
    LOAD_SYM(mithril, glGetShaderInfoLog_fn, p_glGetShaderInfoLog, "glGetShaderInfoLog");
    LOAD_SYM(mithril, glCreateProgram_fn, p_glCreateProgram, "glCreateProgram");
    LOAD_SYM(mithril, glAttachShader_fn, p_glAttachShader, "glAttachShader");
    LOAD_SYM(mithril, glLinkProgram_fn, p_glLinkProgram, "glLinkProgram");
    LOAD_SYM(mithril, glGetProgramiv_fn, p_glGetProgramiv, "glGetProgramiv");
    LOAD_SYM(mithril, glGetProgramInfoLog_fn, p_glGetProgramInfoLog, "glGetProgramInfoLog");
    LOAD_SYM(mithril, glUseProgram_fn, p_glUseProgram, "glUseProgram");
    LOAD_SYM(mithril, glGenVertexArrays_fn, p_glGenVertexArrays, "glGenVertexArrays");
    LOAD_SYM(mithril, glBindVertexArray_fn, p_glBindVertexArray, "glBindVertexArray");
    LOAD_SYM(mithril, glDrawArrays_fn, p_glDrawArrays, "glDrawArrays");
    LOAD_SYM(mithril, glFinish_fn, p_glFinish, "glFinish");
    LOAD_SYM(mithril, glReadPixels_fn, p_glReadPixels, "glReadPixels");
    LOAD_SYM(mithril, glDeleteShader_fn, p_glDeleteShader, "glDeleteShader");
    LOAD_SYM(mithril, glDeleteProgram_fn, p_glDeleteProgram, "glDeleteProgram");
    LOAD_SYM(mithril, glDeleteVertexArrays_fn, p_glDeleteVertexArrays, "glDeleteVertexArrays");

    hook_counter_fn hook_intercepts =
        (hook_counter_fn)dlsym(mithril, "mithrilAndroidVkHookIntercepts");
    hook_counter_fn hook_redirects =
        (hook_counter_fn)dlsym(mithril, "mithrilAndroidVkHookRedirects");
    if (!hook_intercepts || !hook_redirects) {
        LOGE("missing runtime hook counter exports");
        write_failure(result_path, "resolve_hook_counters", "missing hook counter exports");
        ANativeWindow_release(window);
        dlclose(mithril);
        return 21;
    }

    EGLDisplay display = p_eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
        write_failure(result_path, "eglGetDisplay", "EGL_NO_DISPLAY");
        ANativeWindow_release(window);
        dlclose(mithril);
        return 5;
    }

    EGLint egl_major = 0, egl_minor = 0;
    if (p_eglInitialize(display, &egl_major, &egl_minor) != EGL_TRUE) {
        char msg[64];
        snprintf(msg, sizeof(msg), "eglError=0x%x", p_eglGetError());
        write_failure(result_path, "eglInitialize", msg);
        ANativeWindow_release(window);
        dlclose(mithril);
        return 6;
    }

    const EGLint attrs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_NONE
    };

    EGLint count = 0;
    if (p_eglChooseConfig(display, attrs, NULL, 0, &count) != EGL_TRUE || count < 1) {
        write_failure(result_path, "eglChooseConfig_count", "no matching config");
        ANativeWindow_release(window);
        dlclose(mithril);
        return 7;
    }

    EGLConfig config = 0;
    if (p_eglChooseConfig(display, attrs, &config, 1, &count) != EGL_TRUE || !config) {
        write_failure(result_path, "eglChooseConfig", "config selection failed");
        ANativeWindow_release(window);
        dlclose(mithril);
        return 8;
    }

    EGLint visual_id = 0;
    if (p_eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &visual_id) != EGL_TRUE) {
        write_failure(result_path, "eglGetConfigAttrib", "EGL_NATIVE_VISUAL_ID failed");
        ANativeWindow_release(window);
        dlclose(mithril);
        return 9;
    }

    // This is deliberately the same transition FCL performs after querying
    // EGL_NATIVE_VISUAL_ID. A bad EGL token mapping becomes an immediate,
    // visible E2E failure instead of a later opaque Surface error.
    int geometry_rc = ANativeWindow_setBuffersGeometry(window, 960, 540, visual_id);
    if (geometry_rc != 0) {
        char msg[96];
        snprintf(msg, sizeof(msg), "visual_id=%d rc=%d", visual_id, geometry_rc);
        write_failure(result_path, "ANativeWindow_setBuffersGeometry", msg);
        ANativeWindow_release(window);
        dlclose(mithril);
        return 10;
    }

    if (p_eglBindAPI(EGL_OPENGL_ES_API) != EGL_TRUE) {
        write_failure(result_path, "eglBindAPI", "EGL_OPENGL_ES_API rejected");
        ANativeWindow_release(window);
        dlclose(mithril);
        return 11;
    }

    EGLSurface egl_surface =
        p_eglCreateWindowSurface(display, config, (EGLNativeWindowType)window, NULL);
    if (egl_surface == EGL_NO_SURFACE) {
        char msg[64];
        snprintf(msg, sizeof(msg), "eglError=0x%x", p_eglGetError());
        write_failure(result_path, "eglCreateWindowSurface", msg);
        ANativeWindow_release(window);
        dlclose(mithril);
        return 12;
    }

    const EGLint ctx_attrs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };
    EGLContext context =
        p_eglCreateContext(display, config, EGL_NO_CONTEXT, ctx_attrs);
    if (context == EGL_NO_CONTEXT) {
        write_failure(result_path, "eglCreateContext", "EGL_NO_CONTEXT");
        p_eglDestroySurface(display, egl_surface);
        ANativeWindow_release(window);
        dlclose(mithril);
        return 13;
    }

    if (p_eglMakeCurrent(display, egl_surface, egl_surface, context) != EGL_TRUE) {
        write_failure(result_path, "eglMakeCurrent", "make-current failed");
        p_eglDestroyContext(display, context);
        p_eglDestroySurface(display, egl_surface);
        ANativeWindow_release(window);
        dlclose(mithril);
        return 14;
    }

    EGLint width = 0, height = 0;
    p_eglQuerySurface(display, egl_surface, EGL_WIDTH, &width);
    p_eglQuerySurface(display, egl_surface, EGL_HEIGHT, &height);
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) {
        write_failure(result_path, "eglQuerySurface", "invalid drawable size");
        return 15;
    }

    const char* egl_vendor = p_eglQueryString(display, EGL_VENDOR);
    const char* egl_version = p_eglQueryString(display, EGL_VERSION);
    const char* gl_vendor = (const char*)p_glGetString(GL_VENDOR);
    const char* gl_renderer = (const char*)p_glGetString(GL_RENDERER);
    const char* gl_version = (const char*)p_glGetString(GL_VERSION);

    LOGI("EGL %d.%d vendor=%s version=%s", egl_major, egl_minor,
         egl_vendor ? egl_vendor : "(null)", egl_version ? egl_version : "(null)");
    LOGI("GL vendor=%s renderer=%s version=%s",
         gl_vendor ? gl_vendor : "(null)",
         gl_renderer ? gl_renderer : "(null)",
         gl_version ? gl_version : "(null)");

    p_eglSwapInterval(display, 0);
    p_glViewport(0, 0, width, height);
    p_glClearColor(0.02f, 0.03f, 0.05f, 1.0f);
    p_glClear(GL_COLOR_BUFFER_BIT);

    const char* vs_src =
        "#version 330 core\n"
        "const vec2 P[3] = vec2[3](vec2(-1.0,-1.0), vec2(3.0,-1.0), vec2(-1.0,3.0));\n"
        "const vec3 C[3] = vec3[3](vec3(1.0,0.05,0.05), vec3(0.05,1.0,0.05), vec3(0.05,0.2,1.0));\n"
        "out vec3 vColor;\n"
        "void main(){ gl_Position=vec4(P[gl_VertexID],0.0,1.0); vColor=C[gl_VertexID]; }\n";
    const char* fs_src =
        "#version 330 core\n"
        "in vec3 vColor; out vec4 fragColor;\n"
        "void main(){ fragColor=vec4(vColor,1.0); }\n";

    GLuint vs = p_glCreateShader(GL_VERTEX_SHADER);
    GLuint fs = p_glCreateShader(GL_FRAGMENT_SHADER);
    p_glShaderSource(vs, 1, &vs_src, NULL);
    p_glShaderSource(fs, 1, &fs_src, NULL);
    p_glCompileShader(vs);
    p_glCompileShader(fs);
    GLint vs_ok = 0, fs_ok = 0;
    p_glGetShaderiv(vs, GL_COMPILE_STATUS, &vs_ok);
    p_glGetShaderiv(fs, GL_COMPILE_STATUS, &fs_ok);
    if (!vs_ok || !fs_ok) {
        if (!vs_ok) log_shader_error(vs, p_glGetShaderInfoLog);
        if (!fs_ok) log_shader_error(fs, p_glGetShaderInfoLog);
        write_failure(result_path, "shader_compile", "GLSL 330 shader compile failed");
        return 16;
    }

    GLuint program = p_glCreateProgram();
    p_glAttachShader(program, vs);
    p_glAttachShader(program, fs);
    p_glLinkProgram(program);
    GLint linked = 0;
    p_glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        log_program_error(program, p_glGetProgramInfoLog);
        write_failure(result_path, "program_link", "program link failed");
        return 17;
    }

    GLuint vao = 0;
    p_glGenVertexArrays(1, &vao);
    p_glBindVertexArray(vao);
    p_glUseProgram(program);
    p_glDrawArrays(GL_TRIANGLES, 0, 3);
    p_glFinish();

    if (p_glGetError() != GL_NO_ERROR) {
        write_failure(result_path, "gpu_draw", "GL error after draw/finish");
        return 18;
    }

    size_t bytes = (size_t)width * (size_t)height * 4u;
    unsigned char* rgba = (unsigned char*)malloc(bytes);
    if (!rgba) {
        write_failure(result_path, "allocate_readback", "malloc failed");
        return 19;
    }
    memset(rgba, 0, bytes);
    p_glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (p_glGetError() != GL_NO_ERROR) {
        free(rgba);
        write_failure(result_path, "glReadPixels", "GL error after readback");
        return 22;
    }

    FILE* raw = fopen(raw_path, "wb");
    if (!raw || fwrite(rgba, 1, bytes, raw) != bytes) {
        if (raw) fclose(raw);
        free(rgba);
        write_failure(result_path, "write_readback", strerror(errno));
        return 23;
    }
    fclose(raw);

    // Cheap but robust non-uniformity probe: quantize a few thousand samples.
    uint32_t unique[256] = {0};
    int unique_count = 0;
    int min_luma = 255, max_luma = 0;
    const size_t pixels = (size_t)width * (size_t)height;
    size_t step = pixels / 4096u;
    if (step < 1) step = 1;
    for (size_t p = 0; p < pixels; p += step) {
        const unsigned char* px = rgba + p * 4u;
        int luma = (px[0] + px[1] + px[2]) / 3;
        if (luma < min_luma) min_luma = luma;
        if (luma > max_luma) max_luma = luma;
        uint32_t key = ((uint32_t)(px[0] >> 2) << 16)
                     | ((uint32_t)(px[1] >> 2) << 8)
                     | ((uint32_t)(px[2] >> 2));
        int seen = 0;
        for (int i = 0; i < unique_count; ++i) {
            if (unique[i] == key) { seen = 1; break; }
        }
        if (!seen && unique_count < 256) unique[unique_count++] = key;
    }

    const size_t center = ((size_t)(height / 2) * (size_t)width + (size_t)(width / 2)) * 4u;
    unsigned center_rgba[4] = {
        rgba[center + 0], rgba[center + 1], rgba[center + 2], rgba[center + 3]
    };

    // Present twice. The second draw forces the post-present acquire/reuse path
    // instead of proving only the first swapchain image.
    int swap_count = 0;
    if (p_eglSwapBuffers(display, egl_surface) == EGL_TRUE) ++swap_count;

    p_glViewport(0, 0, width, height);
    p_glClearColor(0.02f, 0.03f, 0.05f, 1.0f);
    p_glClear(GL_COLOR_BUFFER_BIT);
    p_glUseProgram(program);
    p_glBindVertexArray(vao);
    p_glDrawArrays(GL_TRIANGLES, 0, 3);
    p_glFinish();
    if (p_eglSwapBuffers(display, egl_surface) == EGL_TRUE) ++swap_count;

    int intercepts = hook_intercepts();
    int redirects = hook_redirects();

    FILE* out = fopen(result_path, "wb");
    if (!out) {
        free(rgba);
        return 24;
    }
    fprintf(out, "{\n");
    fprintf(out, "  \"status\": \"pass\",\n");
    fprintf(out, "  \"mode\": \"%s\",\n",
            turnip_probe == JNI_TRUE ? "hook-fallback" : "stock");
    fprintf(out, "  \"egl_major\": %d, \"egl_minor\": %d,\n", egl_major, egl_minor);
    fprintf(out, "  \"visual_id\": %d,\n", visual_id);
    fprintf(out, "  \"width\": %d, \"height\": %d,\n", width, height);
    fprintf(out, "  \"swap_count\": %d,\n", swap_count);
    fprintf(out, "  \"hook_intercepts\": %d, \"hook_redirects\": %d,\n",
            intercepts, redirects);
    fprintf(out, "  \"sample_unique_colors\": %d,\n", unique_count);
    fprintf(out, "  \"sample_luma_min\": %d, \"sample_luma_max\": %d,\n",
            min_luma, max_luma);
    fprintf(out, "  \"center_rgba\": [%u,%u,%u,%u],\n",
            center_rgba[0], center_rgba[1], center_rgba[2], center_rgba[3]);

    fputs("  \"egl_vendor\": \"", out); json_escape(out, egl_vendor); fputs("\",\n", out);
    fputs("  \"egl_version\": \"", out); json_escape(out, egl_version); fputs("\",\n", out);
    fputs("  \"gl_vendor\": \"", out); json_escape(out, gl_vendor); fputs("\",\n", out);
    fputs("  \"gl_renderer\": \"", out); json_escape(out, gl_renderer); fputs("\",\n", out);
    fputs("  \"gl_version\": \"", out); json_escape(out, gl_version); fputs("\"\n", out);
    fprintf(out, "}\n");
    fclose(out);

    LOGI("PASS mode=%s size=%dx%d visual=%d swaps=%d unique=%d luma=%d..%d hook=%d/%d",
         turnip_probe == JNI_TRUE ? "hook-fallback" : "stock",
         width, height, visual_id, swap_count, unique_count,
         min_luma, max_luma, intercepts, redirects);

    free(rgba);
    p_glDeleteVertexArrays(1, &vao);
    p_glDeleteProgram(program);
    p_glDeleteShader(vs);
    p_glDeleteShader(fs);
    p_eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    p_eglDestroyContext(display, context);
    p_eglDestroySurface(display, egl_surface);
    p_eglTerminate(display);
    ANativeWindow_release(window);
    dlclose(mithril);

    (*env)->ReleaseStringUTFChars(env, raw_frame_path, raw_path);
    (*env)->ReleaseStringUTFChars(env, result_json_path, result_path);
    (*env)->ReleaseStringUTFChars(env, native_lib_dir, native_dir);
    (*env)->ReleaseStringUTFChars(env, cache_dir, cache_path);
    return 0;
}
