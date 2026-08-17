/* DirectMetal resident-buffer streaming regression.
 *
 * Proves two properties that the generic GL semantic tests cannot observe:
 *  1. a small glBufferSubData update does not CPU-copy the whole resident GL buffer;
 *  2. completed resident generations are recycled rather than allocating forever.
 *
 * Pixel readback remains the semantic oracle.  The DirectMetal diagnostic ABI is
 * only used to make the performance-shape assertions deterministic in CI.
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/mithril/directmetal_diagnostics.h"

#define GL_VERTEX_SHADER       0x8B31
#define GL_FRAGMENT_SHADER     0x8B30
#define GL_COMPILE_STATUS      0x8B81
#define GL_LINK_STATUS         0x8B82
#define GL_ARRAY_BUFFER        0x8892
#define GL_BUFFER_SIZE         0x8764
#define GL_DYNAMIC_DRAW        0x88E8
#define GL_FLOAT               0x1406
#define GL_FALSE               0
#define GL_TRIANGLES           0x0004
#define GL_COLOR_BUFFER_BIT    0x00004000
#define GL_RGBA                0x1908
#define GL_UNSIGNED_BYTE       0x1401
#define GL_NO_ERROR            0

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef long long GLsizeiptr;
typedef long long GLintptr;
typedef unsigned char GLboolean;

typedef GLuint (*PFN_glCreateShader)(GLenum);
typedef void (*PFN_glShaderSource)(GLuint, GLsizei, const char* const*, const GLint*);
typedef void (*PFN_glCompileShader)(GLuint);
typedef void (*PFN_glGetShaderiv)(GLuint, GLenum, GLint*);
typedef GLuint (*PFN_glCreateProgram)(void);
typedef void (*PFN_glAttachShader)(GLuint, GLuint);
typedef void (*PFN_glLinkProgram)(GLuint);
typedef void (*PFN_glGetProgramiv)(GLuint, GLenum, GLint*);
typedef void (*PFN_glUseProgram)(GLuint);
typedef void (*PFN_glGenVertexArrays)(GLsizei, GLuint*);
typedef void (*PFN_glBindVertexArray)(GLuint);
typedef void (*PFN_glGenBuffers)(GLsizei, GLuint*);
typedef void (*PFN_glBindBuffer)(GLenum, GLuint);
typedef void (*PFN_glBufferData)(GLenum, GLsizeiptr, const void*, GLenum);
typedef void (*PFN_glBufferSubData)(GLenum, GLintptr, GLsizeiptr, const void*);
typedef void (*PFN_glGetBufferParameteriv)(GLenum, GLenum, GLint*);
typedef void (*PFN_glEnableVertexAttribArray)(GLuint);
typedef void (*PFN_glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
typedef void (*PFN_glDrawArrays)(GLenum, GLint, GLsizei);
typedef void (*PFN_glClearColor)(float, float, float, float);
typedef void (*PFN_glClear)(unsigned int);
typedef void (*PFN_glFinish)(void);
typedef void (*PFN_glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
typedef GLenum (*PFN_glGetError)(void);
typedef void (*PFN_resetBufferStats)(void);
typedef int (*PFN_getBufferStats)(MithrilDirectMetalBufferStatsV1*, size_t);

static int failures;
#define CHECK(c, fmt, ...) do { \
    if (c) printf("ok  : " fmt "\n", ##__VA_ARGS__); \
    else { printf("FAIL: " fmt "\n", ##__VA_ARGS__); ++failures; } \
} while (0)

static int pixel_is(const uint8_t px[4], uint8_t r, uint8_t g, uint8_t b) {
    return abs((int)px[0] - r) <= 3 && abs((int)px[1] - g) <= 3 &&
           abs((int)px[2] - b) <= 3 && px[3] >= 252;
}

static const char* VS =
    "#version 150\n"
    "layout(location=0) in vec2 pos;\n"
    "layout(location=1) in vec4 color;\n"
    "out vec4 vColor;\n"
    "void main() { vColor = color; gl_Position = vec4(pos, 0.0, 1.0); }\n";

static const char* FS =
    "#version 150\n"
    "in vec4 vColor;\n"
    "layout(location=0) out vec4 fragColor;\n"
    "void main() { fragColor = vColor; }\n";

int main(void) {
    const char* library = getenv("MITHRIL_LIBRARY");
    if (!library || !*library) library = "./output/libmithril.dylib";
    void* h = dlopen(library, RTLD_NOW | RTLD_GLOBAL);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }

#define LOAD(type, name, symbol) type name = (type)dlsym(h, symbol)
    LOAD(PFN_glCreateShader, createShader, "glCreateShader");
    LOAD(PFN_glShaderSource, shaderSource, "glShaderSource");
    LOAD(PFN_glCompileShader, compileShader, "glCompileShader");
    LOAD(PFN_glGetShaderiv, getShaderiv, "glGetShaderiv");
    LOAD(PFN_glCreateProgram, createProgram, "glCreateProgram");
    LOAD(PFN_glAttachShader, attachShader, "glAttachShader");
    LOAD(PFN_glLinkProgram, linkProgram, "glLinkProgram");
    LOAD(PFN_glGetProgramiv, getProgramiv, "glGetProgramiv");
    LOAD(PFN_glUseProgram, useProgram, "glUseProgram");
    LOAD(PFN_glGenVertexArrays, genVertexArrays, "glGenVertexArrays");
    LOAD(PFN_glBindVertexArray, bindVertexArray, "glBindVertexArray");
    LOAD(PFN_glGenBuffers, genBuffers, "glGenBuffers");
    LOAD(PFN_glBindBuffer, bindBuffer, "glBindBuffer");
    LOAD(PFN_glBufferData, bufferData, "glBufferData");
    LOAD(PFN_glBufferSubData, bufferSubData, "glBufferSubData");
    LOAD(PFN_glGetBufferParameteriv, getBufferParameteriv, "glGetBufferParameteriv");
    LOAD(PFN_glEnableVertexAttribArray, enableVertexAttribArray, "glEnableVertexAttribArray");
    LOAD(PFN_glVertexAttribPointer, vertexAttribPointer, "glVertexAttribPointer");
    LOAD(PFN_glDrawArrays, drawArrays, "glDrawArrays");
    LOAD(PFN_glClearColor, clearColor, "glClearColor");
    LOAD(PFN_glClear, clear, "glClear");
    LOAD(PFN_glFinish, finish, "glFinish");
    LOAD(PFN_glReadPixels, readPixels, "glReadPixels");
    LOAD(PFN_glGetError, getError, "glGetError");
    LOAD(PFN_resetBufferStats, resetStats, "mithrilResetDirectMetalBufferStats");
    LOAD(PFN_getBufferStats, getStats, "mithrilGetDirectMetalBufferStatsV1");
#undef LOAD

    CHECK(createShader && shaderSource && compileShader && getShaderiv &&
          createProgram && attachShader && linkProgram && getProgramiv &&
          useProgram && genVertexArrays && bindVertexArray && genBuffers &&
          bindBuffer && bufferData && bufferSubData && getBufferParameteriv &&
          enableVertexAttribArray && vertexAttribPointer && drawArrays &&
          clearColor && clear && finish && readPixels && getError &&
          resetStats && getStats,
          "required GL and DirectMetal diagnostic symbols resolve");
    if (failures) return failures;

    GLuint vs = createShader(GL_VERTEX_SHADER);
    GLuint fs = createShader(GL_FRAGMENT_SHADER);
    shaderSource(vs, 1, &VS, NULL);
    shaderSource(fs, 1, &FS, NULL);
    compileShader(vs);
    compileShader(fs);
    GLint ok = 0;
    getShaderiv(vs, GL_COMPILE_STATUS, &ok);
    CHECK(ok, "vertex shader compiles");
    getShaderiv(fs, GL_COMPILE_STATUS, &ok);
    CHECK(ok, "fragment shader compiles");
    GLuint program = createProgram();
    attachShader(program, vs);
    attachShader(program, fs);
    linkProgram(program);
    getProgramiv(program, GL_LINK_STATUS, &ok);
    CHECK(ok, "program links");
    useProgram(program);

    const float positions[6] = {
        -0.7f, -0.7f,
         0.7f, -0.7f,
         0.0f,  0.7f,
    };
    const float white[12] = {
        1,1,1,1, 1,1,1,1, 1,1,1,1,
    };
    const float red[12] = {
        1,0,0,1, 1,0,0,1, 1,0,0,1,
    };
    const float green[12] = {
        0,1,0,1, 0,1,0,1, 0,1,0,1,
    };
    enum { LARGE_BUFFER_SIZE = 256 * 1024 };
    uint8_t* initial = (uint8_t*)calloc(1, LARGE_BUFFER_SIZE);
    CHECK(initial != NULL, "host fixture allocation succeeds");
    if (!initial) return failures;
    memcpy(initial, white, sizeof(white));

    GLuint vao = 0, positionBuffer = 0, colorBuffer = 0;
    genVertexArrays(1, &vao);
    bindVertexArray(vao);

    genBuffers(1, &positionBuffer);
    bindBuffer(GL_ARRAY_BUFFER, positionBuffer);
    bufferData(GL_ARRAY_BUFFER, sizeof(positions), positions, GL_DYNAMIC_DRAW);
    enableVertexAttribArray(0);
    vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), 0);

    genBuffers(1, &colorBuffer);
    bindBuffer(GL_ARRAY_BUFFER, colorBuffer);
    bufferData(GL_ARRAY_BUFFER, LARGE_BUFFER_SIZE, initial, GL_DYNAMIC_DRAW);
    enableVertexAttribArray(1);
    vertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);
    free(initial);

    clearColor(0, 0, 0, 1);
    clear(GL_COLOR_BUFFER_BIT);
    drawArrays(GL_TRIANGLES, 0, 3);
    finish();
    uint8_t px[4] = {0};
    readPixels(256, 256, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    CHECK(pixel_is(px, 255, 255, 255), "initial resident generation renders white");

    resetStats();
    bindBuffer(GL_ARRAY_BUFFER, colorBuffer);
    bufferSubData(GL_ARRAY_BUFFER, 0, sizeof(red), red);
    drawArrays(GL_TRIANGLES, 0, 3);
    finish();
    readPixels(256, 256, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    CHECK(pixel_is(px, 255, 0, 0), "partial update renders red");

    MithrilDirectMetalBufferStatsV1 first = {0};
    CHECK(getStats(&first, sizeof(first)), "first streaming stats read succeeds");
    CHECK(first.full_cpu_upload_bytes == 0,
          "partial update avoids full CPU upload (%llu bytes)",
          (unsigned long long)first.full_cpu_upload_bytes);
    CHECK(first.partial_cpu_upload_bytes == sizeof(red),
          "CPU uploads exactly modified range (%llu bytes)",
          (unsigned long long)first.partial_cpu_upload_bytes);
    CHECK(first.preserve_blit_bytes == LARGE_BUFFER_SIZE - sizeof(red),
          "GPU preserves untouched bytes (%llu bytes)",
          (unsigned long long)first.preserve_blit_bytes);
    CHECK(first.resident_allocations == 1 && first.resident_reuses == 0,
          "first replacement allocates one resident generation (%llu alloc, %llu reuse)",
          (unsigned long long)first.resident_allocations,
          (unsigned long long)first.resident_reuses);

    resetStats();
    bufferSubData(GL_ARRAY_BUFFER, 0, sizeof(green), green);
    drawArrays(GL_TRIANGLES, 0, 3);
    finish();
    readPixels(256, 256, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    CHECK(pixel_is(px, 0, 255, 0), "recycled generation renders green");

    MithrilDirectMetalBufferStatsV1 second = {0};
    CHECK(getStats(&second, sizeof(second)), "second streaming stats read succeeds");
    CHECK(second.full_cpu_upload_bytes == 0 &&
          second.partial_cpu_upload_bytes == sizeof(green),
          "second update remains partial (%llu full, %llu partial)",
          (unsigned long long)second.full_cpu_upload_bytes,
          (unsigned long long)second.partial_cpu_upload_bytes);
    CHECK(second.resident_reuses >= 1,
          "completed resident generation is recycled (%llu reuse)",
          (unsigned long long)second.resident_reuses);

    /* A NULL glBufferData store has a logical size without requiring an eager
     * initialized CPU payload.  The source invariant is checked by CI; this
     * runtime assertion protects the observable GL size contract. */
    GLuint lazy = 0;
    genBuffers(1, &lazy);
    bindBuffer(GL_ARRAY_BUFFER, lazy);
    bufferData(GL_ARRAY_BUFFER, LARGE_BUFFER_SIZE, NULL, GL_DYNAMIC_DRAW);
    GLint logicalSize = 0;
    getBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &logicalSize);
    CHECK(logicalSize == LARGE_BUFFER_SIZE,
          "NULL glBufferData preserves logical GL_BUFFER_SIZE (%d)", logicalSize);

    CHECK(getError() == GL_NO_ERROR, "streaming scenario leaves GL_NO_ERROR");
    dlclose(h);
    return failures ? 1 : 0;
}
