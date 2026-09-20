/* DirectMetal asynchronous PSO precompile regression. */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../include/mithril/directmetal_diagnostics.h"

#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_MAX_SAMPLES 0x8D57
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_FLOAT 0x1406
#define GL_FALSE 0
#define GL_TRIANGLES 0x0004
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_NO_ERROR 0

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef long long GLsizeiptr;
typedef unsigned char GLboolean;

typedef GLuint (*PFN_CreateShader)(GLenum);
typedef void (*PFN_ShaderSource)(GLuint,GLsizei,const char* const*,const GLint*);
typedef void (*PFN_CompileShader)(GLuint);
typedef void (*PFN_GetShaderiv)(GLuint,GLenum,GLint*);
typedef GLuint (*PFN_CreateProgram)(void);
typedef void (*PFN_AttachShader)(GLuint,GLuint);
typedef void (*PFN_LinkProgram)(GLuint);
typedef void (*PFN_GetProgramiv)(GLuint,GLenum,GLint*);
typedef void (*PFN_UseProgram)(GLuint);
typedef void (*PFN_GetIntegerv)(GLenum,GLint*);
typedef void (*PFN_GenVertexArrays)(GLsizei,GLuint*);
typedef void (*PFN_BindVertexArray)(GLuint);
typedef void (*PFN_GenBuffers)(GLsizei,GLuint*);
typedef void (*PFN_BindBuffer)(GLenum,GLuint);
typedef void (*PFN_BufferData)(GLenum,GLsizeiptr,const void*,GLenum);
typedef void (*PFN_EnableVertexAttribArray)(GLuint);
typedef void (*PFN_VertexAttribPointer)(GLuint,GLint,GLenum,GLboolean,GLsizei,const void*);
typedef void (*PFN_DrawArrays)(GLenum,GLint,GLsizei);
typedef void (*PFN_ClearColor)(float,float,float,float);
typedef void (*PFN_Clear)(unsigned int);
typedef void (*PFN_Finish)(void);
typedef void (*PFN_ReadPixels)(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void*);
typedef GLenum (*PFN_GetError)(void);
typedef void (*PFN_ResetStats)(void);
typedef int (*PFN_GetStats)(MithrilDirectMetalPipelineStatsV1*,size_t);

