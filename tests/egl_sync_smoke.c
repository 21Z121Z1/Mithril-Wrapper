#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <EGL/egl.h>
#include <GL/glcorearb.h>

static int failures = 0;
#define CHECK(c, fmt, ...) do { if (c) printf("ok : " fmt "\n", ##__VA_ARGS__); \
    else { printf("FAIL: " fmt "\n", ##__VA_ARGS__); ++failures; } } while (0)
#define RESOLVE(var, sym) do { var = dlsym(h, sym); CHECK(var != NULL, "%s resolved", sym); } while (0)

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    void* h = dlopen(argv[1], RTLD_NOW | RTLD_GLOBAL);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }

    EGLDisplay (*getDisplay)(EGLNativeDisplayType) = NULL;
    EGLBoolean (*initialize)(EGLDisplay,EGLint*,EGLint*) = NULL;
    EGLBoolean (*bindAPI)(EGLenum) = NULL;
    EGLBoolean (*getConfigs)(EGLDisplay,EGLConfig*,EGLint,EGLint*) = NULL;
    EGLContext (*createContext)(EGLDisplay,EGLConfig,EGLContext,const EGLint*) = NULL;
    EGLSurface (*createPbuffer)(EGLDisplay,EGLConfig,const EGLint*) = NULL;
    EGLBoolean (*makeCurrent)(EGLDisplay,EGLSurface,EGLSurface,EGLContext) = NULL;
    EGLSync (*createSync)(EGLDisplay,EGLenum,const EGLAttrib*) = NULL;
    EGLint (*clientWaitSync)(EGLDisplay,EGLSync,EGLint,EGLTime) = NULL;
    EGLBoolean (*getSyncAttrib)(EGLDisplay,EGLSync,EGLint,EGLAttrib*) = NULL;
    EGLBoolean (*destroySync)(EGLDisplay,EGLSync) = NULL;
    EGLBoolean (*waitClient)(void) = NULL;
    EGLBoolean (*destroySurface)(EGLDisplay,EGLSurface) = NULL;
    EGLBoolean (*destroyContext)(EGLDisplay,EGLContext) = NULL;

    void (*genTextures)(GLsizei,GLuint*) = NULL;
    void (*bindTexture)(GLenum,GLuint) = NULL;
    void (*texImage2D)(GLenum,GLint,GLint,GLsizei,GLsizei,GLint,GLenum,GLenum,const void*) = NULL;
    void (*genFramebuffers)(GLsizei,GLuint*) = NULL;
    void (*bindFramebuffer)(GLenum,GLuint) = NULL;
    void (*framebufferTexture2D)(GLenum,GLenum,GLenum,GLuint,GLint) = NULL;
    GLenum (*checkFramebufferStatus)(GLenum) = NULL;
    void (*clearColor)(GLfloat,GLfloat,GLfloat,GLfloat) = NULL;
    void (*clear)(GLbitfield) = NULL;

    RESOLVE(getDisplay, "eglGetDisplay"); RESOLVE(initialize, "eglInitialize");
    RESOLVE(bindAPI, "eglBindAPI"); RESOLVE(getConfigs, "eglGetConfigs");
    RESOLVE(createContext, "eglCreateContext"); RESOLVE(createPbuffer, "eglCreatePbufferSurface");
    RESOLVE(makeCurrent, "eglMakeCurrent"); RESOLVE(createSync, "eglCreateSync");
    RESOLVE(clientWaitSync, "eglClientWaitSync"); RESOLVE(getSyncAttrib, "eglGetSyncAttrib");
    RESOLVE(destroySync, "eglDestroySync"); RESOLVE(waitClient, "eglWaitClient");
    RESOLVE(destroySurface, "eglDestroySurface"); RESOLVE(destroyContext, "eglDestroyContext");
    RESOLVE(genTextures, "glGenTextures"); RESOLVE(bindTexture, "glBindTexture");
    RESOLVE(texImage2D, "glTexImage2D"); RESOLVE(genFramebuffers, "glGenFramebuffers");
    RESOLVE(bindFramebuffer, "glBindFramebuffer"); RESOLVE(framebufferTexture2D, "glFramebufferTexture2D");
    RESOLVE(checkFramebufferStatus, "glCheckFramebufferStatus"); RESOLVE(clearColor, "glClearColor");
    RESOLVE(clear, "glClear");
    if (failures) return 1;

    EGLDisplay dpy = getDisplay(EGL_DEFAULT_DISPLAY);
    EGLint maj=0,min=0;
    CHECK(dpy != EGL_NO_DISPLAY && initialize(dpy,&maj,&min), "EGL %d.%d initializes", maj, min);
    CHECK(bindAPI(EGL_OPENGL_API), "EGL OpenGL client API bound");
    EGLConfig cfg = NULL; EGLint ncfg = 0;
    CHECK(getConfigs(dpy,&cfg,1,&ncfg) && ncfg > 0 && cfg, "EGL config available");
    const EGLint ctxAttrs[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3, EGL_NONE };
    EGLContext ctx = createContext(dpy,cfg,EGL_NO_CONTEXT,ctxAttrs);
    CHECK(ctx != EGL_NO_CONTEXT, "EGL context created");
    const EGLint pbAttrs[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
    EGLSurface surf = createPbuffer(dpy,cfg,pbAttrs);
    CHECK(surf != EGL_NO_SURFACE, "EGL pbuffer placeholder created");
    CHECK(makeCurrent(dpy,surf,surf,ctx), "EGL context made current");

    GLuint tex=0,fbo=0;
    genTextures(1,&tex); bindTexture(GL_TEXTURE_2D,tex);
    texImage2D(GL_TEXTURE_2D,0,GL_RGBA8,16,16,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    genFramebuffers(1,&fbo); bindFramebuffer(GL_FRAMEBUFFER,fbo);
    framebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
    CHECK(checkFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE, "user FBO complete");
    clearColor(0.25f,0.5f,0.75f,1.0f); clear(GL_COLOR_BUFFER_BIT);

    EGLSync sync = createSync(dpy,EGL_SYNC_FENCE,NULL);
    CHECK(sync != EGL_NO_SYNC, "eglCreateSync inserted a fence after GPU clear");
    EGLAttrib status = EGL_UNSIGNALED;
    CHECK(getSyncAttrib(dpy,sync,EGL_SYNC_STATUS,&status), "eglGetSyncAttrib status query succeeds");
    EGLint wr = clientWaitSync(dpy,sync,EGL_SYNC_FLUSH_COMMANDS_BIT,1000000000ULL);
    CHECK(wr == EGL_CONDITION_SATISFIED, "eglClientWaitSync observes completion (0x%x)", wr);
    status = EGL_UNSIGNALED;
    CHECK(getSyncAttrib(dpy,sync,EGL_SYNC_STATUS,&status) && status == EGL_SIGNALED,
          "EGL fence status is monotonic SIGNALED after wait (0x%lx)", (unsigned long)status);
    CHECK(destroySync(dpy,sync), "EGL sync destroyed");

    // A second command followed by eglWaitClient must be fully complete when it returns.
    clearColor(0.1f,0.2f,0.3f,1.0f); clear(GL_COLOR_BUFFER_BIT);
    CHECK(waitClient(), "eglWaitClient completes prior client API work");
    EGLSync afterWait = createSync(dpy,EGL_SYNC_FENCE,NULL);
    status = EGL_UNSIGNALED;
    CHECK(afterWait != EGL_NO_SYNC && getSyncAttrib(dpy,afterWait,EGL_SYNC_STATUS,&status) &&
          status == EGL_SIGNALED,
          "fence inserted after eglWaitClient is already satisfied (0x%lx)", (unsigned long)status);
    if (afterWait != EGL_NO_SYNC) destroySync(dpy,afterWait);

    makeCurrent(dpy,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    destroySurface(dpy,surf); destroyContext(dpy,ctx);
    dlclose(h);
    printf("EGL SYNC SMOKE: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
