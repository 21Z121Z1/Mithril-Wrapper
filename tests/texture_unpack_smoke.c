/*
 * texture_unpack_smoke.c
 *
 * Regression for OpenGL pixel-unpack addressing on DirectVulkan.
 * Upload a 2x2 rectangle from an 8x8 RGBA source while using non-default
 * GL_UNPACK_ROW_LENGTH / GL_UNPACK_SKIP_PIXELS / GL_UNPACK_SKIP_ROWS.
 * A backend that only honors GL_UNPACK_ALIGNMENT will copy the wrong texels.
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/glcorearb.h>

typedef void (*genTextures_fn)(GLsizei, GLuint*);
typedef void (*bindTexture_fn)(GLenum, GLuint);
typedef void (*texImage2D_fn)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
typedef void (*texSubImage2D_fn)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*);
typedef void (*texParameteri_fn)(GLenum, GLenum, GLint);
typedef void (*pixelStorei_fn)(GLenum, GLint);
typedef void (*genFramebuffers_fn)(GLsizei, GLuint*);
typedef void (*bindFramebuffer_fn)(GLenum, GLuint);
typedef void (*framebufferTexture2D_fn)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum (*checkFramebufferStatus_fn)(GLenum);
typedef void (*readPixels_fn)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
typedef void (*finish_fn)(void);
typedef GLenum (*getError_fn)(void);
typedef const GLubyte* (*getString_fn)(GLenum);

#define RESOLVE(var, type, sym) do { \
    var = (type)dlsym(h, sym); \
    if (!(var)) { fprintf(stderr, "missing symbol %s\n", sym); return 3; } \
} while (0)

static void set_pixel(uint8_t* src, int x, int y) {
    uint8_t* p = src + ((size_t)y * 8u + (size_t)x) * 4u;
    p[0] = (uint8_t)(10 + x * 17);
    p[1] = (uint8_t)(20 + y * 19);
    p[2] = (uint8_t)(30 + x * 5 + y * 7);
    p[3] = 255;
}

static int expect_pixel(const uint8_t* actual, int sx, int sy, int index) {
    const uint8_t expected[4] = {
        (uint8_t)(10 + sx * 17),
        (uint8_t)(20 + sy * 19),
        (uint8_t)(30 + sx * 5 + sy * 7),
        255
    };
    if (memcmp(actual, expected, 4) != 0) {
        fprintf(stderr,
                "pixel[%d] mismatch: got=(%u,%u,%u,%u) expected=(%u,%u,%u,%u)\n",
                index,
                actual[0], actual[1], actual[2], actual[3],
                expected[0], expected[1], expected[2], expected[3]);
        return 0;
    }
    return 1;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s /path/to/libmithril.dylib\n", argv[0]);
        return 2;
    }
    void* h = dlopen(argv[1], RTLD_NOW | RTLD_GLOBAL);
    if (!h) {
        fprintf(stderr, "dlopen: %s\n", dlerror());
        return 2;
    }

    genTextures_fn genTextures; bindTexture_fn bindTexture; texImage2D_fn texImage2D;
    texSubImage2D_fn texSubImage2D; texParameteri_fn texParameteri; pixelStorei_fn pixelStorei;
    genFramebuffers_fn genFramebuffers; bindFramebuffer_fn bindFramebuffer;
    framebufferTexture2D_fn framebufferTexture2D; checkFramebufferStatus_fn checkFramebufferStatus;
    readPixels_fn readPixels; finish_fn finish; getError_fn getError; getString_fn getString;

    RESOLVE(genTextures, genTextures_fn, "glGenTextures");
    RESOLVE(bindTexture, bindTexture_fn, "glBindTexture");
    RESOLVE(texImage2D, texImage2D_fn, "glTexImage2D");
    RESOLVE(texSubImage2D, texSubImage2D_fn, "glTexSubImage2D");
    RESOLVE(texParameteri, texParameteri_fn, "glTexParameteri");
    RESOLVE(pixelStorei, pixelStorei_fn, "glPixelStorei");
    RESOLVE(genFramebuffers, genFramebuffers_fn, "glGenFramebuffers");
    RESOLVE(bindFramebuffer, bindFramebuffer_fn, "glBindFramebuffer");
    RESOLVE(framebufferTexture2D, framebufferTexture2D_fn, "glFramebufferTexture2D");
    RESOLVE(checkFramebufferStatus, checkFramebufferStatus_fn, "glCheckFramebufferStatus");
    RESOLVE(readPixels, readPixels_fn, "glReadPixels");
    RESOLVE(finish, finish_fn, "glFinish");
    RESOLVE(getError, getError_fn, "glGetError");
    RESOLVE(getString, getString_fn, "glGetString");

    printf("backend: %s\n", getString(GL_VERSION));

    uint8_t src[8 * 8 * 4];
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) set_pixel(src, x, y);
    }

    GLuint tex = 0, fbo = 0;
    genTextures(1, &tex);
    bindTexture(GL_TEXTURE_2D, tex);
    texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    texImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    pixelStorei(GL_UNPACK_ALIGNMENT, 1);
    pixelStorei(GL_UNPACK_ROW_LENGTH, 8);
    pixelStorei(GL_UNPACK_SKIP_PIXELS, 2);
    pixelStorei(GL_UNPACK_SKIP_ROWS, 3);
    texSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, src);

    /* Restore defaults so later operations cannot accidentally depend on them. */
    pixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    pixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    pixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    pixelStorei(GL_UNPACK_ALIGNMENT, 4);

    GLenum upload_err = getError();
    genFramebuffers(1, &fbo);
    bindFramebuffer(GL_FRAMEBUFFER, fbo);
    framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (checkFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "FBO incomplete\n");
        return 4;
    }

    finish();
    uint8_t pixels[2 * 2 * 4] = {0};
    readPixels(0, 0, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    GLenum final_err = getError();

    int ok = upload_err == GL_NO_ERROR && final_err == GL_NO_ERROR;
    ok &= expect_pixel(pixels + 0, 2, 3, 0);
    ok &= expect_pixel(pixels + 4, 3, 3, 1);
    ok &= expect_pixel(pixels + 8, 2, 4, 2);
    ok &= expect_pixel(pixels + 12, 3, 4, 3);

    printf("upload_err=0x%04x final_err=0x%04x\n",
           (unsigned)upload_err, (unsigned)final_err);
    if (!ok) {
        fprintf(stderr, "TEXTURE UNPACK SMOKE FAILED\n");
        return 1;
    }
    printf("TEXTURE UNPACK SMOKE PASSED\n");
    return 0;
}
