/* OpenGL 3.3 default-block uniform-array semantics smoke.
 *
 * Exercises queried element locations, partial vector/matrix array writes,
 * count overrun truncation, active-uniform naming, getters, validation, and
 * backend execution. The same source runs against DirectMetal and the Vulkan
 * reference backend so frontend semantics cannot silently diverge by backend.
 */

#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_ACTIVE_UNIFORMS 0x8B86
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_FLOAT 0x1406
#define GL_FALSE 0
#define GL_TRIANGLE_STRIP 0x0005
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_RENDERER 0x1F01
#define GL_NO_ERROR 0
#define GL_INVALID_VALUE 0x0501
#define GL_INVALID_OPERATION 0x0502

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef int GLsizei;
typedef long GLsizeiptr;
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
typedef void (*fnGetActiveUniform)(GLuint, GLuint, GLsizei, GLsizei*, GLint*,
                                   GLenum*, char*);
typedef void (*fnUseProgram)(GLuint);
typedef GLint (*fnGetUniformLocation)(GLuint, const char*);
typedef void (*fnGetUniformfv)(GLuint, GLint, float*);
typedef void (*fnUniform4f)(GLint, float, float, float, float);
typedef void (*fnUniform4fv)(GLint, GLsizei, const float*);
typedef void (*fnUniformMatrix2fv)(GLint, GLsizei, GLboolean, const float*);
typedef void (*fnGenVertexArrays)(GLsizei, GLuint*);
typedef void (*fnBindVertexArray)(GLuint);
typedef void (*fnGenBuffers)(GLsizei, GLuint*);
typedef void (*fnBindBuffer)(GLenum, GLuint);
typedef void (*fnBufferData)(GLenum, GLsizeiptr, const void*, GLenum);
typedef void (*fnEnableVertexAttribArray)(GLuint);
typedef void (*fnVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean,
                                      GLsizei, const void*);
typedef void (*fnViewport)(GLint, GLint, GLsizei, GLsizei);
typedef void (*fnClearColor)(float, float, float, float);
typedef void (*fnClear)(GLbitfield);
typedef void (*fnDrawArrays)(GLenum, GLint, GLsizei);
typedef void (*fnFinish)(void);
typedef void (*fnReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum,
                             void*);

static int failures;

#define CHECK(condition, ...) do {                                          \
    if (condition) printf("ok  : " __VA_ARGS__);                           \
    else { printf("FAIL: " __VA_ARGS__); ++failures; }                     \
    printf("\n");                                                          \
} while (0)

#define LOAD(type, variable, symbol) type variable = (type)dlsym(library, symbol)

static const char* vertex_source =
    "#version 150\n"
    "layout(location=0) in vec2 position;\n"
    "void main() { gl_Position = vec4(position, 0.0, 1.0); }\n";

static const char* fragment_source =
    "#version 150\n"
    "uniform vec4 palette[3];\n"
    "uniform mat2 transform[2];\n"
    "uniform vec4 scalar;\n"
    "layout(location=0) out vec4 color;\n"
    "void main() {\n"
    "  vec2 shifted = transform[1] * vec2(1.0, 1.0);\n"
    "  color = palette[0] + palette[1] + palette[2] + scalar;\n"
    "  color.rg += shifted;\n"
    "}\n";

static int vec_near(const float* value, const float* expected, int count) {
    for (int i = 0; i < count; ++i)
        if (fabsf(value[i] - expected[i]) > 0.0001f) return 0;
    return 1;
}

