/* Contract smoke test: dlopen libmithril.so and replay Amethyst's EGL init. */
#include <dlfcn.h>
#include <stdio.h>
#include <assert.h>

#if defined(__APPLE__) && defined(__OBJC__)
#import <QuartzCore/CAMetalLayer.h>
#endif

#define EGL_DEFAULT_DISPLAY 0
#define EGL_RED_SIZE 0x3024
#define EGL_GREEN_SIZE 0x3023
#define EGL_BLUE_SIZE 0x3022
#define EGL_ALPHA_SIZE 0x3021
#define EGL_DEPTH_SIZE 0x3025
#define EGL_SURFACE_TYPE 0x3033
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_WINDOW_BIT 0x0004
#define EGL_PBUFFER_BIT 0x0001
#define EGL_OPENGL_BIT 0x0008
#define EGL_OPENGL_API 0x30A2
#define EGL_NATIVE_VISUAL_ID 0x302E
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#define EGL_WIDTH 0x3057
#define EGL_HEIGHT 0x3056
#define EGL_NONE 0x3038
#define GL_COLOR_BUFFER_BIT 0x00004000

typedef void* (*eglGetDisplay_fn)(int);
typedef int   (*eglInitialize_fn)(void*, int*, int*);
typedef int   (*eglChooseConfig_fn)(void*, const int*, void**, int, int*);
typedef int   (*eglGetConfigAttrib_fn)(void*, void*, int, int*);
typedef int   (*eglBindAPI_fn)(int);
typedef void* (*eglCreateContext_fn)(void*, void*, void*, const int*);
typedef void* (*eglCreateWindowSurface_fn)(void*, void*, void*, const int*);
typedef int   (*eglMakeCurrent_fn)(void*, void*, void*, void*);
typedef int   (*eglQuerySurface_fn)(void*, void*, int, int*);
typedef int   (*eglSwapBuffers_fn)(void*, void*);
typedef int   (*eglTerminate_fn)(void*);
typedef void  (*glClearColor_fn)(float, float, float, float);
typedef void  (*glClear_fn)(unsigned int);
typedef void  (*glFinish_fn)(void);

int main(void) {
    void* h = dlopen("./output/libmithril.so", RTLD_NOW | RTLD_GLOBAL);
    if (!h) { printf("dlopen: %s\n", dlerror()); return 1; }

    eglGetDisplay_fn        eglGetDisplay        = (eglGetDisplay_fn)dlsym(h, "eglGetDisplay");
    eglInitialize_fn        eglInitialize        = (eglInitialize_fn)dlsym(h, "eglInitialize");
    eglChooseConfig_fn      eglChooseConfig      = (eglChooseConfig_fn)dlsym(h, "eglChooseConfig");
    eglGetConfigAttrib_fn   eglGetConfigAttrib   = (eglGetConfigAttrib_fn)dlsym(h, "eglGetConfigAttrib");
    eglBindAPI_fn           eglBindAPI           = (eglBindAPI_fn)dlsym(h, "eglBindAPI");
    eglCreateContext_fn     eglCreateContext     = (eglCreateContext_fn)dlsym(h, "eglCreateContext");
    eglCreateWindowSurface_fn eglCreateWindowSurface = (eglCreateWindowSurface_fn)dlsym(h, "eglCreateWindowSurface");
    eglMakeCurrent_fn       eglMakeCurrent       = (eglMakeCurrent_fn)dlsym(h, "eglMakeCurrent");
    eglQuerySurface_fn      eglQuerySurface      = (eglQuerySurface_fn)dlsym(h, "eglQuerySurface");
    eglSwapBuffers_fn       eglSwapBuffers       = (eglSwapBuffers_fn)dlsym(h, "eglSwapBuffers");
    eglTerminate_fn         eglTerminate         = (eglTerminate_fn)dlsym(h, "eglTerminate");
    glClearColor_fn         glClearColor         = (glClearColor_fn)dlsym(h, "glClearColor");
    glClear_fn              glClear              = (glClear_fn)dlsym(h, "glClear");
    glFinish_fn             glFinish             = (glFinish_fn)dlsym(h, "glFinish");

    if (!(eglGetDisplay && eglInitialize && eglChooseConfig && eglGetConfigAttrib &&
          eglBindAPI && eglMakeCurrent && eglCreateContext && eglCreateWindowSurface &&
          eglQuerySurface && eglSwapBuffers && eglTerminate &&
          glClearColor && glClear && glFinish))
        { printf("missing symbols\n"); return 2; }

    int major = 0, minor = 0, n = 0;
    void* dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!dpy) { printf("eglGetDisplay failed\n"); return 3; }
    if (!eglInitialize(dpy, &major, &minor)) { printf("eglInitialize failed\n"); return 4; }
    printf("eglInitialize: %d.%d\n", major, minor);

    const int attribs[] = {
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24, EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE
    };
    void* cfg = NULL;
    if (!eglChooseConfig(dpy, attribs, &cfg, 1, &n) || n < 1 || !cfg) {
        printf("eglChooseConfig failed (n=%d)\n", n); return 5;
    }
    printf("eglChooseConfig: %d config(s)\n", n);

    int vid = -1;
    eglGetConfigAttrib(dpy, cfg, EGL_NATIVE_VISUAL_ID, &vid);
    printf("EGL_NATIVE_VISUAL_ID: %d\n", vid);

    if (!eglBindAPI(EGL_OPENGL_API)) { printf("eglBindAPI failed\n"); return 6; }

    const int ctx_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    void* ctx = eglCreateContext(dpy, cfg, NULL, ctx_attribs);
    if (!ctx) { printf("eglCreateContext failed\n"); return 7; }

