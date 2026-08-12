/* DirectMetal fixed sampler-array compatibility smoke.
 *
 * Covers OpenGL 3.3 observable array locations/reflection and the native
 * resource path. `tex[2]` is followed by a scalar sampler so the expected
 * pixel also proves that a sampler array consumes two distinct Metal texture /
 * sampler slots instead of aliasing the following resource.
 */

#include <dlfcn.h>
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
#define GL_RGBA8 0x8058
#define GL_UNSIGNED_BYTE 0x1401
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_NEAREST 0x2600
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
typedef void (*fnGetShaderiv)(GLuint, GLenum, GLint*);
typedef GLuint (*fnCreateProgram)(void);
typedef void (*fnAttachShader)(GLuint, GLuint);
typedef void (*fnLinkProgram)(GLuint);
typedef void (*fnGetProgramiv)(GLuint, GLenum, GLint*);
typedef void (*fnGetActiveUniform)(GLuint, GLuint, GLsizei, GLsizei*, GLint*, GLenum*, char*);
typedef void (*fnUseProgram)(GLuint);
typedef GLint (*fnGetUniformLocation)(GLuint, const char*);
typedef void (*fnUniform1i)(GLint, GLint);
typedef void (*fnUniform1iv)(GLint, GLsizei, const GLint*);
typedef void (*fnGetUniformiv)(GLuint, GLint, GLint*);
typedef void (*fnGenVertexArrays)(GLsizei, GLuint*);
typedef void (*fnBindVertexArray)(GLuint);
typedef void (*fnGenBuffers)(GLsizei, GLuint*);
typedef void (*fnBindBuffer)(GLenum, GLuint);
typedef void (*fnBufferData)(GLenum, GLsizeiptr, const void*, GLenum);
typedef void (*fnEnableVertexAttribArray)(GLuint);
typedef void (*fnVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
typedef void (*fnGenTextures)(GLsizei, GLuint*);
typedef void (*fnActiveTexture)(GLenum);
typedef void (*fnBindTexture)(GLenum, GLuint);
typedef void (*fnTexParameteri)(GLenum, GLenum, GLint);
typedef void (*fnTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
typedef void (*fnViewport)(GLint, GLint, GLsizei, GLsizei);
typedef void (*fnClearColor)(float, float, float, float);
typedef void (*fnClear)(GLbitfield);
typedef void (*fnDrawArrays)(GLenum, GLint, GLsizei);
typedef void (*fnFinish)(void);
typedef void (*fnReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);

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
    "uniform sampler2D tex[2];\n"
    "uniform sampler2D tail;\n"
    "layout(location=0) out vec4 color;\n"
    "void main() {\n"
    "  vec2 uv = vec2(0.5, 0.5);\n"
    "  color = texture(tex[0], uv) * 0.2\n"
    "        + texture(tex[1], uv) * 0.3\n"
    "        + texture(tail, uv) * 0.5;\n"
    "}\n";

static int pixel_near(const unsigned char p[4], int r, int g, int b, int a) {
    return abs((int)p[0] - r) <= 4 && abs((int)p[1] - g) <= 4 &&
           abs((int)p[2] - b) <= 4 && abs((int)p[3] - a) <= 4;
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
    LOAD(fnUniform1i, uniform1i, "glUniform1i");
    LOAD(fnUniform1iv, uniform1iv, "glUniform1iv");
    LOAD(fnGetUniformiv, getUniformiv, "glGetUniformiv");
    LOAD(fnGenVertexArrays, genVertexArrays, "glGenVertexArrays");
    LOAD(fnBindVertexArray, bindVertexArray, "glBindVertexArray");
    LOAD(fnGenBuffers, genBuffers, "glGenBuffers");
    LOAD(fnBindBuffer, bindBuffer, "glBindBuffer");
    LOAD(fnBufferData, bufferData, "glBufferData");
    LOAD(fnEnableVertexAttribArray, enableVertexAttribArray, "glEnableVertexAttribArray");
    LOAD(fnVertexAttribPointer, vertexAttribPointer, "glVertexAttribPointer");
    LOAD(fnGenTextures, genTextures, "glGenTextures");
    LOAD(fnActiveTexture, activeTexture, "glActiveTexture");
    LOAD(fnBindTexture, bindTexture, "glBindTexture");
    LOAD(fnTexParameteri, texParameteri, "glTexParameteri");
    LOAD(fnTexImage2D, texImage2D, "glTexImage2D");
    LOAD(fnViewport, viewport, "glViewport");
    LOAD(fnClearColor, clearColor, "glClearColor");
    LOAD(fnClear, clear, "glClear");
    LOAD(fnDrawArrays, drawArrays, "glDrawArrays");
    LOAD(fnFinish, finish, "glFinish");
    LOAD(fnReadPixels, readPixels, "glReadPixels");

    CHECK(getString && getError && createShader && shaderSource && compileShader &&
              getShaderiv && createProgram && attachShader && linkProgram &&
              getProgramiv && getActiveUniform && useProgram && getUniformLocation &&
              uniform1i && uniform1iv && getUniformiv && genVertexArrays &&
              bindVertexArray && genBuffers && bindBuffer && bufferData &&
              enableVertexAttribArray && vertexAttribPointer && genTextures &&
              activeTexture && bindTexture && texParameteri && texImage2D &&
              viewport && clearColor && clear && drawArrays && finish && readPixels,
          "required sampler-array symbols resolve");
    if (failures) return 1;

    const char* renderer = (const char*)getString(GL_RENDERER);
    CHECK(renderer && strstr(renderer, "Mithril DirectMetal"),
          "DirectMetal backend is active (%s)", renderer ? renderer : "null");

    GLuint vs = createShader(GL_VERTEX_SHADER);
    GLuint fs = createShader(GL_FRAGMENT_SHADER);
    shaderSource(vs, 1, &vertex_source, NULL);
    shaderSource(fs, 1, &fragment_source, NULL);
    compileShader(vs);
    compileShader(fs);
    GLint vs_ok = 0, fs_ok = 0;
    getShaderiv(vs, GL_COMPILE_STATUS, &vs_ok);
    getShaderiv(fs, GL_COMPILE_STATUS, &fs_ok);
    CHECK(vs_ok && fs_ok, "sampler-array shaders compile");

    GLuint program = createProgram();
    attachShader(program, vs);
    attachShader(program, fs);
    linkProgram(program);
    GLint linked = 0;
    getProgramiv(program, GL_LINK_STATUS, &linked);
    CHECK(linked, "sampler-array program links");
    useProgram(program);

    GLint base = getUniformLocation(program, "tex");
    GLint tex0 = getUniformLocation(program, "tex[0]");
    GLint tex1 = getUniformLocation(program, "tex[1]");
    GLint tex2 = getUniformLocation(program, "tex[2]");
    GLint tail = getUniformLocation(program, "tail");
    CHECK(base >= 0 && base == tex0,
          "sampler array base aliases tex[0] (%d)", base);
    CHECK(tex1 >= 0 && tex1 != tex0 && tex2 == -1 && tail >= 0,
          "sampler array elements and following scalar expose distinct locations");

    GLint active = 0;
    getProgramiv(program, GL_ACTIVE_UNIFORMS, &active);
    int saw_tex = 0;
    for (GLint i = 0; i < active; ++i) {
        char name[128] = {0};
        GLsizei length = 0;
        GLint size = 0;
        GLenum type = 0;
        getActiveUniform(program, (GLuint)i, sizeof(name), &length, &size, &type, name);
        (void)length;
        (void)type;
        if (strcmp(name, "tex[0]") == 0 && size == 2) saw_tex = 1;
    }
    CHECK(saw_tex, "active reflection reports sampler array name/size");

    const GLint units[2] = {0, 1};
    uniform1iv(base, 2, units);
    uniform1i(tail, 2);
    GLint got0 = -1, got1 = -1, gottail = -1;
    getUniformiv(program, tex0, &got0);
    getUniformiv(program, tex1, &got1);
    getUniformiv(program, tail, &gottail);
    CHECK(got0 == 0 && got1 == 1 && gottail == 2,
          "sampler array/scalar texture units round-trip (0,1,2)");
    CHECK(getError() == GL_NO_ERROR,
          "sampler-array uniform setup has no GL error");

    const unsigned char colors[3][4] = {
        {255, 0, 0, 255},
        {0, 255, 0, 255},
        {0, 0, 255, 255},
    };
    GLuint textures[3] = {0};
    genTextures(3, textures);
    for (int unit = 0; unit < 3; ++unit) {
        activeTexture(GL_TEXTURE0 + (GLenum)unit);
        bindTexture(GL_TEXTURE_2D, textures[unit]);
        texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        texImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0,
                   GL_RGBA, GL_UNSIGNED_BYTE, colors[unit]);
    }
    activeTexture(GL_TEXTURE0);
    CHECK(getError() == GL_NO_ERROR, "three sampler backing textures upload");

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
    CHECK(pixel_near(pixel, 51, 77, 128, 255),
          "sampler array + following sampler execute in distinct Metal slots (%u,%u,%u,%u)",
          pixel[0], pixel[1], pixel[2], pixel[3]);
    CHECK(getError() == GL_NO_ERROR,
          "sampler-array draw/readback completes without GL errors");

    printf("\nsampler_array_smoke: %s (%d failure%s)\n",
           failures ? "FAIL" : "PASS", failures,
           failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
