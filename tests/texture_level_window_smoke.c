/* DirectMetal GL_TEXTURE_MAX_LEVEL completeness regression. */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_FLOAT 0x1406
#define GL_FALSE 0
#define GL_TRIANGLE_STRIP 0x0005
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE_2D 0x0DE1
#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_UNSIGNED_BYTE 0x1401
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MAX_LEVEL 0x813D
#define GL_NEAREST 0x2600
#define GL_NEAREST_MIPMAP_LINEAR 0x2702
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_NO_ERROR 0
#define GL_INVALID_VALUE 0x0501

typedef unsigned int GLenum; typedef unsigned int GLuint; typedef int GLint;
typedef int GLsizei; typedef long GLsizeiptr; typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef GLenum (*fnGetError)(void);
typedef GLuint (*fnCreateShader)(GLenum);
typedef void (*fnShaderSource)(GLuint, GLsizei, const char* const*, const GLint*);
typedef void (*fnCompileShader)(GLuint); typedef GLuint (*fnCreateProgram)(void);
typedef void (*fnAttachShader)(GLuint, GLuint); typedef void (*fnLinkProgram)(GLuint);
typedef void (*fnUseProgram)(GLuint); typedef GLint (*fnGetUniformLocation)(GLuint,const char*);
typedef void (*fnUniform1i)(GLint,GLint); typedef void (*fnGenVertexArrays)(GLsizei,GLuint*);
typedef void (*fnBindVertexArray)(GLuint); typedef void (*fnGenBuffers)(GLsizei,GLuint*);
typedef void (*fnBindBuffer)(GLenum,GLuint); typedef void (*fnBufferData)(GLenum,GLsizeiptr,const void*,GLenum);
typedef void (*fnEnableVertexAttribArray)(GLuint); typedef void (*fnVertexAttribPointer)(GLuint,GLint,GLenum,GLboolean,GLsizei,const void*);
typedef void (*fnGenTextures)(GLsizei,GLuint*); typedef void (*fnActiveTexture)(GLenum);
typedef void (*fnBindTexture)(GLenum,GLuint); typedef void (*fnTexImage2D)(GLenum,GLint,GLint,GLsizei,GLsizei,GLint,GLenum,GLenum,const void*);
typedef void (*fnTexParameteri)(GLenum,GLenum,GLint); typedef void (*fnGetTexParameteriv)(GLenum,GLenum,GLint*);
typedef void (*fnViewport)(GLint,GLint,GLsizei,GLsizei); typedef void (*fnClearColor)(float,float,float,float);
typedef void (*fnClear)(GLbitfield); typedef void (*fnDrawArrays)(GLenum,GLint,GLsizei);
typedef void (*fnFinish)(void); typedef void (*fnReadPixels)(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void*);

static int failures;
#define CHECK(c,m,...) do { if(c) printf("ok  : " m "\n", ##__VA_ARGS__); else { printf("FAIL: " m "\n", ##__VA_ARGS__); ++failures; } } while(0)
#define LOAD(t,n) t n=(t)dlsym(h,#n)
static int near8(unsigned char v,int want){ return abs((int)v-want)<=3; }