static int failures;
#define CHECK(c, fmt, ...) do { if (c) printf("ok  : " fmt "\n", ##__VA_ARGS__); \
    else { printf("FAIL: " fmt "\n", ##__VA_ARGS__); ++failures; } } while (0)

int main(void) {
    enum { N = 32 };
    const char* lib=getenv("MITHRIL_LIBRARY");
    if(!lib||!*lib) lib="./output/libmithril.dylib";
    void* h=dlopen(lib,RTLD_NOW|RTLD_GLOBAL);
    if(!h){fprintf(stderr,"dlopen: %s\n",dlerror());return 2;}
#define LOAD(type,name,sym) type name=(type)dlsym(h,sym)
    LOAD(PFN_CreateShader,createShader,"glCreateShader");
    LOAD(PFN_ShaderSource,shaderSource,"glShaderSource");
    LOAD(PFN_CompileShader,compileShader,"glCompileShader");
    LOAD(PFN_GetShaderiv,getShaderiv,"glGetShaderiv");
    LOAD(PFN_CreateProgram,createProgram,"glCreateProgram");
    LOAD(PFN_AttachShader,attachShader,"glAttachShader");
    LOAD(PFN_LinkProgram,linkProgram,"glLinkProgram");
    LOAD(PFN_GetProgramiv,getProgramiv,"glGetProgramiv");
    LOAD(PFN_UseProgram,useProgram,"glUseProgram");
    LOAD(PFN_GetIntegerv,getIntegerv,"glGetIntegerv");
    LOAD(PFN_GenVertexArrays,genVertexArrays,"glGenVertexArrays");
    LOAD(PFN_BindVertexArray,bindVertexArray,"glBindVertexArray");
    LOAD(PFN_GenBuffers,genBuffers,"glGenBuffers");
    LOAD(PFN_BindBuffer,bindBuffer,"glBindBuffer");
    LOAD(PFN_BufferData,bufferData,"glBufferData");
    LOAD(PFN_EnableVertexAttribArray,enableVertexAttribArray,"glEnableVertexAttribArray");
    LOAD(PFN_VertexAttribPointer,vertexAttribPointer,"glVertexAttribPointer");
    LOAD(PFN_DrawArrays,drawArrays,"glDrawArrays");
    LOAD(PFN_ClearColor,clearColor,"glClearColor");
    LOAD(PFN_Clear,clear,"glClear");
    LOAD(PFN_Finish,finish,"glFinish");
    LOAD(PFN_ReadPixels,readPixels,"glReadPixels");
    LOAD(PFN_GetError,getError,"glGetError");
    LOAD(PFN_ResetStats,resetStats,"mithrilResetDirectMetalPipelineStats");
    LOAD(PFN_GetStats,getStats,"mithrilGetDirectMetalPipelineStatsV1");
#undef LOAD
    CHECK(createShader&&shaderSource&&compileShader&&getShaderiv&&createProgram&&
          attachShader&&linkProgram&&getProgramiv&&useProgram&&getIntegerv&&
          genVertexArrays&&bindVertexArray&&genBuffers&&bindBuffer&&bufferData&&
          enableVertexAttribArray&&vertexAttribPointer&&drawArrays&&clearColor&&
          clear&&finish&&readPixels&&getError&&resetStats&&getStats,
          "required async PSO symbols resolve");
    if(failures) return failures;

    GLint samples=0; getIntegerv(GL_MAX_SAMPLES,&samples);
    CHECK(samples>0,"backend initialized before program link (%d samples)",samples);
    const char* vs_src="#version 150\nlayout(location=0) in vec2 pos;\nvoid main(){gl_Position=vec4(pos,0,1);}\n";
    const char* fs_src="#version 150\nlayout(location=0) out vec4 color;\nvoid main(){color=vec4(1,0,0,1);}\n";
    GLuint vs=createShader(GL_VERTEX_SHADER), fs=createShader(GL_FRAGMENT_SHADER);
    shaderSource(vs,1,&vs_src,NULL); shaderSource(fs,1,&fs_src,NULL);
    compileShader(vs); compileShader(fs); GLint ok=0;
    getShaderiv(vs,GL_COMPILE_STATUS,&ok); CHECK(ok,"vertex shader compiles");
    getShaderiv(fs,GL_COMPILE_STATUS,&ok); CHECK(ok,"fragment shader compiles");
    GLuint program=createProgram(); attachShader(program,vs); attachShader(program,fs);
    linkProgram(program); getProgramiv(program,GL_LINK_STATUS,&ok); CHECK(ok,"program links/prewarms");
    useProgram(program);

    const float vertices[6]={-0.8f,-0.8f,0.8f,-0.8f,0.0f,0.8f};
    GLuint vao=0,vbo=0; genVertexArrays(1,&vao); bindVertexArray(vao);
    genBuffers(1,&vbo); bindBuffer(GL_ARRAY_BUFFER,vbo);
    bufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
    enableVertexAttribArray(0); vertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),0);

    clearColor(0,0,0,1); clear(GL_COLOR_BUFFER_BIT); resetStats();
    for(int i=0;i<N;++i) drawArrays(GL_TRIANGLES,0,3);
    MithrilDirectMetalPipelineStatsV1 stats={0};
    CHECK(getStats(&stats,sizeof(stats)),"pre-submit PSO stats read succeeds");
    CHECK(stats.async_requests==1,"32 identical draws schedule one async PSO (%llu)",
          (unsigned long long)stats.async_requests);
    CHECK(stats.async_reuses==N-1,"remaining draws reuse pending PSO future (%llu)",
          (unsigned long long)stats.async_reuses);
    CHECK(stats.sync_fallbacks==0,"no synchronous PSO fallback before submit");

    finish();
    uint8_t px[4]={0}; readPixels(256,256,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px);
    stats=(MithrilDirectMetalPipelineStatsV1){0}; getStats(&stats,sizeof(stats));
    CHECK(px[0]>=252&&px[1]<=3&&px[2]<=3&&px[3]>=252,
          "async-precompiled pipeline renders expected red pixel");
    CHECK(stats.async_resolved==1,"encode resolves one async PSO (%llu)",
          (unsigned long long)stats.async_resolved);
    CHECK(stats.sync_fallbacks==0,"first submit uses no synchronous PSO compile");

    drawArrays(GL_TRIANGLES,0,3); finish();
    stats=(MithrilDirectMetalPipelineStatsV1){0}; getStats(&stats,sizeof(stats));
    CHECK(stats.async_requests==1,"later identical draw schedules no new PSO");
    CHECK(stats.pipeline_cache_hits>=1,"later draw hits resident pipeline cache (%llu)",
          (unsigned long long)stats.pipeline_cache_hits);
    CHECK(stats.sync_fallbacks==0,"cached draw keeps synchronous fallback at zero");
    CHECK(getError()==GL_NO_ERROR,"async PSO scenario leaves GL_NO_ERROR");
    dlclose(h);
    return failures?1:0;
}
