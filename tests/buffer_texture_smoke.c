/* DirectMetal typed buffer-texture vertical smoke.
 *
 * Proves GLSL isamplerBuffer -> SPIR-V -> native MSL texture_buffer, a typed
 * R32I Metal buffer view, live glBufferSubData visibility, and attachment
 * lifetime after deleting the source GL buffer name.
 *
 * Build:
 *   clang -std=c11 -Wall -Wextra -o /tmp/mithril-buffer-texture-smoke \
 *       tests/buffer_texture_smoke.c
 * Run:
 *   MITHRIL_BACKEND=metal MTL_DEBUG_LAYER=1 \
 *       /tmp/mithril-buffer-texture-smoke
 */

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_FLOAT 0x1406
#define GL_FALSE 0
#define GL_TRIANGLE_STRIP 0x0005
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_BUFFER 0x8C2A
#define GL_R32I 0x8235
#define GL_RGBA8 0x8058
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_RENDERER 0x1F01
#define GL_NO_ERROR 0

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef int GLsizei;
typedef long GLintptr;
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
typedef void (*fnUseProgram)(GLuint);
typedef GLint (*fnGetUniformLocation)(GLuint, const char*);
typedef void (*fnUniform1i)(GLint, GLint);
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
typedef void (*fnGenTextures)(GLsizei, GLuint*);
typedef void (*fnBindTexture)(GLenum, GLuint);
typedef void (*fnActiveTexture)(GLenum);
typedef void (*fnTexBuffer)(GLenum, GLenum, GLuint);
typedef void (*fnTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint,
                             GLenum, GLenum, const void*);
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

#define LOAD(type, variable, symbol)                                        \
    type variable = (type)dlsym(library, symbol)

static const char* vertex_source =
    "#version 150\n"
    "layout(location=0) in vec2 position;\n"
    "void main() { gl_Position = vec4(position, 0.0, 1.0); }\n";

static const char* fragment_source =
    "#version 150\n"
    "uniform isamplerBuffer values;\n"
    "layout(location=0) out vec4 color;\n"
    "void main() {\n"
    "  int value = texelFetch(values, 0).r;\n"
    "  color = value == 7 ? vec4(1,0,0,1) :\n"
    "          value == 11 ? vec4(0,1,0,1) : vec4(0,0,1,1);\n"
    "}\n";

