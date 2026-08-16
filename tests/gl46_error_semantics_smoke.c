/* Behavioral oracle for core OpenGL error queue semantics. */
#include <dlfcn.h>
#include <stdio.h>
#include <GL/glcorearb.h>

typedef const GLubyte* (*getString_t)(GLenum);
typedef GLenum (*getError_t)(void);
typedef void (*bindTexture_t)(GLenum, GLuint);

#define LOAD(type, var, name) \
    type var = (type)dlsym(h, name); \
    if (!(var)) { fprintf(stderr, "missing %s\n", name); return 3; }

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s /path/to/libmithril.dylib\n", argv[0]);
        return 2;
    }
    void* h = dlopen(argv[1], RTLD_NOW | RTLD_GLOBAL);
    if (!h) {
        fprintf(stderr, "dlopen: %s\n", dlerror());
        return 2;
    }
    LOAD(getString_t, getString, "glGetString");
    LOAD(getError_t, getError, "glGetError");
    LOAD(bindTexture_t, bindTexture, "glBindTexture");

    const char* version = (const char*)getString(GL_VERSION);
    if (!version || !*version) {
        fprintf(stderr, "Mithril did not initialize\n");
        return 1;
    }

    /* Drain any initialization diagnostics so this test has an exact queue
     * boundary. A well-formed fresh context should already be empty. */
    for (int i = 0; i < 32 && getError() != GL_NO_ERROR; ++i) {}

    bindTexture(0xDEADBEEFu, 0);
    GLenum first = getError();
    GLenum second = getError();
    printf("GL46 ERROR SEMANTICS first=0x%04x second=0x%04x\n",
           (unsigned)first, (unsigned)second);
    if (first != GL_INVALID_ENUM) {
        fprintf(stderr, "expected GL_INVALID_ENUM, got 0x%04x\n", (unsigned)first);
        return 1;
    }
    if (second != GL_NO_ERROR) {
        fprintf(stderr, "expected queue to be empty after consuming one error, got 0x%04x\n",
                (unsigned)second);
        return 1;
    }
    puts("GL46 ERROR SEMANTICS ALL PASSED");
    dlclose(h);
    return 0;
}
