/* DirectMetal incomplete-framebuffer regression smoke.
 *
 * OpenGL requires rendering/readback commands against an incomplete framebuffer
 * to fail with GL_INVALID_FRAMEBUFFER_OPERATION and ignore the command.  This
 * test deliberately leaves a depth-only FBO on the default color draw/read
 * selectors, then exercises every implemented frontend path that can render
 * to or read from the current framebuffer.  The process surviving the sequence
 * is part of the regression contract: invalid GL state must never reach Metal
 * as a crash-prone render target.
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GL_NO_ERROR 0
#define GL_NONE 0
#define GL_INVALID_FRAMEBUFFER_OPERATION 0x0506
#define GL_LINE_LOOP 0x0002
#define GL_TRIANGLES 0x0004
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_RGBA 0x1908
#define GL_DEPTH_COMPONENT 0x1902
#define GL_UNSIGNED_BYTE 0x1401
#define GL_FLOAT 0x1406
#define GL_TEXTURE_2D 0x0DE1
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_RGBA8 0x8058
#define GL_DEPTH_COMPONENT32F 0x8CAC
#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_NEAREST 0x2600

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLubyte;

typedef GLenum (*fn_glGetError)(void);
typedef void (*fn_glGenTextures)(GLsizei, GLuint*);
typedef void (*fn_glBindTexture)(GLenum, GLuint);
typedef void (*fn_glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei,
                                 GLint, GLenum, GLenum, const void*);
typedef void (*fn_glGenFramebuffers)(GLsizei, GLuint*);
typedef void (*fn_glBindFramebuffer)(GLenum, GLuint);
typedef void (*fn_glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum (*fn_glCheckFramebufferStatus)(GLenum);
typedef void (*fn_glDrawBuffer)(GLenum);
typedef void (*fn_glReadBuffer)(GLenum);
typedef void (*fn_glClear)(GLbitfield);
typedef void (*fn_glDrawArrays)(GLenum, GLint, GLsizei);
typedef void (*fn_glDrawElements)(GLenum, GLsizei, GLenum, const void*);
typedef void (*fn_glMultiDrawArrays)(GLenum, const GLint*, const GLsizei*, GLsizei);
typedef void (*fn_glMultiDrawElements)(GLenum, const GLsizei*, GLenum, const void* const*, GLsizei);
typedef void (*fn_glGenBuffers)(GLsizei, GLuint*);
typedef void (*fn_glBindBuffer)(GLenum, GLuint);
typedef void (*fn_glBufferData)(GLenum, long, const void*, GLenum);
typedef void (*fn_glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum,
                                 void*);
typedef void (*fn_glCopyTexImage2D)(GLenum, GLint, GLenum, GLint, GLint,
                                     GLsizei, GLsizei, GLint);
typedef void (*fn_glBlitFramebuffer)(GLint, GLint, GLint, GLint, GLint, GLint,
                                      GLint, GLint, GLbitfield, GLenum);

static int failures;
#define CHECK(cond, fmt, ...) do { \
    if (cond) printf("ok  : " fmt "\n", ##__VA_ARGS__); \
    else { printf("FAIL: " fmt "\n", ##__VA_ARGS__); ++failures; } \
} while (0)

static int expect_error(fn_glGetError get_error, GLenum expected,
                        const char* operation) {
    GLenum observed = get_error();
    if (observed == expected) {
        printf("ok  : %s -> GL_INVALID_FRAMEBUFFER_OPERATION\n", operation);
        return 1;
    }
    printf("FAIL: %s error=0x%x expected=0x%x\n", operation,
           (unsigned)observed, (unsigned)expected);
    ++failures;
    return 0;
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
    LOAD(fn_glClear, glClear);
    LOAD(fn_glDrawArrays, glDrawArrays);
    LOAD(fn_glDrawElements, glDrawElements);
    LOAD(fn_glMultiDrawArrays, glMultiDrawArrays);
    LOAD(fn_glMultiDrawElements, glMultiDrawElements);
    LOAD(fn_glGenBuffers, glGenBuffers);
    LOAD(fn_glBindBuffer, glBindBuffer);
    LOAD(fn_glBufferData, glBufferData);
    LOAD(fn_glReadPixels, glReadPixels);
    LOAD(fn_glCopyTexImage2D, glCopyTexImage2D);
    LOAD(fn_glBlitFramebuffer, glBlitFramebuffer);
#undef LOAD

    CHECK(glGetError && glGenTextures && glBindTexture && glTexImage2D &&
          glGenFramebuffers && glBindFramebuffer && glFramebufferTexture2D &&
          glCheckFramebufferStatus && glDrawBuffer && glReadBuffer && glClear &&
          glDrawArrays && glDrawElements && glMultiDrawArrays &&
          glMultiDrawElements && glGenBuffers && glBindBuffer && glBufferData &&
          glReadPixels && glCopyTexImage2D && glBlitFramebuffer,
          "all incomplete-FBO regression symbols resolved");
    if (failures) return 1;

    GLuint depth = 0, fbo = 0;
    glGenTextures(1, &depth);
    glBindTexture(GL_TEXTURE_2D, depth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, 16, 16, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                           GL_TEXTURE_2D, depth, 0);
    CHECK(glGetError() == GL_NO_ERROR, "depth-only setup leaves GL_NO_ERROR");
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE,
          "depth-only FBO is incomplete on default color selectors");

    /* Rendering commands must validate completeness even when no fragments
     * can be produced (zero count / zero-sized read / zero-area blit). */
    glClear(GL_DEPTH_BUFFER_BIT);
    expect_error(glGetError, GL_INVALID_FRAMEBUFFER_OPERATION, "glClear");
    glClear(0);
    expect_error(glGetError, GL_INVALID_FRAMEBUFFER_OPERATION, "glClear(zero-mask)");

    glDrawArrays(GL_TRIANGLES, 0, 3);
    expect_error(glGetError, GL_INVALID_FRAMEBUFFER_OPERATION,
                 "glDrawArrays(nonzero)");
    glDrawArrays(GL_TRIANGLES, 0, 0);
    expect_error(glGetError, GL_INVALID_FRAMEBUFFER_OPERATION,
                 "glDrawArrays(zero-count)");
    glDrawArrays(GL_LINE_LOOP, 0, 0);
    expect_error(glGetError, GL_INVALID_FRAMEBUFFER_OPERATION,
                 "glDrawArrays(line-loop zero-count)");

    GLuint ebo = 0;
    glGenBuffers(1, &ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, 0, NULL, GL_STATIC_DRAW);
    glDrawElements(GL_TRIANGLES, 0, GL_UNSIGNED_BYTE, (const void*)0);
    expect_error(glGetError, GL_INVALID_FRAMEBUFFER_OPERATION,
                 "glDrawElements(zero-count)");
    glMultiDrawArrays(GL_TRIANGLES, NULL, NULL, 0);
    expect_error(glGetError, GL_INVALID_FRAMEBUFFER_OPERATION,
                 "glMultiDrawArrays(zero-drawcount)");
    glMultiDrawElements(GL_TRIANGLES, NULL, GL_UNSIGNED_BYTE, NULL, 0);
    expect_error(glGetError, GL_INVALID_FRAMEBUFFER_OPERATION,
                 "glMultiDrawElements(zero-drawcount)");

    unsigned char sentinel[4] = {17, 34, 51, 68};
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, sentinel);
    expect_error(glGetError, GL_INVALID_FRAMEBUFFER_OPERATION,
                 "glReadPixels(nonzero)");
    CHECK(sentinel[0] == 17 && sentinel[1] == 34 &&
          sentinel[2] == 51 && sentinel[3] == 68,
          "failed readback leaves destination untouched");
    glReadPixels(0, 0, 0, 0, GL_RGBA, GL_UNSIGNED_BYTE, sentinel);
    expect_error(glGetError, GL_INVALID_FRAMEBUFFER_OPERATION,
                 "glReadPixels(zero-area)");

    GLuint copied = 0;
    glGenTextures(1, &copied);
    glBindTexture(GL_TEXTURE_2D, copied);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 0, 0, 4, 4, 0);
    expect_error(glGetError, GL_INVALID_FRAMEBUFFER_OPERATION,
                 "glCopyTexImage2D");

    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
    glBlitFramebuffer(0, 0, 4, 4, 0, 0, 4, 4,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    expect_error(glGetError, GL_INVALID_FRAMEBUFFER_OPERATION,
                 "glBlitFramebuffer(nonzero)");
    glBlitFramebuffer(0, 0, 0, 0, 0, 0, 0, 0,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    expect_error(glGetError, GL_INVALID_FRAMEBUFFER_OPERATION,
                 "glBlitFramebuffer(zero-area)");

    /* The exact same attachment becomes complete once the legal depth-only
     * draw/read selectors are selected. This distinguishes an invalid test
     * harness from a broken depth attachment implementation. */
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
          "depth-only FBO becomes complete with draw/read GL_NONE");
    CHECK(glGetError() == GL_NO_ERROR,
          "legal depth-only selector transition leaves GL_NO_ERROR");

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    dlclose(h);

    printf("\ndirectmetal_incomplete_fbo_smoke: %s (%d failures)\n",
           failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
