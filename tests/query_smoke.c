/* DirectMetal occlusion-query vertical smoke.
 *
 * Proves GL query lifetime/state reaches Metal visibility-result encoding,
 * query generations can span multiple command buffers without losing counts,
 * boolean queries observe depth rejection, and result retrieval is the only
 * mandatory CPU completion point.
 */

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_NO_ERROR 0
#define GL_INVALID_OPERATION 0x0502
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_FLOAT 0x1406
#define GL_TRIANGLES 0x0004
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_TEST 0x0B71
#define GL_NEVER 0x0200
#define GL_SAMPLES_PASSED 0x8914
#define GL_ANY_SAMPLES_PASSED 0x8C2F
#define GL_CURRENT_QUERY 0x8865
#define GL_QUERY_COUNTER_BITS 0x8864
#define GL_QUERY_RESULT 0x8866
#define GL_QUERY_RESULT_AVAILABLE 0x8867
#define GL_TIMESTAMP 0x8E28
#define GL_RENDERER 0x1F01

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef int GLsizei;
typedef intptr_t GLsizeiptr;
typedef unsigned char GLboolean;
typedef int64_t GLint64;
typedef uint64_t GLuint64;

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
typedef void (*fnVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean,
                                      GLsizei, const void*);
typedef void (*fnClearColor)(float, float, float, float);
typedef void (*fnClear)(GLbitfield);
typedef void (*fnDrawArrays)(GLenum, GLint, GLsizei);
typedef void (*fnFlush)(void);
typedef void (*fnEnable)(GLenum);
typedef void (*fnDisable)(GLenum);
typedef void (*fnDepthFunc)(GLenum);
typedef void (*fnGenQueries)(GLsizei, GLuint*);
typedef void (*fnDeleteQueries)(GLsizei, const GLuint*);
typedef GLboolean (*fnIsQuery)(GLuint);
typedef void (*fnBeginQuery)(GLenum, GLuint);
typedef void (*fnEndQuery)(GLenum);
typedef void (*fnGetQueryiv)(GLenum, GLenum, GLint*);
typedef void (*fnGetQueryObjectuiv)(GLuint, GLenum, GLuint*);
typedef void (*fnGetQueryObjectiv)(GLuint, GLenum, GLint*);
typedef void (*fnGetQueryObjectui64v)(GLuint, GLenum, GLuint64*);
typedef void (*fnGetQueryObjecti64v)(GLuint, GLenum, GLint64*);
typedef void (*fnQueryCounter)(GLuint, GLenum);

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
    "void main() { gl_Position = vec4(position, 0.0, 1.0); }\n";

