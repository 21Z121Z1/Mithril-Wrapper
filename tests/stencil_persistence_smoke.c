/* DirectMetal cross-command-buffer stencil persistence regression.
 *
 * Uniaball's Vulkan reference path once discarded stencil contents at render
 * pass boundaries.  This test keeps that failure mode backend-neutral: write a
 * stencil mark, force command-buffer completion, then require a later render
 * pass to load the mark.  Both the default framebuffer and a packed
 * depth/stencil renderbuffer FBO execute on real Metal and are pixel checked.
 */

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GL_FALSE 0
#define GL_NO_ERROR 0
#define GL_RENDERER 0x1F01
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_FLOAT 0x1406
#define GL_UNSIGNED_BYTE 0x1401
#define GL_TRIANGLES 0x0004
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_STENCIL_BUFFER_BIT 0x00000400
#define GL_RGBA 0x1908
#define GL_DEPTH_TEST 0x0B71
#define GL_STENCIL_TEST 0x0B90
#define GL_ALWAYS 0x0207
#define GL_EQUAL 0x0202
#define GL_KEEP 0x1E00
#define GL_REPLACE 0x1E01
#define GL_FRAMEBUFFER 0x8D40
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#define GL_RGBA8 0x8058
#define GL_DEPTH24_STENCIL8 0x88F0

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef int GLsizei;
typedef intptr_t GLsizeiptr;
typedef unsigned char GLboolean;
typedef const unsigned char* (*fnGetString)(GLenum);
typedef GLenum (*fnGetError)(void);
typedef GLuint (*fnCreateShader)(GLenum);
typedef void (*fnShaderSource)(GLuint, GLsizei, const char* const*, const GLint*);
typedef void (*fnCompileShader)(GLuint);
typedef void (*fnGetShaderiv)(GLuint, GLenum, GLint*);
typedef GLuint (*fnCreateProgram)(void);
typedef void (*fnAttachShader)(GLuint, GLuint);
typedef void (*fnLinkProgram)(GLuint);
typedef void (*fnGetProgramiv)(GLuint, GLenum, GLint*);
typedef void (*fnUseProgram)(GLuint);
typedef GLint (*fnGetUniformLocation)(GLuint, const char*);
typedef void (*fnUniform4f)(GLint, float, float, float, float);
typedef void (*fnGenVertexArrays)(GLsizei, GLuint*);
typedef void (*fnBindVertexArray)(GLuint);
typedef void (*fnGenBuffers)(GLsizei, GLuint*);
typedef void (*fnBindBuffer)(GLenum, GLuint);
typedef void (*fnBufferData)(GLenum, GLsizeiptr, const void*, GLenum);
typedef void (*fnEnableVertexAttribArray)(GLuint);
typedef void (*fnVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean,
                                      GLsizei, const void*);
typedef void (*fnViewport)(GLint, GLint, GLsizei, GLsizei);
typedef void (*fnEnable)(GLenum);
typedef void (*fnDisable)(GLenum);
typedef void (*fnStencilFunc)(GLenum, GLint, GLuint);
typedef void (*fnStencilOp)(GLenum, GLenum, GLenum);
typedef void (*fnStencilMask)(GLuint);
typedef void (*fnClearStencil)(GLint);
typedef void (*fnClearColor)(float, float, float, float);
typedef void (*fnClear)(GLbitfield);
typedef void (*fnDrawArrays)(GLenum, GLint, GLsizei);
typedef void (*fnFinish)(void);
typedef void (*fnReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum,
                             void*);
typedef void (*fnGenFramebuffers)(GLsizei, GLuint*);
typedef void (*fnBindFramebuffer)(GLenum, GLuint);
typedef void (*fnDeleteFramebuffers)(GLsizei, const GLuint*);
typedef void (*fnGenRenderbuffers)(GLsizei, GLuint*);
typedef void (*fnBindRenderbuffer)(GLenum, GLuint);
typedef void (*fnRenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei);
typedef void (*fnFramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint);
typedef GLenum (*fnCheckFramebufferStatus)(GLenum);
typedef void (*fnDeleteRenderbuffers)(GLsizei, const GLuint*);

static int failures;

#define CHECK(condition, ...) do {                                          \
    if (condition) printf("ok  : " __VA_ARGS__);                           \
    else { printf("FAIL: " __VA_ARGS__); ++failures; }                     \
    printf("\n");                                                          \
} while (0)

#define LOAD(type, variable, symbol)                                        \
    type variable = (type)dlsym(library, symbol)

static const char* vertex_source =
    "#version 330 core\n"
    "layout(location=0) in vec2 position;\n"
    "void main() { gl_Position = vec4(position, 0.0, 1.0); }\n";

static const char* fragment_source =
    "#version 330 core\n"
    "uniform vec4 tint;\n"
    "layout(location=0) out vec4 color;\n"
    "void main() { color = tint; }\n";

