/*
 * gl_smoke.c — Mithril-Wrapper advertised GL 3.3 state-machine smoke.
 *
 * This is deliberately a small ABI/state test. Real GPU semantics are covered
 * by render_smoke.c and the DirectMetal semantic-oracle suite. Keep this file
 * aligned with the capability contract: do not turn symbol presence into a
 * higher advertised GL version and do not suppress glGetError.
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/glcorearb.h>

typedef void (*getIntegerv_fn)(GLenum, GLint*);
typedef const GLubyte* (*getString_fn)(GLenum);
typedef GLenum (*getError_fn)(void);
typedef void (*viewport_fn)(GLint, GLint, GLsizei, GLsizei);
typedef void (*scissor_fn)(GLint, GLint, GLsizei, GLsizei);
typedef void (*enable_fn)(GLenum);
typedef void (*disable_fn)(GLenum);
typedef GLboolean (*isEnabled_fn)(GLenum);
typedef void (*enablei_fn)(GLenum, GLuint);
typedef void (*disablei_fn)(GLenum, GLuint);
typedef GLboolean (*isEnabledi_fn)(GLenum, GLuint);
typedef void (*clearColor_fn)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (*depthMask_fn)(GLboolean);
typedef void (*colorMask_fn)(GLboolean, GLboolean, GLboolean, GLboolean);
typedef void (*blendFunc_fn)(GLenum, GLenum);
typedef void (*cullFace_fn)(GLenum);
typedef void (*frontFace_fn)(GLenum);
typedef void (*depthFunc_fn)(GLenum);
typedef void (*flush_fn)(void);
typedef void (*finish_fn)(void);
typedef void (*genTextures_fn)(GLsizei, GLuint*);
typedef void (*bindTexture_fn)(GLenum, GLuint);
typedef void (*texParameteri_fn)(GLenum, GLenum, GLint);
typedef void (*generateMipmap_fn)(GLenum);
typedef void (*drawArrays_fn)(GLenum, GLint, GLsizei);
typedef void (*drawElements_fn)(GLenum, GLsizei, GLenum, const void*);

static int checks = 0;
static int failures = 0;
#define CHECK(cond, fmt, ...) do { \
    ++checks; \
    if (cond) printf("ok : " fmt "\n", ##__VA_ARGS__); \
    else { printf("FAIL: " fmt "\n", ##__VA_ARGS__); ++failures; } \
} while (0)

static void* open_libmithril(int argc, char** argv) {
    const char* candidates[4];
    int n = 0;
    if (argc > 1) candidates[n++] = argv[1];
    candidates[n++] = "./output/libmithril.so";
    candidates[n++] = "./output/libmithril.dylib";
    candidates[n++] = "./build/libmithril.dylib";
    for (int i = 0; i < n; ++i) {
        void* h = dlopen(candidates[i], RTLD_NOW | RTLD_GLOBAL);
        if (h) { printf("loaded: %s\n", candidates[i]); return h; }
    }
    fprintf(stderr, "dlopen libmithril failed: %s\n", dlerror());
    return NULL;
}

#define RESOLVE(var, type, name) do { \
    var = (type)dlsym(h, name); \
    CHECK(var != NULL, "resolve %s", name); \
} while (0)

int main(int argc, char** argv) {
    void* h = open_libmithril(argc, argv);
    if (!h) return 2;

    getIntegerv_fn getIntegerv = NULL;
    getString_fn getString = NULL;
    getError_fn getError = NULL;
    viewport_fn viewport = NULL;
    scissor_fn scissor = NULL;
    enable_fn enable = NULL;
    disable_fn disable = NULL;
    isEnabled_fn isEnabled = NULL;
    enablei_fn enablei = NULL;
    disablei_fn disablei = NULL;
    isEnabledi_fn isEnabledi = NULL;
    clearColor_fn clearColor = NULL;
    depthMask_fn depthMask = NULL;
    colorMask_fn colorMask = NULL;
    blendFunc_fn blendFunc = NULL;
    cullFace_fn cullFace = NULL;
    frontFace_fn frontFace = NULL;
    depthFunc_fn depthFunc = NULL;
    flush_fn flush = NULL;
    finish_fn finish = NULL;
    genTextures_fn genTextures = NULL;
    bindTexture_fn bindTexture = NULL;
    texParameteri_fn texParameteri = NULL;
    generateMipmap_fn generateMipmap = NULL;
    drawArrays_fn drawArrays = NULL;
    drawElements_fn drawElements = NULL;

    RESOLVE(getIntegerv, getIntegerv_fn, "glGetIntegerv");
    RESOLVE(getString, getString_fn, "glGetString");
    RESOLVE(getError, getError_fn, "glGetError");
    RESOLVE(viewport, viewport_fn, "glViewport");
    RESOLVE(scissor, scissor_fn, "glScissor");
    RESOLVE(enable, enable_fn, "glEnable");
    RESOLVE(disable, disable_fn, "glDisable");
    RESOLVE(isEnabled, isEnabled_fn, "glIsEnabled");
    RESOLVE(enablei, enablei_fn, "glEnablei");
    RESOLVE(disablei, disablei_fn, "glDisablei");
    RESOLVE(isEnabledi, isEnabledi_fn, "glIsEnabledi");
    RESOLVE(clearColor, clearColor_fn, "glClearColor");
    RESOLVE(depthMask, depthMask_fn, "glDepthMask");
    RESOLVE(colorMask, colorMask_fn, "glColorMask");
    RESOLVE(blendFunc, blendFunc_fn, "glBlendFunc");
    RESOLVE(cullFace, cullFace_fn, "glCullFace");
    RESOLVE(frontFace, frontFace_fn, "glFrontFace");
    RESOLVE(depthFunc, depthFunc_fn, "glDepthFunc");
    RESOLVE(flush, flush_fn, "glFlush");
    RESOLVE(finish, finish_fn, "glFinish");
    RESOLVE(genTextures, genTextures_fn, "glGenTextures");
    RESOLVE(bindTexture, bindTexture_fn, "glBindTexture");
    RESOLVE(texParameteri, texParameteri_fn, "glTexParameteri");
    RESOLVE(generateMipmap, generateMipmap_fn, "glGenerateMipmap");
    RESOLVE(drawArrays, drawArrays_fn, "glDrawArrays");
    RESOLVE(drawElements, drawElements_fn, "glDrawElements");
    if (failures) return 1;

    GLint major = 0, minor = 0;
    getIntegerv(GL_MAJOR_VERSION, &major);
    getIntegerv(GL_MINOR_VERSION, &minor);
    CHECK(major == 3 && minor == 3,
          "GL_MAJOR_VERSION=%d GL_MINOR_VERSION=%d == advertised 3.3", major, minor);

    const char* version = (const char*)getString(GL_VERSION);
    const char* glsl = (const char*)getString(GL_SHADING_LANGUAGE_VERSION);
    CHECK(version && strstr(version, "3.3.0") && strstr(version, "Mithril-Wrapper"),
          "GL_VERSION is contracted 3.3 Mithril string (%s)", version ? version : "null");
    CHECK(glsl && strstr(glsl, "3.30"),
          "GLSL version is contracted 3.30 (%s)", glsl ? glsl : "null");

    while (getError() != GL_NO_ERROR) {}
    CHECK(getError() == GL_NO_ERROR, "initial error queue empty");

    GLint bogus = 0;
    getIntegerv(0xC0FFEEu, &bogus);
    CHECK(getError() == GL_INVALID_ENUM, "invalid glGetIntegerv records GL_INVALID_ENUM");
    CHECK(getError() == GL_NO_ERROR, "glGetError consumes getter error");

    enable(0xC0FFEEu);
    CHECK(getError() == GL_INVALID_ENUM, "invalid glEnable records GL_INVALID_ENUM");
    CHECK(getError() == GL_NO_ERROR, "glGetError consumes enable error");
    disable(0xC0FFEEu);
    CHECK(getError() == GL_INVALID_ENUM, "invalid glDisable records GL_INVALID_ENUM");
    CHECK(getError() == GL_NO_ERROR, "glGetError consumes disable error");

    viewport(10, 20, 640, 480);
    GLint vp[4] = {-1,-1,-1,-1};
    getIntegerv(GL_VIEWPORT, vp);
    CHECK(vp[0] == 10 && vp[1] == 20 && vp[2] == 640 && vp[3] == 480,
          "viewport state round-trip (%d,%d %dx%d)", vp[0], vp[1], vp[2], vp[3]);

    scissor(1, 2, 300, 200);
    GLint sc[4] = {-1,-1,-1,-1};
    getIntegerv(GL_SCISSOR_BOX, sc);
    CHECK(sc[0] == 1 && sc[1] == 2 && sc[2] == 300 && sc[3] == 200,
          "scissor state round-trip (%d,%d %dx%d)", sc[0], sc[1], sc[2], sc[3]);

    CHECK(isEnabled(GL_DEPTH_TEST) == GL_FALSE, "depth test disabled by default");
    enable(GL_DEPTH_TEST);
    CHECK(isEnabled(GL_DEPTH_TEST) == GL_TRUE, "glEnable depth test visible in state");
    disable(GL_DEPTH_TEST);
    CHECK(isEnabled(GL_DEPTH_TEST) == GL_FALSE, "glDisable depth test visible in state");
    enablei(GL_BLEND, 0);
    CHECK(isEnabledi(GL_BLEND, 0) == GL_TRUE, "indexed blend enable visible in state");
    disablei(GL_BLEND, 0);
    CHECK(isEnabledi(GL_BLEND, 0) == GL_FALSE, "indexed blend disable visible in state");

    clearColor(0.1f, 0.2f, 0.3f, 1.0f);
    depthMask(GL_TRUE);
    colorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    cullFace(GL_BACK);
    frontFace(GL_CCW);
    depthFunc(GL_LESS);
    flush();
    finish();
    CHECK(getError() == GL_NO_ERROR, "valid state setters leave no error");

    GLuint tex = 0;
    genTextures(1, &tex);
    CHECK(tex != 0, "glGenTextures allocates nonzero name (%u)", tex);
    bindTexture(GL_TEXTURE_2D, tex);
    texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    CHECK(getError() == GL_NO_ERROR, "bound texture accepts mipmap min filter");

    CHECK(drawArrays && drawElements && generateMipmap,
          "draw/mipmap entry points remain exported without implying GL > 3.3");

    printf("\nGL 3.3 STATE SMOKE: %d checks, %d failure(s)\n", checks, failures);
    if (!failures) printf("GL SMOKE ALL PASSED\n");
    return failures ? 1 : 0;
}
