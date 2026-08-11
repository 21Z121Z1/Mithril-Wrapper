/* DirectMetal independent sampler-object vertical smoke.
 *
 * Draws two 2x1 textures through sampler uniforms mapped to non-matching GL
 * texture units in one deferred Metal batch. Sampler and texture objects are
 * deleted before glFinish so readback also guards the frontend snapshot/native
 * lifetime boundary that real multi-texture Minecraft draws depend on.
 *
 * Build:
 *   clang -std=c11 -o /tmp/mithril-sampler-smoke tests/sampler_smoke.c
 * Run:
 *   MITHRIL_BACKEND=metal MTL_DEBUG_LAYER=1 /tmp/mithril-sampler-smoke
 */

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_FLOAT 0x1406
#define GL_FALSE 0
#define GL_TRIANGLE_STRIP 0x0005
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE_2D 0x0DE1
#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_UNSIGNED_BYTE 0x1401
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_RENDERER 0x1F01
#define GL_NO_ERROR 0

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
typedef GLuint (*fnCreateProgram)(void);
typedef void (*fnAttachShader)(GLuint, GLuint);
typedef void (*fnLinkProgram)(GLuint);
typedef void (*fnUseProgram)(GLuint);
typedef GLint (*fnGetUniformLocation)(GLuint, const char*);
typedef void (*fnUniform1i)(GLint, GLint);
typedef void (*fnGenVertexArrays)(GLsizei, GLuint*);
typedef void (*fnBindVertexArray)(GLuint);
typedef void (*fnGenBuffers)(GLsizei, GLuint*);
typedef void (*fnBindBuffer)(GLenum, GLuint);
typedef void (*fnBufferData)(GLenum, GLsizeiptr, const void*, GLenum);
typedef void (*fnEnableVertexAttribArray)(GLuint);
typedef void (*fnVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean,
                                      GLsizei, const void*);
typedef void (*fnGenTextures)(GLsizei, GLuint*);
typedef void (*fnDeleteTextures)(GLsizei, const GLuint*);
typedef GLboolean (*fnIsTexture)(GLuint);
typedef void (*fnBindTexture)(GLenum, GLuint);
typedef void (*fnActiveTexture)(GLenum);
typedef void (*fnTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint,
                             GLenum, GLenum, const void*);