static int pixel_near(const unsigned char pixel[4], int r, int g, int b) {
    return abs((int)pixel[0] - r) <= 3 &&
           abs((int)pixel[1] - g) <= 3 &&
           abs((int)pixel[2] - b) <= 3 &&
           abs((int)pixel[3] - 255) <= 3;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const char* path = getenv("MITHRIL_LIBRARY");
#if defined(__APPLE__)
    if (!path) path = "./output/libmithril.dylib";
#else
    if (!path) path = "./output/libmithril.so";
#endif
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
    LOAD(fnGetActiveUniform, getActiveUniform, "glGetActiveUniform");
    LOAD(fnUseProgram, useProgram, "glUseProgram");
    LOAD(fnGetUniformLocation, getUniformLocation, "glGetUniformLocation");
    LOAD(fnGetUniformfv, getUniformfv, "glGetUniformfv");
    LOAD(fnUniform4f, uniform4f, "glUniform4f");
    LOAD(fnUniform4fv, uniform4fv, "glUniform4fv");
    LOAD(fnUniformMatrix2fv, uniformMatrix2fv, "glUniformMatrix2fv");
    LOAD(fnGenVertexArrays, genVertexArrays, "glGenVertexArrays");
    LOAD(fnBindVertexArray, bindVertexArray, "glBindVertexArray");
    LOAD(fnGenBuffers, genBuffers, "glGenBuffers");
    LOAD(fnBindBuffer, bindBuffer, "glBindBuffer");
    LOAD(fnBufferData, bufferData, "glBufferData");
    LOAD(fnEnableVertexAttribArray, enableVertexAttribArray,
         "glEnableVertexAttribArray");
    LOAD(fnVertexAttribPointer, vertexAttribPointer, "glVertexAttribPointer");
    LOAD(fnViewport, viewport, "glViewport");
    LOAD(fnClearColor, clearColor, "glClearColor");
    LOAD(fnClear, clear, "glClear");
    LOAD(fnDrawArrays, drawArrays, "glDrawArrays");
    LOAD(fnFinish, finish, "glFinish");
    LOAD(fnReadPixels, readPixels, "glReadPixels");

    CHECK(getString && getError && createShader && shaderSource &&
              compileShader && getShaderiv && createProgram && attachShader &&
              linkProgram && getProgramiv && getActiveUniform && useProgram &&
              getUniformLocation && getUniformfv && uniform4f && uniform4fv &&
              uniformMatrix2fv && genVertexArrays && bindVertexArray &&
              genBuffers && bindBuffer && bufferData && enableVertexAttribArray &&
              vertexAttribPointer && viewport && clearColor && clear &&
              drawArrays && finish && readPixels,
          "required uniform-array symbols resolve");
    if (failures) return 1;

    const char* renderer = (const char*)getString(GL_RENDERER);
    CHECK(renderer && strstr(renderer, "Mithril"),
          "Mithril backend is active (%s)", renderer ? renderer : "null");

    GLuint vertex = createShader(GL_VERTEX_SHADER);
    GLuint fragment = createShader(GL_FRAGMENT_SHADER);
    shaderSource(vertex, 1, &vertex_source, NULL);
    shaderSource(fragment, 1, &fragment_source, NULL);
    compileShader(vertex);
    compileShader(fragment);
    GLint vertex_ok = 0, fragment_ok = 0;
    getShaderiv(vertex, GL_COMPILE_STATUS, &vertex_ok);
    getShaderiv(fragment, GL_COMPILE_STATUS, &fragment_ok);
    CHECK(vertex_ok && fragment_ok, "uniform-array shaders compile");

    GLuint program = createProgram();
    attachShader(program, vertex);
    attachShader(program, fragment);
    linkProgram(program);
    GLint link_ok = 0;
    getProgramiv(program, GL_LINK_STATUS, &link_ok);
    CHECK(link_ok, "uniform-array program links");
    useProgram(program);

    GLint palette = getUniformLocation(program, "palette");
    GLint palette0 = getUniformLocation(program, "palette[0]");
    GLint palette1 = getUniformLocation(program, "palette[1]");
    GLint palette2 = getUniformLocation(program, "palette[2]");
    GLint palette3 = getUniformLocation(program, "palette[3]");
    CHECK(palette >= 0 && palette == palette0,
          "array base and [0] resolve to the same location (%d)", palette);
    CHECK(palette1 >= 0 && palette2 >= 0 && palette1 != palette0 &&
              palette2 != palette0 && palette1 != palette2,
          "active array elements expose distinct opaque locations (%d,%d,%d)",
          palette0, palette1, palette2);
    CHECK(palette3 == -1, "inactive out-of-range array element is -1");

    GLint transform = getUniformLocation(program, "transform");
    GLint transform0 = getUniformLocation(program, "transform[0]");
    GLint transform1 = getUniformLocation(program, "transform[1]");
    GLint scalar = getUniformLocation(program, "scalar");
    CHECK(transform >= 0 && transform == transform0 && transform1 >= 0 &&
              transform1 != transform0,
          "matrix-array element locations resolve independently");
    CHECK(scalar >= 0, "scalar control uniform resolves");

    GLint active_count = 0;
    getProgramiv(program, GL_ACTIVE_UNIFORMS, &active_count);
    int saw_palette = 0, saw_transform = 0;
    for (GLint i = 0; i < active_count; ++i) {
        char name[128] = {0};
        GLsizei length = 0;
        GLint size = 0;
        GLenum type = 0;
        getActiveUniform(program, (GLuint)i, sizeof(name), &length, &size,
                         &type, name);
        (void)length;
        (void)type;
        if (strcmp(name, "palette[0]") == 0 && size == 3) saw_palette = 1;
        if (strcmp(name, "transform[0]") == 0 && size == 2) saw_transform = 1;
    }
    CHECK(saw_palette && saw_transform,
          "active array reflection reports [0] names and declared sizes");

    const float initial_palette[12] = {
        .05f, .02f, .01f, 0.f,
        .10f, .03f, .02f, 0.f,
        .15f, .04f, .03f, 0.f,
    };
    uniform4fv(palette, 3, initial_palette);
    const float tail[8] = {
        .20f, .05f, .04f, 0.f,
        .10f, .06f, .05f, 0.f,
    };
    uniform4fv(palette1, 2, tail);
    const float overrun[8] = {
        .15f, .07f, .06f, 0.f,
        .90f, .90f, .90f, 0.f,
    };
    uniform4fv(palette2, 2, overrun);
    CHECK(getError() == GL_NO_ERROR,
          "array write beyond the active tail ignores excess elements");

    float got0[4] = {0}, got1[4] = {0}, got2[4] = {0};
    const float expected0[4] = {.05f, .02f, .01f, 0.f};
    const float expected1[4] = {.20f, .05f, .04f, 0.f};
    const float expected2[4] = {.15f, .07f, .06f, 0.f};
    getUniformfv(program, palette0, got0);
    getUniformfv(program, palette1, got1);
    getUniformfv(program, palette2, got2);
    CHECK(vec_near(got0, expected0, 4) && vec_near(got1, expected1, 4) &&
              vec_near(got2, expected2, 4),
          "per-element getters observe preserved partial-array state");

    uniform4f(scalar, .02f, .01f, .01f, 1.f);
    const float invalid_scalar_array[8] = {
        .8f, .8f, .8f, .8f, .9f, .9f, .9f, .9f};
    uniform4fv(scalar, 2, invalid_scalar_array);
    CHECK(getError() == GL_INVALID_OPERATION,
          "count > 1 on a non-array uniform is rejected");
    float scalar_value[4] = {0};
    const float expected_scalar[4] = {.02f, .01f, .01f, 1.f};
    getUniformfv(program, scalar, scalar_value);
    CHECK(vec_near(scalar_value, expected_scalar, 4),
          "rejected scalar-array write leaves the previous value intact");

    uniform4fv(palette0, -1, initial_palette);
    CHECK(getError() == GL_INVALID_VALUE, "negative uniform count is rejected");

    const float matrix1[4] = {.03f, 0.f, 0.f, .04f};
    uniformMatrix2fv(transform1, 1, GL_FALSE, matrix1);
    float got_matrix1[4] = {0};
    getUniformfv(program, transform1, got_matrix1);
    CHECK(vec_near(got_matrix1, matrix1, 4),
          "matrix-array element getter observes partial write");
    CHECK(getError() == GL_NO_ERROR,
          "uniform-array state setup finishes without residual GL errors");

    const float quad[] = {-1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, 1.f};
    GLuint vao = 0, vbo = 0;
    genVertexArrays(1, &vao);
    bindVertexArray(vao);
    genBuffers(1, &vbo);
    bindBuffer(GL_ARRAY_BUFFER, vbo);
    bufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    enableVertexAttribArray(0);
    vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), NULL);

    viewport(0, 0, 64, 64);
    clearColor(0.f, 0.f, 0.f, 1.f);
    clear(GL_COLOR_BUFFER_BIT);
    drawArrays(GL_TRIANGLE_STRIP, 0, 4);
    finish();

    unsigned char pixel[4] = {0};
    readPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    CHECK(pixel_near(pixel, 115, 48, 31),
          "vector/matrix arrays execute through backend strides (%u,%u,%u,%u)",
          pixel[0], pixel[1], pixel[2], pixel[3]);
    CHECK(getError() == GL_NO_ERROR,
          "uniform-array draw/readback completes without GL errors");

    printf("\nuniform_array_smoke: %s (%d failure%s)\n",
           failures ? "FAIL" : "PASS", failures,
           failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
