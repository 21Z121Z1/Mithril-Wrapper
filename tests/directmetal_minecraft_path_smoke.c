/*
 * DirectMetal controls for the two Minecraft rendering seams that ordinary
 * offscreen render_smoke does not exercise:
 *
 *   1. user FBO -> default framebuffer glBlitFramebuffer, which approximates
 *      Minecraft's final composition/resolve path;
 *   2. glMultiDrawElementsIndirect on a real GPU target, which exercises the
 *      terrain-style indirect draw path advertised through
 *      GL_ARB_multi_draw_indirect.
 *
 * The controls intentionally use an EGL pbuffer so they remain independent
 * from WindowServer/CAMetalLayer presentation. They are L4 renderer controls,
 * not L5 presentation evidence.
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <EGL/egl.h>
#include <GL/glcorearb.h>

#define BLIT_W 64
#define BLIT_H 64
#define MDI_W 128
#define MDI_H 64

static int failures = 0;
static int checks = 0;

#define CHECK(cond, fmt, ...) do { \
    ++checks; \
    if (cond) printf("ok  : " fmt "\n", ##__VA_ARGS__); \
    else { printf("FAIL: " fmt "\n", ##__VA_ARGS__); ++failures; } \
} while (0)

typedef struct Api {
    PFNGLGETSTRINGPROC getString;
    PFNGLGETERRORPROC getError;
    PFNGLFINISHPROC finish;
    PFNGLVIEWPORTPROC viewport;
    PFNGLDISABLEPROC disable;
    PFNGLCLEARCOLORPROC clearColor;
    PFNGLCLEARPROC clear;
    PFNGLREADPIXELSPROC readPixels;
    PFNGLGENTEXTURESPROC genTextures;
    PFNGLBINDTEXTUREPROC bindTexture;
    PFNGLTEXIMAGE2DPROC texImage2D;
    PFNGLTEXPARAMETERIPROC texParameteri;
    PFNGLGENFRAMEBUFFERSPROC genFramebuffers;
    PFNGLBINDFRAMEBUFFERPROC bindFramebuffer;
    PFNGLFRAMEBUFFERTEXTURE2DPROC framebufferTexture2D;
    PFNGLCHECKFRAMEBUFFERSTATUSPROC checkFramebufferStatus;
    PFNGLBLITFRAMEBUFFERPROC blitFramebuffer;
    PFNGLCREATESHADERPROC createShader;
    PFNGLSHADERSOURCEPROC shaderSource;
    PFNGLCOMPILESHADERPROC compileShader;
    PFNGLGETSHADERIVPROC getShaderiv;
    PFNGLGETSHADERINFOLOGPROC getShaderInfoLog;
    PFNGLCREATEPROGRAMPROC createProgram;
    PFNGLATTACHSHADERPROC attachShader;
    PFNGLLINKPROGRAMPROC linkProgram;
    PFNGLGETPROGRAMIVPROC getProgramiv;
    PFNGLGETPROGRAMINFOLOGPROC getProgramInfoLog;
    PFNGLUSEPROGRAMPROC useProgram;
    PFNGLDELETESHADERPROC deleteShader;
    PFNGLGENVERTEXARRAYSPROC genVertexArrays;
    PFNGLBINDVERTEXARRAYPROC bindVertexArray;
    PFNGLGENBUFFERSPROC genBuffers;
    PFNGLBINDBUFFERPROC bindBuffer;
    PFNGLBUFFERDATAPROC bufferData;
    PFNGLVERTEXATTRIBPOINTERPROC vertexAttribPointer;
    PFNGLENABLEVERTEXATTRIBARRAYPROC enableVertexAttribArray;
    PFNGLMULTIDRAWELEMENTSINDIRECTPROC multiDrawElementsIndirect;
} Api;

#define LOAD_GL(field, type, symbol) do { \
    api->field = (type)dlsym(handle, symbol); \
    CHECK(api->field != NULL, "resolve %s", symbol); \
} while (0)

static int load_gl(void* handle, Api* api) {
    memset(api, 0, sizeof(*api));
    LOAD_GL(getString, PFNGLGETSTRINGPROC, "glGetString");
    LOAD_GL(getError, PFNGLGETERRORPROC, "glGetError");
    LOAD_GL(finish, PFNGLFINISHPROC, "glFinish");
    LOAD_GL(viewport, PFNGLVIEWPORTPROC, "glViewport");
    LOAD_GL(disable, PFNGLDISABLEPROC, "glDisable");
    LOAD_GL(clearColor, PFNGLCLEARCOLORPROC, "glClearColor");
    LOAD_GL(clear, PFNGLCLEARPROC, "glClear");
    LOAD_GL(readPixels, PFNGLREADPIXELSPROC, "glReadPixels");
    LOAD_GL(genTextures, PFNGLGENTEXTURESPROC, "glGenTextures");
    LOAD_GL(bindTexture, PFNGLBINDTEXTUREPROC, "glBindTexture");
    LOAD_GL(texImage2D, PFNGLTEXIMAGE2DPROC, "glTexImage2D");
    LOAD_GL(texParameteri, PFNGLTEXPARAMETERIPROC, "glTexParameteri");
    LOAD_GL(genFramebuffers, PFNGLGENFRAMEBUFFERSPROC, "glGenFramebuffers");
    LOAD_GL(bindFramebuffer, PFNGLBINDFRAMEBUFFERPROC, "glBindFramebuffer");
    LOAD_GL(framebufferTexture2D, PFNGLFRAMEBUFFERTEXTURE2DPROC, "glFramebufferTexture2D");
    LOAD_GL(checkFramebufferStatus, PFNGLCHECKFRAMEBUFFERSTATUSPROC, "glCheckFramebufferStatus");
    LOAD_GL(blitFramebuffer, PFNGLBLITFRAMEBUFFERPROC, "glBlitFramebuffer");
    LOAD_GL(createShader, PFNGLCREATESHADERPROC, "glCreateShader");
    LOAD_GL(shaderSource, PFNGLSHADERSOURCEPROC, "glShaderSource");
    LOAD_GL(compileShader, PFNGLCOMPILESHADERPROC, "glCompileShader");
    LOAD_GL(getShaderiv, PFNGLGETSHADERIVPROC, "glGetShaderiv");
    LOAD_GL(getShaderInfoLog, PFNGLGETSHADERINFOLOGPROC, "glGetShaderInfoLog");
    LOAD_GL(createProgram, PFNGLCREATEPROGRAMPROC, "glCreateProgram");
    LOAD_GL(attachShader, PFNGLATTACHSHADERPROC, "glAttachShader");
    LOAD_GL(linkProgram, PFNGLLINKPROGRAMPROC, "glLinkProgram");
    LOAD_GL(getProgramiv, PFNGLGETPROGRAMIVPROC, "glGetProgramiv");
    LOAD_GL(getProgramInfoLog, PFNGLGETPROGRAMINFOLOGPROC, "glGetProgramInfoLog");
    LOAD_GL(useProgram, PFNGLUSEPROGRAMPROC, "glUseProgram");
    LOAD_GL(deleteShader, PFNGLDELETESHADERPROC, "glDeleteShader");
    LOAD_GL(genVertexArrays, PFNGLGENVERTEXARRAYSPROC, "glGenVertexArrays");
    LOAD_GL(bindVertexArray, PFNGLBINDVERTEXARRAYPROC, "glBindVertexArray");
    LOAD_GL(genBuffers, PFNGLGENBUFFERSPROC, "glGenBuffers");
    LOAD_GL(bindBuffer, PFNGLBINDBUFFERPROC, "glBindBuffer");
    LOAD_GL(bufferData, PFNGLBUFFERDATAPROC, "glBufferData");
    LOAD_GL(vertexAttribPointer, PFNGLVERTEXATTRIBPOINTERPROC, "glVertexAttribPointer");
    LOAD_GL(enableVertexAttribArray, PFNGLENABLEVERTEXATTRIBARRAYPROC, "glEnableVertexAttribArray");
    LOAD_GL(multiDrawElementsIndirect, PFNGLMULTIDRAWELEMENTSINDIRECTPROC,
            "glMultiDrawElementsIndirect");
    return failures == 0;
}
#undef LOAD_GL

static int setup_egl(void* handle) {
    EGLDisplay (*getDisplay)(EGLNativeDisplayType) = NULL;
    EGLBoolean (*initialize)(EGLDisplay, EGLint*, EGLint*) = NULL;
    EGLBoolean (*bindAPI)(EGLenum) = NULL;
    EGLBoolean (*getConfigs)(EGLDisplay, EGLConfig*, EGLint, EGLint*) = NULL;
    EGLContext (*createContext)(EGLDisplay, EGLConfig, EGLContext,
                                const EGLint*) = NULL;
    EGLSurface (*createPbuffer)(EGLDisplay, EGLConfig, const EGLint*) = NULL;
    EGLBoolean (*makeCurrent)(EGLDisplay, EGLSurface, EGLSurface,
                              EGLContext) = NULL;

#define LOAD_EGL(var, type, name) do { \
    var = (type)dlsym(handle, name); \
    CHECK(var != NULL, "resolve %s", name); \
    if (!var) return 0; \
} while (0)
    LOAD_EGL(getDisplay, EGLDisplay (*)(EGLNativeDisplayType), "eglGetDisplay");
    LOAD_EGL(initialize, EGLBoolean (*)(EGLDisplay, EGLint*, EGLint*), "eglInitialize");
    LOAD_EGL(bindAPI, EGLBoolean (*)(EGLenum), "eglBindAPI");
    LOAD_EGL(getConfigs, EGLBoolean (*)(EGLDisplay, EGLConfig*, EGLint, EGLint*), "eglGetConfigs");
    LOAD_EGL(createContext, EGLContext (*)(EGLDisplay, EGLConfig, EGLContext, const EGLint*),
             "eglCreateContext");
    LOAD_EGL(createPbuffer, EGLSurface (*)(EGLDisplay, EGLConfig, const EGLint*),
             "eglCreatePbufferSurface");
    LOAD_EGL(makeCurrent, EGLBoolean (*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext),
             "eglMakeCurrent");
#undef LOAD_EGL

    EGLDisplay display = getDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0, minor = 0;
    CHECK(display != EGL_NO_DISPLAY, "EGL display exists");
    if (display == EGL_NO_DISPLAY) return 0;
    CHECK(initialize(display, &major, &minor) == EGL_TRUE,
          "EGL initializes (%d.%d)", major, minor);
    CHECK(bindAPI(EGL_OPENGL_API) == EGL_TRUE, "EGL binds desktop OpenGL API");

    EGLConfig config = NULL;
    EGLint count = 0;
    CHECK(getConfigs(display, &config, 1, &count) == EGL_TRUE && count > 0 && config,
          "EGL config available");
    if (!config) return 0;

    const EGLint context_attrs[] = {
        EGL_CONTEXT_MAJOR_VERSION, 3,
        EGL_CONTEXT_MINOR_VERSION, 3,
        EGL_NONE,
    };
    const EGLint pbuffer_attrs[] = {
        EGL_WIDTH, MDI_W,
        EGL_HEIGHT, MDI_H,
        EGL_NONE,
    };
    EGLContext context = createContext(display, config, EGL_NO_CONTEXT, context_attrs);
    EGLSurface surface = createPbuffer(display, config, pbuffer_attrs);
    CHECK(context != EGL_NO_CONTEXT, "EGL OpenGL context created");
    CHECK(surface != EGL_NO_SURFACE, "EGL pbuffer %dx%d created", MDI_W, MDI_H);
    if (context == EGL_NO_CONTEXT || surface == EGL_NO_SURFACE) return 0;
    CHECK(makeCurrent(display, surface, surface, context) == EGL_TRUE,
          "EGL context made current");
    return 1;
}

static int color_class(uint8_t r, uint8_t g, uint8_t b) {
    if (r > 240 && g < 16 && b < 16) return 0;
    if (r < 16 && g > 240 && b < 16) return 1;
    if (r < 16 && g < 16 && b > 240) return 2;
    if (r > 240 && g > 240 && b > 240) return 3;
    return -1;
}

static void default_fbo_blit_control(Api* api) {
    uint8_t pattern[BLIT_W * BLIT_H * 4];
    for (int y = 0; y < BLIT_H; ++y) {
        for (int x = 0; x < BLIT_W; ++x) {
            int q = (x >= BLIT_W / 2) + 2 * (y >= BLIT_H / 2);
            static const uint8_t colors[4][4] = {
                {255, 0, 0, 255}, {0, 255, 0, 255},
                {0, 0, 255, 255}, {255, 255, 255, 255},
            };
            memcpy(&pattern[(y * BLIT_W + x) * 4], colors[q], 4);
        }
    }

    GLuint tex = 0, fbo = 0;
    api->genTextures(1, &tex);
    api->bindTexture(GL_TEXTURE_2D, tex);
    api->texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    api->texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    api->texImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, BLIT_W, BLIT_H, 0,
                    GL_RGBA, GL_UNSIGNED_BYTE, pattern);
    api->genFramebuffers(1, &fbo);
    api->bindFramebuffer(GL_FRAMEBUFFER, fbo);
    api->framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_TEXTURE_2D, tex, 0);
    CHECK(api->checkFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
          "final-blit source FBO complete");

    api->bindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    api->viewport(0, 0, BLIT_W, BLIT_H);
    api->clearColor(0.0f, 0.0f, 0.0f, 1.0f);
    api->clear(GL_COLOR_BUFFER_BIT);
    api->bindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    api->bindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    api->blitFramebuffer(0, 0, BLIT_W, BLIT_H,
                         0, 0, BLIT_W, BLIT_H,
                         GL_COLOR_BUFFER_BIT, GL_NEAREST);
    api->bindFramebuffer(GL_FRAMEBUFFER, 0);
    api->finish();

    uint8_t actual[BLIT_W * BLIT_H * 4];
    memset(actual, 0, sizeof(actual));
    api->readPixels(0, 0, BLIT_W, BLIT_H, GL_RGBA, GL_UNSIGNED_BYTE, actual);
    api->finish();
    GLenum err = api->getError();
    CHECK(err == GL_NO_ERROR, "user-FBO -> default-FBO blit/readback has no GL error (0x%x)", err);

    uint32_t classes[4] = {0, 0, 0, 0};
    uint32_t other = 0;
    for (int i = 0; i < BLIT_W * BLIT_H; ++i) {
        int c = color_class(actual[i * 4], actual[i * 4 + 1], actual[i * 4 + 2]);
        if (c >= 0) classes[c]++; else other++;
    }
    for (int c = 0; c < 4; ++c) {
        CHECK(classes[c] > 500, "default framebuffer retains quadrant color %d (%u pixels)",
              c, classes[c]);
    }
    CHECK(other < 64, "default framebuffer has no unexplained flat-color replacement (%u other pixels)",
          other);
    printf("FINAL_BLIT_COUNTS red=%u green=%u blue=%u white=%u other=%u\n",
           classes[0], classes[1], classes[2], classes[3], other);
}

static GLuint compile_program(Api* api) {
    static const char* vs_src =
        "#version 330 core\n"
        "layout(location=0) in vec2 position;\n"
        "void main(){ gl_Position=vec4(position,0.0,1.0); }\n";
    static const char* fs_src =
        "#version 330 core\n"
        "out vec4 color;\n"
        "void main(){ color=vec4(1.0,0.05,0.02,1.0); }\n";
    GLuint vs = api->createShader(GL_VERTEX_SHADER);
    GLuint fs = api->createShader(GL_FRAGMENT_SHADER);
    api->shaderSource(vs, 1, &vs_src, NULL);
    api->shaderSource(fs, 1, &fs_src, NULL);
    api->compileShader(vs);
    api->compileShader(fs);
    GLint vs_ok = GL_FALSE, fs_ok = GL_FALSE;
    api->getShaderiv(vs, GL_COMPILE_STATUS, &vs_ok);
    api->getShaderiv(fs, GL_COMPILE_STATUS, &fs_ok);
    if (vs_ok != GL_TRUE || fs_ok != GL_TRUE) {
        char log[2048]; GLsizei n = 0;
        if (vs_ok != GL_TRUE) {
            api->getShaderInfoLog(vs, (GLsizei)sizeof(log), &n, log);
            fprintf(stderr, "vertex shader compile failed: %.*s\n", (int)n, log);
        }
        if (fs_ok != GL_TRUE) {
            api->getShaderInfoLog(fs, (GLsizei)sizeof(log), &n, log);
            fprintf(stderr, "fragment shader compile failed: %.*s\n", (int)n, log);
        }
    }
    CHECK(vs_ok == GL_TRUE && fs_ok == GL_TRUE, "MDI control shaders compile");
    if (vs_ok != GL_TRUE || fs_ok != GL_TRUE) return 0;

    GLuint program = api->createProgram();
    api->attachShader(program, vs);
    api->attachShader(program, fs);
    api->linkProgram(program);
    GLint linked = GL_FALSE;
    api->getProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        char log[2048]; GLsizei n = 0;
        api->getProgramInfoLog(program, (GLsizei)sizeof(log), &n, log);
        fprintf(stderr, "program link failed: %.*s\n", (int)n, log);
    }
    CHECK(linked == GL_TRUE, "MDI control program links");
    api->deleteShader(vs);
    api->deleteShader(fs);
    return linked == GL_TRUE ? program : 0;
}

typedef struct DrawElementsIndirectCommand {
    uint32_t count;
    uint32_t instanceCount;
    uint32_t firstIndex;
    int32_t baseVertex;
    uint32_t baseInstance;
} DrawElementsIndirectCommand;

static void mdi_control(Api* api) {
    GLuint tex = 0, fbo = 0;
    api->genTextures(1, &tex);
    api->bindTexture(GL_TEXTURE_2D, tex);
    api->texImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, MDI_W, MDI_H, 0,
                    GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    api->genFramebuffers(1, &fbo);
    api->bindFramebuffer(GL_FRAMEBUFFER, fbo);
    api->framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              GL_TEXTURE_2D, tex, 0);
    CHECK(api->checkFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
          "MDI target FBO complete");

    GLuint program = compile_program(api);
    if (!program) return;

    static const GLfloat vertices[] = {
        -0.95f, -0.80f,  -0.08f, -0.80f,  -0.52f, 0.80f,
         0.08f, -0.80f,   0.95f, -0.80f,   0.52f, 0.80f,
    };
    static const GLushort indices[] = {0, 1, 2, 3, 4, 5};
    static const DrawElementsIndirectCommand commands[2] = {
        {3, 1, 0, 0, 0},
        {3, 1, 3, 0, 0},
    };

    GLuint vao = 0, vbo = 0, ebo = 0, indirect = 0;
    api->genVertexArrays(1, &vao);
    api->bindVertexArray(vao);
    api->genBuffers(1, &vbo);
    api->bindBuffer(GL_ARRAY_BUFFER, vbo);
    api->bufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    api->vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * (GLsizei)sizeof(GLfloat),
                             (const void*)0);
    api->enableVertexAttribArray(0);
    api->genBuffers(1, &ebo);
    api->bindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    api->bufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    api->genBuffers(1, &indirect);
    api->bindBuffer(GL_DRAW_INDIRECT_BUFFER, indirect);
    api->bufferData(GL_DRAW_INDIRECT_BUFFER, sizeof(commands), commands, GL_STATIC_DRAW);

    api->bindFramebuffer(GL_FRAMEBUFFER, fbo);
    api->viewport(0, 0, MDI_W, MDI_H);
    api->disable(GL_SCISSOR_TEST);
    api->disable(GL_CULL_FACE);
    api->disable(GL_DEPTH_TEST);
    api->disable(GL_BLEND);
    api->clearColor(0.0f, 0.0f, 0.0f, 1.0f);
    api->clear(GL_COLOR_BUFFER_BIT);
    api->useProgram(program);
    api->multiDrawElementsIndirect(GL_TRIANGLES, GL_UNSIGNED_SHORT,
                                   (const void*)0, 2, 0);
    api->finish();

    uint8_t pixels[MDI_W * MDI_H * 4];
    api->readPixels(0, 0, MDI_W, MDI_H, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    api->finish();
    GLenum err = api->getError();
    CHECK(err == GL_NO_ERROR, "glMultiDrawElementsIndirect leaves no GL error (0x%x)", err);

    uint32_t left = 0, right = 0, lit = 0;
    for (int y = 0; y < MDI_H; ++y) {
        for (int x = 0; x < MDI_W; ++x) {
            const uint8_t* p = &pixels[(y * MDI_W + x) * 4];
            if (p[0] > 180 && p[1] < 80 && p[2] < 80) {
                lit++;
                if (x < MDI_W / 2) left++; else right++;
            }
        }
    }
    CHECK(left > 300, "first indirect record rasterizes left triangle (%u lit pixels)", left);
    CHECK(right > 300, "second indirect record rasterizes right triangle (%u lit pixels)", right);
    CHECK(lit > 1000, "multi-draw indirect produces nontrivial raster output (%u lit pixels)", lit);
    printf("MDI_COUNTS left=%u right=%u total=%u\n", left, right, lit);
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "./build/libmithril.dylib";
    void* handle = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
    CHECK(handle != NULL, "dlopen %s", path);
    if (!handle) {
        fprintf(stderr, "dlopen: %s\n", dlerror());
        return 2;
    }
    if (!setup_egl(handle)) return 2;

    Api api;
    if (!load_gl(handle, &api)) return 2;
    const char* version = (const char*)api.getString(GL_VERSION);
    CHECK(version && strstr(version, "Metal 3 (DirectMetal)") && strstr(version, "Mithril-Wrapper"),
          "DirectMetal is active (%s)", version ? version : "null");

    default_fbo_blit_control(&api);
    mdi_control(&api);

    printf("MINECRAFT PATH SMOKE: %d checks, %d failure(s)\n", checks, failures);
    if (failures == 0) printf("MINECRAFT PATH SMOKE ALL PASSED\n");
    return failures ? 1 : 0;
}
