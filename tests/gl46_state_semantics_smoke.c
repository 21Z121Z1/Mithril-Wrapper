/* Focused behavioral oracle for GL 4.1/4.4/4.5 state semantics used by
 * modern Minecraft renderers.  This deliberately checks externally observable
 * state, not implementation details. */
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <GL/glcorearb.h>

#ifndef GL_TEXTURE_BINDING_2D_ARRAY
#define GL_TEXTURE_BINDING_2D_ARRAY 0x8C1D
#endif
#ifndef GL_UNIFORM_BUFFER_BINDING
#define GL_UNIFORM_BUFFER_BINDING 0x8A28
#endif

static int checks = 0;
static int failures = 0;
#define CHECK(c, fmt, ...) do { \
    ++checks; \
    if (c) printf("ok  : " fmt "\n", ##__VA_ARGS__); \
    else { printf("FAIL: " fmt "\n", ##__VA_ARGS__); ++failures; } \
} while (0)

#define LOAD(type, var, name) \
    type var = (type)dlsym(h, name); \
    if (!(var)) { fprintf(stderr, "missing %s\n", name); return 3; }

typedef const GLubyte* (*getString_t)(GLenum);
typedef void (*getIntegerv_t)(GLenum, GLint*);
typedef void (*getIntegeri_t)(GLenum, GLuint, GLint*);
typedef void (*activeTexture_t)(GLenum);
typedef void (*genTextures_t)(GLsizei, GLuint*);
typedef void (*bindTexture_t)(GLenum, GLuint);
typedef void (*bindTextureUnit_t)(GLuint, GLuint);
typedef void (*bindTextures_t)(GLuint, GLsizei, const GLuint*);
typedef void (*genBuffers_t)(GLsizei, GLuint*);
typedef void (*bindBufferBase_t)(GLenum, GLuint, GLuint);
typedef void (*bindBuffersRange_t)(GLenum, GLuint, GLsizei, const GLuint*, const GLintptr*, const GLsizeiptr*);
typedef GLuint (*createShader_t)(GLenum);
typedef void (*shaderSource_t)(GLuint, GLsizei, const GLchar* const*, const GLint*);
typedef void (*compileShader_t)(GLuint);
typedef void (*getShaderiv_t)(GLuint, GLenum, GLint*);
typedef GLuint (*createProgram_t)(void);
typedef void (*attachShader_t)(GLuint, GLuint);
typedef void (*linkProgram_t)(GLuint);
typedef void (*getProgramiv_t)(GLuint, GLenum, GLint*);
typedef void (*useProgram_t)(GLuint);
typedef GLint (*getUniformLocation_t)(GLuint, const GLchar*);
typedef void (*programUniform1f_t)(GLuint, GLint, GLfloat);
typedef void (*getUniformfv_t)(GLuint, GLint, GLfloat*);
typedef void (*deleteShader_t)(GLuint);
typedef void (*deleteProgram_t)(GLuint);

static GLuint make_program(createShader_t createShader,
                           shaderSource_t shaderSource,
                           compileShader_t compileShader,
                           getShaderiv_t getShaderiv,
                           createProgram_t createProgram,
                           attachShader_t attachShader,
                           linkProgram_t linkProgram,
                           getProgramiv_t getProgramiv,
                           deleteShader_t deleteShader) {
    static const char* vs =
        "#version 330 core\n"
        "void main(){ gl_Position=vec4(0.0,0.0,0.0,1.0); }\n";
    static const char* fs =
        "#version 330 core\n"
        "uniform float uValue;\n"
        "out vec4 outColor;\n"
        "void main(){ outColor=vec4(uValue,0.0,0.0,1.0); }\n";
    GLuint v = createShader(GL_VERTEX_SHADER);
    GLuint f = createShader(GL_FRAGMENT_SHADER);
    shaderSource(v, 1, &vs, NULL);
    shaderSource(f, 1, &fs, NULL);
    compileShader(v);
    compileShader(f);
    GLint okv = 0, okf = 0;
    getShaderiv(v, GL_COMPILE_STATUS, &okv);
    getShaderiv(f, GL_COMPILE_STATUS, &okf);
    CHECK(okv == GL_TRUE && okf == GL_TRUE, "program-uniform shaders compile");
    GLuint p = createProgram();
    attachShader(p, v);
    attachShader(p, f);
    linkProgram(p);
    GLint linked = 0;
    getProgramiv(p, GL_LINK_STATUS, &linked);
    CHECK(linked == GL_TRUE, "program-uniform program links");
    deleteShader(v);
    deleteShader(f);
    return p;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s /path/to/libmithril.dylib\n", argv[0]);
        return 2;
    }
    void* h = dlopen(argv[1], RTLD_NOW | RTLD_GLOBAL);
    if (!h) {
        fprintf(stderr, "dlopen: %s\n", dlerror());
        return 2;
    }

    LOAD(getString_t, getString, "glGetString");
    LOAD(getIntegerv_t, getIntegerv, "glGetIntegerv");
    LOAD(getIntegeri_t, getIntegeri, "glGetIntegeri_v");
    LOAD(activeTexture_t, activeTexture, "glActiveTexture");
    LOAD(genTextures_t, genTextures, "glGenTextures");
    LOAD(bindTexture_t, bindTexture, "glBindTexture");
    LOAD(bindTextureUnit_t, bindTextureUnit, "glBindTextureUnit");
    LOAD(bindTextures_t, bindTextures, "glBindTextures");
    LOAD(genBuffers_t, genBuffers, "glGenBuffers");
    LOAD(bindBufferBase_t, bindBufferBase, "glBindBufferBase");
    LOAD(bindBuffersRange_t, bindBuffersRange, "glBindBuffersRange");
    LOAD(createShader_t, createShader, "glCreateShader");
    LOAD(shaderSource_t, shaderSource, "glShaderSource");
    LOAD(compileShader_t, compileShader, "glCompileShader");
    LOAD(getShaderiv_t, getShaderiv, "glGetShaderiv");
    LOAD(createProgram_t, createProgram, "glCreateProgram");
    LOAD(attachShader_t, attachShader, "glAttachShader");
    LOAD(linkProgram_t, linkProgram, "glLinkProgram");
    LOAD(getProgramiv_t, getProgramiv, "glGetProgramiv");
    LOAD(useProgram_t, useProgram, "glUseProgram");
    LOAD(getUniformLocation_t, getUniformLocation, "glGetUniformLocation");
    LOAD(programUniform1f_t, programUniform1f, "glProgramUniform1f");
    LOAD(getUniformfv_t, getUniformfv, "glGetUniformfv");
    LOAD(deleteShader_t, deleteShader, "glDeleteShader");
    LOAD(deleteProgram_t, deleteProgram, "glDeleteProgram");

    const char* version = (const char*)getString(GL_VERSION);
    CHECK(version && version[0], "Mithril initializes (%s)", version ? version : "null");

    /* GL 4.1 separate-program uniform semantics: glProgramUniform* may modify
     * another program, but GL_CURRENT_PROGRAM is invariant. */
    GLuint programA = make_program(createShader, shaderSource, compileShader,
                                   getShaderiv, createProgram, attachShader,
                                   linkProgram, getProgramiv, deleteShader);
    GLuint programB = make_program(createShader, shaderSource, compileShader,
                                   getShaderiv, createProgram, attachShader,
                                   linkProgram, getProgramiv, deleteShader);
    GLint locB = getUniformLocation(programB, "uValue");
    CHECK(locB >= 0, "uniform location in target program exists (%d)", locB);
    useProgram(programA);
    GLint current = 0;
    getIntegerv(GL_CURRENT_PROGRAM, &current);
    CHECK((GLuint)current == programA, "program A is current before DSA uniform update");
    programUniform1f(programB, locB, 0.625f);
    current = 0;
    getIntegerv(GL_CURRENT_PROGRAM, &current);
    CHECK((GLuint)current == programA,
          "glProgramUniform1f preserves GL_CURRENT_PROGRAM (got %d, want %u)",
          current, programA);
    GLfloat uniformValue = 0.0f;
    getUniformfv(programB, locB, &uniformValue);
    CHECK(fabsf(uniformValue - 0.625f) < 0.0001f,
          "glProgramUniform1f updates the addressed program (%.4f)", uniformValue);

    /* GL 4.5 DSA texture binding: object's established target is used and the
     * active texture selector is untouched. */
    GLuint tex = 0;
    genTextures(1, &tex);
    activeTexture(GL_TEXTURE0 + 1);
    bindTexture(GL_TEXTURE_2D_ARRAY, tex); /* establishes immutable target */
    activeTexture(GL_TEXTURE0 + 3);
    GLint beforeActive = 0;
    getIntegerv(GL_ACTIVE_TEXTURE, &beforeActive);
    bindTextureUnit(5, tex);
    GLint afterActive = 0;
    getIntegerv(GL_ACTIVE_TEXTURE, &afterActive);
    CHECK(afterActive == beforeActive,
          "glBindTextureUnit preserves GL_ACTIVE_TEXTURE (0x%x)", afterActive);
    activeTexture(GL_TEXTURE0 + 5);
    GLint arrayBinding = 0;
    getIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &arrayBinding);
    CHECK((GLuint)arrayBinding == tex,
          "glBindTextureUnit uses texture object's 2D-array target (%d)", arrayBinding);

    activeTexture(GL_TEXTURE0 + 3);
    GLuint one = tex;
    bindTextures(7, 1, &one);
    getIntegerv(GL_ACTIVE_TEXTURE, &afterActive);
    CHECK(afterActive == GL_TEXTURE0 + 3,
          "glBindTextures preserves GL_ACTIVE_TEXTURE (0x%x)", afterActive);
    activeTexture(GL_TEXTURE0 + 7);
    arrayBinding = 0;
    getIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &arrayBinding);
    CHECK((GLuint)arrayBinding == tex,
          "glBindTextures derives the target for each texture (%d)", arrayBinding);
    bindTextures(7, 1, NULL);
    arrayBinding = -1;
    getIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &arrayBinding);
    CHECK(arrayBinding == 0,
          "glBindTextures(NULL) clears target bindings (%d)", arrayBinding);

    /* GL 4.4 multi-bind null-array rule. */
    GLuint buffer = 0;
    genBuffers(1, &buffer);
    bindBufferBase(GL_UNIFORM_BUFFER, 7, buffer);
    GLint indexed = 0;
    getIntegeri(GL_UNIFORM_BUFFER_BINDING, 7, &indexed);
    CHECK((GLuint)indexed == buffer, "indexed UBO binding established (%d)", indexed);
    bindBuffersRange(GL_UNIFORM_BUFFER, 7, 1, NULL, NULL, NULL);
    indexed = -1;
    getIntegeri(GL_UNIFORM_BUFFER_BINDING, 7, &indexed);
    CHECK(indexed == 0, "glBindBuffersRange(NULL) resets indexed binding (%d)", indexed);

    useProgram(0);
    deleteProgram(programA);
    deleteProgram(programB);
    dlclose(h);

    printf("GL46 STATE SEMANTICS: %d checks, %d failure(s)\n", checks, failures);
    if (failures) return 1;
    puts("GL46 STATE SEMANTICS ALL PASSED");
    return 0;
}
