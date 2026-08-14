/*
 * mc_gui_smoke.c - synthetic Minecraft GUI render-contract regression test.
 *
 * This is intentionally independent of Minecraft.  It drives the same GL
 * semantics that the 26.2 menu path uses: a user FBO, an indexed position/UV
 * quad, named cross-stage UBOs, a sampled RGBA atlas, alpha blending, scissor,
 * culling, and a real GPU readback.  The test also keeps the smaller semantic
 * regressions close to the synthetic path:
 *
 *   - user-FBO front-face/culling observable behaviour;
 *   - GL_PIXEL_UNPACK_BUFFER byte-offset uploads, including an OOB reject;
 *   - float/int anisotropy capability queries;
 *   - repeated UBO/texture/descriptor use with fences across transient frames.
 *
 * A passing test proves the compatibility contract, not Minecraft device
 * acceptance.  The real iPad GUI checkpoint remains a separate gate.
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <EGL/egl.h>
#include <GL/glcorearb.h>

#ifndef GL_INVALID_INDEX
#define GL_INVALID_INDEX 0xFFFFFFFFu
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif
#ifndef GL_ALREADY_SIGNALED
#define GL_ALREADY_SIGNALED 0x911A
#endif
#ifndef GL_TIMEOUT_EXPIRED
#define GL_TIMEOUT_EXPIRED 0x911B
#endif
#ifndef GL_CONDITION_SATISFIED
#define GL_CONDITION_SATISFIED 0x911C
#endif
#ifndef GL_SYNC_FLUSH_COMMANDS_BIT
#define GL_SYNC_FLUSH_COMMANDS_BIT 0x00000001
#endif
#ifndef GL_SYNC_GPU_COMMANDS_COMPLETE
#define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#endif

typedef void (*genTextures_fn)(GLsizei, GLuint*);
typedef void (*bindTexture_fn)(GLenum, GLuint);
typedef void (*texImage2D_fn)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint,
                              GLenum, GLenum, const void*);
typedef void (*texSubImage2D_fn)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei,
                                 GLenum, GLenum, const void*);
typedef void (*texParameteri_fn)(GLenum, GLenum, GLint);
typedef void (*activeTexture_fn)(GLenum);
typedef void (*pixelStorei_fn)(GLenum, GLint);
typedef void (*genFramebuffers_fn)(GLsizei, GLuint*);
typedef void (*bindFramebuffer_fn)(GLenum, GLuint);
typedef void (*framebufferTex2D_fn)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum (*checkFramebufferStatus_fn)(GLenum);
typedef void (*genVertexArrays_fn)(GLsizei, GLuint*);
typedef void (*bindVertexArray_fn)(GLuint);
typedef void (*genBuffers_fn)(GLsizei, GLuint*);
typedef void (*bindBuffer_fn)(GLenum, GLuint);
typedef void (*bufferData_fn)(GLenum, GLsizeiptr, const void*, GLenum);
typedef void (*bufferSubData_fn)(GLenum, GLintptr, GLsizeiptr, const void*);
typedef void (*bindBufferBase_fn)(GLenum, GLuint, GLuint);
typedef void (*vertexAttribPointer_fn)(GLuint, GLint, GLenum, GLboolean,
                                       GLsizei, const void*);
typedef void (*enableVertexAttribArray_fn)(GLuint);
typedef GLuint (*createShader_fn)(GLenum);
typedef void (*shaderSource_fn)(GLuint, GLsizei, const GLchar* const*,
                               const GLint*);
typedef void (*compileShader_fn)(GLuint);
typedef void (*getShaderiv_fn)(GLuint, GLenum, GLint*);
typedef GLuint (*createProgram_fn)(void);
typedef void (*attachShader_fn)(GLuint, GLuint);
typedef void (*linkProgram_fn)(GLuint);
typedef void (*getProgramiv_fn)(GLuint, GLenum, GLint*);
typedef void (*deleteShader_fn)(GLuint);
typedef void (*useProgram_fn)(GLuint);
typedef GLuint (*getUniformBlockIndex_real_fn)(GLuint, const GLchar*);
typedef void (*uniformBlockBinding_fn)(GLuint, GLuint, GLuint);
typedef GLint (*getUniformLocation_fn)(GLuint, const GLchar*);
typedef void (*uniform1i_fn)(GLint, GLint);
typedef void (*viewport_fn)(GLint, GLint, GLsizei, GLsizei);
typedef void (*scissor_fn)(GLint, GLint, GLsizei, GLsizei);
typedef void (*clearColor_fn)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (*clear_fn)(GLbitfield);
typedef void (*enable_fn)(GLenum);
typedef void (*disable_fn)(GLenum);
typedef void (*cullFace_fn)(GLenum);
typedef void (*frontFace_fn)(GLenum);
typedef void (*blendFunc_fn)(GLenum, GLenum);
typedef void (*drawElements_fn)(GLenum, GLsizei, GLenum, const void*);
typedef void (*finish_fn)(void);
typedef void (*readPixels_fn)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum,
                              void*);
typedef void (*getIntegerv_fn)(GLenum, GLint*);
typedef void (*getFloatv_fn)(GLenum, GLfloat*);
typedef const GLubyte* (*getString_fn)(GLenum);
typedef GLsync (*fenceSync_fn)(GLenum, GLbitfield);
typedef GLenum (*clientWaitSync_fn)(GLsync, GLbitfield, GLuint64);
typedef void (*deleteSync_fn)(GLsync);

typedef struct Api {
    genTextures_fn genTextures;
    bindTexture_fn bindTexture;
    texImage2D_fn texImage2D;
    texSubImage2D_fn texSubImage2D;
    texParameteri_fn texParameteri;
    activeTexture_fn activeTexture;
    pixelStorei_fn pixelStorei;
    genFramebuffers_fn genFramebuffers;
    bindFramebuffer_fn bindFramebuffer;
    framebufferTex2D_fn framebufferTex2D;
    checkFramebufferStatus_fn checkFramebufferStatus;
    genVertexArrays_fn genVertexArrays;
    bindVertexArray_fn bindVertexArray;
    genBuffers_fn genBuffers;
    bindBuffer_fn bindBuffer;
    bufferData_fn bufferData;
    bufferSubData_fn bufferSubData;
    bindBufferBase_fn bindBufferBase;
    vertexAttribPointer_fn vertexAttribPointer;
    enableVertexAttribArray_fn enableVertexAttribArray;
    createShader_fn createShader;
    shaderSource_fn shaderSource;
    compileShader_fn compileShader;
    getShaderiv_fn getShaderiv;
    createProgram_fn createProgram;
    attachShader_fn attachShader;
    linkProgram_fn linkProgram;
    getProgramiv_fn getProgramiv;
    deleteShader_fn deleteShader;
    useProgram_fn useProgram;
    getUniformBlockIndex_real_fn getUniformBlockIndex;
    uniformBlockBinding_fn uniformBlockBinding;
    getUniformLocation_fn getUniformLocation;
    uniform1i_fn uniform1i;
    viewport_fn viewport;
    scissor_fn scissor;
    clearColor_fn clearColor;
    clear_fn clear;
    enable_fn enable;
    disable_fn disable;
    cullFace_fn cullFace;
    frontFace_fn frontFace;
    blendFunc_fn blendFunc;
    drawElements_fn drawElements;
    finish_fn finish;
    readPixels_fn readPixels;
    getIntegerv_fn getIntegerv;
    getFloatv_fn getFloatv;
    getString_fn getString;
    fenceSync_fn fenceSync;
    clientWaitSync_fn clientWaitSync;
    deleteSync_fn deleteSync;
} Api;

static int failures = 0;
static int checks = 0;

#define CHECK(condition, format, ...) do { \
    ++checks; \
    if (condition) printf("ok : " format "\n", ##__VA_ARGS__); \
    else { printf("FAIL: " format "\n", ##__VA_ARGS__); ++failures; } \
} while (0)

static void* open_mithril(int argc, char** argv) {
    const char* candidates[4];
    int count = 0;
    if (argc > 1) candidates[count++] = argv[1];
    candidates[count++] = "./output/libmithril.dylib";
    candidates[count++] = "./build/libmithril.dylib";
    candidates[count++] = "./build/output/libmithril.dylib";
    for (int i = 0; i < count; ++i) {
        void* handle = dlopen(candidates[i], RTLD_NOW | RTLD_GLOBAL);
        if (handle) {
            printf("loaded: %s\n", candidates[i]);
            return handle;
        }
    }
    fprintf(stderr, "dlopen failed: %s\n", dlerror());
    return NULL;
}

static int setup_egl_context(void* handle) {
    EGLDisplay (*getDisplay)(EGLNativeDisplayType) = NULL;
    EGLBoolean (*initialize)(EGLDisplay, EGLint*, EGLint*) = NULL;
    EGLBoolean (*bindAPI)(EGLenum) = NULL;
    EGLBoolean (*getConfigs)(EGLDisplay, EGLConfig*, EGLint, EGLint*) = NULL;
    EGLContext (*createContext)(EGLDisplay, EGLConfig, EGLContext,
                                const EGLint*) = NULL;
    EGLSurface (*createPbuffer)(EGLDisplay, EGLConfig, const EGLint*) = NULL;
    EGLBoolean (*makeCurrent)(EGLDisplay, EGLSurface, EGLSurface,
                              EGLContext) = NULL;

#define LOAD_EGL(field, type, symbol) do { \
    field = (type)dlsym(handle, symbol); \
    if (!field) { \
        fprintf(stderr, "missing EGL symbol %s\n", symbol); \
        return 0; \
    } \
} while (0)
    LOAD_EGL(getDisplay, EGLDisplay (*)(EGLNativeDisplayType), "eglGetDisplay");
    LOAD_EGL(initialize, EGLBoolean (*)(EGLDisplay, EGLint*, EGLint*),
             "eglInitialize");
    LOAD_EGL(bindAPI, EGLBoolean (*)(EGLenum), "eglBindAPI");
    LOAD_EGL(getConfigs, EGLBoolean (*)(EGLDisplay, EGLConfig*, EGLint, EGLint*),
             "eglGetConfigs");
    LOAD_EGL(createContext,
             EGLContext (*)(EGLDisplay, EGLConfig, EGLContext, const EGLint*),
             "eglCreateContext");
    LOAD_EGL(createPbuffer,
             EGLSurface (*)(EGLDisplay, EGLConfig, const EGLint*),
             "eglCreatePbufferSurface");
    LOAD_EGL(makeCurrent,
             EGLBoolean (*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext),
             "eglMakeCurrent");
#undef LOAD_EGL

    EGLDisplay display = getDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0, minor = 0;
    if (display == EGL_NO_DISPLAY || !initialize(display, &major, &minor) ||
        !bindAPI(EGL_OPENGL_API)) {
        fprintf(stderr, "EGL initialization failed\n");
        return 0;
    }
    EGLConfig config = NULL;
    EGLint config_count = 0;
    if (!getConfigs(display, &config, 1, &config_count) || config_count <= 0 ||
        !config) {
        fprintf(stderr, "EGL config query failed\n");
        return 0;
    }
    const EGLint context_attrs[] = {
        EGL_CONTEXT_MAJOR_VERSION, 3,
        EGL_CONTEXT_MINOR_VERSION, 3,
        EGL_NONE,
    };
    EGLContext context = createContext(display, config, EGL_NO_CONTEXT,
                                       context_attrs);
    const EGLint pbuffer_attrs[] = {
        EGL_WIDTH, 378,
        EGL_HEIGHT, 248,
        EGL_NONE,
    };
    EGLSurface surface = createPbuffer(display, config, pbuffer_attrs);
    if (context == EGL_NO_CONTEXT || surface == EGL_NO_SURFACE ||
        !makeCurrent(display, surface, surface, context)) {
        fprintf(stderr, "EGL context/surface setup failed\n");
        return 0;
    }
    printf("EGL context active: %d.%d (pbuffer path)\n", major, minor);
    return 1;
}

#define LOAD(field, type, symbol) do { \
    api->field = (type)dlsym(handle, symbol); \
    if (!api->field) { printf("FAIL: missing symbol %s\n", symbol); ++failures; } \
} while (0)

static void load_api(void* handle, Api* api) {
    memset(api, 0, sizeof(*api));
    LOAD(genTextures, genTextures_fn, "glGenTextures");
    LOAD(bindTexture, bindTexture_fn, "glBindTexture");
    LOAD(texImage2D, texImage2D_fn, "glTexImage2D");
    LOAD(texSubImage2D, texSubImage2D_fn, "glTexSubImage2D");
    LOAD(texParameteri, texParameteri_fn, "glTexParameteri");
    LOAD(activeTexture, activeTexture_fn, "glActiveTexture");
    LOAD(pixelStorei, pixelStorei_fn, "glPixelStorei");
    LOAD(genFramebuffers, genFramebuffers_fn, "glGenFramebuffers");
    LOAD(bindFramebuffer, bindFramebuffer_fn, "glBindFramebuffer");
    LOAD(framebufferTex2D, framebufferTex2D_fn, "glFramebufferTexture2D");
    LOAD(checkFramebufferStatus, checkFramebufferStatus_fn,
         "glCheckFramebufferStatus");
    LOAD(genVertexArrays, genVertexArrays_fn, "glGenVertexArrays");
    LOAD(bindVertexArray, bindVertexArray_fn, "glBindVertexArray");
    LOAD(genBuffers, genBuffers_fn, "glGenBuffers");
    LOAD(bindBuffer, bindBuffer_fn, "glBindBuffer");
    LOAD(bufferData, bufferData_fn, "glBufferData");
    LOAD(bufferSubData, bufferSubData_fn, "glBufferSubData");
    LOAD(bindBufferBase, bindBufferBase_fn, "glBindBufferBase");
    LOAD(vertexAttribPointer, vertexAttribPointer_fn, "glVertexAttribPointer");
    LOAD(enableVertexAttribArray, enableVertexAttribArray_fn,
         "glEnableVertexAttribArray");
    LOAD(createShader, createShader_fn, "glCreateShader");
    LOAD(shaderSource, shaderSource_fn, "glShaderSource");
    LOAD(compileShader, compileShader_fn, "glCompileShader");
    LOAD(getShaderiv, getShaderiv_fn, "glGetShaderiv");
    LOAD(createProgram, createProgram_fn, "glCreateProgram");
    LOAD(attachShader, attachShader_fn, "glAttachShader");
    LOAD(linkProgram, linkProgram_fn, "glLinkProgram");
    LOAD(getProgramiv, getProgramiv_fn, "glGetProgramiv");
    LOAD(deleteShader, deleteShader_fn, "glDeleteShader");
    LOAD(useProgram, useProgram_fn, "glUseProgram");
    LOAD(getUniformBlockIndex, getUniformBlockIndex_real_fn,
         "glGetUniformBlockIndex");
    LOAD(uniformBlockBinding, uniformBlockBinding_fn, "glUniformBlockBinding");
    LOAD(getUniformLocation, getUniformLocation_fn, "glGetUniformLocation");
    LOAD(uniform1i, uniform1i_fn, "glUniform1i");
    LOAD(viewport, viewport_fn, "glViewport");
    LOAD(scissor, scissor_fn, "glScissor");
    LOAD(clearColor, clearColor_fn, "glClearColor");
    LOAD(clear, clear_fn, "glClear");
    LOAD(enable, enable_fn, "glEnable");
    LOAD(disable, disable_fn, "glDisable");
    LOAD(cullFace, cullFace_fn, "glCullFace");
    LOAD(frontFace, frontFace_fn, "glFrontFace");
    LOAD(blendFunc, blendFunc_fn, "glBlendFunc");
    LOAD(drawElements, drawElements_fn, "glDrawElements");
    LOAD(finish, finish_fn, "glFinish");
    LOAD(readPixels, readPixels_fn, "glReadPixels");
    LOAD(getIntegerv, getIntegerv_fn, "glGetIntegerv");
    LOAD(getFloatv, getFloatv_fn, "glGetFloatv");
    LOAD(getString, getString_fn, "glGetString");
    LOAD(fenceSync, fenceSync_fn, "glFenceSync");
    LOAD(clientWaitSync, clientWaitSync_fn, "glClientWaitSync");
    LOAD(deleteSync, deleteSync_fn, "glDeleteSync");
}

static GLuint make_program(Api* api, const char* vs_source, const char* fs_source,
                           const char* label) {
    GLuint vs = api->createShader(GL_VERTEX_SHADER);
    GLuint fs = api->createShader(GL_FRAGMENT_SHADER);
    if (!vs || !fs) return 0;
    api->shaderSource(vs, 1, &vs_source, NULL);
    api->shaderSource(fs, 1, &fs_source, NULL);
    api->compileShader(vs);
    api->compileShader(fs);
    GLint vs_ok = GL_FALSE, fs_ok = GL_FALSE;
    api->getShaderiv(vs, GL_COMPILE_STATUS, &vs_ok);
    api->getShaderiv(fs, GL_COMPILE_STATUS, &fs_ok);
    CHECK(vs_ok == GL_TRUE, "%s vertex shader compiled", label);
    CHECK(fs_ok == GL_TRUE, "%s fragment shader compiled", label);
    if (vs_ok != GL_TRUE || fs_ok != GL_TRUE) return 0;

    GLuint program = api->createProgram();
    api->attachShader(program, vs);
    api->attachShader(program, fs);
    api->linkProgram(program);
    GLint linked = GL_FALSE;
    api->getProgramiv(program, GL_LINK_STATUS, &linked);
    CHECK(linked == GL_TRUE, "%s program linked", label);
    api->deleteShader(vs);
    api->deleteShader(fs);
    return linked == GL_TRUE ? program : 0;
}

static int make_target(Api* api, GLuint* fbo, GLuint* color) {
    api->genTextures(1, color);
    api->bindTexture(GL_TEXTURE_2D, *color);
    api->texImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 378, 248, 0,
                    GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    api->texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    api->texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    api->genFramebuffers(1, fbo);
    api->bindFramebuffer(GL_FRAMEBUFFER, *fbo);
    api->framebufferTex2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                          GL_TEXTURE_2D, *color, 0);
    GLenum status = api->checkFramebufferStatus(GL_FRAMEBUFFER);
    CHECK(status == GL_FRAMEBUFFER_COMPLETE,
          "378x248 user FBO is complete (status=0x%x)", status);
    return status == GL_FRAMEBUFFER_COMPLETE;
}

typedef struct Vertex {
    GLfloat x, y, u, v;
} Vertex;

static void make_quad_from_vertices(Api* api, GLuint* vao, GLuint* vbo,
                                    GLuint* ebo, const Vertex vertices[4]) {
    static const GLushort indices[6] = {0, 1, 2, 0, 2, 3};
    api->genVertexArrays(1, vao);
    api->bindVertexArray(*vao);
    api->genBuffers(1, vbo);
    api->bindBuffer(GL_ARRAY_BUFFER, *vbo);
    api->bufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * 4, vertices, GL_STATIC_DRAW);
    api->vertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                             (const void*)0);
    api->vertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                             (const void*)(2 * sizeof(GLfloat)));
    api->enableVertexAttribArray(0);
    api->enableVertexAttribArray(1);
    api->genBuffers(1, ebo);
    api->bindBuffer(GL_ELEMENT_ARRAY_BUFFER, *ebo);
    api->bufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices,
                    GL_STATIC_DRAW);
}

static void make_quad(Api* api, GLuint* vao, GLuint* vbo, GLuint* ebo) {
    static const Vertex vertices[4] = {
        {-0.72f, -0.65f, 0.0f, 0.0f},
        { 0.72f, -0.65f, 1.0f, 0.0f},
        { 0.72f,  0.65f, 1.0f, 1.0f},
        {-0.72f,  0.65f, 0.0f, 1.0f},
    };
    make_quad_from_vertices(api, vao, vbo, ebo, vertices);
}

static void make_logical_quad(Api* api, GLuint* vao, GLuint* vbo, GLuint* ebo) {
    /* Minecraft GUI vertices use a top-left logical origin.  The projection
     * below maps this rectangle into GL clip space while retaining the
     * indexed position/UV layout used by the menu draw path. */
    static const Vertex vertices[4] = {
        { 64.0f,  48.0f, 0.0f, 0.0f},
        {314.0f,  48.0f, 1.0f, 0.0f},
        {314.0f, 200.0f, 1.0f, 1.0f},
        { 64.0f, 200.0f, 0.0f, 1.0f},
    };
    make_quad_from_vertices(api, vao, vbo, ebo, vertices);
}

