/* Exact host-side EGL replay for Amethyst-iOS's Mithril renderer path.
 *
 * Amethyst currently groups Mithril with its non-ANGLE renderers when it
 * negotiates EGL.  It therefore asks for an ES 3 capable config and binds the
 * OpenGL ES client API even though LWJGL subsequently resolves Mithril's
 * desktop OpenGL 3.3 exports.  This smoke keeps that compatibility seam
 * explicit and drives a real CAMetalLayer through present and resize.
 */

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define EGL_FALSE 0
#define EGL_TRUE 1
#define EGL_DEFAULT_DISPLAY 0
#define EGL_SUCCESS 0x3000
#define EGL_NONE 0x3038
#define EGL_RED_SIZE 0x3024
#define EGL_GREEN_SIZE 0x3023
#define EGL_BLUE_SIZE 0x3022
#define EGL_ALPHA_SIZE 0x3021
#define EGL_DEPTH_SIZE 0x3025
#define EGL_SURFACE_TYPE 0x3033
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_WINDOW_BIT 0x0004
#define EGL_PBUFFER_BIT 0x0001
#define EGL_OPENGL_ES3_BIT 0x0040
#define EGL_OPENGL_ES_API 0x30A0
#define EGL_CONTEXT_CLIENT_TYPE 0x3097
#define EGL_CONTEXT_CLIENT_VERSION 0x3098
#define EGL_WIDTH 0x3057
#define EGL_HEIGHT 0x3056
#define EGL_DRAW 0x3059
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_SCISSOR_TEST 0x0C11
#define GL_NO_ERROR 0

typedef void* (*fn_eglGetDisplay)(intptr_t);
typedef int (*fn_eglInitialize)(void*, int*, int*);
typedef int (*fn_eglChooseConfig)(void*, const int*, void**, int, int*);
typedef int (*fn_eglGetConfigAttrib)(void*, void*, int, int*);
typedef int (*fn_eglBindAPI)(unsigned int);
typedef unsigned int (*fn_eglQueryAPI)(void);
typedef void* (*fn_eglCreateContext)(void*, void*, void*, const int*);
typedef int (*fn_eglQueryContext)(void*, void*, int, int*);
typedef void* (*fn_eglCreateWindowSurface)(void*, void*, void*, const int*);
typedef int (*fn_eglMakeCurrent)(void*, void*, void*, void*);
typedef void* (*fn_eglGetCurrentContext)(void);
typedef void* (*fn_eglGetCurrentSurface)(int);
typedef int (*fn_eglQuerySurface)(void*, void*, int, int*);
typedef int (*fn_eglSwapBuffers)(void*, void*);
typedef int (*fn_eglDestroySurface)(void*, void*);
typedef int (*fn_eglDestroyContext)(void*, void*);
typedef int (*fn_eglReleaseThread)(void);
typedef int (*fn_eglTerminate)(void*);
typedef int (*fn_eglGetError)(void);
typedef void (*fn_glClearColor)(float, float, float, float);
typedef void (*fn_glClear)(unsigned int);
typedef void (*fn_glEnable)(unsigned int);
typedef void (*fn_glDisable)(unsigned int);
typedef void (*fn_glScissor)(int, int, int, int);
typedef void (*fn_glFinish)(void);
typedef unsigned int (*fn_glGetError)(void);
typedef bool (*fn_mithrilTestArmNextPresentedPixel)(uint32_t, uint32_t);
typedef bool (*fn_mithrilTestReadPresentedPixels)(unsigned char[4],
                                                   unsigned char[4],
                                                   unsigned char[4]);

static int failures = 0;