static int pixel_is(const unsigned char pixel[4], unsigned char r,
                    unsigned char g, unsigned char b) {
    return abs((int)pixel[0] - r) <= 3 &&
           abs((int)pixel[1] - g) <= 3 &&
           abs((int)pixel[2] - b) <= 3 && pixel[3] >= 252;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const char* path = getenv("MITHRIL_LIBRARY");
    if (!path) path = "./output/libmithril.dylib";
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
    LOAD(fnUniform1i, uniform1i, "glUniform1i");
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
    LOAD(fnGenTextures, genTextures, "glGenTextures");
    LOAD(fnBindTexture, bindTexture, "glBindTexture");
    LOAD(fnActiveTexture, activeTexture, "glActiveTexture");
    LOAD(fnTexBuffer, texBuffer, "glTexBuffer");
    LOAD(fnTexImage2D, texImage2D, "glTexImage2D");
    LOAD(fnViewport, viewport, "glViewport");
    LOAD(fnClearColor, clearColor, "glClearColor");
    LOAD(fnClear, clear, "glClear");
    LOAD(fnDrawArrays, drawArrays, "glDrawArrays");
    LOAD(fnFinish, finish, "glFinish");
    LOAD(fnReadPixels, readPixels, "glReadPixels");

    CHECK(getString && getError && createShader && shaderSource &&
              compileShader && getShaderiv && createProgram && attachShader &&
              linkProgram && getProgramiv && useProgram &&
              getUniformLocation && uniform1i && genVertexArrays &&
              bindVertexArray && genBuffers && deleteBuffers && bindBuffer &&
              bufferData && bufferSubData && enableVertexAttribArray &&
              vertexAttribPointer && genTextures && bindTexture &&
              activeTexture && texBuffer && texImage2D && viewport &&
              clearColor && clear && drawArrays && finish && readPixels,
          "required buffer-texture symbols resolve");
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
    CHECK(vertex_ok && fragment_ok, "samplerBuffer shaders compile to shared SPIR-V");

    GLuint program = createProgram();
    attachShader(program, vertex);
    attachShader(program, fragment);
    linkProgram(program);
    GLint linked = 0;
    getProgramiv(program, GL_LINK_STATUS, &linked);
    CHECK(linked, "samplerBuffer program links and reflects");
    useProgram(program);
    GLint values_location = getUniformLocation(program, "values");
    uniform1i(values_location, 0);
    CHECK(values_location >= 0, "isamplerBuffer uniform resolves");

    const float quad[] = {-1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, 1.f};
    GLuint vao = 0, vertex_buffer = 0;
    genVertexArrays(1, &vao);
    bindVertexArray(vao);
    genBuffers(1, &vertex_buffer);
    bindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
    bufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    enableVertexAttribArray(0);
    vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), NULL);

    GLint value = 7;
    GLuint data_buffer = 0, buffer_texture = 0;
    genBuffers(1, &data_buffer);
    bindBuffer(GL_ARRAY_BUFFER, data_buffer);
    bufferData(GL_ARRAY_BUFFER, sizeof(value), &value, GL_STATIC_DRAW);
    genTextures(1, &buffer_texture);
    activeTexture(GL_TEXTURE0);
    bindTexture(GL_TEXTURE_BUFFER, buffer_texture);
    texBuffer(GL_TEXTURE_BUFFER, GL_R32I, data_buffer);

    /* A later bind to another target in unit 0 must not displace the buffer
     * target selected by the reflected isamplerBuffer type. */
    GLuint unrelated_2d = 0;
    const unsigned char white[] = {255, 255, 255, 255};
    genTextures(1, &unrelated_2d);
    bindTexture(GL_TEXTURE_2D, unrelated_2d);
    texImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, white);

    viewport(0, 0, 64, 64);
    clearColor(0.f, 0.f, 0.f, 1.f);
    clear(GL_COLOR_BUFFER_BIT);
    drawArrays(GL_TRIANGLE_STRIP, 0, 4);
    finish();
    unsigned char pixel[4] = {0};
    readPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    CHECK(pixel_is(pixel, 255, 0, 0),
          "native R32I texture buffer reads initial value (%u,%u,%u,%u)",
          pixel[0], pixel[1], pixel[2], pixel[3]);

    value = 11;
    bindBuffer(GL_ARRAY_BUFFER, data_buffer);
    bufferSubData(GL_ARRAY_BUFFER, 0, sizeof(value), &value);
    clear(GL_COLOR_BUFFER_BIT);
    drawArrays(GL_TRIANGLE_STRIP, 0, 4);
    finish();
    readPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    CHECK(pixel_is(pixel, 0, 255, 0),
          "bufferSubData version reaches the next Metal draw (%u,%u,%u,%u)",
          pixel[0], pixel[1], pixel[2], pixel[3]);

    deleteBuffers(1, &data_buffer);
    clear(GL_COLOR_BUFFER_BIT);
    drawArrays(GL_TRIANGLE_STRIP, 0, 4);
    finish();
    readPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    CHECK(pixel_is(pixel, 0, 255, 0),
          "texture attachment retains storage after buffer-name deletion "
          "(%u,%u,%u,%u)", pixel[0], pixel[1], pixel[2], pixel[3]);
    CHECK(getError() == GL_NO_ERROR,
          "buffer-texture draw/readback finishes without GL errors");

    printf("\nbuffer_texture_smoke: %s (%d failure%s)\n",
           failures ? "FAIL" : "PASS", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