static const char* fragment_source =
    "#version 330 core\n"
    "layout(location=0) out vec4 color;\n"
    "void main() { color = vec4(1.0); }\n";

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
    LOAD(fnBindBuffer, bindBuffer, "glBindBuffer");
    LOAD(fnBufferData, bufferData, "glBufferData");
    LOAD(fnEnableVertexAttribArray, enableVertexAttribArray,
         "glEnableVertexAttribArray");
    LOAD(fnVertexAttribPointer, vertexAttribPointer, "glVertexAttribPointer");
    LOAD(fnClearColor, clearColor, "glClearColor");
    LOAD(fnClear, clear, "glClear");
    LOAD(fnDrawArrays, drawArrays, "glDrawArrays");
    LOAD(fnFlush, flush, "glFlush");
    LOAD(fnEnable, enable, "glEnable");
    LOAD(fnDisable, disable, "glDisable");
    LOAD(fnDepthFunc, depthFunc, "glDepthFunc");
    LOAD(fnGenQueries, genQueries, "glGenQueries");
    LOAD(fnDeleteQueries, deleteQueries, "glDeleteQueries");
    LOAD(fnIsQuery, isQuery, "glIsQuery");
    LOAD(fnBeginQuery, beginQuery, "glBeginQuery");
    LOAD(fnEndQuery, endQuery, "glEndQuery");
    LOAD(fnGetQueryiv, getQueryiv, "glGetQueryiv");
    LOAD(fnGetQueryObjectuiv, getQueryObjectuiv, "glGetQueryObjectuiv");
    LOAD(fnGetQueryObjectiv, getQueryObjectiv, "glGetQueryObjectiv");
    LOAD(fnGetQueryObjectui64v, getQueryObjectui64v,
         "glGetQueryObjectui64v");
    LOAD(fnGetQueryObjecti64v, getQueryObjecti64v, "glGetQueryObjecti64v");
    LOAD(fnQueryCounter, queryCounter, "glQueryCounter");
    CHECK(getString && getError && createShader && shaderSource &&
              compileShader && getShaderiv && createProgram && attachShader &&
              linkProgram && getProgramiv && useProgram && genVertexArrays &&
              bindVertexArray && genBuffers && bindBuffer && bufferData &&
              enableVertexAttribArray && vertexAttribPointer && clearColor &&
              clear && drawArrays && flush && enable && disable && depthFunc &&
              genQueries && deleteQueries && isQuery && beginQuery && endQuery &&
              getQueryiv && getQueryObjectuiv && getQueryObjectiv &&
              getQueryObjectui64v && getQueryObjecti64v && queryCounter,
          "required GL occlusion-query symbols resolve");
    if (failures) return 1;

    const char* renderer = (const char*)getString(GL_RENDERER);
    CHECK(renderer && strstr(renderer, "DirectMetal"),
          "context is explicitly DirectMetal (%s)", renderer ? renderer : "null");

    GLuint vs = createShader(GL_VERTEX_SHADER);
    GLuint fs = createShader(GL_FRAGMENT_SHADER);
    shaderSource(vs, 1, &vertex_source, NULL);
    shaderSource(fs, 1, &fragment_source, NULL);
    compileShader(vs);
    compileShader(fs);
    GLint vs_ok = 0, fs_ok = 0;
    getShaderiv(vs, GL_COMPILE_STATUS, &vs_ok);
    getShaderiv(fs, GL_COMPILE_STATUS, &fs_ok);
    GLuint program = createProgram();
    attachShader(program, vs);
    attachShader(program, fs);
    linkProgram(program);
    GLint linked = 0;
    getProgramiv(program, GL_LINK_STATUS, &linked);
    useProgram(program);
    CHECK(vs_ok && fs_ok && linked, "query draw shaders compile and link");

    const float vertices[6] = {-0.8f, -0.8f, 0.8f, -0.8f, 0.f, 0.8f};
    GLuint vao = 0, vbo = 0;
    genVertexArrays(1, &vao);
    bindVertexArray(vao);
    genBuffers(1, &vbo);
    bindBuffer(GL_ARRAY_BUFFER, vbo);
    bufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    enableVertexAttribArray(0);
    vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), NULL);
    clearColor(0.f, 0.f, 0.f, 1.f);
    clear(GL_COLOR_BUFFER_BIT);

    GLuint queries[5] = {0, 0, 0, 0, 0};
    genQueries(5, queries);
    CHECK(!isQuery(queries[0]),
          "generated name becomes a query object only on first BeginQuery");

    GLint bits = 0, current = -1;
    getQueryiv(GL_SAMPLES_PASSED, GL_QUERY_COUNTER_BITS, &bits);
    beginQuery(GL_SAMPLES_PASSED, queries[0]);
    getQueryiv(GL_SAMPLES_PASSED, GL_CURRENT_QUERY, &current);
    drawArrays(GL_TRIANGLES, 0, 3);
    endQuery(GL_SAMPLES_PASSED);
    GLuint64 one_draw = 0;
    getQueryObjectui64v(queries[0], GL_QUERY_RESULT, &one_draw);
    CHECK(bits == 64 && current == (GLint)queries[0] &&
              isQuery(queries[0]) && one_draw > 0,
          "counting query exposes state and native sample count (%llu)",
          (unsigned long long)one_draw);

    beginQuery(GL_SAMPLES_PASSED, queries[1]);
    drawArrays(GL_TRIANGLES, 0, 3);
    flush();
    drawArrays(GL_TRIANGLES, 0, 3);
    endQuery(GL_SAMPLES_PASSED);
    GLuint available = 0;
    getQueryObjectuiv(queries[1], GL_QUERY_RESULT_AVAILABLE, &available);
    GLuint64 two_draws = 0;
    getQueryObjectui64v(queries[1], GL_QUERY_RESULT, &two_draws);
    getQueryObjectuiv(queries[1], GL_QUERY_RESULT_AVAILABLE, &available);
    CHECK(two_draws == one_draw * 2 && available == GL_TRUE,
          "one GL query aggregates two Metal command-buffer segments (%llu)",
          (unsigned long long)two_draws);

    beginQuery(GL_ANY_SAMPLES_PASSED, queries[2]);
    drawArrays(GL_TRIANGLES, 0, 3);
    endQuery(GL_ANY_SAMPLES_PASSED);
    enable(GL_DEPTH_TEST);
    depthFunc(GL_NEVER);
    beginQuery(GL_ANY_SAMPLES_PASSED, queries[3]);
    drawArrays(GL_TRIANGLES, 0, 3);
    endQuery(GL_ANY_SAMPLES_PASSED);
    disable(GL_DEPTH_TEST);
    GLint any_visible = 0;
    GLint any_occluded = -1;
    getQueryObjectiv(queries[2], GL_QUERY_RESULT, &any_visible);
    getQueryObjectiv(queries[3], GL_QUERY_RESULT, &any_occluded);
    CHECK(any_visible == GL_TRUE && any_occluded == GL_FALSE,
          "one Metal visibility buffer holds distinct boolean query offsets");

    enable(GL_DEPTH_TEST);
    depthFunc(GL_NEVER);
    beginQuery(GL_ANY_SAMPLES_PASSED, queries[2]);
    drawArrays(GL_TRIANGLES, 0, 3);
    endQuery(GL_ANY_SAMPLES_PASSED);
    GLint64 reused_occluded = -1;
    getQueryObjecti64v(queries[2], GL_QUERY_RESULT, &reused_occluded);
    disable(GL_DEPTH_TEST);
    CHECK(reused_occluded == GL_FALSE,
          "reused query generation resets and observes depth rejection");

    beginQuery(GL_ANY_SAMPLES_PASSED, queries[4]);
    drawArrays(GL_TRIANGLES, 0, 3);
    deleteQueries(1, &queries[4]);
    CHECK(!isQuery(queries[4]), "deleting an active query releases its GL name");
    endQuery(GL_ANY_SAMPLES_PASSED);
    flush();
    CHECK(getError() == GL_NO_ERROR,
          "deleted active query retains deferred native work through EndQuery");

    queryCounter(queries[0], GL_TIMESTAMP);
    CHECK(getError() == GL_INVALID_OPERATION,
          "timer query is explicitly unsupported instead of returning fake time");

    deleteQueries(4, queries);
    dlclose(library);
    printf("\nquery_smoke: %s (%d failure%s)\n",
           failures ? "FAIL" : "PASS", failures,
           failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
