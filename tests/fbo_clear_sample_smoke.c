/*
 * fbo_clear_sample_smoke.c
 *
 * Regression for the DirectVulkan clear-only user-FBO layout path:
 *   mipmapped glTexImage2D(NULL) -> attach level 1 -> glClear -> sample it.
 *
 * A Vulkan backend must transition the source image to attachment-optimal
 * before the clear pass, then to a sampled read-only layout after the pass.
 * This covers both clear-only layout tracking and the attachment-subresource
 * contract: the render pass must use a single-mip view and the 8x8 level-1
 * extent rather than the texture's 16x16 base-level view/extent.
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/glcorearb.h>

typedef void (*genTextures_fn)(GLsizei, GLuint*);
typedef void (*bindTexture_fn)(GLenum, GLuint);
typedef void (*texImage2D_fn)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
typedef void (*texParameteri_fn)(GLenum, GLenum, GLint);
typedef void (*activeTexture_fn)(GLenum);
typedef void (*genFramebuffers_fn)(GLsizei, GLuint*);
typedef void (*bindFramebuffer_fn)(GLenum, GLuint);
typedef void (*framebufferTexture2D_fn)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum (*checkFramebufferStatus_fn)(GLenum);
typedef void (*clearColor_fn)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (*clear_fn)(GLbitfield);
typedef void (*viewport_fn)(GLint, GLint, GLsizei, GLsizei);
typedef GLuint (*createShader_fn)(GLenum);
typedef void (*shaderSource_fn)(GLuint, GLsizei, const GLchar* const*, const GLint*);
typedef void (*compileShader_fn)(GLuint);
typedef void (*getShaderiv_fn)(GLuint, GLenum, GLint*);
typedef GLuint (*createProgram_fn)(void);
typedef void (*attachShader_fn)(GLuint, GLuint);
typedef void (*linkProgram_fn)(GLuint);
typedef void (*getProgramiv_fn)(GLuint, GLenum, GLint*);
typedef void (*useProgram_fn)(GLuint);
typedef GLint (*getUniformLocation_fn)(GLuint, const GLchar*);
typedef void (*uniform1i_fn)(GLint, GLint);
typedef void (*genVertexArrays_fn)(GLsizei, GLuint*);
typedef void (*bindVertexArray_fn)(GLuint);
typedef void (*drawArrays_fn)(GLenum, GLint, GLsizei);
typedef void (*finish_fn)(void);
typedef void (*readPixels_fn)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
typedef GLenum (*getError_fn)(void);
typedef const GLubyte* (*getString_fn)(GLenum);

#define RESOLVE(var, type, sym) do { \
    var = (type)dlsym(h, sym); \
    if (!(var)) { fprintf(stderr, "missing symbol %s\n", sym); return 3; } \
} while (0)

static GLuint compile_stage(createShader_fn createShader, shaderSource_fn shaderSource,
                            compileShader_fn compileShader, getShaderiv_fn getShaderiv,
                            GLenum stage, const char* src) {
    GLuint sh = createShader(stage);
    shaderSource(sh, 1, &src, NULL);
    compileShader(sh);
    GLint ok = 0;
    getShaderiv(sh, GL_COMPILE_STATUS, &ok);
    return ok ? sh : 0;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s /path/to/libmithril.dylib\n", argv[0]); return 2; }
    void* h = dlopen(argv[1], RTLD_NOW | RTLD_GLOBAL);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }

    genTextures_fn genTextures; bindTexture_fn bindTexture; texImage2D_fn texImage2D;
    texParameteri_fn texParameteri; activeTexture_fn activeTexture;
    genFramebuffers_fn genFramebuffers; bindFramebuffer_fn bindFramebuffer;
    framebufferTexture2D_fn framebufferTexture2D; checkFramebufferStatus_fn checkFramebufferStatus;
    clearColor_fn clearColor; clear_fn clear; viewport_fn viewport;
    createShader_fn createShader; shaderSource_fn shaderSource; compileShader_fn compileShader;
    getShaderiv_fn getShaderiv; createProgram_fn createProgram; attachShader_fn attachShader;
    linkProgram_fn linkProgram; getProgramiv_fn getProgramiv; useProgram_fn useProgram;
    getUniformLocation_fn getUniformLocation; uniform1i_fn uniform1i;
    genVertexArrays_fn genVertexArrays; bindVertexArray_fn bindVertexArray;
    drawArrays_fn drawArrays; finish_fn finish; readPixels_fn readPixels;
    getError_fn getError; getString_fn getString;

    RESOLVE(genTextures, genTextures_fn, "glGenTextures");
    RESOLVE(bindTexture, bindTexture_fn, "glBindTexture");
    RESOLVE(texImage2D, texImage2D_fn, "glTexImage2D");
    RESOLVE(texParameteri, texParameteri_fn, "glTexParameteri");
    RESOLVE(activeTexture, activeTexture_fn, "glActiveTexture");
    RESOLVE(genFramebuffers, genFramebuffers_fn, "glGenFramebuffers");
    RESOLVE(bindFramebuffer, bindFramebuffer_fn, "glBindFramebuffer");
    RESOLVE(framebufferTexture2D, framebufferTexture2D_fn, "glFramebufferTexture2D");
    RESOLVE(checkFramebufferStatus, checkFramebufferStatus_fn, "glCheckFramebufferStatus");
    RESOLVE(clearColor, clearColor_fn, "glClearColor");
    RESOLVE(clear, clear_fn, "glClear");
    RESOLVE(viewport, viewport_fn, "glViewport");
    RESOLVE(createShader, createShader_fn, "glCreateShader");
    RESOLVE(shaderSource, shaderSource_fn, "glShaderSource");
    RESOLVE(compileShader, compileShader_fn, "glCompileShader");
    RESOLVE(getShaderiv, getShaderiv_fn, "glGetShaderiv");
    RESOLVE(createProgram, createProgram_fn, "glCreateProgram");
    RESOLVE(attachShader, attachShader_fn, "glAttachShader");
    RESOLVE(linkProgram, linkProgram_fn, "glLinkProgram");
    RESOLVE(getProgramiv, getProgramiv_fn, "glGetProgramiv");
    RESOLVE(useProgram, useProgram_fn, "glUseProgram");
    RESOLVE(getUniformLocation, getUniformLocation_fn, "glGetUniformLocation");
    RESOLVE(uniform1i, uniform1i_fn, "glUniform1i");
    RESOLVE(genVertexArrays, genVertexArrays_fn, "glGenVertexArrays");
    RESOLVE(bindVertexArray, bindVertexArray_fn, "glBindVertexArray");
    RESOLVE(drawArrays, drawArrays_fn, "glDrawArrays");
    RESOLVE(finish, finish_fn, "glFinish");
    RESOLVE(readPixels, readPixels_fn, "glReadPixels");
    RESOLVE(getError, getError_fn, "glGetError");
    RESOLVE(getString, getString_fn, "glGetString");

    const char* version = (const char*)getString(GL_VERSION);
    printf("backend: %s\n", version ? version : "(null)");

    GLuint tex[2] = {0, 0};
    GLuint fbo[2] = {0, 0};
    genTextures(2, tex);
    genFramebuffers(2, fbo);

    /* Source: define storage with NULL data, then touch it ONLY through glClear. */
    bindTexture(GL_TEXTURE_2D, tex[0]);
    texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    texImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    texImage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    texParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 1);
    texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1);
    bindFramebuffer(GL_FRAMEBUFFER, fbo[0]);
    framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex[0], 1);
    if (checkFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "source FBO incomplete\n"); return 4;
    }
    viewport(0, 0, 8, 8);
    clearColor(0.25f, 0.50f, 0.75f, 1.0f);
    clear(GL_COLOR_BUFFER_BIT);
    GLenum err_after_clear = getError();

    /* Destination: draw a full-screen triangle sampling the clear-only source. */
    bindTexture(GL_TEXTURE_2D, tex[1]);
    texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    texImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    bindFramebuffer(GL_FRAMEBUFFER, fbo[1]);
    framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex[1], 0);
    if (checkFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "destination FBO incomplete\n"); return 5;
    }

    const char* vs_src =
        "#version 330 core\n"
        "out vec2 uv;\n"
        "void main(){\n"
        "  vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0, (gl_VertexID == 2) ? 3.0 : -1.0);\n"
        "  gl_Position = vec4(p,0.0,1.0);\n"
        "  uv = p * 0.5 + 0.5;\n"
        "}\n";
    const char* fs_src =
        "#version 330 core\n"
        "in vec2 uv;\n"
        "uniform sampler2D srcTex;\n"
        "out vec4 color;\n"
        "void main(){ color = textureLod(srcTex, uv, 1.0); }\n";

    GLuint vs = compile_stage(createShader, shaderSource, compileShader, getShaderiv, GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_stage(createShader, shaderSource, compileShader, getShaderiv, GL_FRAGMENT_SHADER, fs_src);
    if (!vs || !fs) { fprintf(stderr, "shader compile failed\n"); return 6; }
    GLuint program = createProgram();
    attachShader(program, vs); attachShader(program, fs); linkProgram(program);
    GLint linked = 0; getProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) { fprintf(stderr, "program link failed\n"); return 7; }

    useProgram(program);
    GLint samplerLoc = getUniformLocation(program, "srcTex");
    if (samplerLoc < 0) { fprintf(stderr, "srcTex uniform missing\n"); return 8; }
    uniform1i(samplerLoc, 0);
    activeTexture(GL_TEXTURE0);
    bindTexture(GL_TEXTURE_2D, tex[0]);

    GLuint vao = 0; genVertexArrays(1, &vao); bindVertexArray(vao);
    viewport(0, 0, 16, 16);
    drawArrays(GL_TRIANGLES, 0, 3);
    finish();

    unsigned char pixel[4] = {0,0,0,0};
    readPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    GLenum final_err = getError();
    printf("clear_err=0x%04x final_err=0x%04x pixel=(%u,%u,%u,%u)\n",
           (unsigned)err_after_clear, (unsigned)final_err,
           pixel[0], pixel[1], pixel[2], pixel[3]);

    const int ok = err_after_clear == GL_NO_ERROR && final_err == GL_NO_ERROR &&
                   pixel[0] >= 62 && pixel[0] <= 66 &&
                   pixel[1] >= 126 && pixel[1] <= 130 &&
                   pixel[2] >= 189 && pixel[2] <= 193 &&
                   pixel[3] >= 253;
    if (!ok) {
        fprintf(stderr, "FBO CLEAR SAMPLE SMOKE FAILED\n");
        return 1;
    }
    printf("FBO CLEAR SAMPLE SMOKE PASSED\n");
    return 0;
}