int main(void) {
    const char* path=getenv("MITHRIL_LIBRARY");
#if defined(__APPLE__)
    if(!path) path="./output/libmithril.dylib";
#else
    if(!path) path="./output/libmithril.so";
#endif
    void* h=dlopen(path,RTLD_NOW|RTLD_GLOBAL); if(!h){fprintf(stderr,"dlopen: %s\n",dlerror());return 2;}
    LOAD(fnGetError,glGetError); LOAD(fnCreateShader,glCreateShader); LOAD(fnShaderSource,glShaderSource);
    LOAD(fnCompileShader,glCompileShader); LOAD(fnCreateProgram,glCreateProgram); LOAD(fnAttachShader,glAttachShader);
    LOAD(fnLinkProgram,glLinkProgram); LOAD(fnUseProgram,glUseProgram); LOAD(fnGetUniformLocation,glGetUniformLocation);
    LOAD(fnUniform1i,glUniform1i); LOAD(fnGenVertexArrays,glGenVertexArrays); LOAD(fnBindVertexArray,glBindVertexArray);
    LOAD(fnGenBuffers,glGenBuffers); LOAD(fnBindBuffer,glBindBuffer); LOAD(fnBufferData,glBufferData);
    LOAD(fnEnableVertexAttribArray,glEnableVertexAttribArray); LOAD(fnVertexAttribPointer,glVertexAttribPointer);
    LOAD(fnGenTextures,glGenTextures); LOAD(fnActiveTexture,glActiveTexture); LOAD(fnBindTexture,glBindTexture);
    LOAD(fnTexImage2D,glTexImage2D); LOAD(fnTexParameteri,glTexParameteri); LOAD(fnGetTexParameteriv,glGetTexParameteriv);
    LOAD(fnViewport,glViewport); LOAD(fnClearColor,glClearColor); LOAD(fnClear,glClear); LOAD(fnDrawArrays,glDrawArrays);
    LOAD(fnFinish,glFinish); LOAD(fnReadPixels,glReadPixels);
    CHECK(glGetError&&glCreateShader&&glShaderSource&&glCompileShader&&glCreateProgram&&glAttachShader&&glLinkProgram&&glUseProgram&&glGetUniformLocation&&glUniform1i&&glGenVertexArrays&&glBindVertexArray&&glGenBuffers&&glBindBuffer&&glBufferData&&glEnableVertexAttribArray&&glVertexAttribPointer&&glGenTextures&&glActiveTexture&&glBindTexture&&glTexImage2D&&glTexParameteri&&glGetTexParameteriv&&glViewport&&glClearColor&&glClear&&glDrawArrays&&glFinish&&glReadPixels,"required symbols resolve");
    if(failures) return 1;
    const char* vs_src="#version 150\nlayout(location=0) in vec2 p;\nvoid main(){gl_Position=vec4(p,0,1);}\n";
    const char* fs_src="#version 150\nuniform sampler2D image;\nlayout(location=0) out vec4 c;\nvoid main(){c=texture(image,vec2(0.5));}\n";
    GLuint vs=glCreateShader(GL_VERTEX_SHADER),fs=glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(vs,1,&vs_src,NULL); glShaderSource(fs,1,&fs_src,NULL); glCompileShader(vs); glCompileShader(fs);
    GLuint prog=glCreateProgram(); glAttachShader(prog,vs); glAttachShader(prog,fs); glLinkProgram(prog); glUseProgram(prog); GLint loc=glGetUniformLocation(prog,"image"); glUniform1i(loc,0);
    const float quad[]={-1,-1,1,-1,-1,1,1,1}; GLuint vao=0,vbo=0; glGenVertexArrays(1,&vao); glBindVertexArray(vao); glGenBuffers(1,&vbo); glBindBuffer(GL_ARRAY_BUFFER,vbo); glBufferData(GL_ARRAY_BUFFER,sizeof(quad),quad,GL_STATIC_DRAW); glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),NULL);
    const unsigned char red[]={255,0,0,255,255,0,0,255,255,0,0,255,255,0,0,255}; GLuint tex=0; glGenTextures(1,&tex); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,tex); glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,red); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST_MIPMAP_LINEAR); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,0);
    GLint max_level=-1; glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,&max_level); CHECK(max_level==0,"GL_TEXTURE_MAX_LEVEL round-trips as texture state");
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,-1); CHECK(glGetError()==GL_INVALID_VALUE,"negative GL_TEXTURE_MAX_LEVEL is rejected");
    glViewport(0,0,16,16); glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT); glDrawArrays(GL_TRIANGLE_STRIP,0,4); glFinish(); unsigned char px[4]={0}; glReadPixels(8,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px); CHECK(near8(px[0],255)&&near8(px[1],0)&&near8(px[2],0)&&near8(px[3],255),"MAX_LEVEL=0 makes level-zero mipmapped texture complete (%u,%u,%u,%u)",px[0],px[1],px[2],px[3]); CHECK(glGetError()==GL_NO_ERROR,"level-window draw leaves GL_NO_ERROR");
    printf("\nTEXTURE LEVEL WINDOW SMOKE %s\n",failures?"FAILED":"ALL PASSED"); return failures?1:0;
}
