/* DirectMetal resident EBO regression.
 *
 * Pixel readback protects indexed-draw semantics. DirectMetal diagnostics make
 * the performance shape deterministic: UInt16/UInt32 use the resident source,
 * while unsupported UInt8 continues through the compatibility staging path.
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/mithril/directmetal_diagnostics.h"

#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_FLOAT 0x1406
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_SHORT 0x1403
#define GL_UNSIGNED_INT 0x1405
#define GL_FALSE 0
#define GL_TRIANGLES 0x0004
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_RGBA 0x1908
#define GL_NO_ERROR 0

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef long long GLsizeiptr;
typedef long long GLintptr;
typedef unsigned char GLboolean;

typedef GLuint (*PFN_CreateShader)(GLenum);
typedef void (*PFN_ShaderSource)(GLuint, GLsizei, const char* const*, const GLint*);
typedef void (*PFN_CompileShader)(GLuint);
typedef void (*PFN_GetShaderiv)(GLuint, GLenum, GLint*);
typedef GLuint (*PFN_CreateProgram)(void);
typedef void (*PFN_AttachShader)(GLuint, GLuint);
typedef void (*PFN_LinkProgram)(GLuint);
typedef void (*PFN_GetProgramiv)(GLuint, GLenum, GLint*);
typedef void (*PFN_UseProgram)(GLuint);
typedef void (*PFN_GenVertexArrays)(GLsizei, GLuint*);
typedef void (*PFN_BindVertexArray)(GLuint);
typedef void (*PFN_GenBuffers)(GLsizei, GLuint*);
typedef void (*PFN_BindBuffer)(GLenum, GLuint);
typedef void (*PFN_BufferData)(GLenum, GLsizeiptr, const void*, GLenum);
typedef void (*PFN_BufferSubData)(GLenum, GLintptr, GLsizeiptr, const void*);
typedef void (*PFN_EnableVertexAttribArray)(GLuint);
typedef void (*PFN_VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
typedef void (*PFN_DrawElements)(GLenum, GLsizei, GLenum, const void*);
typedef void (*PFN_ClearColor)(float, float, float, float);
typedef void (*PFN_Clear)(unsigned int);
typedef void (*PFN_Finish)(void);
typedef void (*PFN_ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
typedef GLenum (*PFN_GetError)(void);
typedef void (*PFN_ResetIndexStats)(void);
typedef int (*PFN_GetIndexStats)(MithrilDirectMetalIndexStatsV1*, size_t);
typedef void (*PFN_ResetBufferStats)(void);
typedef int (*PFN_GetBufferStats)(MithrilDirectMetalBufferStatsV1*, size_t);

static int failures;
#define CHECK(c, fmt, ...) do { if (c) printf("ok  : " fmt "\n", ##__VA_ARGS__); \
    else { printf("FAIL: " fmt "\n", ##__VA_ARGS__); ++failures; } } while (0)

static int pixel_red(const uint8_t px[4]) {
    return px[0] >= 250 && px[1] <= 4 && px[2] <= 4 && px[3] >= 250;
}

static const char* VS =
    "#version 150\n"
    "layout(location=0) in vec2 pos;\n"
    "void main(){ gl_Position=vec4(pos,0.0,1.0); }\n";
static const char* FS =
    "#version 150\n"
    "layout(location=0) out vec4 c;\n"
    "void main(){ c=vec4(1.0,0.0,0.0,1.0); }\n";

int main(void) {
    const char* lib = getenv("MITHRIL_LIBRARY");
    if (!lib || !*lib) lib = "./output/libmithril.dylib";
    void* h = dlopen(lib, RTLD_NOW | RTLD_GLOBAL);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }
#define LOAD(type, name, sym) type name = (type)dlsym(h, sym)
    LOAD(PFN_CreateShader, createShader, "glCreateShader");
    LOAD(PFN_ShaderSource, shaderSource, "glShaderSource");
    LOAD(PFN_CompileShader, compileShader, "glCompileShader");
    LOAD(PFN_GetShaderiv, getShaderiv, "glGetShaderiv");
    LOAD(PFN_CreateProgram, createProgram, "glCreateProgram");
    LOAD(PFN_AttachShader, attachShader, "glAttachShader");
    LOAD(PFN_LinkProgram, linkProgram, "glLinkProgram");
    LOAD(PFN_GetProgramiv, getProgramiv, "glGetProgramiv");
    LOAD(PFN_UseProgram, useProgram, "glUseProgram");
    LOAD(PFN_GenVertexArrays, genVertexArrays, "glGenVertexArrays");
    LOAD(PFN_BindVertexArray, bindVertexArray, "glBindVertexArray");
    LOAD(PFN_GenBuffers, genBuffers, "glGenBuffers");
    LOAD(PFN_BindBuffer, bindBuffer, "glBindBuffer");
    LOAD(PFN_BufferData, bufferData, "glBufferData");
    LOAD(PFN_BufferSubData, bufferSubData, "glBufferSubData");
    LOAD(PFN_EnableVertexAttribArray, enableVertexAttribArray, "glEnableVertexAttribArray");
    LOAD(PFN_VertexAttribPointer, vertexAttribPointer, "glVertexAttribPointer");
    LOAD(PFN_DrawElements, drawElements, "glDrawElements");
    LOAD(PFN_ClearColor, clearColor, "glClearColor");
    LOAD(PFN_Clear, clear, "glClear");
    LOAD(PFN_Finish, finish, "glFinish");
    LOAD(PFN_ReadPixels, readPixels, "glReadPixels");
    LOAD(PFN_GetError, getError, "glGetError");
    LOAD(PFN_ResetIndexStats, resetIndexStats, "mithrilResetDirectMetalIndexStats");
    LOAD(PFN_GetIndexStats, getIndexStats, "mithrilGetDirectMetalIndexStatsV1");
    LOAD(PFN_ResetBufferStats, resetBufferStats, "mithrilResetDirectMetalBufferStats");
    LOAD(PFN_GetBufferStats, getBufferStats, "mithrilGetDirectMetalBufferStatsV1");
#undef LOAD
    CHECK(createShader && shaderSource && compileShader && getShaderiv &&
          createProgram && attachShader && linkProgram && getProgramiv &&
          useProgram && genVertexArrays && bindVertexArray && genBuffers &&
          bindBuffer && bufferData && bufferSubData && enableVertexAttribArray &&
          vertexAttribPointer && drawElements && clearColor && clear && finish &&
          readPixels && getError && resetIndexStats && getIndexStats &&
          resetBufferStats && getBufferStats,
          "required GL and DirectMetal index diagnostic symbols resolve");
    if (failures) return failures;

    GLuint vs=createShader(GL_VERTEX_SHADER), fs=createShader(GL_FRAGMENT_SHADER);
    shaderSource(vs,1,&VS,NULL); shaderSource(fs,1,&FS,NULL);
    compileShader(vs); compileShader(fs);
    GLint ok=0; getShaderiv(vs,GL_COMPILE_STATUS,&ok); CHECK(ok,"vertex shader compiles");
    getShaderiv(fs,GL_COMPILE_STATUS,&ok); CHECK(ok,"fragment shader compiles");
    GLuint program=createProgram(); attachShader(program,vs); attachShader(program,fs);
    linkProgram(program); getProgramiv(program,GL_LINK_STATUS,&ok); CHECK(ok,"program links");
    useProgram(program);

    const float vertices[8] = {-0.8f,-0.8f, 0.8f,-0.8f, 0.0f,0.8f, 0.8f,0.8f};
    GLuint vao=0,vbo=0,ebo=0;
    genVertexArrays(1,&vao); bindVertexArray(vao);
    genBuffers(1,&vbo); bindBuffer(GL_ARRAY_BUFFER,vbo);
    bufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_DYNAMIC_DRAW);
    enableVertexAttribArray(0); vertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),0);

    enum { EBO_SIZE = 256 * 1024, INDEX_OFFSET = 64 };
    uint8_t* storage=(uint8_t*)calloc(1,EBO_SIZE);
    CHECK(storage != NULL,"EBO fixture allocates");
    if (!storage) return failures;
    const uint32_t initial32[3]={0,1,2};
    memcpy(storage+INDEX_OFFSET,initial32,sizeof(initial32));
    genBuffers(1,&ebo); bindBuffer(GL_ELEMENT_ARRAY_BUFFER,ebo);
    bufferData(GL_ELEMENT_ARRAY_BUFFER,EBO_SIZE,storage,GL_DYNAMIC_DRAW);
    free(storage);

    clearColor(0,0,0,1); clear(GL_COLOR_BUFFER_BIT);
    resetIndexStats();
    drawElements(GL_TRIANGLES,3,GL_UNSIGNED_INT,(const void*)(uintptr_t)INDEX_OFFSET);
    finish();
    uint8_t px[4]={0}; readPixels(256,256,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px);
    CHECK(pixel_red(px),"resident UInt32 EBO renders expected triangle");
    MithrilDirectMetalIndexStatsV1 indexStats={0};
    CHECK(getIndexStats(&indexStats,sizeof(indexStats)),"UInt32 index stats read succeeds");
    CHECK(indexStats.resident_index_draws==1 && indexStats.transient_index_draws==0,
          "UInt32 draw uses resident EBO (%llu resident, %llu transient)",
          (unsigned long long)indexStats.resident_index_draws,
          (unsigned long long)indexStats.transient_index_draws);
    CHECK(indexStats.resident_index_bytes==sizeof(initial32),
          "UInt32 resident draw references exactly %llu index bytes",
          (unsigned long long)indexStats.resident_index_bytes);

    /* Updating only the three UInt32 indices must reuse the resident-buffer
     * generation mechanism from phase 2 instead of rebuilding the 256 KiB EBO. */
    const uint32_t updated32[3]={2,1,0};
    resetIndexStats(); resetBufferStats();
    bufferSubData(GL_ELEMENT_ARRAY_BUFFER,INDEX_OFFSET,sizeof(updated32),updated32);
    drawElements(GL_TRIANGLES,3,GL_UNSIGNED_INT,(const void*)(uintptr_t)INDEX_OFFSET);
    finish();
    MithrilDirectMetalBufferStatsV1 bufferStats={0};
    CHECK(getBufferStats(&bufferStats,sizeof(bufferStats)),"updated EBO buffer stats read succeeds");
    CHECK(bufferStats.full_cpu_upload_bytes==0 &&
          bufferStats.partial_cpu_upload_bytes==sizeof(updated32),
          "EBO update uploads only dirty 12 bytes (%llu full, %llu partial)",
          (unsigned long long)bufferStats.full_cpu_upload_bytes,
          (unsigned long long)bufferStats.partial_cpu_upload_bytes);
    CHECK(getIndexStats(&indexStats,sizeof(indexStats)) &&
          indexStats.resident_index_draws==1 && indexStats.transient_index_draws==0,
          "updated UInt32 EBO remains resident");

    /* UInt16 has a native Metal index type too. Keep values below 0xffff,
     * because Metal reserves the largest value as its unconditional sentinel. */
    const uint16_t indices16[3]={0,1,2};
    bufferData(GL_ELEMENT_ARRAY_BUFFER,EBO_SIZE,NULL,GL_DYNAMIC_DRAW);
    bufferSubData(GL_ELEMENT_ARRAY_BUFFER,INDEX_OFFSET,sizeof(indices16),indices16);
    resetIndexStats();
    drawElements(GL_TRIANGLES,3,GL_UNSIGNED_SHORT,(const void*)(uintptr_t)INDEX_OFFSET);
    finish();
    CHECK(getIndexStats(&indexStats,sizeof(indexStats)) &&
          indexStats.resident_index_draws==1 && indexStats.transient_index_draws==0 &&
          indexStats.resident_index_bytes==sizeof(indices16),
          "UInt16 draw uses resident EBO (%llu bytes)",
          (unsigned long long)indexStats.resident_index_bytes);

    /* UInt8 is not a Metal index type. It must keep using the compatibility
     * u32 staging path; this protects fallback rather than silently widening the
     * resident ABI beyond what Metal can bind. */
    const uint8_t indices8[3]={0,1,2};
    bufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(indices8),indices8,GL_DYNAMIC_DRAW);
    resetIndexStats();
    drawElements(GL_TRIANGLES,3,GL_UNSIGNED_BYTE,0);
    finish();
    CHECK(getIndexStats(&indexStats,sizeof(indexStats)) &&
          indexStats.resident_index_draws==0 && indexStats.transient_index_draws==1,
          "UInt8 remains on compatibility index path");

    CHECK(getError()==GL_NO_ERROR,"resident index scenario leaves GL_NO_ERROR");
    dlclose(h);
    return failures ? 1 : 0;
}
