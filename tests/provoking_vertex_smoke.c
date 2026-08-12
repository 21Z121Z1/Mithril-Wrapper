/* DirectMetal provoking-vertex execution smoke.
 *
 * OpenGL 3.3 defaults to GL_LAST_VERTEX_CONVENTION for flat-shaded triangle
 * outputs. DirectMetal must preserve that observable value and switch to the
 * first vertex when glProvokingVertex requests it.
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
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_FLOAT 0x1406
#define GL_FALSE 0
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_TRIANGLE_FAN 0x0006
#define GL_LINES 0x0001
#define GL_LINE_STRIP 0x0003
#define GL_PRIMITIVE_RESTART 0x8F9D
#define GL_CULL_FACE 0x0B44
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_INT 0x1405
#define GL_RENDERER 0x1F01
#define GL_NO_ERROR 0
#define GL_INVALID_ENUM 0x0500
#define GL_FIRST_VERTEX_CONVENTION 0x8E4D
#define GL_LAST_VERTEX_CONVENTION 0x8E4E
#define GL_PROVOKING_VERTEX 0x8E4F

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef int GLsizei;
typedef long GLsizeiptr;
typedef unsigned char GLboolean;

typedef const unsigned char* (*fnGetString)(GLenum);
typedef GLenum (*fnGetError)(void);
typedef void (*fnGetIntegerv)(GLenum, GLint*);
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
typedef void (*fnGenVertexArrays)(GLsizei, GLuint*);
typedef void (*fnBindVertexArray)(GLuint);
typedef void (*fnGenBuffers)(GLsizei, GLuint*);
typedef void (*fnBindBuffer)(GLenum, GLuint);
typedef void (*fnBufferData)(GLenum, GLsizeiptr, const void*, GLenum);
typedef void (*fnEnableVertexAttribArray)(GLuint);
typedef void (*fnVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean,
                                      GLsizei, const void*);
typedef void (*fnProvokingVertex)(GLenum);
typedef void (*fnPrimitiveRestartIndex)(GLuint);
typedef void (*fnEnable)(GLenum);
typedef void (*fnDisable)(GLenum);
typedef void (*fnViewport)(GLint, GLint, GLsizei, GLsizei);
typedef void (*fnClearColor)(float, float, float, float);
typedef void (*fnClear)(GLbitfield);
typedef void (*fnDrawArrays)(GLenum, GLint, GLsizei);
typedef void (*fnDrawElements)(GLenum, GLsizei, GLenum, const void*);
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
    "layout(location=1) in vec3 vertexColor;\n"
    "flat out vec3 selectedColor;\n"
    "void main() {\n"
    "  gl_Position = vec4(position, 0.0, 1.0);\n"
    "  selectedColor = vertexColor;\n"
    "}\n";

static const char* fragment_source =
    "#version 150\n"
    "flat in vec3 selectedColor;\n"
    "layout(location=0) out vec4 color;\n"
    "void main() { color = vec4(selectedColor, 1.0); }\n";

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
    LOAD(fnGetIntegerv, getIntegerv, "glGetIntegerv");
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
    LOAD(fnVertexAttribPointer, vertexAttribPointer, "glVertexAttribPointer");
    LOAD(fnProvokingVertex, provokingVertex, "glProvokingVertex");
    LOAD(fnPrimitiveRestartIndex, primitiveRestartIndex,
         "glPrimitiveRestartIndex");
    LOAD(fnEnable, enable, "glEnable");
    LOAD(fnDisable, disable, "glDisable");
    LOAD(fnViewport, viewport, "glViewport");
    LOAD(fnClearColor, clearColor, "glClearColor");
    LOAD(fnClear, clear, "glClear");
    LOAD(fnDrawArrays, drawArrays, "glDrawArrays");
    LOAD(fnDrawElements, drawElements, "glDrawElements");
    LOAD(fnFinish, finish, "glFinish");
    LOAD(fnReadPixels, readPixels, "glReadPixels");

    CHECK(getString && getError && getIntegerv && createShader &&
              shaderSource && compileShader && getShaderiv && createProgram &&
              attachShader && linkProgram && getProgramiv && useProgram &&
              genVertexArrays && bindVertexArray && genBuffers && bindBuffer &&
              bufferData && enableVertexAttribArray && vertexAttribPointer &&
              provokingVertex && primitiveRestartIndex && enable && disable &&
              viewport && clearColor && clear && drawArrays && drawElements &&
              finish && readPixels,
          "required provoking-vertex symbols resolve");
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
    CHECK(vertex_ok && fragment_ok, "flat-varying shaders compile");
    GLuint program = createProgram();
    attachShader(program, vertex);
    attachShader(program, fragment);
    linkProgram(program);
    GLint link_ok = 0;
    getProgramiv(program, GL_LINK_STATUS, &link_ok);
    CHECK(link_ok, "flat-varying program links");
    useProgram(program);

    const float vertices[] = {
        -0.8f, -0.8f, 1.f, 0.f, 0.f,
         0.8f, -0.8f, 0.f, 1.f, 0.f,
         0.0f,  0.8f, 0.f, 0.f, 1.f,
    };
    GLuint vao = 0, vbo = 0, ebo = 0;
    genVertexArrays(1, &vao);
    bindVertexArray(vao);
    genBuffers(1, &vbo);
    bindBuffer(GL_ARRAY_BUFFER, vbo);
    bufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    enableVertexAttribArray(0);
    vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), NULL);
    enableVertexAttribArray(1);
    vertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                        (const void*)(2 * sizeof(float)));
    genBuffers(1, &ebo);
    bindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);

    GLint mode = 0;
    getIntegerv(GL_PROVOKING_VERTEX, &mode);
    CHECK(mode == GL_LAST_VERTEX_CONVENTION,
          "default convention is GL_LAST_VERTEX_CONVENTION (0x%x)", mode);
    CHECK(getError() == GL_NO_ERROR,
          "GL_PROVOKING_VERTEX is a valid integer state query");

    clearColor(0.f, 0.f, 0.f, 1.f);
    clear(GL_COLOR_BUFFER_BIT);
    viewport(0, 32, 32, 32);
    drawArrays(GL_TRIANGLES, 0, 3);
    provokingVertex(GL_FIRST_VERTEX_CONVENTION);
    viewport(32, 32, 32, 32);
    drawArrays(GL_TRIANGLES, 0, 3);
    provokingVertex(GL_LAST_VERTEX_CONVENTION);
    viewport(0, 0, 64, 32);
    drawArrays(GL_TRIANGLES, 0, 3);
    provokingVertex(0xDEAD);
    CHECK(getError() == GL_INVALID_ENUM,
          "invalid provoking convention reports GL_INVALID_ENUM");
    getIntegerv(GL_PROVOKING_VERTEX, &mode);
    CHECK(mode == GL_LAST_VERTEX_CONVENTION && getError() == GL_NO_ERROR,
          "invalid convention leaves the previous LAST state unchanged");
    finish();

    unsigned char default_pixel[4] = {0};
    unsigned char first_pixel[4] = {0};
    unsigned char last_pixel[4] = {0};
    readPixels(16, 48, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, default_pixel);
    readPixels(48, 48, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, first_pixel);
    readPixels(32, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, last_pixel);
    CHECK(pixel_near(default_pixel, 0, 0, 255),
          "default GL convention selects last vertex (%u,%u,%u,%u)",
          default_pixel[0], default_pixel[1], default_pixel[2], default_pixel[3]);
    CHECK(pixel_near(first_pixel, 255, 0, 0),
          "FIRST convention selects first vertex (%u,%u,%u,%u)",
          first_pixel[0], first_pixel[1], first_pixel[2], first_pixel[3]);
    CHECK(pixel_near(last_pixel, 0, 0, 255),
          "LAST convention selects last vertex (%u,%u,%u,%u)",
          last_pixel[0], last_pixel[1], last_pixel[2], last_pixel[3]);
    CHECK(getError() == GL_NO_ERROR,
          "provoking-vertex draw/readback finishes without GL errors");

    const float strip_vertices[] = {
        -0.9f, -0.9f, 1.f, 0.f, 0.f,
         0.9f, -0.9f, 0.f, 1.f, 0.f,
        -0.9f,  0.9f, 0.f, 0.f, 1.f,
         0.9f,  0.9f, 1.f, 1.f, 0.f,
    };
    bindBuffer(GL_ARRAY_BUFFER, vbo);
    bufferData(GL_ARRAY_BUFFER, sizeof(strip_vertices), strip_vertices,
               GL_STATIC_DRAW);
    enable(GL_CULL_FACE);
    provokingVertex(GL_LAST_VERTEX_CONVENTION);
    clear(GL_COLOR_BUFFER_BIT);
    viewport(0, 0, 64, 64);
    drawArrays(GL_TRIANGLE_STRIP, 0, 4);
    finish();
    unsigned char strip_left[4] = {0}, strip_right[4] = {0};
    readPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, strip_left);
    readPixels(48, 48, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, strip_right);
    CHECK(pixel_near(strip_left, 0, 0, 255) &&
              pixel_near(strip_right, 255, 255, 0),
          "LAST triangle strip selects i+2 across alternating winding "
          "(left=%u,%u,%u right=%u,%u,%u)",
          strip_left[0], strip_left[1], strip_left[2],
          strip_right[0], strip_right[1], strip_right[2]);

    provokingVertex(GL_FIRST_VERTEX_CONVENTION);
    clear(GL_COLOR_BUFFER_BIT);
    drawArrays(GL_TRIANGLE_STRIP, 0, 4);
    finish();
    readPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, strip_left);
    readPixels(48, 48, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, strip_right);
    CHECK(pixel_near(strip_left, 255, 0, 0) &&
              pixel_near(strip_right, 0, 255, 0),
          "FIRST triangle strip selects i across alternating winding "
          "(left=%u,%u,%u right=%u,%u,%u)",
          strip_left[0], strip_left[1], strip_left[2],
          strip_right[0], strip_right[1], strip_right[2]);

    const float fan_vertices[] = {
         0.0f,  0.0f, 1.f, 0.f, 0.f,
        -0.9f, -0.9f, 0.f, 1.f, 0.f,
         0.9f, -0.9f, 0.f, 0.f, 1.f,
    };
    bufferData(GL_ARRAY_BUFFER, sizeof(fan_vertices), fan_vertices,
               GL_STATIC_DRAW);
    provokingVertex(GL_FIRST_VERTEX_CONVENTION);
    clear(GL_COLOR_BUFFER_BIT);
    drawArrays(GL_TRIANGLE_FAN, 0, 3);
    finish();
    unsigned char fan_pixel[4] = {0};
    readPixels(32, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, fan_pixel);
    CHECK(pixel_near(fan_pixel, 0, 255, 0),
          "FIRST triangle fan selects first non-spoke vertex (%u,%u,%u,%u)",
          fan_pixel[0], fan_pixel[1], fan_pixel[2], fan_pixel[3]);
    provokingVertex(GL_LAST_VERTEX_CONVENTION);
    clear(GL_COLOR_BUFFER_BIT);
    drawArrays(GL_TRIANGLE_FAN, 0, 3);
    finish();
    readPixels(32, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, fan_pixel);
    CHECK(pixel_near(fan_pixel, 0, 0, 255),
          "LAST triangle fan selects i+2 (%u,%u,%u,%u)",
          fan_pixel[0], fan_pixel[1], fan_pixel[2], fan_pixel[3]);

    /* y=17/64 maps to the centre of framebuffer row 40 in a 64px viewport.
     * Avoid putting the one-pixel Metal line exactly on a pixel boundary;
     * this test discriminates provoking color, not implementation-defined
     * tie-breaking between adjacent line-fragment diamonds. */
    const float line_vertices[] = {
        -0.9f, 0.265625f, 1.f, 0.f, 0.f,
         0.0f, 0.265625f, 0.f, 1.f, 0.f,
         0.9f, 0.265625f, 0.f, 0.f, 1.f,
    };
    bufferData(GL_ARRAY_BUFFER, sizeof(line_vertices), line_vertices,
               GL_STATIC_DRAW);
    provokingVertex(GL_FIRST_VERTEX_CONVENTION);
    clear(GL_COLOR_BUFFER_BIT);
    drawArrays(GL_LINE_STRIP, 0, 3);
    finish();
    unsigned char line_left[4] = {0}, line_right[4] = {0};
    readPixels(16, 40, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, line_left);
    readPixels(48, 40, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, line_right);
    CHECK(pixel_near(line_left, 255, 0, 0) &&
              pixel_near(line_right, 0, 255, 0),
          "FIRST line strip selects i (left=%u,%u,%u right=%u,%u,%u)",
          line_left[0], line_left[1], line_left[2],
          line_right[0], line_right[1], line_right[2]);
    provokingVertex(GL_LAST_VERTEX_CONVENTION);
    clear(GL_COLOR_BUFFER_BIT);
    drawArrays(GL_LINE_STRIP, 0, 3);
    finish();
    readPixels(16, 40, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, line_left);
    readPixels(48, 40, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, line_right);
    CHECK(pixel_near(line_left, 0, 255, 0) &&
              pixel_near(line_right, 0, 0, 255),
          "LAST line strip selects i+1 (left=%u,%u,%u right=%u,%u,%u)",
          line_left[0], line_left[1], line_left[2],
          line_right[0], line_right[1], line_right[2]);

    provokingVertex(GL_FIRST_VERTEX_CONVENTION);
    clear(GL_COLOR_BUFFER_BIT);
    drawArrays(GL_LINES, 0, 2);
    finish();
    readPixels(16, 40, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, line_left);
    CHECK(pixel_near(line_left, 255, 0, 0),
          "FIRST independent line selects vertex 2i-1 (%u,%u,%u,%u)",
          line_left[0], line_left[1], line_left[2], line_left[3]);
    provokingVertex(GL_LAST_VERTEX_CONVENTION);
    clear(GL_COLOR_BUFFER_BIT);
    drawArrays(GL_LINES, 0, 2);
    finish();
    readPixels(16, 40, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, line_left);
    CHECK(pixel_near(line_left, 0, 255, 0),
          "LAST independent line selects vertex 2i (%u,%u,%u,%u)",
          line_left[0], line_left[1], line_left[2], line_left[3]);

    const float restart_vertices[] = {
        -0.9f, -0.8f, 1.f, 0.f, 0.f,
        -0.1f, -0.8f, 0.f, 1.f, 0.f,
        -0.5f,  0.8f, 0.f, 0.f, 1.f,
         0.1f, -0.8f, 0.f, 1.f, 1.f,
         0.9f, -0.8f, 1.f, 0.f, 1.f,
         0.5f,  0.8f, 1.f, 1.f, 0.f,
    };
    const GLuint restart_indices[] = {0, 1, 2, 99, 3, 4, 5};
    bufferData(GL_ARRAY_BUFFER, sizeof(restart_vertices), restart_vertices,
               GL_STATIC_DRAW);
    bindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    bufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(restart_indices),
               restart_indices, GL_STATIC_DRAW);
    enable(GL_PRIMITIVE_RESTART);
    primitiveRestartIndex(99);
    provokingVertex(GL_LAST_VERTEX_CONVENTION);
    clear(GL_COLOR_BUFFER_BIT);
    drawElements(GL_TRIANGLE_STRIP, 7, GL_UNSIGNED_INT, NULL);
    finish();
    unsigned char restart_left[4] = {0}, restart_right[4] = {0};
    readPixels(16, 26, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, restart_left);
    readPixels(48, 26, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, restart_right);
    CHECK(pixel_near(restart_left, 0, 0, 255) &&
              pixel_near(restart_right, 255, 255, 0),
          "primitive restart resets strip provoking sequence "
          "(left=%u,%u,%u right=%u,%u,%u)",
          restart_left[0], restart_left[1], restart_left[2],
          restart_right[0], restart_right[1], restart_right[2]);
    disable(GL_CULL_FACE);
    CHECK(getError() == GL_NO_ERROR,
          "strip/fan/restart provoking draws finish without GL errors");

    printf("\nprovoking_vertex_smoke: %s (%d failure%s)\n",
           failures ? "FAIL" : "PASS", failures,
           failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