static void clear_target(Api* api) {
    api->clearColor(0.0f, 0.0f, 0.0f, 0.0f);
    api->clear(GL_COLOR_BUFFER_BIT);
}

static void read_center(Api* api, unsigned char px[4]) {
    memset(px, 0, 4);
    api->finish();
    api->readPixels(189, 124, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
}

static int nonblack(const unsigned char px[4]) {
    return px[0] > 4 || px[1] > 4 || px[2] > 4;
}

static const char* solid_vs =
    "#version 330 core\n"
    "layout(location=0) in vec2 aPos;\n"
    "void main(){ gl_Position=vec4(aPos,0.0,1.0); }\n";
static const char* solid_fs =
    "#version 330 core\n"
    "out vec4 fragColor;\n"
    "void main(){ fragColor=vec4(0.92,0.08,0.03,1.0); }\n";

static void identity(GLfloat matrix[16]);

static void front_face_regression(Api* api, GLuint fbo, GLuint vao, GLuint ebo,
                                  GLuint solid_program) {
    (void)ebo;
    static const GLushort ccw[6] = {0, 1, 2, 0, 2, 3};
    static const GLushort cw[6]  = {0, 2, 1, 0, 3, 2};
    api->bindFramebuffer(GL_FRAMEBUFFER, fbo);
    api->bindVertexArray(vao);
    api->useProgram(solid_program);
    api->viewport(0, 0, 378, 248);
    api->scissor(0, 0, 378, 248);
    api->enable(GL_SCISSOR_TEST);
    api->disable(GL_BLEND);
    api->disable(GL_DEPTH_TEST);
    api->enable(GL_CULL_FACE);
    api->cullFace(GL_BACK);

    api->bufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(ccw), ccw, GL_STATIC_DRAW);
    api->frontFace(GL_CCW);
    clear_target(api);
    api->drawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (const void*)0);
    unsigned char px[4];
    read_center(api, px);
    CHECK(nonblack(px), "user FBO GL_CCW + GL_BACK keeps CCW quad visible "
          "(rgba=%u,%u,%u,%u)", px[0], px[1], px[2], px[3]);

    api->bufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(cw), cw, GL_STATIC_DRAW);
    api->frontFace(GL_CW);
    clear_target(api);
    api->drawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (const void*)0);
    read_center(api, px);
    CHECK(nonblack(px), "user FBO GL_CW + GL_BACK keeps CW quad visible "
          "(rgba=%u,%u,%u,%u)", px[0], px[1], px[2], px[3]);

    api->bufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(ccw), ccw, GL_STATIC_DRAW);
    api->frontFace(GL_CCW);
    api->cullFace(GL_FRONT);
    clear_target(api);
    api->drawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (const void*)0);
    read_center(api, px);
    CHECK(!nonblack(px), "user FBO GL_CCW + GL_FRONT culls CCW quad "
          "(rgba=%u,%u,%u,%u)", px[0], px[1], px[2], px[3]);
    api->cullFace(GL_BACK);
}

