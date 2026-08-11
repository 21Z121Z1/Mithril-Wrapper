/* DirectMetal loose-matrix uniform execution smoke.
 *
 * A mat3 occupies three 16-byte columns in the synthetic uniform block even
 * though glUniformMatrix3fv supplies nine tightly packed floats. Deferred
 * draws verify that transpose=GL_TRUE is normalized before snapshotting and
 * that a non-square mat2x3 uses its reflected column stride as well.
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
#define GL_TRUE 1
#define GL_TRIANGLE_STRIP 0x0005
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
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
typedef void (*fnShaderSource)(GLuint, GLsizei, const char* const*,
                               const GLint*);
typedef void (*fnCompileShader)(GLuint);
typedef void (*fnGetShaderiv)(GLuint, GLenum, GLint*);
typedef GLuint (*fnCreateProgram)(void);
typedef void (*fnAttachShader)(GLuint, GLuint);
typedef void (*fnLinkProgram)(GLuint);
typedef void (*fnGetProgramiv)(GLuint, GLenum, GLint*);
typedef void (*fnUseProgram)(GLuint);
typedef GLint (*fnGetUniformLocation)(GLuint, const char*);
typedef void (*fnUniformMatrix3fv)(GLint, GLsizei, GLboolean, const float*);
typedef void (*fnUniformMatrix2x3fv)(GLint, GLsizei, GLboolean, const float*);
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

#define LOAD(type, variable, symbol)                                        \
    type variable = (type)dlsym(library, symbol)

static const char* vertex_source =
    "#version 150\n"
    "layout(location=0) in vec2 position;\n"
    "void main() { gl_Position = vec4(position, 0.0, 1.0); }\n";

static const char* fragment_source =
    "#version 150\n"
    "uniform mat3 transform;\n"
    "uniform mat2x3 stretch;\n"
    "layout(location=0) out vec4 color;\n"
    "void main() {\n"
    "  color = vec4(transform * vec3(1.0) + stretch * vec2(1.0), 1.0);\n"
    "}\n";

static int pixel_near(const unsigned char pixel[4], int r, int g, int b) {
    return abs((int)pixel[0] - r) <= 3 &&
           abs((int)pixel[1] - g) <= 3 &&
           abs((int)pixel[2] - b) <= 3 &&
           abs((int)pixel[3] - 255) <= 3;
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
    LOAD(fnUniformMatrix3fv, uniformMatrix3fv, "glUniformMatrix3fv");
    LOAD(fnUniformMatrix2x3fv, uniformMatrix2x3fv, "glUniformMatrix2x3fv");
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
              linkProgram && getProgramiv && useProgram && getUniformLocation &&
              uniformMatrix3fv && uniformMatrix2x3fv && genVertexArrays &&
              bindVertexArray && genBuffers && bindBuffer && bufferData &&
              enableVertexAttribArray && vertexAttribPointer && viewport &&
              clearColor && clear && drawArrays && finish && readPixels,
          "required matrix-uniform symbols resolve");
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
    CHECK(vertex_ok && fragment_ok, "matrix shaders compile");
    GLuint program = createProgram();
    attachShader(program, vertex);
    attachShader(program, fragment);
    linkProgram(program);
    GLint link_ok = 0;
    getProgramiv(program, GL_LINK_STATUS, &link_ok);
    CHECK(link_ok, "matrix program links");
    useProgram(program);
    GLint transform = getUniformLocation(program, "transform");
    GLint stretch = getUniformLocation(program, "stretch");
    CHECK(transform >= 0, "mat3 uniform resolves");
    CHECK(stretch >= 0, "mat2x3 uniform resolves");

    const float quad[] = {-1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, 1.f};
    GLuint vao = 0, vbo = 0;
    genVertexArrays(1, &vao);
    bindVertexArray(vao);
    genBuffers(1, &vbo);
    bindBuffer(GL_ARRAY_BUFFER, vbo);
    bufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    enableVertexAttribArray(0);
    vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), NULL);

    const float column_major[9] = {
        0.2f, 0.0f, 0.0f,
        0.0f, 0.4f, 0.0f,
        0.0f, 0.0f, 0.6f,
    };
    const float row_major[9] = {
        0.1f, 0.2f, 0.0f,
        0.0f, 0.3f, 0.1f,
        0.2f, 0.0f, 0.2f,
    };
    const float zero_mat3[9] = {0.f};
    const float zero_mat2x3[6] = {0.f};
    const float row_major_mat2x3[6] = {
        0.1f, 0.2f,
        0.2f, 0.1f,
        0.3f, 0.0f,
    };
    clearColor(0.f, 0.f, 0.f, 1.f);
    clear(GL_COLOR_BUFFER_BIT);
    uniformMatrix2x3fv(stretch, 1, GL_FALSE, zero_mat2x3);
    uniformMatrix3fv(transform, 1, GL_FALSE, column_major);
    viewport(0, 32, 32, 32);
    drawArrays(GL_TRIANGLE_STRIP, 0, 4);
    uniformMatrix3fv(transform, 1, GL_TRUE, row_major);
    viewport(32, 32, 32, 32);
    drawArrays(GL_TRIANGLE_STRIP, 0, 4);
    uniformMatrix3fv(transform, 1, GL_FALSE, zero_mat3);
    uniformMatrix2x3fv(stretch, 1, GL_TRUE, row_major_mat2x3);
    viewport(0, 0, 64, 32);
    drawArrays(GL_TRIANGLE_STRIP, 0, 4);
    finish();

    unsigned char left[4] = {0}, right[4] = {0}, bottom[4] = {0};
    readPixels(16, 48, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, left);
    readPixels(48, 48, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, right);
    readPixels(32, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, bottom);
    CHECK(pixel_near(left, 51, 102, 153),
          "mat3 columns honor reflected std140 stride (%u,%u,%u,%u)",
          left[0], left[1], left[2], left[3]);
    CHECK(pixel_near(right, 77, 102, 102),
          "transpose=true normalizes row-major input (%u,%u,%u,%u)",
          right[0], right[1], right[2], right[3]);
    CHECK(pixel_near(bottom, 77, 77, 77),
          "mat2x3 honors non-square stride and transpose (%u,%u,%u,%u)",
          bottom[0], bottom[1], bottom[2], bottom[3]);
    CHECK(getError() == GL_NO_ERROR,
          "matrix uniform draw/readback finishes without GL errors");

    printf("\nmatrix_uniform_smoke: %s (%d failure%s)\n",
           failures ? "FAIL" : "PASS", failures,
           failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