typedef void (*fnTexParameteri)(GLenum, GLenum, GLint);
typedef void (*fnGenSamplers)(GLsizei, GLuint*);
typedef void (*fnDeleteSamplers)(GLsizei, const GLuint*);
typedef GLboolean (*fnIsSampler)(GLuint);
typedef void (*fnBindSampler)(GLuint, GLuint);
typedef void (*fnSamplerParameteri)(GLuint, GLenum, GLint);
typedef void (*fnGetSamplerParameteriv)(GLuint, GLenum, GLint*);
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
    "uniform sampler2D firstImage;\n"
    "uniform sampler2D secondImage;\n"
    "uniform int selection;\n"
    "layout(location=0) out vec4 color;\n"
    "void main() {\n"
    "  vec4 first = texture(firstImage, vec2(0.4, 0.5));\n"
    "  vec4 second = texture(secondImage, vec2(0.4, 0.5));\n"
    "  color = selection == 0 ? first :\n"
    "      (selection == 1 ? second : mix(first, second, 0.5));\n"
    "}\n";

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
    LOAD(fnCreateProgram, createProgram, "glCreateProgram");
    LOAD(fnAttachShader, attachShader, "glAttachShader");
    LOAD(fnLinkProgram, linkProgram, "glLinkProgram");
    LOAD(fnUseProgram, useProgram, "glUseProgram");
    LOAD(fnGetUniformLocation, getUniformLocation, "glGetUniformLocation");
    LOAD(fnUniform1i, uniform1i, "glUniform1i");
    LOAD(fnGenVertexArrays, genVertexArrays, "glGenVertexArrays");
    LOAD(fnBindVertexArray, bindVertexArray, "glBindVertexArray");
    LOAD(fnGenBuffers, genBuffers, "glGenBuffers");
    LOAD(fnBindBuffer, bindBuffer, "glBindBuffer");
    LOAD(fnBufferData, bufferData, "glBufferData");
    LOAD(fnEnableVertexAttribArray, enableVertexAttribArray,
         "glEnableVertexAttribArray");
    LOAD(fnVertexAttribPointer, vertexAttribPointer, "glVertexAttribPointer");
    LOAD(fnGenTextures, genTextures, "glGenTextures");
    LOAD(fnDeleteTextures, deleteTextures, "glDeleteTextures");
    LOAD(fnIsTexture, isTexture, "glIsTexture");
    LOAD(fnBindTexture, bindTexture, "glBindTexture");
    LOAD(fnActiveTexture, activeTexture, "glActiveTexture");
    LOAD(fnTexImage2D, texImage2D, "glTexImage2D");
    LOAD(fnTexParameteri, texParameteri, "glTexParameteri");
    LOAD(fnGenSamplers, genSamplers, "glGenSamplers");
    LOAD(fnDeleteSamplers, deleteSamplers, "glDeleteSamplers");
    LOAD(fnIsSampler, isSampler, "glIsSampler");
    LOAD(fnBindSampler, bindSampler, "glBindSampler");
    LOAD(fnSamplerParameteri, samplerParameteri, "glSamplerParameteri");
    LOAD(fnGetSamplerParameteriv, getSamplerParameteriv,
         "glGetSamplerParameteriv");
    LOAD(fnViewport, viewport, "glViewport");
    LOAD(fnClearColor, clearColor, "glClearColor");
    LOAD(fnClear, clear, "glClear");
    LOAD(fnDrawArrays, drawArrays, "glDrawArrays");
    LOAD(fnFinish, finish, "glFinish");
    LOAD(fnReadPixels, readPixels, "glReadPixels");

    CHECK(getString && getError && createShader && shaderSource &&
              compileShader && createProgram && attachShader && linkProgram &&
              useProgram && getUniformLocation && uniform1i &&
              genVertexArrays && bindVertexArray && genBuffers && bindBuffer &&
              bufferData && enableVertexAttribArray && vertexAttribPointer &&
              genTextures && deleteTextures && isTexture && bindTexture &&
              activeTexture && texImage2D &&
              texParameteri && genSamplers && deleteSamplers && isSampler &&
              bindSampler && samplerParameteri && getSamplerParameteriv &&
              viewport && clearColor && clear && drawArrays && finish &&
              readPixels,
          "required independent-sampler symbols resolve");
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
    GLuint program = createProgram();
    attachShader(program, vertex);
    attachShader(program, fragment);
    linkProgram(program);
    useProgram(program);
    GLint first_location = getUniformLocation(program, "firstImage");
    GLint second_location = getUniformLocation(program, "secondImage");
    GLint selection_location = getUniformLocation(program, "selection");
    uniform1i(first_location, 5);
    uniform1i(second_location, 2);
    CHECK(first_location >= 0 && second_location >= 0 &&
              selection_location >= 0,
          "two sampler uniforms and loose integer uniform resolve");

    const float quad[] = {-1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, 1.f};
    GLuint vao = 0, vbo = 0;
    genVertexArrays(1, &vao);
    bindVertexArray(vao);
    genBuffers(1, &vbo);
    bindBuffer(GL_ARRAY_BUFFER, vbo);
    bufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    enableVertexAttribArray(0);
    vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), NULL);

    const unsigned char first_texels[] = {
        255, 0, 0, 255, 0, 0, 255, 255};
    const unsigned char second_texels[] = {
        0, 255, 0, 255, 255, 255, 255, 255};
    GLuint textures[2] = {0, 0};
    genTextures(2, textures);
    activeTexture(GL_TEXTURE0 + 5);
    bindTexture(GL_TEXTURE_2D, textures[0]);
    texImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 1, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, first_texels);
    texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    activeTexture(GL_TEXTURE0 + 2);
    bindTexture(GL_TEXTURE_2D, textures[1]);
    texImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 1, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, second_texels);
    texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    GLuint samplers[2] = {0, 0};
    genSamplers(2, samplers);
    samplerParameteri(samplers[0], GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    samplerParameteri(samplers[0], GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    samplerParameteri(samplers[0], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    samplerParameteri(samplers[1], GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    samplerParameteri(samplers[1], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    samplerParameteri(samplers[1], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    GLint nearest = 0, linear = 0;
    getSamplerParameteriv(samplers[0], GL_TEXTURE_MAG_FILTER, &nearest);
    getSamplerParameteriv(samplers[1], GL_TEXTURE_MAG_FILTER, &linear);
    CHECK(isSampler(samplers[0]) && isSampler(samplers[1]),
          "generated names are live sampler objects");
    CHECK(nearest == GL_NEAREST && linear == GL_LINEAR,
          "sampler parameter state round-trips independently");

    clearColor(0.f, 1.f, 0.f, 1.f);
    clear(GL_COLOR_BUFFER_BIT);
    bindSampler(5, samplers[0]);
    bindSampler(2, samplers[1]);
    uniform1i(selection_location, 0);
    viewport(0, 0, 32, 32);
    drawArrays(GL_TRIANGLE_STRIP, 0, 4);
    uniform1i(selection_location, 1);
    viewport(32, 0, 32, 32);
    drawArrays(GL_TRIANGLE_STRIP, 0, 4);
    uniform1i(selection_location, 2);
    viewport(64, 0, 32, 32);
    drawArrays(GL_TRIANGLE_STRIP, 0, 4);

    /* Deletion unbinds the frontend names. Pending draws retain their fully
     * resolved state and native Metal objects until command submission. */
    deleteSamplers(2, samplers);
    deleteTextures(2, textures);
    CHECK(!isSampler(samplers[0]) && !isSampler(samplers[1]),
          "deleted sampler names leave the frontend object table");
    CHECK(!isTexture(textures[0]) && !isTexture(textures[1]),
          "deleted texture names leave the frontend object table");
    finish();

    unsigned char left[4] = {0}, middle[4] = {0}, right[4] = {0};
    readPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, left);
    readPixels(48, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, middle);
    readPixels(80, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, right);
    CHECK(abs((int)left[0] - 255) <= 3 && left[1] <= 3 && left[2] <= 3 &&
              abs((int)left[3] - 255) <= 3,
          "nearest sampler reads the red texel (%u,%u,%u,%u)",
          left[0], left[1], left[2], left[3]);
    CHECK(middle[0] >= 45 && middle[0] <= 115 && middle[1] >= 252 &&
              middle[2] >= 45 && middle[2] <= 115 &&
              abs((int)middle[3] - 255) <= 3,
          "second sampler maps to non-matching unit 2 and blends green/white (%u,%u,%u,%u)",
          middle[0], middle[1], middle[2], middle[3]);
    CHECK(right[0] >= 135 && right[0] <= 195 &&
              right[1] >= 105 && right[1] <= 150 &&
              right[2] >= 20 && right[2] <= 65 &&
              abs((int)right[3] - 255) <= 3,
          "two independently mapped samplers combine in one draw (%u,%u,%u,%u)",
          right[0], right[1], right[2], right[3]);
    CHECK(getError() == GL_NO_ERROR,
          "sampler draw/readback finishes without GL errors");

    printf("\nsampler_smoke: %s (%d failure%s)\n",
           failures ? "FAIL" : "PASS", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
