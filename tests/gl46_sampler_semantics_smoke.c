/* Behavioral oracle for OpenGL 3.3 sampler object state semantics. */
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <GL/glcorearb.h>

#ifndef GL_TEXTURE_BORDER_COLOR
#define GL_TEXTURE_BORDER_COLOR 0x1004
#endif

typedef const GLubyte* (*getString_t)(GLenum);
typedef void (*genSamplers_t)(GLsizei, GLuint*);
typedef void (*deleteSamplers_t)(GLsizei, const GLuint*);
typedef void (*samplerParameteri_t)(GLuint, GLenum, GLint);
typedef void (*getSamplerParameteriv_t)(GLuint, GLenum, GLint*);
typedef void (*samplerParameterIiv_t)(GLuint, GLenum, const GLint*);
typedef void (*getSamplerParameterIiv_t)(GLuint, GLenum, GLint*);
typedef void (*samplerParameterIuiv_t)(GLuint, GLenum, const GLuint*);
typedef void (*getSamplerParameterIuiv_t)(GLuint, GLenum, GLuint*);

#define LOAD(type, var, name) type var=(type)dlsym(h,name); if(!(var)){fprintf(stderr,"missing %s\n",name);return 3;}
#define CHECK(c, msg) do { if (!(c)) { fprintf(stderr,"FAIL: %s\n",msg); ++failures; } else { printf("ok: %s\n",msg); } } while (0)

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    void* h = dlopen(argv[1], RTLD_NOW | RTLD_GLOBAL);
    if (!h) { fprintf(stderr,"dlopen: %s\n",dlerror()); return 2; }
    LOAD(getString_t,getString,"glGetString");
    LOAD(genSamplers_t,genSamplers,"glGenSamplers");
    LOAD(deleteSamplers_t,deleteSamplers,"glDeleteSamplers");
    LOAD(samplerParameteri_t,samplerParameteri,"glSamplerParameteri");
    LOAD(getSamplerParameteriv_t,getSamplerParameteriv,"glGetSamplerParameteriv");
    LOAD(samplerParameterIiv_t,samplerParameterIiv,"glSamplerParameterIiv");
    LOAD(getSamplerParameterIiv_t,getSamplerParameterIiv,"glGetSamplerParameterIiv");
    LOAD(samplerParameterIuiv_t,samplerParameterIuiv,"glSamplerParameterIuiv");
    LOAD(getSamplerParameterIuiv_t,getSamplerParameterIuiv,"glGetSamplerParameterIuiv");
    if (!getString(GL_VERSION)) return 1;

    int failures=0;
    GLuint s=0; genSamplers(1,&s);
    CHECK(s!=0,"sampler name generated");

    samplerParameteri(s,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    GLint scalar=0; getSamplerParameteriv(s,GL_TEXTURE_WRAP_S,&scalar);
    CHECK(scalar==GL_CLAMP_TO_EDGE,"scalar sampler state round-trips");

    const GLint signedBorder[4]={-7,2,99,-1234};
    GLint signedOut[4]={0,0,0,0};
    samplerParameterIiv(s,GL_TEXTURE_BORDER_COLOR,signedBorder);
    getSamplerParameterIiv(s,GL_TEXTURE_BORDER_COLOR,signedOut);
    CHECK(memcmp(signedBorder,signedOut,sizeof(signedBorder))==0,"signed integer border color round-trips exactly");

    const GLuint unsignedBorder[4]={0u,1u,65535u,0x7fffffffu};
    GLuint unsignedOut[4]={0,0,0,0};
    samplerParameterIuiv(s,GL_TEXTURE_BORDER_COLOR,unsignedBorder);
    getSamplerParameterIuiv(s,GL_TEXTURE_BORDER_COLOR,unsignedOut);
    CHECK(memcmp(unsignedBorder,unsignedOut,sizeof(unsignedBorder))==0,"unsigned integer border color round-trips exactly");

    deleteSamplers(1,&s);
    dlclose(h);
    printf("GL46 SAMPLER SEMANTICS failures=%d\n",failures);
    if (failures) return 1;
    puts("GL46 SAMPLER SEMANTICS ALL PASSED");
    return 0;
}
