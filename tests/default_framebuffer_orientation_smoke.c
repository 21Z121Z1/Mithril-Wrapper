/*
 * default_framebuffer_orientation_smoke.c
 *
 * Hard gate for OpenGL lower-left window semantics on the Vulkan/MoltenVK
 * default framebuffer. The shader paints fragments originating from positive
 * clip-space Y red and negative clip-space Y blue. With correct GL semantics,
 * glReadPixels at a high Y coordinate must see red and a low Y coordinate
 * must see blue. A vertically inverted swapchain path swaps these results.
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>
#include <GL/glcorearb.h>

#define LOAD(handle, var, type, sym) do { \
    var = (type)dlsym(handle, sym); \
    if (!(var)) { fprintf(stderr, "missing symbol %s\n", sym); return 3; } \
} while (0)

static int is_red(const unsigned char p[4]) {
    return p[0] > 180 && p[1] < 80 && p[2] < 80 && p[3] > 200;
}
static int is_blue(const unsigned char p[4]) {
    return p[2] > 180 && p[0] < 80 && p[1] < 80 && p[3] > 200;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s /path/to/libmithril.dylib\n", argv[0]); return 2; }
    void* h = dlopen(argv[1], RTLD_NOW | RTLD_GLOBAL);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }

    EGLDisplay (*eglGetDisplay_)(EGLNativeDisplayType);
    EGLBoolean (*eglInitialize_)(EGLDisplay,EGLint*,EGLint*);
    EGLBoolean (*eglBindAPI_)(EGLenum);
    EGLBoolean (*eglGetConfigs_)(EGLDisplay,EGLConfig*,EGLint,EGLint*);
    EGLContext (*eglCreateContext_)(EGLDisplay,EGLConfig,EGLContext,const EGLint*);
    EGLSurface (*eglCreatePbufferSurface_)(EGLDisplay,EGLConfig,const EGLint*);
    EGLBoolean (*eglMakeCurrent_)(EGLDisplay,EGLSurface,EGLSurface,EGLContext);
    LOAD(h, eglGetDisplay_, EGLDisplay (*)(EGLNativeDisplayType), "eglGetDisplay");
    LOAD(h, eglInitialize_, EGLBoolean (*)(EGLDisplay,EGLint*,EGLint*), "eglInitialize");
    LOAD(h, eglBindAPI_, EGLBoolean (*)(EGLenum), "eglBindAPI");
    LOAD(h, eglGetConfigs_, EGLBoolean (*)(EGLDisplay,EGLConfig*,EGLint,EGLint*), "eglGetConfigs");
    LOAD(h, eglCreateContext_, EGLContext (*)(EGLDisplay,EGLConfig,EGLContext,const EGLint*), "eglCreateContext");
    LOAD(h, eglCreatePbufferSurface_, EGLSurface (*)(EGLDisplay,EGLConfig,const EGLint*), "eglCreatePbufferSurface");
    LOAD(h, eglMakeCurrent_, EGLBoolean (*)(EGLDisplay,EGLSurface,EGLSurface,EGLContext), "eglMakeCurrent");

    EGLDisplay dpy = eglGetDisplay_(EGL_DEFAULT_DISPLAY);
    EGLint maj=0,min=0,ncfg=0;
    if (dpy == EGL_NO_DISPLAY || !eglInitialize_(dpy,&maj,&min) || !eglBindAPI_(EGL_OPENGL_API)) return 4;
    EGLConfig cfg = NULL;
    if (!eglGetConfigs_(dpy,&cfg,1,&ncfg) || ncfg < 1 || !cfg) return 5;
    const EGLint ca[] = {EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,EGL_NONE};
    const EGLint pa[] = {EGL_WIDTH,64,EGL_HEIGHT,64,EGL_NONE};
    EGLContext ctx = eglCreateContext_(dpy,cfg,EGL_NO_CONTEXT,ca);
    EGLSurface surf = eglCreatePbufferSurface_(dpy,cfg,pa);
    if (ctx == EGL_NO_CONTEXT || surf == EGL_NO_SURFACE || !eglMakeCurrent_(dpy,surf,surf,ctx)) return 6;

    GLuint (*createShader)(GLenum); void (*shaderSource)(GLuint,GLsizei,const GLchar* const*,const GLint*);
    void (*compileShader)(GLuint); void (*getShaderiv)(GLuint,GLenum,GLint*);
    GLuint (*createProgram)(void); void (*attachShader)(GLuint,GLuint); void (*linkProgram)(GLuint);
    void (*getProgramiv)(GLuint,GLenum,GLint*); void (*useProgram)(GLuint);
    void (*genVertexArrays)(GLsizei,GLuint*); void (*bindVertexArray)(GLuint);
    void (*viewport)(GLint,GLint,GLsizei,GLsizei); void (*disable)(GLenum);
    void (*drawArrays)(GLenum,GLint,GLsizei); void (*finish)(void);
    void (*readPixels)(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void*);
    GLenum (*getError)(void); const GLubyte* (*getString)(GLenum);
    LOAD(h, createShader, GLuint (*)(GLenum), "glCreateShader");
    LOAD(h, shaderSource, void (*)(GLuint,GLsizei,const GLchar* const*,const GLint*), "glShaderSource");
    LOAD(h, compileShader, void (*)(GLuint), "glCompileShader");
    LOAD(h, getShaderiv, void (*)(GLuint,GLenum,GLint*), "glGetShaderiv");
    LOAD(h, createProgram, GLuint (*)(void), "glCreateProgram");
    LOAD(h, attachShader, void (*)(GLuint,GLuint), "glAttachShader");
    LOAD(h, linkProgram, void (*)(GLuint), "glLinkProgram");
    LOAD(h, getProgramiv, void (*)(GLuint,GLenum,GLint*), "glGetProgramiv");
    LOAD(h, useProgram, void (*)(GLuint), "glUseProgram");
    LOAD(h, genVertexArrays, void (*)(GLsizei,GLuint*), "glGenVertexArrays");
    LOAD(h, bindVertexArray, void (*)(GLuint), "glBindVertexArray");
    LOAD(h, viewport, void (*)(GLint,GLint,GLsizei,GLsizei), "glViewport");
    LOAD(h, disable, void (*)(GLenum), "glDisable");
    LOAD(h, drawArrays, void (*)(GLenum,GLint,GLsizei), "glDrawArrays");
    LOAD(h, finish, void (*)(void), "glFinish");
    LOAD(h, readPixels, void (*)(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void*), "glReadPixels");
    LOAD(h, getError, GLenum (*)(void), "glGetError");
    LOAD(h, getString, const GLubyte* (*)(GLenum), "glGetString");

    const char* vs_src =
        "#version 330 core\n"
        "out float sourceY;\n"
        "void main(){\n"
        " vec2 p = (gl_VertexID==0) ? vec2(-1.0,-1.0) : ((gl_VertexID==1) ? vec2(3.0,-1.0) : vec2(-1.0,3.0));\n"
        " sourceY = p.y; gl_Position = vec4(p,0.0,1.0);\n"
        "}\n";
    const char* fs_src =
        "#version 330 core\n"
        "in float sourceY; out vec4 color;\n"
        "void main(){ color = sourceY > 0.0 ? vec4(1,0,0,1) : vec4(0,0,1,1); }\n";
    GLuint vs=createShader(GL_VERTEX_SHADER), fs=createShader(GL_FRAGMENT_SHADER);
    shaderSource(vs,1,&vs_src,NULL); compileShader(vs);
    shaderSource(fs,1,&fs_src,NULL); compileShader(fs);
    GLint ok=0; getShaderiv(vs,GL_COMPILE_STATUS,&ok); if(!ok) return 7;
    getShaderiv(fs,GL_COMPILE_STATUS,&ok); if(!ok) return 8;
    GLuint prog=createProgram(); attachShader(prog,vs); attachShader(prog,fs); linkProgram(prog);
    getProgramiv(prog,GL_LINK_STATUS,&ok); if(!ok) return 9;
    useProgram(prog);
    GLuint vao=0; genVertexArrays(1,&vao); bindVertexArray(vao);
    viewport(0,0,64,64);
    disable(GL_SCISSOR_TEST); disable(GL_CULL_FACE); disable(GL_DEPTH_TEST); disable(GL_BLEND);
    drawArrays(GL_TRIANGLES,0,3); finish();

    unsigned char low[4]={0}, high[4]={0};
    readPixels(32,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,low);
    readPixels(32,56,1,1,GL_RGBA,GL_UNSIGNED_BYTE,high);
    GLenum err=getError();
    printf("backend: %s\n", getString(GL_VERSION));
    printf("low=(%u,%u,%u,%u) high=(%u,%u,%u,%u) err=0x%04x\n",
           low[0],low[1],low[2],low[3],high[0],high[1],high[2],high[3],(unsigned)err);
    if (err != GL_NO_ERROR || !is_blue(low) || !is_red(high)) {
        fprintf(stderr,"DEFAULT FRAMEBUFFER ORIENTATION SMOKE FAILED\n");
        return 1;
    }
    printf("DEFAULT FRAMEBUFFER ORIENTATION SMOKE PASSED\n");
    return 0;
}
