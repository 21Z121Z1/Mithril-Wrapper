/* DirectMetal MRT glClearBuffer* attachment-selection oracle.
 * Establishes distinct attachment contents without relying on MRT shaders,
 * then proves glClearBufferfv(GL_COLOR, drawbuffer, ...) changes only the
 * selected draw buffer and preserves the other attachment.
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>
#include <GL/glcorearb.h>

#ifndef GL_COLOR
#define GL_COLOR 0x1800
#endif
#define W 32
#define H 24

static int checks = 0, failures = 0;
#define CHECK(c,fmt,...) do { ++checks; if (c) printf("ok  : " fmt "\n", ##__VA_ARGS__); else { printf("FAIL: " fmt "\n", ##__VA_ARGS__); ++failures; } } while (0)

typedef struct API {
    const GLubyte* (*GetString)(GLenum);
    GLenum (*GetError)(void);
    void (*Finish)(void);
    void (*GenTextures)(GLsizei, GLuint*);
    void (*BindTexture)(GLenum, GLuint);
    void (*TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
    void (*TexParameteri)(GLenum, GLenum, GLint);
    void (*GenFramebuffers)(GLsizei, GLuint*);
    void (*BindFramebuffer)(GLenum, GLuint);
    void (*FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
    GLenum (*CheckFramebufferStatus)(GLenum);
    void (*DrawBuffers)(GLsizei, const GLenum*);
    void (*ReadBuffer)(GLenum);
    void (*ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
    void (*ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
    void (*Clear)(GLbitfield);
    void (*ClearBufferfv)(GLenum, GLint, const GLfloat*);
} API;

#define LOAD(a,h,f) do { (a)->f=(__typeof__((a)->f))dlsym((h),"gl" #f); CHECK((a)->f!=NULL,"resolve gl%s",#f); } while(0)
static int load_api(void* h, API* a) {
    memset(a,0,sizeof(*a));
    LOAD(a,h,GetString); LOAD(a,h,GetError); LOAD(a,h,Finish);
    LOAD(a,h,GenTextures); LOAD(a,h,BindTexture); LOAD(a,h,TexImage2D); LOAD(a,h,TexParameteri);
    LOAD(a,h,GenFramebuffers); LOAD(a,h,BindFramebuffer); LOAD(a,h,FramebufferTexture2D); LOAD(a,h,CheckFramebufferStatus);
    LOAD(a,h,DrawBuffers); LOAD(a,h,ReadBuffer); LOAD(a,h,ReadPixels); LOAD(a,h,ClearColor); LOAD(a,h,Clear); LOAD(a,h,ClearBufferfv);
    return failures == 0;
}
#undef LOAD

static int egl_setup(void* h) {
    EGLDisplay (*GetDisplay)(EGLNativeDisplayType)=dlsym(h,"eglGetDisplay");
    EGLBoolean (*Initialize)(EGLDisplay,EGLint*,EGLint*)=dlsym(h,"eglInitialize");
    EGLBoolean (*BindAPI)(EGLenum)=dlsym(h,"eglBindAPI");
    EGLBoolean (*GetConfigs)(EGLDisplay,EGLConfig*,EGLint,EGLint*)=dlsym(h,"eglGetConfigs");
    EGLContext (*CreateContext)(EGLDisplay,EGLConfig,EGLContext,const EGLint*)=dlsym(h,"eglCreateContext");
    EGLSurface (*CreatePbufferSurface)(EGLDisplay,EGLConfig,const EGLint*)=dlsym(h,"eglCreatePbufferSurface");
    EGLBoolean (*MakeCurrent)(EGLDisplay,EGLSurface,EGLSurface,EGLContext)=dlsym(h,"eglMakeCurrent");
    CHECK(GetDisplay&&Initialize&&BindAPI&&GetConfigs&&CreateContext&&CreatePbufferSurface&&MakeCurrent,"resolve EGL entry points");
    if (!GetDisplay||!Initialize||!BindAPI||!GetConfigs||!CreateContext||!CreatePbufferSurface||!MakeCurrent) return 0;
    EGLDisplay d=GetDisplay(EGL_DEFAULT_DISPLAY); EGLint ma=0,mi=0;
    CHECK(d!=EGL_NO_DISPLAY && Initialize(d,&ma,&mi),"initialize EGL %d.%d",ma,mi);
    CHECK(BindAPI(EGL_OPENGL_API),"bind OpenGL API");
    EGLConfig cfg=0; EGLint n=0; CHECK(GetConfigs(d,&cfg,1,&n)&&n>0,"get EGL config");
    const EGLint ca[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,EGL_NONE};
    const EGLint pa[]={EGL_WIDTH,W,EGL_HEIGHT,H,EGL_NONE};
    EGLContext c=CreateContext(d,cfg,EGL_NO_CONTEXT,ca); EGLSurface s=CreatePbufferSurface(d,cfg,pa);
    CHECK(c!=EGL_NO_CONTEXT&&s!=EGL_NO_SURFACE,"create pbuffer/context");
    CHECK(MakeCurrent(d,s,s,c),"make current");
    return c!=EGL_NO_CONTEXT&&s!=EGL_NO_SURFACE;
}

static GLuint texture(API* a) {
    GLuint t=0; a->GenTextures(1,&t); a->BindTexture(GL_TEXTURE_2D,t);
    a->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    a->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    a->TexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,W,H,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    return t;
}

static void expect_color(API* a, GLenum attachment, uint8_t r, uint8_t g, uint8_t b, const char* label) {
    uint8_t px[4]={0}; a->ReadBuffer(attachment); a->ReadPixels(W/2,H/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px); a->Finish();
    CHECK(abs((int)px[0]-r)<=2 && abs((int)px[1]-g)<=2 && abs((int)px[2]-b)<=2,
          "%s = (%u,%u,%u,%u)",label,px[0],px[1],px[2],px[3]);
}

int main(int argc,char** argv) {
    const char* path=argc>1?argv[1]:"./libmithril.dylib";
    void* h=dlopen(path,RTLD_NOW|RTLD_GLOBAL); CHECK(h!=NULL,"dlopen %s",path); if(!h) return 2;
    if(!egl_setup(h)) return 2; API a; if(!load_api(h,&a)) return 2;
    const char* ver=(const char*)a.GetString(GL_VERSION); CHECK(ver&&strstr(ver,"Metal 3 (DirectMetal)"),"DirectMetal active (%s)",ver?ver:"null");

    GLuint t0=texture(&a), t1=texture(&a), f=0; a.GenFramebuffers(1,&f); a.BindFramebuffer(GL_FRAMEBUFFER,f);

    /* Seed t0 red and t1 green through a one-color FBO so seed state is
       independent of the indexed-clear behavior under test. */
    a.FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t0,0);
    CHECK(a.CheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"seed t0 FBO complete");
    a.ClearColor(1,0,0,1); a.Clear(GL_COLOR_BUFFER_BIT); a.Finish();
    a.FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t1,0);
    CHECK(a.CheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"seed t1 FBO complete");
    a.ClearColor(0,1,0,1); a.Clear(GL_COLOR_BUFFER_BIT); a.Finish();

    a.FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t0,0);
    a.FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT1,GL_TEXTURE_2D,t1,0);
    const GLenum draws[2]={GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1}; a.DrawBuffers(2,draws);
    CHECK(a.CheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"two-attachment MRT FBO complete");
    expect_color(&a,GL_COLOR_ATTACHMENT0,255,0,0,"attachment0 seeded red");
    expect_color(&a,GL_COLOR_ATTACHMENT1,0,255,0,"attachment1 seeded green");

    const GLfloat blue[4]={0,0,1,1}; a.ClearBufferfv(GL_COLOR,1,blue); a.Finish();
    expect_color(&a,GL_COLOR_ATTACHMENT0,255,0,0,"indexed clear preserves attachment0");
    expect_color(&a,GL_COLOR_ATTACHMENT1,0,0,255,"indexed clear changes attachment1 to blue");
    CHECK(a.GetError()==GL_NO_ERROR,"MRT indexed clear leaves no GL error");

    printf("MRT CLEAR SEMANTICS SMOKE: %d checks, %d failure(s)\n",checks,failures);
    if(!failures) printf("MRT CLEAR SEMANTICS SMOKE ALL PASSED\n");
    return failures?1:0;
}