static void logical_projection_front_face_regression(
    Api* api, GLuint fbo, GLuint vao, GLuint ebo, GLuint projection_program,
    GLuint projection_buffer) {
    (void)ebo;
    GLuint projection_index = api->getUniformBlockIndex(projection_program,
                                                         "Projection");
    CHECK(projection_index != GL_INVALID_INDEX,
          "logical GUI shader exposes named Projection block (index=%u)",
          projection_index);
    if (projection_index != GL_INVALID_INDEX)
        api->uniformBlockBinding(projection_program, projection_index, 1);

    /* A top-left logical GUI projection is the common Minecraft menu shape:
     * y=0 is the top edge and y=height is the bottom edge.  This exercises a
     * real non-identity matrix, a positive-height viewport, and the GL
     * bottom-origin -> Vulkan top-origin conversion instead of only testing
     * direct NDC vertices with an identity transform. */
    GLfloat projection[16];
    identity(projection);
    projection[0] = 2.0f / 378.0f;
    projection[5] = -2.0f / 248.0f;
    projection[12] = -1.0f;
    projection[13] = 1.0f;
    api->bindBuffer(GL_UNIFORM_BUFFER, projection_buffer);
    api->bufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(projection), projection);
    api->bindBufferBase(GL_UNIFORM_BUFFER, 1, projection_buffer);

    api->bindFramebuffer(GL_FRAMEBUFFER, fbo);
    api->bindVertexArray(vao);
    api->useProgram(projection_program);
    api->viewport(17, 23, 341, 201);
    api->scissor(17, 23, 341, 201);
    api->enable(GL_SCISSOR_TEST);
    api->disable(GL_BLEND);
    api->disable(GL_DEPTH_TEST);
    api->enable(GL_CULL_FACE);
    api->cullFace(GL_BACK);

    /* The logical top-left -> top-right -> bottom-right -> bottom-left order
     * is clockwise after this projection, so GL_CW is the observable front
     * face.  The inverse state must cull exactly the same pixels. */
    api->frontFace(GL_CW);
    clear_target(api);
    api->drawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (const void*)0);
    unsigned char px[4];
    read_center(api, px);
    CHECK(nonblack(px),
          "logical GUI projection + GL_CW + GL_BACK keeps quad visible "
          "(rgba=%u,%u,%u,%u)", px[0], px[1], px[2], px[3]);

    api->frontFace(GL_CCW);
    clear_target(api);
    api->drawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (const void*)0);
    read_center(api, px);
    CHECK(!nonblack(px),
          "logical GUI projection + GL_CCW + GL_BACK culls quad "
          "(rgba=%u,%u,%u,%u)", px[0], px[1], px[2], px[3]);

    api->frontFace(GL_CCW);
    api->cullFace(GL_FRONT);
    clear_target(api);
    api->drawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (const void*)0);
    read_center(api, px);
    CHECK(nonblack(px),
          "logical GUI projection + GL_CCW + GL_FRONT keeps quad visible "
          "(rgba=%u,%u,%u,%u)", px[0], px[1], px[2], px[3]);
    api->cullFace(GL_BACK);

    /* The following GUI/descriptor checks intentionally reuse this UBO.  Put
     * its projection back to identity so those checks do not inherit the
     * logical-coordinate probe's matrix. */
    identity(projection);
    api->bufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(projection), projection);
}

