/* DirectMetal GL error visibility + glProgramUniform state-isolation oracle. */
#include <dlfcn.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>
#include <GL/glcorearb.h>

#define W 96
#define H 64
static int checks = 0, failures = 0;
#define CHECK(c,fmt,...) do { ++checks; if (c) printf("ok  : " fmt "\n", ##__VA_ARGS__); else { printf("FAIL: " fmt "\n", ##__VA_ARGS__); ++failures; } } while (0)

typedef struct API {
    const GLubyte* (*GetString)(GLenum);
    GLenum (*GetError)(void);
    void (*GetIntegerv)(GLenum, GLint*);
    void (*Finish)(void);
    void (*Viewport)(GLint, GLint, GLsizei, GLsizei);
    void (*ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
    void (*Clear)(GLbitfield);
    void (*ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
    void (*GenTextures)(GLsizei, GLuint*);
    void (*BindTexture)(GLenum, GLuint);
    void (*TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
    void (*TexParameteri)(GLenum, GLenum, GLint);
    void (*GenFramebuffers)(GLsizei, GLuint*);
    void (*BindFramebuffer)(GLenum, GLuint);
    void (*FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
    GLenum (*CheckFramebufferStatus)(GLenum);
    GLuint (*CreateShader)(GLenum);
    void (*ShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
    void (*CompileShader)(GLuint);
    void (*GetShaderiv)(GLuint, GLenum, GLint*);
    GLuint (*CreateProgram)(void);
    void (*AttachShader)(GLuint, GLuint);
    void (*LinkProgram)(GLuint);
    void (*GetProgramiv)(GLuint, GLenum, GLint*);
    void (*UseProgram)(GLuint);
    GLint (*GetUniformLocation)(GLuint, const GLchar*);
    void (*Uniform3f)(GLint, GLfloat, GLfloat, GLfloat);
    void (*ProgramUniform3f)(GLuint, GLint, GLfloat, GLfloat, GLfloat);
    void (*GetUniformfv)(GLuint, GLint, GLfloat*);
    void (*GenVertexArrays)(GLsizei, GLuint*);
    void (*BindVertexArray)(GLuint);
    void (*GenBuffers)(GLsizei, GLuint*);
    void (*BindBuffer)(GLenum, GLuint);
    void (*BufferData)(GLenum, GLsizeiptr, const void*, GLenum);
    void (*VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
    void (*EnableVertexAttribArray)(GLuint);
    void (*BindVertexBuffer)(GLuint, GLuint, GLintptr, GLsizei);
    void (*DrawArrays)(GLenum, GLint, GLsizei);
} API;

#define LOAD(a,h,f) do { (a)->f = (__typeof__((a)->f))dlsym((h), "gl" #f); CHECK((a)->f != NULL, "resolve gl%s", #f); } while (0)
static int load_api(void* h, API* a) {
    memset(a, 0, sizeof(*a));
    LOAD(a,h,GetString); LOAD(a,h,GetError); LOAD(a,h,GetIntegerv); LOAD(a,h,Finish);
    LOAD(a,h,Viewport); LOAD(a,h,ClearColor); LOAD(a,h,Clear); LOAD(a,h,ReadPixels);
    LOAD(a,h,GenTextures); LOAD(a,h,BindTexture); LOAD(a,h,TexImage2D); LOAD(a,h,TexParameteri);
    LOAD(a,h,GenFramebuffers); LOAD(a,h,BindFramebuffer); LOAD(a,h,FramebufferTexture2D); LOAD(a,h,CheckFramebufferStatus);
    LOAD(a,h,CreateShader); LOAD(a,h,ShaderSource); LOAD(a,h,CompileShader); LOAD(a,h,GetShaderiv);
    LOAD(a,h,CreateProgram); LOAD(a,h,AttachShader); LOAD(a,h,LinkProgram); LOAD(a,h,GetProgramiv); LOAD(a,h,UseProgram);
    LOAD(a,h,GetUniformLocation); LOAD(a,h,Uniform3f); LOAD(a,h,ProgramUniform3f); LOAD(a,h,GetUniformfv);
    LOAD(a,h,GenVertexArrays); LOAD(a,h,BindVertexArray); LOAD(a,h,GenBuffers); LOAD(a,h,BindBuffer); LOAD(a,h,BufferData);
    LOAD(a,h,VertexAttribPointer); LOAD(a,h,EnableVertexAttribArray); LOAD(a,h,BindVertexBuffer); LOAD(a,h,DrawArrays);
    return failures == 0;
}
#undef LOAD

static int egl_setup(void* h) {
    EGLDisplay (*GetDisplay)(EGLNativeDisplayType) = dlsym(h, "eglGetDisplay");
    EGLBoolean (*Initialize)(EGLDisplay,EGLint*,EGLint*) = dlsym(h, "eglInitialize");
    EGLBoolean (*BindAPI)(EGLenum) = dlsym(h, "eglBindAPI");
    EGLBoolean (*GetConfigs)(EGLDisplay,EGLConfig*,EGLint,EGLint*) = dlsym(h, "eglGetConfigs");
    EGLContext (*CreateContext)(EGLDisplay,EGLConfig,EGLContext,const EGLint*) = dlsym(h, "eglCreateContext");
    EGLSurface (*CreatePbufferSurface)(EGLDisplay,EGLConfig,const EGLint*) = dlsym(h, "eglCreatePbufferSurface");
    EGLBoolean (*MakeCurrent)(EGLDisplay,EGLSurface,EGLSurface,EGLContext) = dlsym(h, "eglMakeCurrent");
    CHECK(GetDisplay && Initialize && BindAPI && GetConfigs && CreateContext && CreatePbufferSurface && MakeCurrent,
          "resolve EGL entry points");
    if (!GetDisplay || !Initialize || !BindAPI || !GetConfigs || !CreateContext || !CreatePbufferSurface || !MakeCurrent) return 0;
    EGLDisplay d = GetDisplay(EGL_DEFAULT_DISPLAY); EGLint ma=0,mi=0;
    CHECK(d != EGL_NO_DISPLAY && Initialize(d,&ma,&mi), "initialize EGL");
    CHECK(BindAPI(EGL_OPENGL_API), "bind OpenGL API");
    EGLConfig cfg=0; EGLint n=0; CHECK(GetConfigs(d,&cfg,1,&n) && n>0, "get EGL config");
    const EGLint ca[] = {EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,EGL_NONE};
    const EGLint pa[] = {EGL_WIDTH,W,EGL_HEIGHT,H,EGL_NONE};
    EGLContext c = CreateContext(d,cfg,EGL_NO_CONTEXT,ca); EGLSurface s = CreatePbufferSurface(d,cfg,pa);
    CHECK(c != EGL_NO_CONTEXT && s != EGL_NO_SURFACE, "create pbuffer/context");
    CHECK(MakeCurrent(d,s,s,c), "make context current");
    return c != EGL_NO_CONTEXT && s != EGL_NO_SURFACE;
}

static GLuint make_program(API* a) {
    const char* vs = "#version 330 core\nlayout(location=0) in vec2 p; void main(){ gl_Position=vec4(p,0,1); }\n";
    const char* fs = "#version 330 core\nuniform vec3 uColor; out vec4 o; void main(){ o=vec4(uColor,1); }\n";
    GLuint sv=a->CreateShader(GL_VERTEX_SHADER), sf=a->CreateShader(GL_FRAGMENT_SHADER);
    a->ShaderSource(sv,1,&vs,0); a->ShaderSource(sf,1,&fs,0); a->CompileShader(sv); a->CompileShader(sf);
    GLint vc=0,fc=0; a->GetShaderiv(sv,GL_COMPILE_STATUS,&vc); a->GetShaderiv(sf,GL_COMPILE_STATUS,&fc);
    CHECK(vc && fc, "shader pair compiles");
    GLuint p=a->CreateProgram(); a->AttachShader(p,sv); a->AttachShader(p,sf); a->LinkProgram(p);
    GLint linked=0; a->GetProgramiv(p,GL_LINK_STATUS,&linked); CHECK(linked, "program %u links", p);
    return p;
}

static GLuint make_target(API* a) {
    GLuint t=0,f=0; a->GenTextures(1,&t); a->BindTexture(GL_TEXTURE_2D,t);
    a->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    a->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    a->TexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,W,H,0,GL_RGBA,GL_UNSIGNED_BYTE,0);
    a->GenFramebuffers(1,&f); a->BindFramebuffer(GL_FRAMEBUFFER,f);
    a->FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t,0);
    CHECK(a->CheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE, "FBO complete");
    return f;
}

static int nearf3(const GLfloat* v, GLfloat x, GLfloat y, GLfloat z) {
    return fabsf(v[0]-x)<0.001f && fabsf(v[1]-y)<0.001f && fabsf(v[2]-z)<0.001f;
}

static int dominant(const uint8_t* px, int channel) {
    uint64_t sum[3]={0,0,0};
    for (int i=0;i<W*H;i++) { sum[0]+=px[i*4]; sum[1]+=px[i*4+1]; sum[2]+=px[i*4+2]; }
    int a=(channel+1)%3,b=(channel+2)%3;
    return sum[channel] > sum[a]*4 && sum[channel] > sum[b]*4;
}

int main(int argc, char** argv) {
    const char* path = argc>1 ? argv[1] : "./libmithril.dylib";
    void* h = dlopen(path, RTLD_NOW|RTLD_GLOBAL); CHECK(h != NULL, "dlopen %s", path);
    if (!h) { fprintf(stderr,"%s\n",dlerror()); return 2; }
    if (!egl_setup(h)) return 2;
    API a; if (!load_api(h,&a)) return 2;
    const char* version=(const char*)a.GetString(GL_VERSION);
    CHECK(version && strstr(version,"3.3.0") && strstr(version,"Metal 3 (DirectMetal)"),
          "conservative DirectMetal GL identity (%s)", version?version:"null");

    /* Error channel must be visible and consume exactly the queued error. */
    for (int i=0;i<16 && a.GetError()!=GL_NO_ERROR;i++) {}
    a.BindVertexBuffer(999,0,0,16);
    GLenum e1=a.GetError(), e2=a.GetError();
    CHECK(e1==GL_INVALID_VALUE, "glGetError exposes deterministic GL_INVALID_VALUE (0x%x)", e1);
    CHECK(e2==GL_NO_ERROR, "glGetError consumes the queued error exactly once (0x%x)", e2);

    GLuint f=make_target(&a), pa=make_program(&a), pb=make_program(&a);
    GLint la=a.GetUniformLocation(pa,"uColor"), lb=a.GetUniformLocation(pb,"uColor");
    CHECK(la>=0 && lb>=0, "uniform reflection provides uColor locations (%d,%d)", la, lb);
    a.UseProgram(pa); a.Uniform3f(la,1,0,0);
    GLint before=-1; a.GetIntegerv(GL_CURRENT_PROGRAM,&before);
    a.ProgramUniform3f(pb,lb,0,1,0);
    GLint after=-1; a.GetIntegerv(GL_CURRENT_PROGRAM,&after);
    CHECK(before==(GLint)pa && after==(GLint)pa,
          "glProgramUniform3f preserves glUseProgram selection (%d -> %d, expected %u)", before, after, pa);
    GLfloat ua[4]={0},ub[4]={0}; a.GetUniformfv(pa,la,ua); a.GetUniformfv(pb,lb,ub);
    CHECK(nearf3(ua,1,0,0), "ProgramUniform leaves current program uniform unchanged (%g,%g,%g)", ua[0],ua[1],ua[2]);
    CHECK(nearf3(ub,0,1,0), "ProgramUniform updates only target program (%g,%g,%g)", ub[0],ub[1],ub[2]);

    const GLfloat tri[] = {-0.8f,-0.75f, 0.8f,-0.75f, 0.0f,0.8f};
    GLuint vao=0,vbo=0; a.GenVertexArrays(1,&vao); a.BindVertexArray(vao); a.GenBuffers(1,&vbo);
    a.BindBuffer(GL_ARRAY_BUFFER,vbo); a.BufferData(GL_ARRAY_BUFFER,sizeof(tri),tri,GL_STATIC_DRAW);
    a.VertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(GLfloat),0); a.EnableVertexAttribArray(0);
    a.BindFramebuffer(GL_FRAMEBUFFER,f); a.Viewport(0,0,W,H);
    uint8_t px[W*H*4];

    /* If ProgramUniform leaked pb into currentProgram, this first draw would be green. */
    a.ClearColor(0,0,0,1); a.Clear(GL_COLOR_BUFFER_BIT); a.DrawArrays(GL_TRIANGLES,0,3); a.Finish();
    a.ReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,px); a.Finish();
    CHECK(dominant(px,0), "GPU draw after ProgramUniform still uses current red program");

    a.UseProgram(pb); a.Clear(GL_COLOR_BUFFER_BIT); a.DrawArrays(GL_TRIANGLES,0,3); a.Finish();
    a.ReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,px); a.Finish();
    CHECK(dominant(px,1), "target program received green ProgramUniform value on GPU");
    CHECK(a.GetError()==GL_NO_ERROR, "state semantics oracle ends without GL error");

    printf("STATE SEMANTICS SMOKE: %d checks, %d failure(s)\n", checks, failures);
    if (!failures) printf("STATE SEMANTICS SMOKE ALL PASSED\n");
    return failures ? 1 : 0;
}
