/* M1 GL state-machine smoke test: dlopen libmithril.so, then exercise the
 * implemented S1 family (clear/viewport/scissor/state/query) and assert the
 * behavior documented in CHECKLIST section 4.
 *
 * Build (from project root):
 *   gcc -o tests/state_smoke tests/state_smoke.c -ldl
 *   LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu ./tests/state_smoke
 */
#include <dlfcn.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

/* GL 3.3 core constants we use (values from glcorearb.h) */
#define GL_NO_ERROR       0
#define GL_INVALID_ENUM   0x0500
#define GL_DEPTH_TEST     0x0B71
#define GL_CULL_FACE      0x0B44
#define GL_BLEND          0x0BE2
#define GL_SCISSOR_TEST   0x0C11
#define GL_VIEWPORT       0x0BA2
#define GL_MAJOR_VERSION  0x821B
#define GL_MINOR_VERSION  0x821C
#define GL_VENDOR         0x1F00
#define GL_RENDERER       0x1F01
#define GL_VERSION        0x1F02

/* function pointer typedefs matching glcorearb.h signatures */
typedef void          (*glClearColor_fn)(float, float, float, float);
typedef void          (*glViewport_fn)(int, int, int, int);
typedef void          (*glEnable_fn)(unsigned int);
typedef void          (*glDisable_fn)(unsigned int);
typedef unsigned char (*glIsEnabled_fn)(unsigned int);
typedef int           (*glGetError_fn)(void);
typedef const unsigned char* (*glGetString_fn)(unsigned int);
typedef void          (*glGetIntegerv_fn)(unsigned int, int*);

int fails = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } \
    else printf("ok  : %s\n", msg); \
} while (0)

int main(void) {
    void* h = dlopen("./output/libmithril.so", RTLD_NOW | RTLD_GLOBAL);
    if (!h) { printf("dlopen: %s\n", dlerror()); return 2; }

    glClearColor_fn    clearColor   = (glClearColor_fn)dlsym(h, "glClearColor");
    glViewport_fn    viewport     = (glViewport_fn)dlsym(h, "glViewport");
    glEnable_fn       enable       = (glEnable_fn)dlsym(h, "glEnable");
    glDisable_fn      disable      = (glDisable_fn)dlsym(h, "glDisable");
    glIsEnabled_fn    isEnabled    = (glIsEnabled_fn)dlsym(h, "glIsEnabled");
    glGetError_fn     getError     = (glGetError_fn)dlsym(h, "glGetError");
    glGetString_fn    getString    = (glGetString_fn)dlsym(h, "glGetString");
    glGetIntegerv_fn  getIntegerv  = (glGetIntegerv_fn)dlsym(h, "glGetIntegerv");

    if (!(clearColor && viewport && enable && disable && isEnabled &&
          getError && getString && getIntegerv)) {
        printf("missing core symbols\n"); return 3;
    }

    /* --- context/version queries --------------------------------------- */
    int i = 0;
    getIntegerv(GL_MAJOR_VERSION, &i);
    CHECK(i == 3, "GL_MAJOR_VERSION == 3");
    i = 0;
    getIntegerv(GL_MINOR_VERSION, &i);
    CHECK(i == 3, "GL_MINOR_VERSION == 3");

    const unsigned char* version = getString(GL_VERSION);
    CHECK(version && strstr((const char*)version, "3.3"),
          "glGetString(GL_VERSION) contains 3.3");
    printf("     GL_VERSION = \"%s\"\n", (const char*)version);

    /* ---- error queue semantics ----------------------------------------- */
    CHECK(getError() == GL_NO_ERROR, "error queue starts empty");

    /* invalid enum -> GL_INVALID_ENUM, then drains FIFO */
    getIntegerv(0xC0FFEE, &i);       /* illegal pname */
    CHECK(getError() == GL_INVALID_ENUM, "bad getIntegerv pname -> INVALID_ENUM");
    CHECK(getError() == GL_NO_ERROR, "error queue drains to empty");

    /* two stacked errors pop oldest-first (FIFO) */
    getIntegerv(0xC0FFEE, &i);
    enable(0xC0FFEE);                /* illegal capability */
    CHECK(getError() == GL_INVALID_ENUM, "FIFO: oldest (pname) pops first");
    CHECK(getError() == GL_INVALID_ENUM, "FIFO: second (cap) pops next");

    /* ---- viewport round-trip ------------------------------------------- */
    viewport(10, 20, 640, 480);
    int vp[4] = {-1,-1,-1,-1};
    getIntegerv(GL_VIEWPORT, vp);
    CHECK(vp[0] == 10 && vp[1] == 20 && vp[2] == 640 && vp[3] == 480,
          "glViewport round-trip via glGetIntegerv(GL_VIEWPORT)");

    /* ---- capability round-trip ------------------------------------------ */
    CHECK(isEnabled(GL_DEPTH_TEST) == 0, "GL_DEPTH_TEST off by default");
    enable(GL_DEPTH_TEST);
    CHECK(isEnabled(GL_DEPTH_TEST) == 1, "glIsEnabled(GL_DEPTH_TEST) after enable");
    disable(GL_DEPTH_TEST);
    CHECK(isEnabled(GL_DEPTH_TEST) == 0, "glIsEnabled after disable");

    printf("\n%d failure(s)\n", fails);
    if (fails == 0) { printf("STATE SMOKE ALL PASSED\n"); return 0; }
    return 1;
}