static GLuint make_atlas(Api* api, const unsigned char pixels[16]) {
    GLuint texture = 0;
    api->genTextures(1, &texture);
    api->activeTexture(GL_TEXTURE0);
    api->bindTexture(GL_TEXTURE_2D, texture);
    api->texImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 2, 0,
                    GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    api->texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    api->texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    api->texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    api->texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return texture;
}

static void pbo_regression(Api* api, GLuint fbo, GLuint vao, GLuint ebo,
                           GLuint sampler_program) {
    (void)ebo;
    const unsigned char payload[32] = {
        11, 22, 33, 255, 11, 22, 33, 255,
        11, 22, 33, 255, 11, 22, 33, 255,
        17, 211, 63, 255, 17, 211, 63, 255,
        17, 211, 63, 255, 17, 211, 63, 255,
    };
    GLuint pbo = 0, texture = 0;
    api->genBuffers(1, &pbo);
    api->bindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
    api->bufferData(GL_PIXEL_UNPACK_BUFFER, sizeof(payload), payload,
                    GL_STATIC_DRAW);
    api->genTextures(1, &texture);
    api->bindTexture(GL_TEXTURE_2D, texture);
    api->texImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 2, 0,
                    GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    api->texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    api->texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    api->pixelStorei(GL_UNPACK_ALIGNMENT, 1);

    /* The non-zero argument is a GL byte offset, not a host pointer. */
    api->texSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2, 2,
                       GL_RGBA, GL_UNSIGNED_BYTE, (const void*)(uintptr_t)16);
    api->bindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    api->bindFramebuffer(GL_FRAMEBUFFER, fbo);
    api->bindVertexArray(vao);
    api->useProgram(sampler_program);
    api->activeTexture(GL_TEXTURE0);
    api->bindTexture(GL_TEXTURE_2D, texture);
    GLint sampler = api->getUniformLocation(sampler_program, "atlas");
    CHECK(sampler >= 0, "PBO sampler uniform is active (location=%d)", sampler);
    if (sampler >= 0) api->uniform1i(sampler, 0);
    api->disable(GL_CULL_FACE);
    api->disable(GL_BLEND);
    api->disable(GL_DEPTH_TEST);
    api->bufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof((GLushort[6]){0,1,2,0,2,3}),
                    (const GLushort[6]){0,1,2,0,2,3}, GL_STATIC_DRAW);
    clear_target(api);
    api->drawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (const void*)0);
    unsigned char px[4];
    read_center(api, px);
    CHECK(px[0] >= 12 && px[0] <= 22 && px[1] >= 195 && px[1] <= 225 &&
          px[2] >= 50 && px[2] <= 75 && px[3] == 255,
          "PBO non-zero byte offset samples second pixel set "
          "(rgba=%u,%u,%u,%u)", px[0], px[1], px[2], px[3]);

    /* A one-byte tail cannot contain the requested 2x2 RGBA upload.  The
     * wrapper must reject it without dereferencing address 0x1f.  glGetError
     * is intentionally a compatibility no-op in this project, so unchanged
     * texture contents plus a completed GPU readback is the observable test. */
    api->bindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
    api->texSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2, 2,
                       GL_RGBA, GL_UNSIGNED_BYTE,
                       (const void*)(uintptr_t)(sizeof(payload) - 1));
    api->bindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    clear_target(api);
    api->drawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (const void*)0);
    read_center(api, px);
    CHECK(px[0] >= 12 && px[0] <= 22 && px[1] >= 195 && px[1] <= 225 &&
          px[2] >= 50 && px[2] <= 75 && px[3] == 255,
          "PBO out-of-bounds offset is rejected without changing texture "
          "(rgba=%u,%u,%u,%u)", px[0], px[1], px[2], px[3]);
}

