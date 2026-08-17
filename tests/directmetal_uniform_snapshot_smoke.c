/* DirectMetal loose-uniform snapshot regression.
 *
 * Pixel readback is the semantic oracle. Diagnostics prove that repeated draws
 * with unchanged GL uniform state share one packed snapshot and one frame-arena
 * upload instead of rebuilding a name-keyed map/block for every draw.
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../include/mithril/directmetal_diagnostics.h"

#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
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
typedef GLint (*PFN_GetUniformLocation)(GLuint,const char*);
typedef void (*PFN_Uniform4f)(GLint,float,float,float,float);
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
typedef void (*PFN_ResetUniformStats)(void);
typedef int (*PFN_GetUniformStats)(MithrilDirectMetalUniformStatsV1*,size_t);

static int failures;
#define CHECK(c, fmt, ...) do { if (c) printf("ok  : " fmt "\n", ##__VA_ARGS__); \
    else { printf("FAIL: " fmt "\n", ##__VA_ARGS__); ++failures; } } while (0)

static int pixel_is(const uint8_t p[4], uint8_t r, uint8_t g, uint8_t b) {
    return abs((int)p[0]-r)<=3 && abs((int)p[1]-g)<=3 &&
           abs((int)p[2]-b)<=3 && p[3]>=252;
}

static const char* VS =
    "#version 150\n"
    "layout(location=0) in vec2 pos;\n"
    "void main(){ gl_Position=vec4(pos,0.0,1.0); }\n";
static const char* FS =
    "#version 150\n"
    "uniform vec4 tint;\n"
    "layout(location=0) out vec4 color;\n"
    "void main(){ color=tint; }\n";

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
    LOAD(PFN_GetUniformLocation,getUniformLocation,"glGetUniformLocation");
    LOAD(PFN_Uniform4f,uniform4f,"glUniform4f");
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
    LOAD(PFN_ResetUniformStats,resetStats,"mithrilResetDirectMetalUniformStats");
    LOAD(PFN_GetUniformStats,getStats,"mithrilGetDirectMetalUniformStatsV1");
#undef LOAD
    CHECK(createShader&&shaderSource&&compileShader&&getShaderiv&&createProgram&&
          attachShader&&linkProgram&&getProgramiv&&useProgram&&getUniformLocation&&
          uniform4f&&genVertexArrays&&bindVertexArray&&genBuffers&&bindBuffer&&
          bufferData&&enableVertexAttribArray&&vertexAttribPointer&&drawArrays&&
          clearColor&&clear&&finish&&readPixels&&getError&&resetStats&&getStats,
          "required uniform snapshot symbols resolve");
    if(failures) return failures;

    GLuint vs=createShader(GL_VERTEX_SHADER),fs=createShader(GL_FRAGMENT_SHADER);
    shaderSource(vs,1,&VS,NULL);shaderSource(fs,1,&FS,NULL);
    compileShader(vs);compileShader(fs);
    GLint ok=0;getShaderiv(vs,GL_COMPILE_STATUS,&ok);CHECK(ok,"vertex shader compiles");
    getShaderiv(fs,GL_COMPILE_STATUS,&ok);CHECK(ok,"fragment shader compiles");
    GLuint program=createProgram();attachShader(program,vs);attachShader(program,fs);
    linkProgram(program);getProgramiv(program,GL_LINK_STATUS,&ok);CHECK(ok,"program links");
    useProgram(program);
    GLint tint=getUniformLocation(program,"tint");CHECK(tint>=0,"tint location resolves");

    const float vertices[6]={-0.8f,-0.8f,0.8f,-0.8f,0.0f,0.8f};
    GLuint vao=0,vbo=0;genVertexArrays(1,&vao);bindVertexArray(vao);
    genBuffers(1,&vbo);bindBuffer(GL_ARRAY_BUFFER,vbo);
    bufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
    enableVertexAttribArray(0);vertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),0);

    resetStats();
    uniform4f(tint,1,0,0,1);
    clearColor(0,0,0,1);clear(GL_COLOR_BUFFER_BIT);
    for(int i=0;i<32;++i) drawArrays(GL_TRIANGLES,0,3);
    finish();
    uint8_t px[4]={0};readPixels(256,256,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px);
    CHECK(pixel_is(px,255,0,0),"unchanged snapshot renders red");
    MithrilDirectMetalUniformStatsV1 first={0};
    CHECK(getStats(&first,sizeof(first)),"first uniform stats read succeeds");
    CHECK(first.snapshot_packs==1,"32 unchanged draws pack once (%llu)",
          (unsigned long long)first.snapshot_packs);
    CHECK(first.snapshot_reuses>=31,"unchanged draws reuse snapshot (%llu)",
          (unsigned long long)first.snapshot_reuses);
    CHECK(first.frame_uniform_uploads==1,"fragment snapshot uploads once per frame (%llu)",
          (unsigned long long)first.frame_uniform_uploads);

    resetStats();
    uniform4f(tint,0,1,0,1);
    for(int i=0;i<8;++i) drawArrays(GL_TRIANGLES,0,3);
    finish();
    readPixels(256,256,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px);
    CHECK(pixel_is(px,0,255,0),"changed uniform renders green");
    MithrilDirectMetalUniformStatsV1 second={0};
    CHECK(getStats(&second,sizeof(second)),"second uniform stats read succeeds");
    CHECK(second.snapshot_packs==1 && second.snapshot_reuses>=7,
          "one setter creates one new snapshot (%llu pack, %llu reuse)",
          (unsigned long long)second.snapshot_packs,
          (unsigned long long)second.snapshot_reuses);
    CHECK(second.frame_uniform_uploads==1,"changed snapshot uploads once (%llu)",
          (unsigned long long)second.frame_uniform_uploads);

    CHECK(getError()==GL_NO_ERROR,"uniform snapshot scenario leaves GL_NO_ERROR");
    dlclose(h);
    return failures?1:0;
}