static int pixel_is(const unsigned char pixel[4], unsigned char r,
                    unsigned char g, unsigned char b) {
    return abs((int)pixel[0] - r) <= 2 &&
           abs((int)pixel[1] - g) <= 2 &&
           abs((int)pixel[2] - b) <= 2 && pixel[3] >= 253;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const char* path = getenv("MITHRIL_LIBRARY");
    if (!path || !*path) path = "./output/libmithril.dylib";
    void* library = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
    if (!library) {
        fprintf(stderr, "dlopen(%s): %s\n", path, dlerror());
        return 2;
    }

    LOAD(fnGetString, getString, "glGetString");
    LOAD(fnGetError, getError, "glGetError");
    LOAD(fnCreateShader, createShader, "glCreateShader");
    LOAD(fnShaderSource, shaderSource, "glShaderSource");
    LOAD(fnCompileShader, compileShader, "glCompileShader");
    LOAD(fnGetShaderiv, getShaderiv, "glGetShaderiv");
    LOAD(fnCreateProgram, createProgram, "glCreateProgram");
    LOAD(fnAttachShader, attachShader, "glAttachShader");
    LOAD(fnLinkProgram, linkProgram, "glLinkProgram");
    LOAD(fnGetProgramiv, getProgramiv, "glGetProgramiv");
    LOAD(fnUseProgram, useProgram, "glUseProgram");
    LOAD(fnGetUniformLocation, getUniformLocation, "glGetUniformLocation");
    LOAD(fnUniform4f, uniform4f, "glUniform4f");
    LOAD(fnGenVertexArrays, genVertexArrays, "glGenVertexArrays");
    LOAD(fnBindVertexArray, bindVertexArray, "glBindVertexArray");
    LOAD(fnGenBuffers, genBuffers, "glGenBuffers");
    LOAD(fnBindBuffer, bindBuffer, "glBindBuffer");
    LOAD(fnBufferData, bufferData, "glBufferData");
    LOAD(fnEnableVertexAttribArray, enableVertexAttribArray,
         "glEnableVertexAttribArray");
    LOAD(fnVertexAttribPointer, vertexAttribPointer, "glVertexAttribPointer");
    LOAD(fnViewport, viewport, "glViewport");
    LOAD(fnEnable, enable, "glEnable");
    LOAD(fnDisable, disable, "glDisable");
    LOAD(fnStencilFunc, stencilFunc, "glStencilFunc");
    LOAD(fnStencilOp, stencilOp, "glStencilOp");
    LOAD(fnStencilMask, stencilMask, "glStencilMask");
    LOAD(fnClearStencil, clearStencil, "glClearStencil");
    LOAD(fnClearColor, clearColor, "glClearColor");
    LOAD(fnClear, clear, "glClear");
    LOAD(fnDrawArrays, drawArrays, "glDrawArrays");
    LOAD(fnFinish, finish, "glFinish");
    LOAD(fnReadPixels, readPixels, "glReadPixels");
    LOAD(fnGenFramebuffers, genFramebuffers, "glGenFramebuffers");
    LOAD(fnBindFramebuffer, bindFramebuffer, "glBindFramebuffer");
    LOAD(fnDeleteFramebuffers, deleteFramebuffers, "glDeleteFramebuffers");
    LOAD(fnGenRenderbuffers, genRenderbuffers, "glGenRenderbuffers");
    LOAD(fnBindRenderbuffer, bindRenderbuffer, "glBindRenderbuffer");
    LOAD(fnRenderbufferStorage, renderbufferStorage, "glRenderbufferStorage");
    LOAD(fnFramebufferRenderbuffer, framebufferRenderbuffer,
         "glFramebufferRenderbuffer");
    LOAD(fnCheckFramebufferStatus, checkFramebufferStatus,
         "glCheckFramebufferStatus");
    LOAD(fnDeleteRenderbuffers, deleteRenderbuffers, "glDeleteRenderbuffers");

    CHECK(getString && getError && createShader && shaderSource &&
              compileShader && getShaderiv && createProgram && attachShader &&
              linkProgram && getProgramiv && useProgram && getUniformLocation &&
              uniform4f && genVertexArrays && bindVertexArray && genBuffers &&
              bindBuffer && bufferData && enableVertexAttribArray &&
              vertexAttribPointer && viewport && enable && disable &&
              stencilFunc && stencilOp && stencilMask && clearStencil &&
              clearColor && clear && drawArrays && finish && readPixels &&
              genFramebuffers && bindFramebuffer && deleteFramebuffers &&
              genRenderbuffers && bindRenderbuffer && renderbufferStorage &&
              framebufferRenderbuffer && checkFramebufferStatus &&
              deleteRenderbuffers,
          "required stencil-persistence symbols resolve");
    if (failures) return 1;

    const char* renderer = (const char*)getString(GL_RENDERER);
    CHECK(renderer && strstr(renderer, "DirectMetal"),
          "context is explicitly DirectMetal (%s)", renderer ? renderer : "null");

    GLuint vertex_shader = createShader(GL_VERTEX_SHADER);
    GLuint fragment_shader = createShader(GL_FRAGMENT_SHADER);
    shaderSource(vertex_shader, 1, &vertex_source, NULL);
    shaderSource(fragment_shader, 1, &fragment_source, NULL);
    compileShader(vertex_shader);
    compileShader(fragment_shader);
    GLint vertex_ok = 0, fragment_ok = 0;
    getShaderiv(vertex_shader, GL_COMPILE_STATUS, &vertex_ok);
    getShaderiv(fragment_shader, GL_COMPILE_STATUS, &fragment_ok);
    GLuint program = createProgram();
    attachShader(program, vertex_shader);
    attachShader(program, fragment_shader);
    linkProgram(program);
    GLint linked = 0;
    getProgramiv(program, GL_LINK_STATUS, &linked);
    useProgram(program);
    GLint tint = getUniformLocation(program, "tint");
    CHECK(vertex_ok && fragment_ok && linked && tint >= 0,
          "stencil draw shaders compile, link, and reflect tint");

    const float vertices[6] = {-1.f, -1.f, 3.f, -1.f, -1.f, 3.f};
    GLuint vao = 0, vbo = 0;
    genVertexArrays(1, &vao);
    bindVertexArray(vao);
    genBuffers(1, &vbo);
    bindBuffer(GL_ARRAY_BUFFER, vbo);
    bufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    enableVertexAttribArray(0);
    vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    disable(GL_DEPTH_TEST);
    enable(GL_STENCIL_TEST);
    stencilMask(0xFFu);
    clearStencil(0);

    unsigned char pixel[4] = {0};

    /* Default framebuffer: glFinish ends the first Metal command buffer. */
    bindFramebuffer(GL_FRAMEBUFFER, 0);
    viewport(0, 0, 32, 32);
    stencilFunc(GL_ALWAYS, 7, 0xFFu);
    stencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    clearColor(0.f, 0.f, 0.f, 1.f);
    clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    uniform4f(tint, 1.f, 0.f, 0.f, 1.f);
    drawArrays(GL_TRIANGLES, 0, 3);
    finish();

    stencilFunc(GL_EQUAL, 7, 0xFFu);
    stencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    clear(GL_COLOR_BUFFER_BIT);
    uniform4f(tint, 0.f, 0.f, 1.f, 1.f);
    drawArrays(GL_TRIANGLES, 0, 3);
    finish();
    readPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    CHECK(pixel_is(pixel, 0, 0, 255),
          "default framebuffer reloads stencil after command completion "
          "(%u,%u,%u,%u)", pixel[0], pixel[1], pixel[2], pixel[3]);

    /* Offscreen packed depth/stencil renderbuffer: same persistence contract. */
    GLuint framebuffer = 0, color = 0, depth_stencil = 0;
    genRenderbuffers(1, &color);
    bindRenderbuffer(GL_RENDERBUFFER, color);
    renderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 32, 32);
    genRenderbuffers(1, &depth_stencil);
    bindRenderbuffer(GL_RENDERBUFFER, depth_stencil);
    renderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, 32, 32);
    genFramebuffers(1, &framebuffer);
    bindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    framebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            GL_RENDERBUFFER, color);
    framebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                            GL_RENDERBUFFER, depth_stencil);
    CHECK(checkFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
          "packed depth/stencil renderbuffer FBO is complete");

    viewport(0, 0, 32, 32);
    stencilFunc(GL_ALWAYS, 19, 0xFFu);
    stencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    clearColor(0.f, 0.f, 0.f, 1.f);
    clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    uniform4f(tint, 1.f, 0.f, 0.f, 1.f);
    drawArrays(GL_TRIANGLES, 0, 3);
    finish();

    stencilFunc(GL_EQUAL, 19, 0xFFu);
    stencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    clear(GL_COLOR_BUFFER_BIT);
    uniform4f(tint, 0.f, 1.f, 0.f, 1.f);
    drawArrays(GL_TRIANGLES, 0, 3);
    finish();
    memset(pixel, 0, sizeof(pixel));
    readPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    CHECK(pixel_is(pixel, 0, 255, 0),
          "offscreen FBO reloads stencil after command completion "
          "(%u,%u,%u,%u)", pixel[0], pixel[1], pixel[2], pixel[3]);

    stencilFunc(GL_EQUAL, 1, 0xFFu);
    clear(GL_COLOR_BUFFER_BIT);
    uniform4f(tint, 1.f, 1.f, 1.f, 1.f);
    drawArrays(GL_TRIANGLES, 0, 3);
    finish();
    memset(pixel, 0, sizeof(pixel));
    readPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    CHECK(pixel_is(pixel, 0, 0, 0),
          "offscreen FBO stencil mismatch still rejects the later draw "
          "(%u,%u,%u,%u)", pixel[0], pixel[1], pixel[2], pixel[3]);

    CHECK(getError() == GL_NO_ERROR,
          "stencil persistence regression leaves GL_NO_ERROR");

    bindFramebuffer(GL_FRAMEBUFFER, 0);
    deleteFramebuffers(1, &framebuffer);
    deleteRenderbuffers(1, &color);
    deleteRenderbuffers(1, &depth_stencil);
    dlclose(library);

    printf("\nstencil_persistence_smoke: %s (%d failures)\n",
           failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