typedef struct DynamicBlock {
    GLfloat transform[16];
    GLfloat tint[4];
} DynamicBlock;

static void identity(GLfloat matrix[16]) {
    memset(matrix, 0, sizeof(GLfloat) * 16);
    matrix[0] = 1.0f;
    matrix[5] = 1.0f;
    matrix[10] = 1.0f;
    matrix[15] = 1.0f;
}

static GLuint make_ubo(Api* api, GLuint* dynamic_buffer, GLuint* projection_buffer) {
    DynamicBlock dynamic;
    identity(dynamic.transform);
    dynamic.tint[0] = 1.0f;
    dynamic.tint[1] = 0.75f;
    dynamic.tint[2] = 0.50f;
    dynamic.tint[3] = 0.75f;
    GLfloat projection[16];
    identity(projection);

    api->genBuffers(1, dynamic_buffer);
    api->bindBuffer(GL_UNIFORM_BUFFER, *dynamic_buffer);
    api->bufferData(GL_UNIFORM_BUFFER, sizeof(dynamic), &dynamic, GL_DYNAMIC_DRAW);
    api->bindBufferBase(GL_UNIFORM_BUFFER, 0, *dynamic_buffer);

    api->genBuffers(1, projection_buffer);
    api->bindBuffer(GL_UNIFORM_BUFFER, *projection_buffer);
    api->bufferData(GL_UNIFORM_BUFFER, sizeof(projection), projection, GL_STATIC_DRAW);
    api->bindBufferBase(GL_UNIFORM_BUFFER, 1, *projection_buffer);
    return *dynamic_buffer;
}

