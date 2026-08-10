#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GL_RENDERER 0x1F01
#define GL_NO_ERROR 0
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_TEXTURE_2D 0x0DE1
#define GL_RGBA8 0x8058
#define GL_FRAMEBUFFER 0x8D40
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_COLOR_ATTACHMENT0 0x8CE0

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLsizei;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef unsigned char GLubyte;

typedef const GLubyte* (*fn_glGetString)(GLenum);
typedef GLenum (*fn_glGetError)(void);
typedef void (*fn_glGenTextures)(GLsizei, GLuint*);
typedef void (*fn_glBindTexture)(GLenum, GLuint);
typedef void (*fn_glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
typedef void (*fn_glGenFramebuffers)(GLsizei, GLuint*);
typedef void (*fn_glBindFramebuffer)(GLenum, GLuint);
typedef void (*fn_glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum (*fn_glCheckFramebufferStatus)(GLenum);
typedef void (*fn_glDrawBuffer)(GLenum);
typedef void (*fn_glReadBuffer)(GLenum);
typedef void (*fn_glClearColor)(float, float, float, float);
typedef void (*fn_glClear)(GLbitfield);
typedef void (*fn_glFinish)(void);
typedef void (*fn_glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);

static int near_byte(unsigned char got, int expected) {
    return abs((int)got - expected) <= 2;
}

#define REQUIRE(cond, fmt, ...) do { \
    if (!(cond)) { \
        fprintf(stderr, "HOSTED_DIRECTMETAL_FBO_PROBE_FAIL: " fmt "\n", ##__VA_ARGS__); \
        return 1; \
    } \
} while (0)

int main(void) {
    const char* library = getenv("MITHRIL_LIBRARY");
    if (!library || !*library) library = "./output/libmithril.dylib";
    void* h = dlopen(library, RTLD_NOW | RTLD_GLOBAL);
    REQUIRE(h != NULL, "dlopen failed: %s", dlerror());

#define LOAD(type, name) type name = (type)dlsym(h, #name)
    LOAD(fn_glGetString, glGetString);
    LOAD(fn_glGetError, glGetError);
    LOAD(fn_glGenTextures, glGenTextures);
    LOAD(fn_glBindTexture, glBindTexture);
    LOAD(fn_glTexImage2D, glTexImage2D);
    LOAD(fn_glGenFramebuffers, glGenFramebuffers);
    LOAD(fn_glBindFramebuffer, glBindFramebuffer);
    LOAD(fn_glFramebufferTexture2D, glFramebufferTexture2D);
    LOAD(fn_glCheckFramebufferStatus, glCheckFramebufferStatus);
    LOAD(fn_glDrawBuffer, glDrawBuffer);
    LOAD(fn_glReadBuffer, glReadBuffer);
    LOAD(fn_glClearColor, glClearColor);
    LOAD(fn_glClear, glClear);
    LOAD(fn_glFinish, glFinish);
    LOAD(fn_glReadPixels, glReadPixels);
#undef LOAD

    REQUIRE(glGetString && glGetError && glGenTextures && glBindTexture && glTexImage2D &&
            glGenFramebuffers && glBindFramebuffer && glFramebufferTexture2D &&
            glCheckFramebufferStatus && glDrawBuffer && glReadBuffer && glClearColor &&
            glClear && glFinish && glReadPixels, "required GL entry points are missing");

    const char* renderer = (const char*)glGetString(GL_RENDERER);
    const char* expected = getenv("MITHRIL_EXPECT_RENDERER");
    REQUIRE(renderer != NULL, "GL_RENDERER is null");
    REQUIRE(!expected || strstr(renderer, expected), "unexpected renderer: %s", renderer);
    printf("renderer=%s\n", renderer);

    GLuint texture = 0;
    GLuint fbo = 0;
    unsigned char zero[8 * 8 * 4] = {0};
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, zero);

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    REQUIRE(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
            "base-level RGBA8 FBO is incomplete");

    glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    REQUIRE(glGetError() == GL_NO_ERROR, "GL error after clear/finish");

    unsigned char pixel[4] = {0, 0, 0, 0};
    glReadPixels(3, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    REQUIRE(glGetError() == GL_NO_ERROR, "GL error after readback");
    REQUIRE(near_byte(pixel[0], 64) && near_byte(pixel[1], 128) &&
            near_byte(pixel[2], 191) && near_byte(pixel[3], 255),
            "readback mismatch: got (%u,%u,%u,%u)",
            pixel[0], pixel[1], pixel[2], pixel[3]);

    printf("HOSTED_DIRECTMETAL_FBO_PROBE_PASS renderer=%s rgba=(%u,%u,%u,%u)\n",
           renderer, pixel[0], pixel[1], pixel[2], pixel[3]);
    return 0;
}
