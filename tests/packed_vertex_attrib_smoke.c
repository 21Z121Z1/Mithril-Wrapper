/* OpenGL 3.3 packed current-attribute semantic smoke.
 *
 * Covers every glVertexAttribP{1,2,3,4}{ui,uiv} entry point, signed and
 * unsigned 2_10_10_10_REV decoding, normalized/direct conversion, implicit
 * components, error preservation, and disabled-array shader consumption.
 */

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_NO_ERROR 0
#define GL_INVALID_ENUM 0x0500
#define GL_INVALID_VALUE 0x0501
#define GL_RENDERER 0x1F01
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_FLOAT 0x1406
#define GL_UNSIGNED_INT 0x1405
#define GL_UNSIGNED_BYTE 0x1401
#define GL_TRIANGLES 0x0004
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_RGBA 0x1908
#define GL_VERTEX_ATTRIB_ARRAY_ENABLED 0x8622
#define GL_CURRENT_VERTEX_ATTRIB 0x8626
#define GL_UNSIGNED_INT_2_10_10_10_REV 0x8368
#define GL_INT_2_10_10_10_REV 0x8D9F

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
typedef void (*fnGenVertexArrays)(GLsizei, GLuint*);
typedef void (*fnBindVertexArray)(GLuint);
typedef void (*fnGenBuffers)(GLsizei, GLuint*);
typedef void (*fnBindBuffer)(GLenum, GLuint);
typedef void (*fnBufferData)(GLenum, GLsizeiptr, const void*, GLenum);
typedef void (*fnEnableVertexAttribArray)(GLuint);
typedef void (*fnDisableVertexAttribArray)(GLuint);
typedef void (*fnVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean,
                                      GLsizei, const void*);
typedef void (*fnVertexAttribPui)(GLuint, GLenum, GLboolean, GLuint);
typedef void (*fnVertexAttribPuiv)(GLuint, GLenum, GLboolean, const GLuint*);
typedef void (*fnGetVertexAttribfv)(GLuint, GLenum, float*);
typedef void (*fnGetVertexAttribiv)(GLuint, GLenum, GLint*);
typedef void (*fnClearColor)(float, float, float, float);
typedef void (*fnClear)(GLbitfield);
typedef void (*fnDrawArrays)(GLenum, GLint, GLsizei);
typedef void (*fnReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum,
                             void*);

static int failures;

#define CHECK(condition, ...) do {                                           \
    if (condition) printf("ok  : " __VA_ARGS__);                            \
    else { printf("FAIL: " __VA_ARGS__); ++failures; }                      \
    printf("\n");                                                           \
} while (0)

#define LOAD(type, variable, symbol)                                         \
    type variable = (type)dlsym(library, symbol)

static const char* vertex_source =
    "#version 330 core\n"
    "layout(location=0) in vec2 position;\n"
    "layout(location=1) in vec4 packedColor;\n"
    "out vec4 vertexColor;\n"
    "void main() {\n"
    "  gl_Position = vec4(position, 0.0, 1.0);\n"
    "  vertexColor = packedColor;\n"
    "}\n";

static const char* fragment_source =
    "#version 330 core\n"
    "in vec4 vertexColor;\n"
    "layout(location=0) out vec4 color;\n"
    "void main() { color = vertexColor; }\n";

static GLuint pack_unsigned(GLuint x, GLuint y, GLuint z, GLuint w) {
    return (x & 0x3FFu) | ((y & 0x3FFu) << 10u) |
           ((z & 0x3FFu) << 20u) | ((w & 0x3u) << 30u);
}

static GLuint pack_signed(GLint x, GLint y, GLint z, GLint w) {
    return pack_unsigned((GLuint)x, (GLuint)y, (GLuint)z, (GLuint)w);
}

static float abs_float(float value) {
    return value < 0.f ? -value : value;
}