static void gui_regression(Api* api, GLuint fbo, GLuint vao, GLuint ebo,
                           GLuint gui_program, GLuint atlas,
                           GLuint dynamic_buffer) {
    (void)ebo;
    GLuint dynamic_index = api->getUniformBlockIndex(gui_program,
                                                      "DynamicTransforms");
    GLuint projection_index = api->getUniformBlockIndex(gui_program, "Projection");
    CHECK(dynamic_index != GL_INVALID_INDEX,
          "GUI shader exposes named DynamicTransforms block (index=%u)",
          dynamic_index);
    CHECK(projection_index != GL_INVALID_INDEX,
          "GUI shader exposes named Projection block (index=%u)",
          projection_index);
    if (dynamic_index != GL_INVALID_INDEX)
        api->uniformBlockBinding(gui_program, dynamic_index, 0);
    if (projection_index != GL_INVALID_INDEX)
        api->uniformBlockBinding(gui_program, projection_index, 1);

    api->bindFramebuffer(GL_FRAMEBUFFER, fbo);
    api->bindVertexArray(vao);
    api->useProgram(gui_program);
    api->activeTexture(GL_TEXTURE0);
    api->bindTexture(GL_TEXTURE_2D, atlas);
    GLint sampler = api->getUniformLocation(gui_program, "atlas");
    CHECK(sampler >= 0, "GUI atlas sampler is active (location=%d)", sampler);
    if (sampler >= 0) api->uniform1i(sampler, 0);
    api->bindBufferBase(GL_UNIFORM_BUFFER, 0, dynamic_buffer);
    api->viewport(0, 0, 378, 248);
    api->scissor(0, 0, 378, 248);
    api->enable(GL_SCISSOR_TEST);
    api->enable(GL_CULL_FACE);
    api->cullFace(GL_BACK);
    api->frontFace(GL_CCW);
    api->disable(GL_DEPTH_TEST);
    api->enable(GL_BLEND);
    api->blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    DynamicBlock dynamic;
    identity(dynamic.transform);
    dynamic.tint[0] = 1.0f;
    dynamic.tint[1] = 0.75f;
    dynamic.tint[2] = 0.50f;
    dynamic.tint[3] = 0.75f;
    api->bindBuffer(GL_UNIFORM_BUFFER, dynamic_buffer);
    api->bufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(dynamic), &dynamic);
    clear_target(api);
    api->drawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (const void*)0);
    unsigned char px[4];
    read_center(api, px);
    CHECK(px[0] > 90 && px[1] > 15 && px[1] < 120 && px[2] < 80 && px[3] > 0,
          "synthetic Minecraft GUI quad is visible with alpha/blend "
          "(rgba=%u,%u,%u,%u)", px[0], px[1], px[2], px[3]);

    /* Move only the vertex-side transform outside clip space.  A subsequent
     * restore plus a fragment tint change proves that both stages read the
     * one logical DynamicTransforms binding rather than two stale descriptors. */
    dynamic.transform[12] = 2.5f;
    api->bufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(dynamic.transform),
                       dynamic.transform);
    clear_target(api);
    api->drawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (const void*)0);
    read_center(api, px);
    CHECK(!nonblack(px), "DynamicTransforms vertex transform moves GUI quad offscreen "
          "(rgba=%u,%u,%u,%u)", px[0], px[1], px[2], px[3]);

    identity(dynamic.transform);
    dynamic.tint[0] = 0.10f;
    dynamic.tint[1] = 1.0f;
    dynamic.tint[2] = 0.20f;
    dynamic.tint[3] = 0.75f;
    api->bufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(dynamic), &dynamic);
    clear_target(api);
    api->drawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (const void*)0);
    read_center(api, px);
    CHECK(px[1] > px[0] && px[1] > px[2] && px[3] > 0,
          "DynamicTransforms fragment tint updates through same binding "
          "(rgba=%u,%u,%u,%u)", px[0], px[1], px[2], px[3]);
}

