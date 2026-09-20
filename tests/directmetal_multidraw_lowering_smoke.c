/* MultiDraw shared-state lowering regression.
 *
 * Pixels remain the semantic oracle. Frontend diagnostics prove that each
 * MultiDraw call resolves invariant GL/backend state once while retaining one
 * geometry lowering per subdraw.
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../include/mithril/draw_diagnostics.h"

#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_FLOAT 0x1406
#define GL_UNSIGNED_SHORT 0x1403
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
typedef void (*PFN_GenVertexArrays)(GLsizei,GLuint*);
typedef void (*PFN_BindVertexArray)(GLuint);
typedef void (*PFN_GenBuffers)(GLsizei,GLuint*);
typedef void (*PFN_BindBuffer)(GLenum,GLuint);
typedef void (*PFN_BufferData)(GLenum,GLsizeiptr,const void*,GLenum);
typedef void (*PFN_EnableVertexAttribArray)(GLuint);
typedef void (*PFN_VertexAttribPointer)(GLuint,GLint,GLenum,GLboolean,GLsizei,const void*);
typedef void (*PFN_DrawArrays)(GLenum,GLint,GLsizei);
typedef void (*PFN_MultiDrawArrays)(GLenum,const GLint*,const GLsizei*,GLsizei);
typedef void (*PFN_MultiDrawElements)(GLenum,const GLsizei*,GLenum,const void* const*,GLsizei);
typedef void (*PFN_MultiDrawElementsBaseVertex)(GLenum,const GLsizei*,GLenum,const void* const*,GLsizei,const GLint*);
typedef void (*PFN_ClearColor)(float,float,float,float);
typedef void (*PFN_Clear)(unsigned int);
typedef void (*PFN_Finish)(void);
typedef void (*PFN_ReadPixels)(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void*);
typedef GLenum (*PFN_GetError)(void);
typedef void (*PFN_ResetStats)(void);
typedef int (*PFN_GetStats)(MithrilDrawLoweringStatsV1*,size_t);

static int failures;
#define CHECK(c, fmt, ...) do { if (c) printf("ok  : " fmt "\n", ##__VA_ARGS__); \
    else { printf("FAIL: " fmt "\n", ##__VA_ARGS__); ++failures; } } while (0)

static int red_pixel(const uint8_t p[4]) {
    return p[0] >= 252 && p[1] <= 3 && p[2] <= 3 && p[3] >= 252;
}

static const char* VS =
    "#version 150\n"
    "layout(location=0) in vec2 pos;\n"
    "void main(){ gl_Position=vec4(pos,0.0,1.0); }\n";
static const char* FS =
    "#version 150\n"
    "layout(location=0) out vec4 color;\n"
    "void main(){ color=vec4(1.0,0.0,0.0,1.0); }\n";

int main(void) {
    enum { N = 16 };
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
    LOAD(PFN_GenVertexArrays,genVertexArrays,"glGenVertexArrays");
    LOAD(PFN_BindVertexArray,bindVertexArray,"glBindVertexArray");
    LOAD(PFN_GenBuffers,genBuffers,"glGenBuffers");
    LOAD(PFN_BindBuffer,bindBuffer,"glBindBuffer");
    LOAD(PFN_BufferData,bufferData,"glBufferData");
    LOAD(PFN_EnableVertexAttribArray,enableVertexAttribArray,"glEnableVertexAttribArray");
    LOAD(PFN_VertexAttribPointer,vertexAttribPointer,"glVertexAttribPointer");
    LOAD(PFN_DrawArrays,drawArrays,"glDrawArrays");
    LOAD(PFN_MultiDrawArrays,multiDrawArrays,"glMultiDrawArrays");
    LOAD(PFN_MultiDrawElements,multiDrawElements,"glMultiDrawElements");
    LOAD(PFN_MultiDrawElementsBaseVertex,multiDrawElementsBaseVertex,"glMultiDrawElementsBaseVertex");
    LOAD(PFN_ClearColor,clearColor,"glClearColor");
    LOAD(PFN_Clear,clear,"glClear");
    LOAD(PFN_Finish,finish,"glFinish");
    LOAD(PFN_ReadPixels,readPixels,"glReadPixels");
    LOAD(PFN_GetError,getError,"glGetError");
    LOAD(PFN_ResetStats,resetStats,"mithrilResetDrawLoweringStats");
    LOAD(PFN_GetStats,getStats,"mithrilGetDrawLoweringStatsV1");
#undef LOAD
    CHECK(createShader&&shaderSource&&compileShader&&getShaderiv&&createProgram&&
          attachShader&&linkProgram&&getProgramiv&&useProgram&&genVertexArrays&&
          bindVertexArray&&genBuffers&&bindBuffer&&bufferData&&
          enableVertexAttribArray&&vertexAttribPointer&&drawArrays&&
          multiDrawArrays&&multiDrawElements&&multiDrawElementsBaseVertex&&
          clearColor&&clear&&finish&&readPixels&&getError&&resetStats&&getStats,
          "required multidraw symbols resolve");
    if(failures) return failures;

    GLuint vs=createShader(GL_VERTEX_SHADER), fs=createShader(GL_FRAGMENT_SHADER);
    shaderSource(vs,1,&VS,NULL); shaderSource(fs,1,&FS,NULL);
    compileShader(vs); compileShader(fs);
    GLint ok=0; getShaderiv(vs,GL_COMPILE_STATUS,&ok); CHECK(ok,"vertex shader compiles");
    getShaderiv(fs,GL_COMPILE_STATUS,&ok); CHECK(ok,"fragment shader compiles");
    GLuint program=createProgram(); attachShader(program,vs); attachShader(program,fs);
    linkProgram(program); getProgramiv(program,GL_LINK_STATUS,&ok); CHECK(ok,"program links");
    useProgram(program);

    const float vertices[6]={-0.8f,-0.8f, 0.8f,-0.8f, 0.0f,0.8f};
    const uint16_t indices[3]={0,1,2};
    GLuint vao=0,vbo=0,ebo=0; genVertexArrays(1,&vao); bindVertexArray(vao);
    genBuffers(1,&vbo); bindBuffer(GL_ARRAY_BUFFER,vbo);
    bufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
    enableVertexAttribArray(0); vertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),0);
    genBuffers(1,&ebo); bindBuffer(GL_ELEMENT_ARRAY_BUFFER,ebo);
    bufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(indices),indices,GL_STATIC_DRAW);

    GLint first[N]; GLsizei counts[N]; const void* offsets[N]; GLint bases[N];
    for(int i=0;i<N;++i){first[i]=0;counts[i]=3;offsets[i]=(const void*)0;bases[i]=0;}
    uint8_t px[4]={0}; MithrilDrawLoweringStatsV1 stats={0};

    clearColor(0,0,0,1); clear(GL_COLOR_BUFFER_BIT); resetStats();
    multiDrawArrays(GL_TRIANGLES,first,counts,N); finish();
    readPixels(256,256,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px);
    CHECK(red_pixel(px),"MultiDrawArrays renders expected pixel");
    CHECK(getStats(&stats,sizeof(stats)),"MultiDrawArrays stats read succeeds");
    CHECK(stats.multi_draw_calls==1 && stats.multi_draw_subdraws==N,
          "MultiDrawArrays records one batch / %d subdraws",N);
    CHECK(stats.shared_state_resolves==1 && stats.geometry_lowerings==N,
          "MultiDrawArrays resolves shared state once (%llu/%llu)",
          (unsigned long long)stats.shared_state_resolves,
          (unsigned long long)stats.geometry_lowerings);

    resetStats();
    for(int i=0;i<N;++i) drawArrays(GL_TRIANGLES,0,3);
    finish(); stats=(MithrilDrawLoweringStatsV1){0};
    CHECK(getStats(&stats,sizeof(stats)),"ordinary draw stats read succeeds");
    CHECK(stats.shared_state_resolves==N && stats.geometry_lowerings==N,
          "%d ordinary draws resolve state %d times (%llu)",N,N,
          (unsigned long long)stats.shared_state_resolves);

    clear(GL_COLOR_BUFFER_BIT); resetStats();
    multiDrawElements(GL_TRIANGLES,counts,GL_UNSIGNED_SHORT,offsets,N); finish();
    readPixels(256,256,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px);
    stats=(MithrilDrawLoweringStatsV1){0}; getStats(&stats,sizeof(stats));
    CHECK(red_pixel(px),"MultiDrawElements renders expected pixel");
    CHECK(stats.shared_state_resolves==1 && stats.geometry_lowerings==N,
          "MultiDrawElements resolves shared state once (%llu/%llu)",
          (unsigned long long)stats.shared_state_resolves,
          (unsigned long long)stats.geometry_lowerings);

    clear(GL_COLOR_BUFFER_BIT); resetStats();
    multiDrawElementsBaseVertex(GL_TRIANGLES,counts,GL_UNSIGNED_SHORT,offsets,N,bases);
    finish(); readPixels(256,256,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px);
    stats=(MithrilDrawLoweringStatsV1){0}; getStats(&stats,sizeof(stats));
    CHECK(red_pixel(px),"MultiDrawElementsBaseVertex renders expected pixel");
    CHECK(stats.shared_state_resolves==1 && stats.geometry_lowerings==N,
          "MultiDrawElementsBaseVertex resolves shared state once (%llu/%llu)",
          (unsigned long long)stats.shared_state_resolves,
          (unsigned long long)stats.geometry_lowerings);

    CHECK(getError()==GL_NO_ERROR,"multidraw lowering scenario leaves GL_NO_ERROR");
    dlclose(h);
    return failures?1:0;
}
