/* DirectMetal GL sync vertical smoke.
 *
 * Proves a real GL clear is submitted as native Metal work by glFenceSync,
 * client waits observe command-buffer completion without making ordinary GL
 * submission synchronous, sync queries are live, and deleting an in-flight
 * GL name does not invalidate the native completion callback.
 */

#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_INVALID_ENUM 0x0500
#define GL_INVALID_VALUE 0x0501
#define GL_NO_ERROR 0
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_RENDERER 0x1F01
#define GL_OBJECT_TYPE 0x9112
#define GL_SYNC_CONDITION 0x9113
#define GL_SYNC_STATUS 0x9114
#define GL_SYNC_FLAGS 0x9115
#define GL_SYNC_FENCE 0x9116
#define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#define GL_UNSIGNALED 0x9118
#define GL_SIGNALED 0x9119
#define GL_ALREADY_SIGNALED 0x911A
#define GL_TIMEOUT_EXPIRED 0x911B
#define GL_CONDITION_SATISFIED 0x911C
#define GL_WAIT_FAILED 0x911D
#define GL_SYNC_FLUSH_COMMANDS_BIT 0x00000001
#define GL_TIMEOUT_IGNORED UINT64_MAX

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLboolean;
typedef uint64_t GLuint64;
typedef struct __GLsync* GLsync;

typedef const unsigned char* (*fnGetString)(GLenum);
typedef GLenum (*fnGetError)(void);
typedef void (*fnClearColor)(float, float, float, float);
typedef void (*fnClear)(GLbitfield);
typedef void (*fnReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum,
                             void*);
typedef GLsync (*fnFenceSync)(GLenum, GLbitfield);
typedef void (*fnDeleteSync)(GLsync);
typedef GLboolean (*fnIsSync)(GLsync);
typedef GLenum (*fnClientWaitSync)(GLsync, GLbitfield, GLuint64);
typedef void (*fnWaitSync)(GLsync, GLbitfield, GLuint64);
typedef void (*fnGetSynciv)(GLsync, GLenum, GLsizei, GLsizei*, GLint*);

static int failures;

#define CHECK(condition, ...) do {                                           \
    if (condition) printf("ok  : " __VA_ARGS__);                            \
    else { printf("FAIL: " __VA_ARGS__); ++failures; }                      \
    printf("\n");                                                           \
} while (0)

#define LOAD(type, variable, symbol)                                         \
    type variable = (type)dlsym(library, symbol)

static int pixel_is(const unsigned char pixel[4], int r, int g, int b) {
    return abs((int)pixel[0] - r) <= 3 &&
           abs((int)pixel[1] - g) <= 3 &&
           abs((int)pixel[2] - b) <= 3 && pixel[3] >= 252;
}

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
    LOAD(fnClearColor, clearColor, "glClearColor");
    LOAD(fnClear, clear, "glClear");
    LOAD(fnReadPixels, readPixels, "glReadPixels");
    LOAD(fnFenceSync, fenceSync, "glFenceSync");
    LOAD(fnDeleteSync, deleteSync, "glDeleteSync");
    LOAD(fnIsSync, isSync, "glIsSync");
    LOAD(fnClientWaitSync, clientWaitSync, "glClientWaitSync");
    LOAD(fnWaitSync, waitSync, "glWaitSync");
    LOAD(fnGetSynciv, getSynciv, "glGetSynciv");
    CHECK(getString && getError && clearColor && clear && readPixels &&
              fenceSync && deleteSync && isSync && clientWaitSync &&
              waitSync && getSynciv,
          "required GL sync symbols resolve");
    if (failures) return 1;

    const char* renderer = (const char*)getString(GL_RENDERER);
    CHECK(renderer && strstr(renderer, "DirectMetal"),
          "context is explicitly DirectMetal (%s)", renderer ? renderer : "null");

    CHECK(fenceSync(0, 0) == NULL && getError() == GL_INVALID_ENUM,
          "invalid fence condition is rejected");
    CHECK(fenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 1) == NULL &&
              getError() == GL_INVALID_VALUE,
          "invalid fence flags are rejected");

    clearColor(1.f, 0.f, 0.f, 1.f);
    clear(GL_COLOR_BUFFER_BIT);
    GLsync sync = fenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    CHECK(sync && isSync(sync) == GL_TRUE && getError() == GL_NO_ERROR,
          "fence names a submitted DirectMetal clear");

    GLsizei length = 0;
    GLint value = 0;
    getSynciv(sync, GL_OBJECT_TYPE, 1, &length, &value);
    CHECK(length == 1 && value == GL_SYNC_FENCE,
          "GL_OBJECT_TYPE reports GL_SYNC_FENCE");
    getSynciv(sync, GL_SYNC_CONDITION, 1, &length, &value);
    CHECK(value == GL_SYNC_GPU_COMMANDS_COMPLETE,
          "GL_SYNC_CONDITION reports GPU completion");
    getSynciv(sync, GL_SYNC_FLAGS, 1, &length, &value);
    CHECK(value == 0, "GL_SYNC_FLAGS preserves creation flags");
    getSynciv(sync, GL_SYNC_STATUS, 1, &length, &value);
    CHECK(value == GL_SIGNALED || value == GL_UNSIGNALED,
          "GL_SYNC_STATUS exposes native completion state");

    GLenum first_wait = clientWaitSync(sync, 0, 0);
    CHECK(first_wait == GL_ALREADY_SIGNALED ||
              first_wait == GL_CONDITION_SATISFIED ||
              first_wait == GL_TIMEOUT_EXPIRED,
          "zero-time client wait is nonblocking (0x%x)", first_wait);
    GLenum waited = clientWaitSync(sync, GL_SYNC_FLUSH_COMMANDS_BIT,
                                   GL_TIMEOUT_IGNORED);
    CHECK(waited == GL_ALREADY_SIGNALED || waited == GL_CONDITION_SATISFIED,
          "infinite client wait observes Metal completion (0x%x)", waited);
    getSynciv(sync, GL_SYNC_STATUS, 1, &length, &value);
    CHECK(value == GL_SIGNALED, "completed fence becomes GL_SIGNALED");
    waitSync(sync, 0, GL_TIMEOUT_IGNORED);
    CHECK(getError() == GL_NO_ERROR,
          "same-queue server wait preserves command ordering without CPU idle");

    unsigned char pixel[4] = {0, 0, 0, 0};
    readPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    CHECK(pixel_is(pixel, 255, 0, 0),
          "fenced DirectMetal clear reaches framebuffer readback");

    deleteSync(sync);
    CHECK(isSync(sync) == GL_FALSE && getError() == GL_NO_ERROR,
          "deleted GLsync name is no longer live");
    CHECK(clientWaitSync(sync, 0, 0) == GL_WAIT_FAILED &&
              getError() == GL_INVALID_VALUE,
          "waiting on a deleted sync is rejected safely");

    // The backend completion block owns its completion state independently of
    // the frontend name, so immediate name deletion cannot release it early.
    clearColor(0.f, 1.f, 0.f, 1.f);
    clear(GL_COLOR_BUFFER_BIT);
    GLsync deleted_in_flight = fenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    deleteSync(deleted_in_flight);
    readPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    CHECK(pixel_is(pixel, 0, 255, 0) && getError() == GL_NO_ERROR,
          "deleting an in-flight fence retains native completion safely");

    dlclose(library);
    printf("\nsync_smoke: %s (%d failure%s)\n",
           failures ? "FAIL" : "PASS", failures,
           failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