static void descriptor_stress(Api* api, GLuint fbo, GLuint vao, GLuint gui_program,
                              GLuint atlas, GLuint dynamic_buffer) {
    unsigned char pixels[16];
    memset(pixels, 0, sizeof(pixels));
    for (int frame = 0; frame < 96; ++frame) {
        GLfloat tint[4] = {
            (GLfloat)((frame % 7) + 1) / 8.0f,
            (GLfloat)((frame % 5) + 2) / 7.0f,
            0.75f,
            0.75f,
        };
        for (int pixel = 0; pixel < 4; ++pixel) {
            pixels[pixel * 4 + 0] = (unsigned char)(20 + frame);
            pixels[pixel * 4 + 1] = (unsigned char)(180 - (frame % 40));
            pixels[pixel * 4 + 2] = 40;
            pixels[pixel * 4 + 3] = 255;
        }
        api->bindBuffer(GL_UNIFORM_BUFFER, dynamic_buffer);
        api->bufferSubData(GL_UNIFORM_BUFFER, 64, sizeof(tint), tint);
        api->activeTexture(GL_TEXTURE0);
        api->bindTexture(GL_TEXTURE_2D, atlas);
        api->texSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2, 2,
                           GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        api->bindFramebuffer(GL_FRAMEBUFFER, fbo);
        api->bindVertexArray(vao);
        api->useProgram(gui_program);
        clear_target(api);
        api->drawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (const void*)0);
        api->finish();
        GLsync fence = api->fenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        GLenum wait = fence ? api->clientWaitSync(
            fence, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ULL) : GL_TIMEOUT_EXPIRED;
        CHECK(fence != NULL, "descriptor stress frame %d creates a GPU fence",
              frame);
        CHECK(wait == GL_ALREADY_SIGNALED ||
              wait == GL_CONDITION_SATISFIED,
              "descriptor stress frame %d fence completes (result=0x%x)",
              frame, wait);
        if (fence) api->deleteSync(fence);
        if ((frame % 16) == 0) {
            unsigned char px[4];
            api->readPixels(189, 124, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
            CHECK(nonblack(px), "descriptor stress frame %d readback remains visible "
                  "(rgba=%u,%u,%u,%u)", frame, px[0], px[1], px[2], px[3]);
        }
    }
}

