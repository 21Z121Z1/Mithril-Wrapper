/* Behavioral oracle for uniform reflection semantics exposed by GL 3.1/4.x. */
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <GL/glcorearb.h>

#ifndef GL_UNIFORM_TYPE
#define GL_UNIFORM_TYPE 0x8A37
#endif
#ifndef GL_UNIFORM_SIZE
#define GL_UNIFORM_SIZE 0x8A38
#endif
#ifndef GL_UNIFORM_NAME_LENGTH
#define GL_UNIFORM_NAME_LENGTH 0x8A39
#endif
#ifndef GL_UNIFORM_BLOCK_INDEX
#define GL_UNIFORM_BLOCK_INDEX 0x8A3A
#endif
#ifndef GL_INVALID_INDEX
#define GL_INVALID_INDEX 0xFFFFFFFFu
#endif

typedef const GLubyte* (*getString_t)(GLenum);
typedef GLuint (*createShader_t)(GLenum);
typedef void (*shaderSource_t)(GLuint, GLsizei, const GLchar* const*, const GLint*);
typedef void (*compileShader_t)(GLuint);
typedef void (*getShaderiv_t)(GLuint, GLenum, GLint*);
typedef GLuint (*createProgram_t)(void);
typedef void (*attachShader_t)(GLuint, GLuint);
typedef void (*linkProgram_t)(GLuint);
typedef void (*getProgramiv_t)(GLuint, GLenum, GLint*);
typedef void (*deleteShader_t)(GLuint);
typedef void (*deleteProgram_t)(GLuint);
typedef void (*getUniformIndices_t)(GLuint, GLsizei, const GLchar* const*, GLuint*);
typedef void (*getActiveUniformsiv_t)(GLuint, GLsizei, const GLuint*, GLenum, GLint*);
typedef void (*getActiveUniformName_t)(GLuint, GLuint, GLsizei, GLsizei*, GLchar*);

#define LOAD(type, var, name) type var=(type)dlsym(h,name); if(!(var)){fprintf(stderr,"missing %s\n",name);return 3;}
#define CHECK(c, msg) do { if (!(c)) { fprintf(stderr,"FAIL: %s\n",msg); ++failures; } else { printf("ok: %s\n",msg); } } while (0)

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    void* h = dlopen(argv[1], RTLD_NOW | RTLD_GLOBAL);
    if (!h) { fprintf(stderr,"dlopen: %s\n",dlerror()); return 2; }
    LOAD(getString_t,getString,"glGetString");
    LOAD(createShader_t,createShader,"glCreateShader");
    LOAD(shaderSource_t,shaderSource,"glShaderSource");
    LOAD(compileShader_t,compileShader,"glCompileShader");
    LOAD(getShaderiv_t,getShaderiv,"glGetShaderiv");
    LOAD(createProgram_t,createProgram,"glCreateProgram");
    LOAD(attachShader_t,attachShader,"glAttachShader");
    LOAD(linkProgram_t,linkProgram,"glLinkProgram");
    LOAD(getProgramiv_t,getProgramiv,"glGetProgramiv");
    LOAD(deleteShader_t,deleteShader,"glDeleteShader");
    LOAD(deleteProgram_t,deleteProgram,"glDeleteProgram");
    LOAD(getUniformIndices_t,getUniformIndices,"glGetUniformIndices");
    LOAD(getActiveUniformsiv_t,getActiveUniformsiv,"glGetActiveUniformsiv");
    LOAD(getActiveUniformName_t,getActiveUniformName,"glGetActiveUniformName");

    if (!getString(GL_VERSION)) return 1;
    const char* vs="#version 330 core\nuniform float uScalar; void main(){gl_Position=vec4(uScalar,0.0,0.0,1.0);}\n";
    const char* fs="#version 330 core\nout vec4 c; void main(){c=vec4(1.0);}\n";
    GLuint sv=createShader(GL_VERTEX_SHADER), sf=createShader(GL_FRAGMENT_SHADER);
    shaderSource(sv,1,&vs,NULL); shaderSource(sf,1,&fs,NULL);
    compileShader(sv); compileShader(sf);
    GLint okv=0,okf=0; getShaderiv(sv,GL_COMPILE_STATUS,&okv); getShaderiv(sf,GL_COMPILE_STATUS,&okf);
    int failures=0;
    CHECK(okv==GL_TRUE && okf==GL_TRUE,"reflection shaders compile");
    GLuint p=createProgram(); attachShader(p,sv); attachShader(p,sf); linkProgram(p);
    GLint linked=0; getProgramiv(p,GL_LINK_STATUS,&linked); CHECK(linked==GL_TRUE,"reflection program links");

    const GLchar* uname="uScalar"; GLuint idx=GL_INVALID_INDEX;
    getUniformIndices(p,1,&uname,&idx);
    CHECK(idx!=GL_INVALID_INDEX,"glGetUniformIndices resolves active uniform");
    if (idx!=GL_INVALID_INDEX) {
        GLint type=0,size=0,nameLen=0,block=0;
        getActiveUniformsiv(p,1,&idx,GL_UNIFORM_TYPE,&type);
        getActiveUniformsiv(p,1,&idx,GL_UNIFORM_SIZE,&size);
        getActiveUniformsiv(p,1,&idx,GL_UNIFORM_NAME_LENGTH,&nameLen);
        getActiveUniformsiv(p,1,&idx,GL_UNIFORM_BLOCK_INDEX,&block);
        CHECK(type==GL_FLOAT,"active uniform type is GL_FLOAT");
        CHECK(size==1,"active uniform size is one");
        CHECK(nameLen==(GLint)strlen("uScalar")+1,"active uniform name length is exact");
        CHECK(block==-1,"standalone uniform block index is -1");
        char name[64]={0}; GLsizei copied=0;
        getActiveUniformName(p,idx,sizeof(name),&copied,name);
        CHECK(strcmp(name,"uScalar")==0 && copied==(GLsizei)strlen("uScalar"),"active uniform name round-trips");
    }
    deleteShader(sv); deleteShader(sf); deleteProgram(p); dlclose(h);
    printf("GL46 REFLECTION SEMANTICS failures=%d\n",failures);
    if (failures) return 1;
    puts("GL46 REFLECTION SEMANTICS ALL PASSED");
    return 0;
}