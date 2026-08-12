/* OpenGL 3.3 integer uniform getter precision smoke.
 *
 * Integer default-block uniforms are 32-bit values. Verify that values beyond
 * float's exact 24-bit integer range round-trip through glGetUniformiv/uiv
 * without precision loss, including non-zero array-element locations.
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
#define GL_RENDERER 0x1F01
#define GL_NO_ERROR 0

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;

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
typedef void (*fnUniform4i)(GLint, GLint, GLint, GLint, GLint);
typedef void (*fnUniform1ui)(GLint, GLuint);
typedef void (*fnUniform4ui)(GLint, GLuint, GLuint, GLuint, GLuint);
typedef void (*fnGetUniformiv)(GLuint, GLint, GLint*);
typedef void (*fnGetUniformuiv)(GLuint, GLint, GLuint*);

static int failures;

#define CHECK(condition, ...) do {                                          \
    if (condition) printf("ok  : " __VA_ARGS__);                           \
    else { printf("FAIL: " __VA_ARGS__); ++failures; }                     \
    printf("\n");                                                          \
} while (0)

#define LOAD(type, variable, symbol) type variable = (type)dlsym(library, symbol)

static const char* vertex_source =
    "#version 150\n"
    "uniform ivec4 signedValue;\n"
    "uniform int signedArray[2];\n"
    "void main() {\n"
    "  int keep = signedValue.x ^ signedArray[1];\n"
    "  gl_Position = vec4(float(keep & 1) * 0.001, 0.0, 0.0, 1.0);\n"
    "}\n";

static const char* fragment_source =
    "#version 150\n"
    "uniform uvec4 unsignedValue;\n"
    "uniform uint unsignedArray[2];\n"
    "layout(location=0) out vec4 color;\n"
    "void main() {\n"
    "  uint keep = unsignedValue.x ^ unsignedArray[1];\n"
    "  color = vec4(float(keep & 1u) * 0.001, 0.0, 0.0, 1.0);\n"
    "}\n";

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
    LOAD(fnUniform1i, uniform1i, "glUniform1i");
    LOAD(fnUniform4i, uniform4i, "glUniform4i");
    LOAD(fnUniform1ui, uniform1ui, "glUniform1ui");
    LOAD(fnUniform4ui, uniform4ui, "glUniform4ui");
    LOAD(fnGetUniformiv, getUniformiv, "glGetUniformiv");
    LOAD(fnGetUniformuiv, getUniformuiv, "glGetUniformuiv");

    CHECK(getString && getError && createShader && shaderSource &&
              compileShader && getShaderiv && createProgram && attachShader &&
              linkProgram && getProgramiv && useProgram && getUniformLocation &&
              uniform1i && uniform4i && uniform1ui && uniform4ui &&
              getUniformiv && getUniformuiv,
          "required integer-uniform symbols resolve");
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
    CHECK(vertex_ok && fragment_ok, "integer getter shaders compile");

    GLuint program = createProgram();
    attachShader(program, vertex);
    attachShader(program, fragment);
    linkProgram(program);
    GLint link_ok = 0;
    getProgramiv(program, GL_LINK_STATUS, &link_ok);
    CHECK(link_ok, "integer getter program links");
    useProgram(program);

    GLint signed_value = getUniformLocation(program, "signedValue");
    GLint unsigned_value = getUniformLocation(program, "unsignedValue");
    GLint signed_array0 = getUniformLocation(program, "signedArray[0]");
    GLint signed_array1 = getUniformLocation(program, "signedArray[1]");
    GLint unsigned_array0 = getUniformLocation(program, "unsignedArray[0]");
    GLint unsigned_array1 = getUniformLocation(program, "unsignedArray[1]");
    CHECK(signed_value >= 0 && unsigned_value >= 0 && signed_array0 >= 0 &&
              signed_array1 >= 0 && unsigned_array0 >= 0 &&
              unsigned_array1 >= 0,
          "integer scalar/vector/array locations resolve");

    const GLint signed_expected[4] = {
        INT32_C(2147483523), -INT32_C(2147483519),
        INT32_C(16777217), -INT32_C(16777219)};
    const GLuint unsigned_expected[4] = {
        UINT32_C(4262601815), UINT32_C(4000000001),
        UINT32_C(16777217), UINT32_C(2147483649)};
    uniform4i(signed_value, signed_expected[0], signed_expected[1],
              signed_expected[2], signed_expected[3]);
    uniform4ui(unsigned_value, unsigned_expected[0], unsigned_expected[1],
               unsigned_expected[2], unsigned_expected[3]);

    GLint signed_back[4] = {0};
    GLuint unsigned_back[4] = {0};
    getUniformiv(program, signed_value, signed_back);
    getUniformuiv(program, unsigned_value, unsigned_back);
    CHECK(memcmp(signed_back, signed_expected, sizeof(signed_back)) == 0,
          "ivec4 round-trips all 32 integer bits");
    CHECK(memcmp(unsigned_back, unsigned_expected, sizeof(unsigned_back)) == 0,
          "uvec4 round-trips all 32 integer bits");

    const GLint signed_element = -INT32_C(2000000003);
    const GLuint unsigned_element = UINT32_C(4200000001);
    uniform1i(signed_array1, signed_element);
    uniform1ui(unsigned_array1, unsigned_element);
    GLint signed_element_back = 0;
    GLuint unsigned_element_back = 0;
    getUniformiv(program, signed_array1, &signed_element_back);
    getUniformuiv(program, unsigned_array1, &unsigned_element_back);
    CHECK(signed_element_back == signed_element,
          "non-zero signed array location preserves exact 32-bit value");
    CHECK(unsigned_element_back == unsigned_element,
          "non-zero unsigned array location preserves exact 32-bit value");

    GLint signed_zero = 123;
    GLuint unsigned_zero = 123u;
    getUniformiv(program, signed_array0, &signed_zero);
    getUniformuiv(program, unsigned_array0, &unsigned_zero);
    CHECK(signed_zero == 0 && unsigned_zero == 0u,
          "untouched integer array elements retain the initial zero value");
    CHECK(getError() == GL_NO_ERROR,
          "integer getter precision checks finish without GL errors");

    printf("\nuniform_integer_getter_smoke: %s (%d failure%s)\n",
           failures ? "FAIL" : "PASS", failures,
           failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