int main(int argc, char** argv) {
    void* handle = open_mithril(argc, argv);
    if (!handle) return 2;
    if (argc > 2 && strcmp(argv[2], "--egl") == 0 &&
        !setup_egl_context(handle)) {
        return 2;
    }
    Api api;
    load_api(handle, &api);
    if (failures) {
        printf("MC GUI SMOKE FAILED (missing symbols)\n");
        return 1;
    }

    GLfloat anisotropy = 0.0f;
    GLint anisotropy_i = 0;
    api.getFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &anisotropy);
    api.getIntegerv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &anisotropy_i);
    const GLubyte* extensions = api.getString(GL_EXTENSIONS);
    CHECK(extensions && strstr((const char*)extensions,
                               "GL_EXT_texture_filter_anisotropic") != NULL,
          "anisotropy extension is advertised");
    CHECK(anisotropy >= 1.0f && anisotropy < 128.0f && anisotropy_i >= 1 &&
          anisotropy_i <= 128 &&
          (anisotropy_i - anisotropy < 1.0f) &&
          (anisotropy - anisotropy_i < 1.0f),
          "anisotropy float/int queries agree with a real capability "
          "(float=%.3f int=%d)", anisotropy, anisotropy_i);

    GLuint fbo = 0, color = 0;
    if (!make_target(&api, &fbo, &color)) return 1;
    GLuint vao = 0, vbo = 0, ebo = 0;
    make_quad(&api, &vao, &vbo, &ebo);
    GLuint logical_vao = 0, logical_vbo = 0, logical_ebo = 0;
    make_logical_quad(&api, &logical_vao, &logical_vbo, &logical_ebo);

    GLuint solid_program = make_program(&api, solid_vs, solid_fs, "solid");
    const char* logical_projection_vs =
        "#version 330 core\n"
        "layout(location=0) in vec2 aPos;\n"
        "layout(std140) uniform Projection { mat4 projection; };\n"
        "void main(){ gl_Position=projection*vec4(aPos,0.0,1.0); }\n";
    GLuint logical_projection_program = make_program(
        &api, logical_projection_vs, solid_fs, "logical-projection");
    const char* sampler_vs =
        "#version 330 core\n"
        "layout(location=0) in vec2 aPos;\n"
        "void main(){ gl_Position=vec4(aPos,0.0,1.0); }\n";
    const char* sampler_fs =
        "#version 330 core\n"
        "uniform sampler2D atlas;\n"
        "out vec4 fragColor;\n"
        "void main(){ fragColor=texture(atlas,vec2(0.5)); }\n";
    GLuint sampler_program = make_program(&api, sampler_vs, sampler_fs, "sampler");
    const char* gui_vs =
        "#version 330 core\n"
        "layout(location=0) in vec2 aPos;\n"
        "layout(location=1) in vec2 aUV;\n"
        "layout(std140) uniform DynamicTransforms { mat4 transform; vec4 tint; };\n"
        "layout(std140) uniform Projection { mat4 projection; } projectionBlock;\n"
        "out vec2 vUV;\n"
        "void main(){ gl_Position=projectionBlock.projection*transform*vec4(aPos,0.0,1.0); vUV=aUV; }\n";
    const char* gui_fs =
        "#version 330 core\n"
        "uniform sampler2D atlas;\n"
        "layout(std140) uniform DynamicTransforms { mat4 transform; vec4 tint; };\n"
        "layout(std140) uniform Projection { mat4 projection; } projectionBlock;\n"
        "in vec2 vUV; out vec4 fragColor;\n"
        "void main(){ float keep=projectionBlock.projection[0][0]*0.000001; "
        "fragColor=texture(atlas,vUV)*tint+vec4(keep); }\n";
    GLuint gui_program = make_program(&api, gui_vs, gui_fs, "gui");
    if (!solid_program || !logical_projection_program || !sampler_program ||
        !gui_program) return 1;

    GLuint dynamic_buffer = 0, projection_buffer = 0;
    make_ubo(&api, &dynamic_buffer, &projection_buffer);
    const unsigned char atlas_pixels[16] = {
        220, 80, 30, 255, 220, 80, 30, 255,
        220, 80, 30, 255, 220, 80, 30, 255,
    };
    GLuint atlas = make_atlas(&api, atlas_pixels);

    front_face_regression(&api, fbo, vao, ebo, solid_program);
    logical_projection_front_face_regression(
        &api, fbo, logical_vao, logical_ebo, logical_projection_program,
        projection_buffer);
    pbo_regression(&api, fbo, vao, ebo, sampler_program);
    gui_regression(&api, fbo, vao, ebo, gui_program, atlas, dynamic_buffer);
    descriptor_stress(&api, fbo, vao, gui_program, atlas, dynamic_buffer);

    printf("%s\n", failures ? "MC GUI SMOKE FAILED" :
           "MC GUI SMOKE ALL PASSED");
    printf("checks=%d failures=%d\n", checks, failures);
    dlclose(handle);
    return failures ? 1 : 0;
}