static int vector_matches(const float actual[4], float x, float y, float z,
                          float w) {
    return abs_float(actual[0] - x) <= 0.0001f &&
           abs_float(actual[1] - y) <= 0.0001f &&
           abs_float(actual[2] - z) <= 0.0001f &&
           abs_float(actual[3] - w) <= 0.0001f;
}

static int pixel_matches(const unsigned char actual[4], unsigned char r,
                         unsigned char g, unsigned char b) {
    return abs((int)actual[0] - r) <= 2 &&
           abs((int)actual[1] - g) <= 2 &&
           abs((int)actual[2] - b) <= 2 && actual[3] >= 253;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const char* path = getenv("MITHRIL_LIBRARY");
#if defined(__APPLE__)
    if (!path || !*path) path = "./output/libmithril.dylib";
#else
    if (!path || !*path) path = "./output/libmithril.so";
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
    LOAD(fnUseProgram, useProgram, "glUseProgram");
    LOAD(fnGenVertexArrays, genVertexArrays, "glGenVertexArrays");
    LOAD(fnBindVertexArray, bindVertexArray, "glBindVertexArray");
    LOAD(fnGenBuffers, genBuffers, "glGenBuffers");
    LOAD(fnBindBuffer, bindBuffer, "glBindBuffer");
    LOAD(fnBufferData, bufferData, "glBufferData");
    LOAD(fnEnableVertexAttribArray, enableVertexAttribArray,
         "glEnableVertexAttribArray");
    LOAD(fnDisableVertexAttribArray, disableVertexAttribArray,
         "glDisableVertexAttribArray");
    LOAD(fnVertexAttribPointer, vertexAttribPointer, "glVertexAttribPointer");
    LOAD(fnVertexAttribPui, vertexAttribP1ui, "glVertexAttribP1ui");
    LOAD(fnVertexAttribPuiv, vertexAttribP1uiv, "glVertexAttribP1uiv");
    LOAD(fnVertexAttribPui, vertexAttribP2ui, "glVertexAttribP2ui");
    LOAD(fnVertexAttribPuiv, vertexAttribP2uiv, "glVertexAttribP2uiv");
    LOAD(fnVertexAttribPui, vertexAttribP3ui, "glVertexAttribP3ui");
    LOAD(fnVertexAttribPuiv, vertexAttribP3uiv, "glVertexAttribP3uiv");
    LOAD(fnVertexAttribPui, vertexAttribP4ui, "glVertexAttribP4ui");
    LOAD(fnVertexAttribPuiv, vertexAttribP4uiv, "glVertexAttribP4uiv");
    LOAD(fnGetVertexAttribfv, getVertexAttribfv, "glGetVertexAttribfv");
    LOAD(fnGetVertexAttribiv, getVertexAttribiv, "glGetVertexAttribiv");
    LOAD(fnClearColor, clearColor, "glClearColor");
    LOAD(fnClear, clear, "glClear");
    LOAD(fnDrawArrays, drawArrays, "glDrawArrays");
    LOAD(fnReadPixels, readPixels, "glReadPixels");

    CHECK(getString && getError && createShader && shaderSource &&
              compileShader && getShaderiv && createProgram && attachShader &&
              linkProgram && getProgramiv && useProgram && genVertexArrays &&
              bindVertexArray && genBuffers && bindBuffer && bufferData &&
              enableVertexAttribArray && disableVertexAttribArray &&
              vertexAttribPointer && vertexAttribP1ui && vertexAttribP1uiv &&
              vertexAttribP2ui && vertexAttribP2uiv && vertexAttribP3ui &&
              vertexAttribP3uiv && vertexAttribP4ui && vertexAttribP4uiv &&
              getVertexAttribfv && getVertexAttribiv && clearColor && clear &&
              drawArrays && readPixels,
          "all packed-current-attribute symbols resolve");
    if (failures) return 1;

    const char* renderer = (const char*)getString(GL_RENDERER);
    const char* expected = getenv("MITHRIL_EXPECT_RENDERER");
    CHECK(renderer && (!expected || strstr(renderer, expected)),
          "selected renderer is explicit (%s)", renderer ? renderer : "null");

    GLuint vertex = createShader(GL_VERTEX_SHADER);
    GLuint fragment = createShader(GL_FRAGMENT_SHADER);
    shaderSource(vertex, 1, &vertex_source, NULL);
    shaderSource(fragment, 1, &fragment_source, NULL);
    compileShader(vertex);
    compileShader(fragment);
    GLint vertex_ok = 0, fragment_ok = 0;
    getShaderiv(vertex, GL_COMPILE_STATUS, &vertex_ok);
    getShaderiv(fragment, GL_COMPILE_STATUS, &fragment_ok);
    GLuint program = createProgram();
    attachShader(program, vertex);
    attachShader(program, fragment);
    linkProgram(program);
    GLint linked = 0;
    getProgramiv(program, GL_LINK_STATUS, &linked);
    useProgram(program);
    CHECK(vertex_ok && fragment_ok && linked,
          "packed current-attribute shader compiles and links");

    const float positions[6] = {-1.f, -1.f, 3.f, -1.f, -1.f, 3.f};
    const float array_colors[12] = {
        0.f, 0.f, 1.f, 1.f,
        0.f, 0.f, 1.f, 1.f,
        0.f, 0.f, 1.f, 1.f,
    };
    GLuint vao = 0, buffers[2] = {0};
    genVertexArrays(1, &vao);
    bindVertexArray(vao);
    genBuffers(2, buffers);
    bindBuffer(GL_ARRAY_BUFFER, buffers[0]);
    bufferData(GL_ARRAY_BUFFER, sizeof(positions), positions, GL_STATIC_DRAW);
    enableVertexAttribArray(0);
    vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    bindBuffer(GL_ARRAY_BUFFER, buffers[1]);
    bufferData(GL_ARRAY_BUFFER, sizeof(array_colors), array_colors,
               GL_STATIC_DRAW);
    vertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 0, NULL);
    disableVertexAttribArray(1);

    float current[4] = {0};
    GLint enabled = 1;
    GLuint packed = pack_unsigned(1023, 777, 888, 0);
    vertexAttribP1ui(1, GL_UNSIGNED_INT_2_10_10_10_REV, GL_TRUE, packed);
    getVertexAttribfv(1, GL_CURRENT_VERTEX_ATTRIB, current);
    getVertexAttribiv(1, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &enabled);
    CHECK(vector_matches(current, 1.f, 0.f, 0.f, 1.f) && !enabled,
          "P1ui consumes x only and does not enable the array");

    packed = pack_signed(-512, 31, -7, -1);
    vertexAttribP1uiv(1, GL_INT_2_10_10_10_REV, GL_TRUE, &packed);
    getVertexAttribfv(1, GL_CURRENT_VERTEX_ATTRIB, current);
    CHECK(vector_matches(current, -1.f, 0.f, 0.f, 1.f),
          "P1uiv signed normalization clamps the 10-bit minimum");

    packed = pack_signed(-2, 3, -17, -1);
    vertexAttribP2ui(1, GL_INT_2_10_10_10_REV, GL_FALSE, packed);
    getVertexAttribfv(1, GL_CURRENT_VERTEX_ATTRIB, current);
    CHECK(vector_matches(current, -2.f, 3.f, 0.f, 1.f),
          "P2ui directly converts x/y and ignores packed z/w");

    packed = pack_unsigned(0, 1023, 999, 0);
    vertexAttribP2uiv(1, GL_UNSIGNED_INT_2_10_10_10_REV, GL_TRUE, &packed);
    getVertexAttribfv(1, GL_CURRENT_VERTEX_ATTRIB, current);
    CHECK(vector_matches(current, 0.f, 1.f, 0.f, 1.f),
          "P2uiv normalizes x/y and restores implicit z/w");

    packed = pack_unsigned(0, 1023, 0, 0);
    vertexAttribP3ui(1, GL_UNSIGNED_INT_2_10_10_10_REV, GL_TRUE, packed);
    getVertexAttribfv(1, GL_CURRENT_VERTEX_ATTRIB, current);
    CHECK(vector_matches(current, 0.f, 1.f, 0.f, 1.f),
          "P3ui ignores packed w and supplies implicit one");
    clearColor(0.f, 0.f, 0.f, 1.f);
    clear(GL_COLOR_BUFFER_BIT);
    drawArrays(GL_TRIANGLES, 0, 3);
    unsigned char pixel[4] = {0};
    readPixels(256, 256, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    CHECK(pixel_matches(pixel, 0, 255, 0),
          "P3ui current attribute reaches the renderer as green "
          "(%u,%u,%u,%u)", pixel[0], pixel[1], pixel[2], pixel[3]);

    packed = pack_signed(0, -512, 511, -2);
    vertexAttribP3uiv(1, GL_INT_2_10_10_10_REV, GL_TRUE, &packed);
    getVertexAttribfv(1, GL_CURRENT_VERTEX_ATTRIB, current);
    CHECK(vector_matches(current, 0.f, -1.f, 1.f, 1.f),
          "P3uiv signed normalization handles min/max and ignores w");

    packed = pack_signed(1, -2, 3, -1);
    vertexAttribP4ui(1, GL_INT_2_10_10_10_REV, GL_FALSE, packed);
    getVertexAttribfv(1, GL_CURRENT_VERTEX_ATTRIB, current);
    CHECK(vector_matches(current, 1.f, -2.f, 3.f, -1.f),
          "P4ui directly converts all signed packed components");

    packed = pack_unsigned(1023, 0, 0, 3);
    vertexAttribP4uiv(1, GL_UNSIGNED_INT_2_10_10_10_REV, GL_TRUE, &packed);
    getVertexAttribfv(1, GL_CURRENT_VERTEX_ATTRIB, current);
    CHECK(vector_matches(current, 1.f, 0.f, 0.f, 1.f),
          "P4uiv normalizes all unsigned packed components");
    clear(GL_COLOR_BUFFER_BIT);
    drawArrays(GL_TRIANGLES, 0, 3);
    memset(pixel, 0, sizeof(pixel));
    readPixels(256, 256, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    CHECK(pixel_matches(pixel, 255, 0, 0),
          "P4uiv current attribute reaches the renderer as red "
          "(%u,%u,%u,%u)", pixel[0], pixel[1], pixel[2], pixel[3]);

    vertexAttribP4ui(16, GL_UNSIGNED_INT_2_10_10_10_REV, GL_TRUE, packed);
    CHECK(getError() == GL_INVALID_VALUE,
          "out-of-range packed attribute index reports GL_INVALID_VALUE");
    vertexAttribP4ui(1, GL_UNSIGNED_INT, GL_TRUE, packed);
    CHECK(getError() == GL_INVALID_ENUM,
          "invalid packed attribute type reports GL_INVALID_ENUM");
    getVertexAttribfv(1, GL_CURRENT_VERTEX_ATTRIB, current);
    CHECK(vector_matches(current, 1.f, 0.f, 0.f, 1.f) &&
              getError() == GL_NO_ERROR,
          "rejected packed calls preserve current state and error recovery");

    enableVertexAttribArray(1);
    clear(GL_COLOR_BUFFER_BIT);
    drawArrays(GL_TRIANGLES, 0, 3);
    memset(pixel, 0, sizeof(pixel));
    readPixels(256, 256, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    CHECK(pixel_matches(pixel, 0, 0, 255),
          "packed current setters preserve the disabled array's VBO format "
          "(%u,%u,%u,%u)", pixel[0], pixel[1], pixel[2], pixel[3]);

    dlclose(library);
    printf("\npacked_vertex_attrib_smoke: %s (%d failure%s)\n",
           failures ? "FAIL" : "PASS", failures,
           failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