#define CHECK(condition, format, ...)                                             \
    do {                                                                           \
        if (condition) {                                                           \
            printf("ok  : " format "\n", ##__VA_ARGS__);                        \
        } else {                                                                   \
            printf("FAIL: " format "\n", ##__VA_ARGS__);                       \
            ++failures;                                                            \
        }                                                                          \
    } while (0)

#define LOAD(type, name) type name = reinterpret_cast<type>(dlsym(handle, #name))

@interface CapturingMetalLayer : CAMetalLayer
@property(nonatomic, strong) id<CAMetalDrawable> capturedDrawable;
@end

@implementation CapturingMetalLayer
- (id<CAMetalDrawable>)nextDrawable {
    id<CAMetalDrawable> drawable = [super nextDrawable];
    self.capturedDrawable = drawable;
    return drawable;
}
@end

static bool PixelMatches(const unsigned char pixel[4], unsigned char c0,
                         unsigned char c1, unsigned char c2,
                         unsigned char c3) {
    return abs((int)pixel[0] - c0) <= 2 &&
           abs((int)pixel[1] - c1) <= 2 &&
           abs((int)pixel[2] - c2) <= 2 &&
           abs((int)pixel[3] - c3) <= 2;
}

static bool PixelIsZero(const unsigned char pixel[4]) {
    return pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0 && pixel[3] == 0;
}

int main(void) {
    @autoreleasepool {
        const char* library_path = "./output/libmithril.dylib";
        void* handle = dlopen(library_path, RTLD_NOW | RTLD_GLOBAL);
        CHECK(handle != nullptr, "dlopen %s", library_path);
        if (!handle) {
            printf("dlerror: %s\n", dlerror());
            return 1;
        }

        LOAD(fn_eglGetDisplay, eglGetDisplay);
        LOAD(fn_eglInitialize, eglInitialize);
        LOAD(fn_eglChooseConfig, eglChooseConfig);
        LOAD(fn_eglGetConfigAttrib, eglGetConfigAttrib);
        LOAD(fn_eglBindAPI, eglBindAPI);
        LOAD(fn_eglQueryAPI, eglQueryAPI);
        LOAD(fn_eglCreateContext, eglCreateContext);
        LOAD(fn_eglQueryContext, eglQueryContext);
        LOAD(fn_eglCreateWindowSurface, eglCreateWindowSurface);
        LOAD(fn_eglMakeCurrent, eglMakeCurrent);
        LOAD(fn_eglGetCurrentContext, eglGetCurrentContext);
        LOAD(fn_eglGetCurrentSurface, eglGetCurrentSurface);
        LOAD(fn_eglQuerySurface, eglQuerySurface);
        LOAD(fn_eglSwapBuffers, eglSwapBuffers);
        LOAD(fn_eglDestroySurface, eglDestroySurface);
        LOAD(fn_eglDestroyContext, eglDestroyContext);
        LOAD(fn_eglReleaseThread, eglReleaseThread);
        LOAD(fn_eglTerminate, eglTerminate);
        LOAD(fn_eglGetError, eglGetError);
        LOAD(fn_glClearColor, glClearColor);
        LOAD(fn_glClear, glClear);
        LOAD(fn_glEnable, glEnable);
        LOAD(fn_glDisable, glDisable);
        LOAD(fn_glScissor, glScissor);
        LOAD(fn_glFinish, glFinish);
        LOAD(fn_glGetError, glGetError);
        LOAD(fn_mithrilTestArmNextPresentedPixel,
             mithrilTestArmNextPresentedPixel);
        LOAD(fn_mithrilTestReadPresentedPixels,
             mithrilTestReadPresentedPixels);

        CHECK(eglGetDisplay && eglInitialize && eglChooseConfig &&
                  eglGetConfigAttrib && eglBindAPI && eglQueryAPI &&
                  eglCreateContext && eglQueryContext &&
                  eglCreateWindowSurface && eglMakeCurrent &&
                  eglGetCurrentContext && eglGetCurrentSurface &&
                  eglQuerySurface && eglSwapBuffers && eglDestroySurface &&
                  eglDestroyContext && eglReleaseThread && eglTerminate &&
                  eglGetError && glClearColor && glClear && glEnable &&
                  glDisable && glScissor && glFinish &&
                  glGetError && mithrilTestArmNextPresentedPixel &&
                  mithrilTestReadPresentedPixels,
              "Amethyst EGL/GL symbol contract resolves");
        if (failures) return failures;

        void* display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        int major = 0;
        int minor = 0;
        CHECK(display != nullptr, "eglGetDisplay(EGL_DEFAULT_DISPLAY)");
        CHECK(eglInitialize(display, &major, &minor) == EGL_TRUE &&
                  major == 1 && minor == 5,
              "eglInitialize reports EGL 1.5 (%d.%d)", major, minor);

        const int config_attributes[] = {
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_DEPTH_SIZE, 24,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_NONE,
        };
        void* config = nullptr;
        int config_count = 0;
        CHECK(eglChooseConfig(display, config_attributes, &config, 1,
                              &config_count) == EGL_TRUE &&
                  config_count == 1 && config != nullptr,
              "Amethyst ES3 config negotiation returns one config (%d)",
              config_count);
        if (!config) return failures ? failures : 1;

        int renderable_types = 0;
        CHECK(eglGetConfigAttrib(display, config, EGL_RENDERABLE_TYPE,
                                 &renderable_types) == EGL_TRUE &&
                  (renderable_types & EGL_OPENGL_ES3_BIT) != 0,
              "config advertises the Amethyst ES3 compatibility bit (0x%x)",
              renderable_types);
        CHECK(eglBindAPI(EGL_OPENGL_ES_API) == EGL_TRUE,
              "Amethyst eglBindAPI(EGL_OPENGL_ES_API) succeeds");
        CHECK(eglQueryAPI() == EGL_OPENGL_ES_API,
              "eglQueryAPI reflects the bound compatibility API");

        const int context_attributes[] = {
            EGL_CONTEXT_CLIENT_VERSION, 3,
            EGL_NONE,
        };
        void* context = eglCreateContext(display, config, nullptr,
                                         context_attributes);
        CHECK(context != nullptr, "Amethyst ES3-negotiated context is created");
        if (!context) return failures ? failures : 1;

        int client_type = 0;
        int client_version = 0;
        CHECK(eglQueryContext(display, context, EGL_CONTEXT_CLIENT_TYPE,
                              &client_type) == EGL_TRUE &&
                  client_type == EGL_OPENGL_ES_API,
              "context retains the bound client API (0x%x)", client_type);
        CHECK(eglQueryContext(display, context, EGL_CONTEXT_CLIENT_VERSION,
                              &client_version) == EGL_TRUE &&
                  client_version == 3,
              "context retains Amethyst's requested client version (%d)",
              client_version);

        CapturingMetalLayer* layer = [CapturingMetalLayer layer];
        layer.drawableSize = CGSizeMake(80, 48);
        void* surface = eglCreateWindowSurface(
            display, config, (__bridge void*)layer, nullptr);
        CHECK(surface != nullptr, "real CAMetalLayer window surface is created");
        if (!surface) return failures ? failures : 1;
        CHECK(layer.device != nil &&
                  layer.pixelFormat == MTLPixelFormatBGRA8Unorm &&
                  layer.framebufferOnly,
              "DirectMetal configures a framebuffer-only BGRA8 drawable");

        /* Test instrumentation only: keep the production format and render
         * path, but allow a post-present blit so this smoke can assert actual
         * drawable bytes instead of treating EGL_TRUE as visual evidence. */
        layer.framebufferOnly = NO;

        CHECK(eglMakeCurrent(display, surface, surface, context) == EGL_TRUE,
              "Amethyst surface/context becomes current");
        CHECK(eglGetCurrentContext() == context &&
                  eglGetCurrentSurface(EGL_DRAW) == surface,
              "current EGL handles match the Amethyst bundle");

        int width = 0;
        int height = 0;
        CHECK(eglQuerySurface(display, surface, EGL_WIDTH, &width) == EGL_TRUE &&
                  eglQuerySurface(display, surface, EGL_HEIGHT, &height) == EGL_TRUE &&
                  width == 80 && height == 48,
              "physical drawable size is used instead of logical view size (%dx%d)",
              width, height);

        glClearColor(0.125f, 0.25f, 0.5f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        CHECK(mithrilTestArmNextPresentedPixel(40, 24),
              "first drawable pixel capture is armed");
        CHECK(eglSwapBuffers(display, surface) == EGL_TRUE,
              "DirectMetal presents the first Amethyst frame");
        glFinish();
        unsigned char presented[4] = {0};
        unsigned char reference[4] = {0};
        unsigned char source[4] = {0};
        CHECK(layer.capturedDrawable.texture.width == 80 &&
                  layer.capturedDrawable.texture.height == 48,
              "first presented drawable has the physical 80x48 extent");
        bool captured =
            mithrilTestReadPresentedPixels(presented, reference, source);
        CHECK(captured && PixelMatches(source, 32, 64, 128, 255),
              "pending clear reaches the default RGBA source texture "
              "(%u,%u,%u,%u)", source[0], source[1], source[2], source[3]);
        bool reference_matches =
            captured && PixelMatches(reference, 128, 64, 32, 255);
        CHECK(reference_matches,
              "pending clear reaches the same-pipeline BGRA reference without glFlush "
              "(%u,%u,%u,%u)", reference[0], reference[1], reference[2],
              reference[3]);
        bool drawable_matches =
            captured && PixelMatches(presented, 128, 64, 32, 255);
        bool paravirtual_unreadable =
            reference_matches && PixelIsZero(presented) &&
            strstr(layer.device.name.UTF8String, "Paravirtual") != nullptr;
        CHECK(drawable_matches || paravirtual_unreadable,
              "physical drawable bytes match or Paravirtual readback is "
              "explicitly unavailable "
              "(%u,%u,%u,%u)", presented[0], presented[1], presented[2],
              presented[3]);
        if (paravirtual_unreadable)
            printf("SKIP: Apple Paravirtual CAMetalDrawable byte readback is "
                   "unavailable; same-pipeline BGRA reference passed\n");
        CHECK(glGetError() == GL_NO_ERROR && eglGetError() == EGL_SUCCESS,
              "first present completes without GL/EGL errors");

        layer.drawableSize = CGSizeMake(56, 96);
        glClearColor(0.5f, 0.25f, 0.125f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        CHECK(mithrilTestArmNextPresentedPixel(28, 48),
              "replacement drawable pixel capture is armed");
        CHECK(eglSwapBuffers(display, surface) == EGL_TRUE,
              "DirectMetal presents after a non-square orientation resize");
        glFinish();
        memset(presented, 0, sizeof(presented));
        memset(reference, 0, sizeof(reference));
        memset(source, 0, sizeof(source));
        CHECK(layer.capturedDrawable.texture.width == 56 &&
                  layer.capturedDrawable.texture.height == 96,
              "resized presented drawable has the physical 56x96 extent");
        captured = mithrilTestReadPresentedPixels(presented, reference, source);
        CHECK(captured && PixelMatches(source, 128, 64, 32, 255),
              "resized clear reaches the replacement default RGBA source "
              "(%u,%u,%u,%u)", source[0], source[1], source[2], source[3]);
        reference_matches =
            captured && PixelMatches(reference, 32, 64, 128, 255);
        CHECK(reference_matches,
              "resized clear reaches the same-pipeline BGRA reference "
              "(%u,%u,%u,%u)", reference[0], reference[1], reference[2],
              reference[3]);
        drawable_matches =
            captured && PixelMatches(presented, 32, 64, 128, 255);
        paravirtual_unreadable =
            reference_matches && PixelIsZero(presented) &&
            strstr(layer.device.name.UTF8String, "Paravirtual") != nullptr;
        CHECK(drawable_matches || paravirtual_unreadable,
              "resized physical drawable bytes match or Paravirtual readback "
              "is explicitly unavailable "
              "(%u,%u,%u,%u)", presented[0], presented[1], presented[2],
              presented[3]);
        if (paravirtual_unreadable)
            printf("SKIP: resized Apple Paravirtual CAMetalDrawable byte "
                   "readback is unavailable; BGRA reference passed\n");
        CHECK(eglQuerySurface(display, surface, EGL_WIDTH, &width) == EGL_TRUE &&
                  eglQuerySurface(display, surface, EGL_HEIGHT, &height) == EGL_TRUE &&
                  width == 56 && height == 96,
              "resized physical drawable dimensions propagate (%dx%d)", width,
              height);
        CHECK(glGetError() == GL_NO_ERROR && eglGetError() == EGL_SUCCESS,
              "resized present completes without GL/EGL errors");

        /* The DirectMetal default framebuffer is stored in GL row order
         * (row zero is bottom); only the CAMetalLayer presentation seam may
         * convert it to Apple's top-origin drawable order.  Use asymmetric
         * halves so a global or missing Y flip cannot pass as a solid clear. */
        glEnable(GL_SCISSOR_TEST);
        glScissor(0, 0, 56, 48);
        glClearColor(1.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(0, 48, 56, 48);
        glClearColor(0.f, 0.f, 1.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);

        CHECK(mithrilTestArmNextPresentedPixel(28, 8),
              "top-oriented drawable capture is armed");
        CHECK(eglSwapBuffers(display, surface) == EGL_TRUE,
              "asymmetric default framebuffer presents");
        glFinish();
        memset(presented, 0, sizeof(presented));
        memset(reference, 0, sizeof(reference));
        memset(source, 0, sizeof(source));
        captured = mithrilTestReadPresentedPixels(presented, reference, source);
        CHECK(captured && PixelMatches(source, 0, 0, 255, 255),
              "drawable top maps to the GL top blue source row "
              "(%u,%u,%u,%u)", source[0], source[1], source[2], source[3]);
        reference_matches =
            captured && PixelMatches(reference, 255, 0, 0, 255);
        CHECK(reference_matches,
              "presentation reference places blue at the drawable top "
              "(%u,%u,%u,%u)", reference[0], reference[1], reference[2],
              reference[3]);
        drawable_matches =
            captured && PixelMatches(presented, 255, 0, 0, 255);
        paravirtual_unreadable =
            reference_matches && PixelIsZero(presented) &&
            strstr(layer.device.name.UTF8String, "Paravirtual") != nullptr;
        CHECK(drawable_matches || paravirtual_unreadable,
              "physical drawable top is blue or Paravirtual readback is "
              "explicitly unavailable (%u,%u,%u,%u)", presented[0],
              presented[1], presented[2], presented[3]);

        CHECK(mithrilTestArmNextPresentedPixel(28, 87),
              "bottom-oriented drawable capture is armed");
        CHECK(eglSwapBuffers(display, surface) == EGL_TRUE,
              "unchanged asymmetric framebuffer re-presents");
        glFinish();
        memset(presented, 0, sizeof(presented));
        memset(reference, 0, sizeof(reference));
        memset(source, 0, sizeof(source));
        captured = mithrilTestReadPresentedPixels(presented, reference, source);
        CHECK(captured && PixelMatches(source, 255, 0, 0, 255),
              "drawable bottom maps to the GL bottom red source row "
              "(%u,%u,%u,%u)", source[0], source[1], source[2], source[3]);
        reference_matches =
            captured && PixelMatches(reference, 0, 0, 255, 255);
        CHECK(reference_matches,
              "presentation reference places red at the drawable bottom "
              "(%u,%u,%u,%u)", reference[0], reference[1], reference[2],
              reference[3]);
        drawable_matches =
            captured && PixelMatches(presented, 0, 0, 255, 255);
        paravirtual_unreadable =
            reference_matches && PixelIsZero(presented) &&
            strstr(layer.device.name.UTF8String, "Paravirtual") != nullptr;
        CHECK(drawable_matches || paravirtual_unreadable,
              "physical drawable bottom is red or Paravirtual readback is "
              "explicitly unavailable (%u,%u,%u,%u)", presented[0],
              presented[1], presented[2], presented[3]);
        CHECK(glGetError() == GL_NO_ERROR && eglGetError() == EGL_SUCCESS,
              "asymmetric presentation leaves GL/EGL error state clean");

        CHECK(eglMakeCurrent(display, nullptr, nullptr, nullptr) == EGL_TRUE,
              "context is released from the thread");
        CHECK(eglGetCurrentContext() == nullptr &&
                  eglGetCurrentSurface(EGL_DRAW) == nullptr,
              "released context/surface are no longer reported current");
        CHECK(eglDestroySurface(display, surface) == EGL_TRUE,
              "window surface lifecycle closes cleanly");
        CHECK(eglDestroyContext(display, context) == EGL_TRUE,
              "context lifecycle closes cleanly");
        CHECK(eglTerminate(display) == EGL_TRUE && eglReleaseThread() == EGL_TRUE,
              "display and thread EGL state terminate cleanly");

        printf("\namethyst_egl_smoke: %s (%d failure%s)\n",
               failures ? "FAIL" : "PASS", failures,
               failures == 1 ? "" : "s");
        if (!failures) printf("AMETHYST EGL SMOKE ALL PASSED\n");
    }
    return failures ? 1 : 0;
}
