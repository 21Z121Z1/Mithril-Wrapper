/* DirectMetal first-use program prewarm regression. */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../include/mithril/directmetal_diagnostics.h"
#include "../include/mithril/program_diagnostics.h"

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
typedef void (*PFN_ResetFrontend)(void);
typedef int (*PFN_GetFrontend)(MithrilProgramPrewarmStatsV1*,size_t);
typedef void (*PFN_ResetMetal)(void);
typedef int (*PFN_GetMetal)(MithrilDirectMetalProgramStatsV1*,size_t);

static int failures;
#define CHECK(c, fmt, ...) do { if (c) printf("ok  : " fmt "\n", ##__VA_ARGS__); \
    else { printf("FAIL: " fmt "\n", ##__VA_ARGS__); ++failures; } } while (0)

static GLuint make_shader(PFN_CreateShader createShader,
                          PFN_ShaderSource shaderSource,
                          PFN_CompileShader compileShader,
                          PFN_GetShaderiv getShaderiv,
                          GLenum type, const char* source) {
    GLuint shader=createShader(type); GLint ok=0;
    shaderSource(shader,1,&source,NULL); compileShader(shader);
    getShaderiv(shader,GL_COMPILE_STATUS,&ok);
    CHECK(ok,"shader 0x%x compiles",type);
    return shader;
}

static GLuint make_program(PFN_CreateProgram createProgram,
                           PFN_AttachShader attachShader,
                           PFN_LinkProgram linkProgram,
                           PFN_GetProgramiv getProgramiv,
                           GLuint vs, GLuint fs) {
    GLuint program=createProgram(); GLint ok=0;
    attachShader(program,vs); attachShader(program,fs); linkProgram(program);
    getProgramiv(program,GL_LINK_STATUS,&ok); CHECK(ok,"program %u links",program);
    return program;
}

int main(void) {
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
    LOAD(PFN_ResetFrontend,resetFrontend,"mithrilResetProgramPrewarmStats");
    LOAD(PFN_GetFrontend,getFrontend,"mithrilGetProgramPrewarmStatsV1");
    LOAD(PFN_ResetMetal,resetMetal,"mithrilResetDirectMetalProgramStats");
    LOAD(PFN_GetMetal,getMetal,"mithrilGetDirectMetalProgramStatsV1");
#undef LOAD
    CHECK(createShader&&shaderSource&&compileShader&&getShaderiv&&createProgram&&
          attachShader&&linkProgram&&getProgramiv&&useProgram&&getIntegerv&&
          genVertexArrays&&bindVertexArray&&genBuffers&&bindBuffer&&bufferData&&
          enableVertexAttribArray&&vertexAttribPointer&&drawArrays&&clearColor&&
          clear&&finish&&readPixels&&getError&&resetFrontend&&getFrontend&&
          resetMetal&&getMetal,"required program prewarm symbols resolve");
    if(failures) return failures;

    static const char* VS="#version 150\nlayout(location=0) in vec2 pos;\nvoid main(){gl_Position=vec4(pos,0,1);}\n";
    static const char* FS_RED="#version 150\nlayout(location=0) out vec4 color;\nvoid main(){color=vec4(1,0,0,1);}\n";
    static const char* FS_GREEN="#version 150\nlayout(location=0) out vec4 color;\nvoid main(){color=vec4(0,1,0,1);}\n";

    resetFrontend(); resetMetal();
    GLuint vs=make_shader(createShader,shaderSource,compileShader,getShaderiv,GL_VERTEX_SHADER,VS);
    GLuint red=make_shader(createShader,shaderSource,compileShader,getShaderiv,GL_FRAGMENT_SHADER,FS_RED);
    GLuint programA=make_program(createProgram,attachShader,linkProgram,getProgramiv,vs,red);
    MithrilProgramPrewarmStatsV1 front={0}; MithrilDirectMetalProgramStatsV1 metal={0};
    CHECK(getFrontend(&front,sizeof(front))&&getMetal(&metal,sizeof(metal)),"initial stats read succeeds");
    CHECK(front.frontend_program_bindings==0 && metal.program_compiles==0,
          "link before backend init performs no native compile");

    GLint samples=0; getIntegerv(GL_MAX_SAMPLES,&samples);
    CHECK(samples>0,"GL_MAX_SAMPLES initializes DirectMetal backend (%d)",samples);
    useProgram(programA);
    front=(MithrilProgramPrewarmStatsV1){0}; metal=(MithrilDirectMetalProgramStatsV1){0};
    getFrontend(&front,sizeof(front)); getMetal(&metal,sizeof(metal));
    CHECK(front.use_prewarms==1 && front.draw_fallbacks==0,
          "glUseProgram prewarms program linked before backend init");
    CHECK(metal.program_compiles==1,"use-time prewarm performs one Metal program compile (%llu)",
          (unsigned long long)metal.program_compiles);

    GLuint green=make_shader(createShader,shaderSource,compileShader,getShaderiv,GL_FRAGMENT_SHADER,FS_GREEN);
    GLuint programB=make_program(createProgram,attachShader,linkProgram,getProgramiv,vs,green);
    front=(MithrilProgramPrewarmStatsV1){0}; metal=(MithrilDirectMetalProgramStatsV1){0};
    getFrontend(&front,sizeof(front)); getMetal(&metal,sizeof(metal));
    CHECK(front.link_prewarms==1 && front.use_prewarms==1,
          "program linked after backend init prewarms at link");
    CHECK(metal.program_compiles==2,"second unique program compiles during link (%llu)",
          (unsigned long long)metal.program_compiles);

    useProgram(programB);
    const float vertices[6]={-0.8f,-0.8f,0.8f,-0.8f,0.0f,0.8f};
    GLuint vao=0,vbo=0;genVertexArrays(1,&vao);bindVertexArray(vao);
    genBuffers(1,&vbo);bindBuffer(GL_ARRAY_BUFFER,vbo);
    bufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
    enableVertexAttribArray(0);vertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),0);
    clearColor(0,0,0,1);clear(GL_COLOR_BUFFER_BIT);drawArrays(GL_TRIANGLES,0,3);finish();
    uint8_t px[4]={0};readPixels(256,256,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px);
    CHECK(px[0]<=3 && px[1]>=252 && px[2]<=3 && px[3]>=252,
          "prewarmed program renders expected green pixel");
    front=(MithrilProgramPrewarmStatsV1){0}; metal=(MithrilDirectMetalProgramStatsV1){0};
    getFrontend(&front,sizeof(front)); getMetal(&metal,sizeof(metal));
    CHECK(front.draw_fallbacks==0,"prewarmed first draw performs no frontend program create");
    CHECK(metal.program_compiles==2,"prewarmed first draw performs no additional MSL/library compile");
    CHECK(getError()==GL_NO_ERROR,"program prewarm scenario leaves GL_NO_ERROR");

    dlclose(h);
    return failures?1:0;
}
