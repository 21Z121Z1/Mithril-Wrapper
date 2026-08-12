/* OpenGL 3.3 uniform setter type/size contract smoke.
 *
 * Verifies that the frontend rejects mismatched glUniform* families without
 * mutating prior values, performs the GL-mandated numeric-to-boolean
 * conversion, accepts sampler values only through Uniform1i, and then consumes
 * the accepted values in a real backend draw/readback.
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
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_UNSIGNED_BYTE 0x1401
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_NEAREST 0x2600
#define GL_RENDERER 0x1F01
#define GL_NO_ERROR 0
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
typedef void (*fnUseProgram)(GLuint);
typedef GLint (*fnGetUniformLocation)(GLuint, const char*);
typedef void (*fnGetUniformfv)(GLuint, GLint, float*);
typedef void (*fnGetUniformiv)(GLuint, GLint, GLint*);
typedef void (*fnGetUniformuiv)(GLuint, GLint, GLuint*);
typedef void (*fnUniform1f)(GLint, float);
typedef void (*fnUniform1i)(GLint, GLint);
typedef void (*fnUniform2f)(GLint, float, float);
typedef void (*fnUniform2i)(GLint, GLint, GLint);
typedef void (*fnUniform2ui)(GLint, GLuint, GLuint);
typedef void (*fnUniform3i)(GLint, GLint, GLint, GLint);
typedef void (*fnUniform3ui)(GLint, GLuint, GLuint, GLuint);
typedef void (*fnUniform4f)(GLint, float, float, float, float);
typedef void (*fnUniform4i)(GLint, GLint, GLint, GLint, GLint);
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
typedef void (*fnGenTextures)(GLsizei, GLuint*);
typedef void (*fnBindTexture)(GLenum, GLuint);
typedef void (*fnTexParameteri)(GLenum, GLenum, GLint);
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

#define LOAD(type, variable, symbol) type variable = (type)dlsym(library, symbol)

static const char* vertex_source =
    "#version 150\n"
    "layout(location=0) in vec2 position;\n"
    "void main() { gl_Position = vec4(position, 0.0, 1.0); }\n";

static const char* fragment_source =
    "#version 150\n"
    "uniform vec4 tint;\n"
    "uniform ivec2 signedPair;\n"
    "uniform uvec3 unsignedTriple;\n"
    "uniform bvec2 flags;\n"
    "uniform mat2 basis;\n"
    "uniform sampler2D tex;\n"
    "uniform float sampleWeight;\n"
    "layout(location=0) out vec4 color;\n"
    "void main() {\n"
    "  vec2 b = basis * vec2(1.0, 1.0);\n"
    "  vec4 boolTerm = vec4(flags.x ? 0.05 : 0.0, flags.y ? 0.05 : 0.0, 0.0, 0.0);\n"
    "  vec4 sampled = texture(tex, vec2(0.5, 0.5));\n"
    "  color = tint + vec4(vec2(signedPair) * 0.01, 0.0, 0.0)\n"
    "        + vec4(vec3(unsignedTriple) * 0.001, 0.0) + boolTerm\n"
    "        + vec4(b * 0.02, 0.0, 0.0) + sampled * sampleWeight;\n"
    "}\n";

static int four_equal(const float* value, float a, float b, float c, float d) {
    return value[0] == a && value[1] == b && value[2] == c && value[3] == d;
}

static int pixel_near(const unsigned char pixel[4], int r, int g, int b) {
    return abs((int)pixel[0] - r) <= 4 &&
           abs((int)pixel[1] - g) <= 4 &&
           abs((int)pixel[2] - b) <= 4 &&
           abs((int)pixel[3] - 255) <= 4;
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
    LOAD(fnUseProgram, useProgram, "glUseProgram");
    LOAD(fnGetUniformLocation, getUniformLocation, "glGetUniformLocation");
    LOAD(fnGetUniformfv, getUniformfv, "glGetUniformfv");
    LOAD(fnGetUniformiv, getUniformiv, "glGetUniformiv");
    LOAD(fnGetUniformuiv, getUniformuiv, "glGetUniformuiv");
    LOAD(fnUniform1f, uniform1f, "glUniform1f");
    LOAD(fnUniform1i, uniform1i, "glUniform1i");
    LOAD(fnUniform2f, uniform2f, "glUniform2f");
    LOAD(fnUniform2i, uniform2i, "glUniform2i");
    LOAD(fnUniform2ui, uniform2ui, "glUniform2ui");
    LOAD(fnUniform3i, uniform3i, "glUniform3i");
    LOAD(fnUniform3ui, uniform3ui, "glUniform3ui");
    LOAD(fnUniform4f, uniform4f, "glUniform4f");
    LOAD(fnUniform4i, uniform4i, "glUniform4i");
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
    LOAD(fnGenTextures, genTextures, "glGenTextures");
    LOAD(fnBindTexture, bindTexture, "glBindTexture");
    LOAD(fnTexParameteri, texParameteri, "glTexParameteri");
    LOAD(fnTexImage2D, texImage2D, "glTexImage2D");
    LOAD(fnViewport, viewport, "glViewport");
    LOAD(fnClearColor, clearColor, "glClearColor");
    LOAD(fnClear, clear, "glClear");
    LOAD(fnDrawArrays, drawArrays, "glDrawArrays");
    LOAD(fnFinish, finish, "glFinish");
    LOAD(fnReadPixels, readPixels, "glReadPixels");

    CHECK(getString && getError && createShader && shaderSource &&
              compileShader && getShaderiv && createProgram && attachShader &&
              linkProgram && getProgramiv && useProgram && getUniformLocation &&
              getUniformfv && getUniformiv && getUniformuiv && uniform1f &&
              uniform1i && uniform2f && uniform2i && uniform2ui && uniform3i &&
              uniform3ui && uniform4f && uniform4i && uniform4fv &&
              uniformMatrix2fv && genVertexArrays && bindVertexArray &&
              genBuffers && bindBuffer && bufferData && enableVertexAttribArray &&
              vertexAttribPointer && genTextures && bindTexture && texParameteri &&
              texImage2D && viewport && clearColor && clear && drawArrays &&
              finish && readPixels,
          "required uniform-type symbols resolve");
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
    CHECK(vertex_ok && fragment_ok, "uniform-type shaders compile");

    GLuint program = createProgram();
    attachShader(program, vertex);
    attachShader(program, fragment);
    linkProgram(program);
    GLint link_ok = 0;
    getProgramiv(program, GL_LINK_STATUS, &link_ok);
    CHECK(link_ok, "uniform-type program links");
    useProgram(program);

    GLint tint = getUniformLocation(program, "tint");
    GLint signed_pair = getUniformLocation(program, "signedPair");
    GLint unsigned_triple = getUniformLocation(program, "unsignedTriple");
    GLint flags = getUniformLocation(program, "flags");
    GLint basis = getUniformLocation(program, "basis");
    GLint tex = getUniformLocation(program, "tex");
    GLint sample_weight = getUniformLocation(program, "sampleWeight");
    CHECK(tint >= 0 && signed_pair >= 0 && unsigned_triple >= 0 && flags >= 0 &&
              basis >= 0 && tex >= 0 && sample_weight >= 0,
          "all typed uniforms are active and location-addressable");
    while (getError() != GL_NO_ERROR) {}

    uniform4f(tint, .10f, .20f, .30f, 1.f);
    CHECK(getError() == GL_NO_ERROR, "matching float-vector setter succeeds");
    uniform4i(tint, 9, 9, 9, 9);
    CHECK(getError() == GL_INVALID_OPERATION,
          "integer setter is rejected for vec4");
    float tint_back[4] = {0};
    getUniformfv(program, tint, tint_back);
    CHECK(four_equal(tint_back, .10f, .20f, .30f, 1.f),
          "rejected vec4 write preserves the accepted value");

    uniform2i(signed_pair, 2, -1);
    CHECK(getError() == GL_NO_ERROR, "matching signed-vector setter succeeds");
    uniform2ui(signed_pair, 99u, 99u);
    CHECK(getError() == GL_INVALID_OPERATION,
          "unsigned setter is rejected for ivec2");
    GLint signed_back[2] = {0};
    getUniformiv(program, signed_pair, signed_back);
    CHECK(signed_back[0] == 2 && signed_back[1] == -1,
          "rejected ivec2 write preserves signed values");

    uniform3ui(unsigned_triple, 10u, 20u, 30u);
    CHECK(getError() == GL_NO_ERROR, "matching unsigned-vector setter succeeds");
    uniform3i(unsigned_triple, 7, 7, 7);
    CHECK(getError() == GL_INVALID_OPERATION,
          "signed setter is rejected for uvec3");
    GLuint unsigned_back[3] = {0};
    getUniformuiv(program, unsigned_triple, unsigned_back);
    CHECK(unsigned_back[0] == 10u && unsigned_back[1] == 20u &&
              unsigned_back[2] == 30u,
          "rejected uvec3 write preserves unsigned values");

    uniform2f(flags, 0.f, -2.5f);
    CHECK(getError() == GL_NO_ERROR,
          "bvec2 accepts matching-size floating-point setter");
    float flags_back[2] = {-1.f, -1.f};
    getUniformfv(program, flags, flags_back);
    CHECK(flags_back[0] == 0.f && flags_back[1] == 1.f,
          "boolean conversion normalizes zero/nonzero to 0/1");
    uniform1i(flags, 1);
    CHECK(getError() == GL_INVALID_OPERATION,
          "bvec2 rejects a one-component setter");
    getUniformfv(program, flags, flags_back);
    CHECK(flags_back[0] == 0.f && flags_back[1] == 1.f,
          "rejected boolean write preserves prior converted values");

    const float basis_value[4] = {.5f, 0.f, 0.f, .25f};
    uniformMatrix2fv(basis, 1, GL_FALSE, basis_value);
    CHECK(getError() == GL_NO_ERROR, "matching matrix setter succeeds");
    const float fake_matrix[4] = {1.f, 1.f, 1.f, 1.f};
    uniform4fv(basis, 1, fake_matrix);
    CHECK(getError() == GL_INVALID_OPERATION,
          "vec4 setter is rejected for mat2 despite equal scalar count");
    float basis_back[4] = {0};
    getUniformfv(program, basis, basis_back);
    CHECK(four_equal(basis_back, .5f, 0.f, 0.f, .25f),
          "rejected matrix write preserves the matrix value");

    uniform1i(tex, 0);
    CHECK(getError() == GL_NO_ERROR, "Uniform1i accepts sampler assignment");
    uniform1f(tex, 1.f);
    CHECK(getError() == GL_INVALID_OPERATION,
          "floating-point setter is rejected for sampler");
    GLint sampler_unit = -1;
    getUniformiv(program, tex, &sampler_unit);
    CHECK(sampler_unit == 0, "rejected sampler write preserves texture unit");

    uniform1f(sample_weight, 0.f);
    CHECK(getError() == GL_NO_ERROR, "matching scalar float setter succeeds");

    useProgram(0);
    uniform4f(tint, 1.f, 1.f, 1.f, 1.f);
    CHECK(getError() == GL_INVALID_OPERATION,
          "uniform write without a current program is rejected");
    uniform4f(-1, 1.f, 1.f, 1.f, 1.f);
    CHECK(getError() == GL_NO_ERROR,
          "location -1 remains a silent no-op even without a current program");
    useProgram(program);
    getUniformfv(program, tint, tint_back);
    CHECK(four_equal(tint_back, .10f, .20f, .30f, 1.f),
          "no-program error does not mutate program uniform state");

    const unsigned char white[4] = {255, 255, 255, 255};
    GLuint texture = 0;
    genTextures(1, &texture);
    bindTexture(GL_TEXTURE_2D, texture);
    texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    texImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, white);
    CHECK(getError() == GL_NO_ERROR, "sampler backing texture setup succeeds");

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
    CHECK(pixel_near(pixel, 36, 68, 84),
          "accepted typed values execute through backend upload (%u,%u,%u,%u)",
          pixel[0], pixel[1], pixel[2], pixel[3]);
    CHECK(getError() == GL_NO_ERROR,
          "typed-uniform draw/readback completes without GL errors");

    printf("\nuniform_type_smoke: %s (%d failure%s)\n",
           failures ? "FAIL" : "PASS", failures,
           failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
