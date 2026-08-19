/* DirectMetal Minecraft 26.2 linked-interface regression. */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_FLOAT 0x1406
#define GL_FALSE 0
#define GL_TRIANGLE_STRIP 0x0005
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_RENDERER 0x1F01
#define GL_NO_ERROR 0

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef long GLsizeiptr;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;

typedef const unsigned char* (*fnGetString)(GLenum);
typedef GLenum (*fnGetError)(void);
typedef GLuint (*fnCreateShader)(GLenum);
typedef void (*fnShaderSource)(GLuint, GLsizei, const char* const*, const GLint*);
typedef void (*fnCompileShader)(GLuint);
typedef void (*fnGetShaderiv)(GLuint, GLenum, GLint*);
typedef GLuint (*fnCreateProgram)(void);
typedef void (*fnAttachShader)(GLuint, GLuint);
typedef void (*fnLinkProgram)(GLuint);
typedef void (*fnGetProgramiv)(GLuint, GLenum, GLint*);
typedef void (*fnUseProgram)(GLuint);
typedef void (*fnGenVertexArrays)(GLsizei, GLuint*);
typedef void (*fnBindVertexArray)(GLuint);
typedef void (*fnGenBuffers)(GLsizei, GLuint*);
typedef void (*fnBindBuffer)(GLenum, GLuint);
typedef void (*fnBufferData)(GLenum, GLsizeiptr, const void*, GLenum);
typedef void (*fnEnableVertexAttribArray)(GLuint);
typedef void (*fnVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
typedef void (*fnViewport)(GLint, GLint, GLsizei, GLsizei);
typedef void (*fnClearColor)(float, float, float, float);
typedef void (*fnClear)(GLbitfield);
typedef void (*fnDrawArrays)(GLenum, GLint, GLsizei);
typedef void (*fnFinish)(void);
typedef void (*fnReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);

static int failures;
#define CHECK(c, m, ...) do { if (c) printf("ok  : " m "\n", ##__VA_ARGS__); else { printf("FAIL: " m "\n", ##__VA_ARGS__); ++failures; } } while (0)
#define LOAD(t, n) t n = (t)dlsym(h, #n)

static int near_u8(unsigned char v, int want) {
    return abs((int)v - want) <= 3;
}

int main(void) {
    const char* path = getenv("MITHRIL_LIBRARY");
#if defined(__APPLE__)
    if (!path) path = "./output/libmithril.dylib";
#else
    if (!path) path = "./output/libmithril.so";
#endif
    void* h = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }
    LOAD(fnGetString, glGetString); LOAD(fnGetError, glGetError);
    LOAD(fnCreateShader, glCreateShader); LOAD(fnShaderSource, glShaderSource);
    LOAD(fnCompileShader, glCompileShader); LOAD(fnGetShaderiv, glGetShaderiv);
    LOAD(fnCreateProgram, glCreateProgram); LOAD(fnAttachShader, glAttachShader);
    LOAD(fnLinkProgram, glLinkProgram); LOAD(fnGetProgramiv, glGetProgramiv);
    LOAD(fnUseProgram, glUseProgram); LOAD(fnGenVertexArrays, glGenVertexArrays);
    LOAD(fnBindVertexArray, glBindVertexArray); LOAD(fnGenBuffers, glGenBuffers);
    LOAD(fnBindBuffer, glBindBuffer); LOAD(fnBufferData, glBufferData);
    LOAD(fnEnableVertexAttribArray, glEnableVertexAttribArray);
    LOAD(fnVertexAttribPointer, glVertexAttribPointer); LOAD(fnViewport, glViewport);
    LOAD(fnClearColor, glClearColor); LOAD(fnClear, glClear);
    LOAD(fnDrawArrays, glDrawArrays); LOAD(fnFinish, glFinish);
    LOAD(fnReadPixels, glReadPixels);

    CHECK(glGetString && glGetError && glCreateShader && glShaderSource &&
          glCompileShader && glGetShaderiv && glCreateProgram && glAttachShader &&
          glLinkProgram && glGetProgramiv && glUseProgram && glGenVertexArrays &&
          glBindVertexArray && glGenBuffers && glBindBuffer && glBufferData &&
          glEnableVertexAttribArray && glVertexAttribPointer && glViewport &&
          glClearColor && glClear && glDrawArrays && glFinish && glReadPixels,
          "required symbols resolve");
    if (failures) return 1;
    const char* renderer = (const char*)glGetString(GL_RENDERER);
    CHECK(renderer && strstr(renderer, "DirectMetal"),
          "renderer is DirectMetal (%s)", renderer ? renderer : "null");

    const char* vs_src =
        "#version 150\n"
        "layout(location=0) in vec2 position;\n"
        "out vec4 firstColor;\n"
        "out vec4 secondColor;\n"
        "void main(){ gl_Position=vec4(position,0,1); firstColor=vec4(1,0,0,1); secondColor=vec4(0,1,0,1); }\n";
    const char* fs_src =
        "#version 150\n"
        "in vec4 secondColor;\n"
        "in vec4 firstColor;\n"
        "layout(location=0) out vec4 color;\n"
        "void main(){ color=0.75*firstColor+0.25*secondColor; }\n";
    GLuint vs = glCreateShader(GL_VERTEX_SHADER), fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(vs, 1, &vs_src, NULL); glShaderSource(fs, 1, &fs_src, NULL);
    glCompileShader(vs); glCompileShader(fs);
    GLint ok_vs=0, ok_fs=0; glGetShaderiv(vs, GL_COMPILE_STATUS, &ok_vs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &ok_fs);
    CHECK(ok_vs && ok_fs, "reversed varying-order shaders compile");
    GLuint program = glCreateProgram(); glAttachShader(program, vs); glAttachShader(program, fs);
    glLinkProgram(program); GLint linked=0; glGetProgramiv(program, GL_LINK_STATUS, &linked);
    CHECK(linked, "cross-stage interface links after name/location reconciliation");
    glUseProgram(program);

    const float quad[] = {-1,-1, 1,-1, -1,1, 1,1};
    GLuint vao=0, vbo=0; glGenVertexArrays(1,&vao); glBindVertexArray(vao);
    glGenBuffers(1,&vbo); glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glBufferData(GL_ARRAY_BUFFER,sizeof(quad),quad,GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),NULL);
    glViewport(0,0,16,16); glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLE_STRIP,0,4); glFinish();
    unsigned char px[4]={0}; glReadPixels(8,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px);
    CHECK(near_u8(px[0],191) && near_u8(px[1],64) && near_u8(px[2],0),
          "varyings retain source names across reversed declaration order (%u,%u,%u,%u)",
          px[0],px[1],px[2],px[3]);
    CHECK(glGetError()==GL_NO_ERROR, "runtime closure smoke leaves GL_NO_ERROR");
    printf("\nMINECRAFT26 LINK SMOKE %s\n", failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
