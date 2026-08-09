/* DirectMetal typed interleaved vertex-buffer smoke.
 *
 * Proves one resident VBO can feed signed-short and unsigned-byte integer
 * shader inputs plus normalized unsigned-byte float colour without a per-draw
 * float repack. Buffer versioning and deletion after deferred draw are also
 * checked through framebuffer readback.
 */

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_SHORT 0x1402
#define GL_UNSIGNED_BYTE 0x1401
#define GL_TRUE 1
#define GL_FALSE 0
#define GL_TRIANGLES 0x0004
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_RGBA 0x1908
#define GL_RENDERER 0x1F01
#define GL_NO_ERROR 0
#define GL_VERTEX_ATTRIB_ARRAY_INTEGER 0x88FD

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef int GLsizei;
typedef intptr_t GLintptr;
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
typedef void (*fnDeleteBuffers)(GLsizei, const GLuint*);
typedef void (*fnBindBuffer)(GLenum, GLuint);
typedef void (*fnBufferData)(GLenum, GLsizeiptr, const void*, GLenum);
typedef void (*fnBufferSubData)(GLenum, GLintptr, GLsizeiptr, const void*);
typedef void (*fnEnableVertexAttribArray)(GLuint);
typedef void (*fnVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean,
                                      GLsizei, const void*);
typedef void (*fnVertexAttribIPointer)(GLuint, GLint, GLenum, GLsizei,
                                       const void*);
typedef void (*fnGetVertexAttribiv)(GLuint, GLenum, GLint*);
typedef void (*fnClearColor)(float, float, float, float);
typedef void (*fnClear)(GLbitfield);
typedef void (*fnDrawArrays)(GLenum, GLint, GLsizei);
typedef void (*fnReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum,
                             void*);

struct __attribute__((packed)) PackedVertex {
    int16_t position[2];
    uint8_t tag[4];
    uint8_t color[4];
};
_Static_assert(sizeof(struct PackedVertex) == 12, "packed vertex ABI");

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
    "layout(location=0) in ivec4 packedPosition;\n"
    "layout(location=1) in uvec4 packedTag;\n"
    "layout(location=2) in vec4 normalizedColor;\n"
    "out vec4 vertexColor;\n"
    "void main() {\n"
    "  gl_Position = vec4(vec2(packedPosition.xy), 0.0, 1.0);\n"
    "  vertexColor = (packedPosition.z == 0 && packedPosition.w == 1 &&\n"
    "                 packedTag.x == 200u && packedTag.y == 3u)\n"
    "      ? normalizedColor : vec4(1.0, 0.0, 1.0, 1.0);\n"
    "}\n";

static const char* fragment_source =
    "#version 330 core\n"
    "in vec4 vertexColor;\n"
    "layout(location=0) out vec4 color;\n"
    "void main() { color = vertexColor; }\n";

static void set_color(struct PackedVertex vertices[3],
                      uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < 3; ++i) {
        vertices[i].color[0] = r;
        vertices[i].color[1] = g;
        vertices[i].color[2] = b;
        vertices[i].color[3] = 255;
    }
}

