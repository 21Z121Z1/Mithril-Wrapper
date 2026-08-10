/* DirectMetal framebuffer regression smoke.
 * Covers depth-only completeness and selected mip/slice/depth-plane rendering.
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GL_RENDERER 0x1F01
#define GL_NO_ERROR 0
#define GL_NONE 0
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_RGBA 0x1908
#define GL_DEPTH_COMPONENT 0x1902
#define GL_UNSIGNED_BYTE 0x1401
#define GL_FLOAT 0x1406
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_3D 0x806F
#define GL_TEXTURE_2D_ARRAY 0x8C1A
#define GL_RGBA8 0x8058
#define GL_DEPTH_COMPONENT32F 0x8CAC
#define GL_FRAMEBUFFER 0x8D40
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLsizei;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef unsigned char GLubyte;
typedef double GLdouble;

typedef const GLubyte* (*fn_glGetString)(GLenum);
typedef GLenum (*fn_glGetError)(void);
typedef void (*fn_glGenTextures)(GLsizei, GLuint*);
typedef void (*fn_glBindTexture)(GLenum, GLuint);
typedef void (*fn_glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint,
                                 GLenum, GLenum, const void*);
typedef void (*fn_glTexImage3D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei,
                                 GLint, GLenum, GLenum, const void*);
typedef void (*fn_glGenFramebuffers)(GLsizei, GLuint*);
typedef void (*fn_glBindFramebuffer)(GLenum, GLuint);
typedef void (*fn_glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef void (*fn_glFramebufferTextureLayer)(GLenum, GLenum, GLuint, GLint, GLint);
typedef void (*fn_glFramebufferTexture3D)(GLenum, GLenum, GLenum, GLuint, GLint, GLint);
typedef GLenum (*fn_glCheckFramebufferStatus)(GLenum);
typedef void (*fn_glDrawBuffer)(GLenum);
typedef void (*fn_glReadBuffer)(GLenum);
typedef void (*fn_glClearColor)(float, float, float, float);
typedef void (*fn_glClearDepth)(GLdouble);
typedef void (*fn_glClear)(GLbitfield);
typedef void (*fn_glFinish)(void);
typedef void (*fn_glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);

static int failures;
#define CHECK(cond, fmt, ...) do { \
    if (cond) printf("ok  : " fmt "\n", ##__VA_ARGS__); \
    else { printf("FAIL: " fmt "\n", ##__VA_ARGS__); ++failures; } \
} while (0)

static int rgba_eq(const unsigned char p[4], unsigned char r,
                   unsigned char g, unsigned char b, unsigned char a) {
    return abs((int)p[0] - r) <= 2 && abs((int)p[1] - g) <= 2 &&
           abs((int)p[2] - b) <= 2 && abs((int)p[3] - a) <= 2;
}

int main(void) {
    const char* library = getenv("MITHRIL_LIBRARY");
#if defined(__APPLE__)
    if (!library || !*library) library = "./output/libmithril.dylib";
#else
    if (!library || !*library) library = "./output/libmithril.so";
#endif
    void* h = dlopen(library, RTLD_NOW | RTLD_GLOBAL);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }

#define LOAD(type, name) type name = (type)dlsym(h, #name)
    LOAD(fn_glGetString, glGetString);
    LOAD(fn_glGetError, glGetError);
    LOAD(fn_glGenTextures, glGenTextures);
    LOAD(fn_glBindTexture, glBindTexture);
    LOAD(fn_glTexImage2D, glTexImage2D);
    LOAD(fn_glTexImage3D, glTexImage3D);
    LOAD(fn_glGenFramebuffers, glGenFramebuffers);
    LOAD(fn_glBindFramebuffer, glBindFramebuffer);
    LOAD(fn_glFramebufferTexture2D, glFramebufferTexture2D);
    LOAD(fn_glFramebufferTextureLayer, glFramebufferTextureLayer);
    LOAD(fn_glFramebufferTexture3D, glFramebufferTexture3D);
    LOAD(fn_glCheckFramebufferStatus, glCheckFramebufferStatus);
    LOAD(fn_glDrawBuffer, glDrawBuffer);
    LOAD(fn_glReadBuffer, glReadBuffer);
    LOAD(fn_glClearColor, glClearColor);
    LOAD(fn_glClearDepth, glClearDepth);
    LOAD(fn_glClear, glClear);
    LOAD(fn_glFinish, glFinish);
    LOAD(fn_glReadPixels, glReadPixels);
#undef LOAD

    CHECK(glGetString && glGetError && glGenTextures && glBindTexture &&
          glTexImage2D && glTexImage3D && glGenFramebuffers &&
          glBindFramebuffer && glFramebufferTexture2D &&
          glFramebufferTextureLayer && glFramebufferTexture3D &&
          glCheckFramebufferStatus && glDrawBuffer && glReadBuffer &&
          glClearColor && glClearDepth && glClear && glFinish && glReadPixels,
          "all required FBO symbols resolved");
    if (failures) return 1;

    const char* renderer = (const char*)glGetString(GL_RENDERER);
    const char* expected = getenv("MITHRIL_EXPECT_RENDERER");
    CHECK(renderer && (!expected || strstr(renderer, expected)),
          "selected renderer is explicit (%s)", renderer ? renderer : "null");

    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    GLuint depth = 0;
    glGenTextures(1, &depth);
    glBindTexture(GL_TEXTURE_2D, depth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F,
                 16, 16, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                           GL_TEXTURE_2D, depth, 0);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE,
          "depth-only FBO rejects default color draw/read selectors");
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
          "depth-only FBO is complete with draw/read GL_NONE");
    glClearDepth(0.25);
    glClear(GL_DEPTH_BUFFER_BIT);
    glFinish();
    CHECK(glGetError() == GL_NO_ERROR, "depth-only clear submits without GL error");

    /* The following color-subresource cases intentionally test only the selected
     * color image. Detach the 16x16 depth image so it cannot make the later
     * 4x4 framebuffer incomplete for an unrelated dimension mismatch. */
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                           GL_TEXTURE_2D, 0, 0);

    GLuint miptex = 0;
    unsigned char zero8[8 * 8 * 4] = {0};
    unsigned char zero4[4 * 4 * 4] = {0};
    glGenTextures(1, &miptex);
    glBindTexture(GL_TEXTURE_2D, miptex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 8, 8, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, zero8);
    glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 4, 4, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, zero4);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, miptex, 1);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
          "mip-1 color attachment is complete");
    glClearColor(0.f, 0.f, 1.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    unsigned char px[4] = {0};
    glReadPixels(1, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    CHECK(rgba_eq(px, 0, 0, 255, 255),
          "mip-1 render/readback selects level 1 (%u,%u,%u,%u)",
          px[0], px[1], px[2], px[3]);

    GLuint arraytex = 0;
    unsigned char array_zero[4 * 4 * 2 * 4] = {0};
    glGenTextures(1, &arraytex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, arraytex);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, 4, 4, 2, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, array_zero);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                              arraytex, 0, 1);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
          "2D-array layer 1 attachment is complete");
    glClearColor(0.f, 1.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    memset(px, 0, sizeof(px));
    glReadPixels(1, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    CHECK(rgba_eq(px, 0, 255, 0, 255),
          "array render/readback selects slice 1 (%u,%u,%u,%u)",
          px[0], px[1], px[2], px[3]);

    GLuint volume = 0;
    unsigned char volume_zero[4 * 4 * 2 * 4] = {0};
    glGenTextures(1, &volume);
    glBindTexture(GL_TEXTURE_3D, volume);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, 4, 4, 2, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, volume_zero);
    glFramebufferTexture3D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_3D, volume, 0, 1);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
          "3D depth-plane 1 attachment is complete");
    glClearColor(1.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    memset(px, 0, sizeof(px));
    glReadPixels(1, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    CHECK(rgba_eq(px, 255, 0, 0, 255),
          "3D render/readback selects depthPlane 1 (%u,%u,%u,%u)",
          px[0], px[1], px[2], px[3]);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    CHECK(glGetError() == GL_NO_ERROR, "FBO subresource smoke leaves GL_NO_ERROR");
    printf("\ndirectmetal_fbo_smoke: %s (%d failures)\n",
           failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
