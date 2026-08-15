/* DirectMetal layered FBO boundary regression.
 *
 * A concrete array layer is supported and must map to the requested native
 * slice. Whole-level layered rendering is not implemented yet and must fail
 * closed instead of silently aliasing layer zero.
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GL_NO_ERROR 0
#define GL_INVALID_OPERATION 0x0502
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_FRAMEBUFFER 0x8D40
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_TEXTURE_2D_ARRAY 0x8C1A
#define GL_RGBA8 0x8058
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_RENDERER 0x1F01

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLboolean;

typedef const unsigned char* (*fnGetString)(GLenum);
typedef GLenum (*fnGetError)(void);
typedef void (*fnGenTextures)(GLsizei, GLuint*);
typedef void (*fnBindTexture)(GLenum, GLuint);
typedef void (*fnDeleteTextures)(GLsizei, const GLuint*);
typedef void (*fnTexImage3D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei,
                             GLint, GLenum, GLenum, const void*);
typedef void (*fnGenFramebuffers)(GLsizei, GLuint*);
typedef void (*fnBindFramebuffer)(GLenum, GLuint);
typedef void (*fnDeleteFramebuffers)(GLsizei, const GLuint*);
typedef void (*fnFramebufferTextureLayer)(GLenum, GLenum, GLuint, GLint, GLint);
typedef void (*fnFramebufferTexture)(GLenum, GLenum, GLuint, GLint);
typedef GLenum (*fnCheckFramebufferStatus)(GLenum);
typedef void (*fnClearColor)(float, float, float, float);
typedef void (*fnClear)(GLbitfield);
typedef void (*fnFinish)(void);
typedef void (*fnReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum,
                             void*);

static int failures;
#define CHECK(condition, ...) do {                                           \
    if (condition) printf("ok  : " __VA_ARGS__);                            \
    else { printf("FAIL: " __VA_ARGS__); ++failures; }                      \
    printf("\n");                                                           \
} while (0)
#define LOAD(type, variable, symbol) type variable = (type)dlsym(lib, symbol)

static int pixel_is(const unsigned char p[4], int r, int g, int b) {
    return abs((int)p[0] - r) <= 3 && abs((int)p[1] - g) <= 3 &&
           abs((int)p[2] - b) <= 3 && p[3] >= 252;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const char* path = getenv("MITHRIL_LIBRARY");
    if (!path || !*path) path = "./output/libmithril.dylib";
    void* lib = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
    if (!lib) { fprintf(stderr, "dlopen(%s): %s\n", path, dlerror()); return 2; }

    LOAD(fnGetString, getString, "glGetString");
    LOAD(fnGetError, getError, "glGetError");
    LOAD(fnGenTextures, genTextures, "glGenTextures");
    LOAD(fnBindTexture, bindTexture, "glBindTexture");
    LOAD(fnDeleteTextures, deleteTextures, "glDeleteTextures");
    LOAD(fnTexImage3D, texImage3D, "glTexImage3D");
    LOAD(fnGenFramebuffers, genFramebuffers, "glGenFramebuffers");
    LOAD(fnBindFramebuffer, bindFramebuffer, "glBindFramebuffer");
    LOAD(fnDeleteFramebuffers, deleteFramebuffers, "glDeleteFramebuffers");
    LOAD(fnFramebufferTextureLayer, framebufferTextureLayer,
         "glFramebufferTextureLayer");
    LOAD(fnFramebufferTexture, framebufferTexture, "glFramebufferTexture");
    LOAD(fnCheckFramebufferStatus, checkFramebufferStatus,
         "glCheckFramebufferStatus");
    LOAD(fnClearColor, clearColor, "glClearColor");
    LOAD(fnClear, clear, "glClear");
    LOAD(fnFinish, finish, "glFinish");
    LOAD(fnReadPixels, readPixels, "glReadPixels");

    CHECK(getString && getError && genTextures && bindTexture && deleteTextures &&
              texImage3D && genFramebuffers && bindFramebuffer &&
              deleteFramebuffers && framebufferTextureLayer &&
              framebufferTexture && checkFramebufferStatus && clearColor &&
              clear && finish && readPixels,
          "layered FBO GL entry points resolve");
    if (failures) return 1;
    const char* renderer = (const char*)getString(GL_RENDERER);
    CHECK(renderer && strstr(renderer, "DirectMetal"),
          "context is explicitly DirectMetal (%s)", renderer ? renderer : "null");

    GLuint texture = 0, fbo = 0;
    genTextures(1, &texture);
    bindTexture(GL_TEXTURE_2D_ARRAY, texture);
    texImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, 16, 16, 2, 0,
               GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    CHECK(getError() == GL_NO_ERROR, "2D array texture allocates two slices");

    genFramebuffers(1, &fbo);
    bindFramebuffer(GL_FRAMEBUFFER, fbo);
    framebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            texture, 0, 1);
    CHECK(getError() == GL_NO_ERROR &&
              checkFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
          "array layer 1 forms a complete framebuffer");
    clearColor(0.f, 1.f, 0.f, 1.f);
    clear(GL_COLOR_BUFFER_BIT);
    finish();
    unsigned char pixel[4] = {0, 0, 0, 0};
    readPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    CHECK(pixel_is(pixel, 0, 255, 0),
          "layer 1 receives its green render target contents");

    framebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            texture, 0, 0);
    CHECK(getError() == GL_NO_ERROR &&
              checkFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
          "array layer 0 forms a complete framebuffer independently");
    clearColor(1.f, 0.f, 0.f, 1.f);
    clear(GL_COLOR_BUFFER_BIT);
    finish();
    readPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    CHECK(pixel_is(pixel, 255, 0, 0),
          "layer 0 receives red without aliasing layer 1");

    framebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            texture, 0, 1);
    finish();
    readPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    CHECK(pixel_is(pixel, 0, 255, 0),
          "returning to layer 1 preserves its independent green contents");

    CHECK(getError() == GL_NO_ERROR,
          "layered slice error queue is clean before whole-level request");
    framebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, texture, 0);
    CHECK(getError() == GL_INVALID_OPERATION,
          "whole-level layered attachment fails closed instead of aliasing layer zero");
    CHECK(checkFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
          "rejected whole-level request preserves the prior concrete layer attachment");
    readPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    CHECK(pixel_is(pixel, 0, 255, 0) && getError() == GL_NO_ERROR,
          "rejected whole-level request leaves layer 1 contents intact");

    bindFramebuffer(GL_FRAMEBUFFER, 0);
    deleteFramebuffers(1, &fbo);
    deleteTextures(1, &texture);
    CHECK(getError() == GL_NO_ERROR, "layered FBO resources close cleanly");
    dlclose(lib);
    printf("\nlayered_fbo_smoke: %s (%d failure%s)\n",
           failures ? "FAIL" : "PASS", failures,
           failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