static int pixel_is(const unsigned char pixel[4], int r, int g, int b) {
    return abs((int)pixel[0] - r) <= 3 &&
           abs((int)pixel[1] - g) <= 3 &&
           abs((int)pixel[2] - b) <= 3 && pixel[3] >= 252;
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
    LOAD(fnGenVertexArrays, genVertexArrays, "glGenVertexArrays");
    LOAD(fnBindVertexArray, bindVertexArray, "glBindVertexArray");
    LOAD(fnGenBuffers, genBuffers, "glGenBuffers");
    LOAD(fnDeleteBuffers, deleteBuffers, "glDeleteBuffers");
    LOAD(fnBindBuffer, bindBuffer, "glBindBuffer");
    LOAD(fnBufferData, bufferData, "glBufferData");
    LOAD(fnBufferSubData, bufferSubData, "glBufferSubData");
    LOAD(fnEnableVertexAttribArray, enableVertexAttribArray,
         "glEnableVertexAttribArray");
    LOAD(fnVertexAttribPointer, vertexAttribPointer, "glVertexAttribPointer");
    LOAD(fnVertexAttribIPointer, vertexAttribIPointer, "glVertexAttribIPointer");
    LOAD(fnGetVertexAttribiv, getVertexAttribiv, "glGetVertexAttribiv");
    LOAD(fnClearColor, clearColor, "glClearColor");
    LOAD(fnClear, clear, "glClear");
    LOAD(fnDrawArrays, drawArrays, "glDrawArrays");
    LOAD(fnReadPixels, readPixels, "glReadPixels");
    CHECK(getString && getError && createShader && shaderSource &&
              compileShader && getShaderiv && createProgram && attachShader &&
              linkProgram && getProgramiv && useProgram && genVertexArrays &&
              bindVertexArray && genBuffers && deleteBuffers && bindBuffer &&
              bufferData && bufferSubData && enableVertexAttribArray &&
              vertexAttribPointer && vertexAttribIPointer &&
              getVertexAttribiv && clearColor && clear && drawArrays &&
              readPixels,
          "required typed-vertex symbols resolve");
    if (failures) return 1;

    const char* renderer = (const char*)getString(GL_RENDERER);
    CHECK(renderer && strstr(renderer, "DirectMetal"),
          "context is explicitly DirectMetal (%s)", renderer ? renderer : "null");

    GLuint vertex = createShader(GL_VERTEX_SHADER);
    GLuint fragment = createShader(GL_FRAGMENT_SHADER);
    shaderSource(vertex, 1, &vertex_source, NULL);
    shaderSource(fragment, 1, &fragment_source, NULL);
    compileShader(vertex);
    compileShader(fragment);
    GLint vertex_ok = 0, fragment_ok = 0;
    getShaderiv(vertex, GL_COMPILE_STATUS, &vertex_ok);
    getShaderiv(fragment, GL_COMPILE_STATUS, &fragment_ok);
    CHECK(vertex_ok && fragment_ok,
          "ivec/uvec/normalized-float shaders compile to shared SPIR-V");

    GLuint program = createProgram();
    attachShader(program, vertex);
    attachShader(program, fragment);
    linkProgram(program);
    GLint linked = 0;
    getProgramiv(program, GL_LINK_STATUS, &linked);
    CHECK(linked, "typed vertex program links");
    useProgram(program);

    struct PackedVertex vertices[3] = {
        {{-1, -1}, {200, 3, 17, 29}, {0, 255, 0, 255}},
        {{ 1, -1}, {200, 3, 17, 29}, {0, 255, 0, 255}},
        {{ 0,  1}, {200, 3, 17, 29}, {0, 255, 0, 255}},
    };
    GLuint vao = 0, buffer = 0;
    genVertexArrays(1, &vao);
    bindVertexArray(vao);
    genBuffers(1, &buffer);
    bindBuffer(GL_ARRAY_BUFFER, buffer);
    bufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    enableVertexAttribArray(0);
    vertexAttribIPointer(0, 2, GL_SHORT, sizeof(struct PackedVertex),
                         (const void*)0);
    enableVertexAttribArray(1);
    vertexAttribIPointer(1, 4, GL_UNSIGNED_BYTE, sizeof(struct PackedVertex),
                         (const void*)4);
    enableVertexAttribArray(2);
    vertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                        sizeof(struct PackedVertex), (const void*)8);
    GLint integer_position = 0, integer_tag = 0, integer_color = 1;
    getVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_INTEGER, &integer_position);
    getVertexAttribiv(1, GL_VERTEX_ATTRIB_ARRAY_INTEGER, &integer_tag);
    getVertexAttribiv(2, GL_VERTEX_ATTRIB_ARRAY_INTEGER, &integer_color);
    CHECK(integer_position && integer_tag && !integer_color,
          "frontend preserves integer-path versus normalized-float state");

    unsigned char pixel[4] = {0, 0, 0, 0};
    clearColor(0.f, 0.f, 0.f, 1.f);
    clear(GL_COLOR_BUFFER_BIT);
    drawArrays(GL_TRIANGLES, 0, 3);
    readPixels(256, 256, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    CHECK(pixel_is(pixel, 0, 255, 0),
          "native Short2/UChar4/UChar4Normalized VBO renders green");

    set_color(vertices, 0, 0, 255);
    bufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    clear(GL_COLOR_BUFFER_BIT);
    drawArrays(GL_TRIANGLES, 0, 3);
    readPixels(256, 256, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    CHECK(pixel_is(pixel, 0, 0, 255),
          "content-version update refreshes resident typed VBO once");

    set_color(vertices, 255, 0, 0);
    bufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    clear(GL_COLOR_BUFFER_BIT);
    drawArrays(GL_TRIANGLES, 0, 3);
    deleteBuffers(1, &buffer);
    readPixels(256, 256, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    CHECK(pixel_is(pixel, 255, 0, 0) && getError() == GL_NO_ERROR,
          "deferred typed draw retains VBO after GL name deletion");

    dlclose(library);
    printf("\ntyped_vertex_smoke: %s (%d failure%s)\n",
           failures ? "FAIL" : "PASS", failures,
           failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