#if defined(__APPLE__) && defined(__OBJC__)
    CAMetalLayer* metal_layer = [CAMetalLayer layer];
    metal_layer.drawableSize = CGSizeMake(64, 64);
    void* win = (__bridge void*)metal_layer;
#else
    /* The Vulkan backend does not consume the platform window yet. */
    void* win = (void*)0x1234;
#endif
    void* surf = eglCreateWindowSurface(dpy, cfg, win, NULL);
    if (!surf) { printf("eglCreateWindowSurface failed\n"); return 8; }

    if (!eglMakeCurrent(dpy, surf, surf, ctx)) { printf("eglMakeCurrent failed\n"); return 9; }
    int surface_width = 0, surface_height = 0;
    if (!eglQuerySurface(dpy, surf, EGL_WIDTH, &surface_width) ||
        !eglQuerySurface(dpy, surf, EGL_HEIGHT, &surface_height)) {
        printf("eglQuerySurface failed\n"); return 10;
    }
#if defined(__APPLE__) && defined(__OBJC__)
    if (surface_width != 64 || surface_height != 64) {
        printf("unexpected surface size: %dx%d\n", surface_width, surface_height); return 11;
    }
#endif
    glClearColor(0.125f, 0.25f, 0.5f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!eglSwapBuffers(dpy, surf)) { printf("eglSwapBuffers failed\n"); return 10; }
    /* Wait only for validation: production eglSwapBuffers remains nonblocking. */
    glFinish();
#if defined(__APPLE__) && defined(__OBJC__)
    metal_layer.drawableSize = CGSizeMake(96, 48);
    glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!eglSwapBuffers(dpy, surf)) { printf("resized eglSwapBuffers failed\n"); return 12; }
    glFinish();
    if (!eglQuerySurface(dpy, surf, EGL_WIDTH, &surface_width) ||
        !eglQuerySurface(dpy, surf, EGL_HEIGHT, &surface_height) ||
        surface_width != 96 || surface_height != 48) {
        printf("unexpected resized surface: %dx%d\n", surface_width, surface_height);
        return 13;
    }
#endif
    eglMakeCurrent(dpy, NULL, NULL, NULL);
    eglTerminate(dpy);

    printf("ALL CONTRACT CHECKS PASSED\n");
    return 0;
}
