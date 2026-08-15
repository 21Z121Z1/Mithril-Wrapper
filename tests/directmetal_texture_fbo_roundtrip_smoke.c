/* DirectMetal texture-upload -> FBO attachment -> glReadPixels control.
 * Reports several transformation hypotheses instead of weakening the oracle.
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/glcorearb.h>

typedef void (*genTextures_fn)(GLsizei, GLuint*);
typedef void (*bindTexture_fn)(GLenum, GLuint);
typedef void (*texImage2D_fn)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
typedef void (*genFramebuffers_fn)(GLsizei, GLuint*);
typedef void (*bindFramebuffer_fn)(GLenum, GLuint);
typedef void (*framebufferTex2D_fn)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum (*checkFBO_fn)(GLenum);
typedef void (*readPixels_fn)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
typedef void (*finish_fn)(void);
typedef GLenum (*getError_fn)(void);
typedef const GLubyte* (*getString_fn)(GLenum);

#define W 32
#define H 32
static int failures;
#define CHECK(c, fmt, ...) do { if (c) printf("ok  : " fmt "\n", ##__VA_ARGS__); else { printf("FAIL: " fmt "\n", ##__VA_ARGS__); ++failures; } } while (0)
#define LOAD(type, name) type name=(type)dlsym(h,#name); CHECK(name!=NULL,"resolve %s",#name)

static int mismatch(const unsigned char* a, const unsigned char* b) {
    int n=0; for (int i=0;i<W*H*4;i++) n += a[i]!=b[i]; return n;
}
static void transform(const unsigned char* in, unsigned char* out, int flip_y, int swap_rb) {
    for (int y=0;y<H;y++) for (int x=0;x<W;x++) {
        int sy=flip_y ? H-1-y : y;
        const unsigned char* p=in+(sy*W+x)*4;
        unsigned char* q=out+(y*W+x)*4;
        q[0]=swap_rb?p[2]:p[0]; q[1]=p[1]; q[2]=swap_rb?p[0]:p[2]; q[3]=p[3];
    }
}
int main(int argc,char**argv) {
    const char* path=argc>1?argv[1]:"./build/libmithril.dylib";
    void* h=dlopen(path,RTLD_NOW|RTLD_GLOBAL); CHECK(h!=NULL,"dlopen %s",path); if(!h)return 2;
    LOAD(genTextures_fn,glGenTextures); LOAD(bindTexture_fn,glBindTexture); LOAD(texImage2D_fn,glTexImage2D);
    LOAD(genFramebuffers_fn,glGenFramebuffers); LOAD(bindFramebuffer_fn,glBindFramebuffer);
    LOAD(framebufferTex2D_fn,glFramebufferTexture2D); LOAD(checkFBO_fn,glCheckFramebufferStatus);
    LOAD(readPixels_fn,glReadPixels); LOAD(finish_fn,glFinish); LOAD(getError_fn,glGetError); LOAD(getString_fn,glGetString);
    if(failures)return 1;
    const char* ver=(const char*)glGetString(GL_VERSION);
    CHECK(ver&&strstr(ver,"Metal 3 (DirectMetal)"),"DirectMetal active (%s)",ver?ver:"null");
    unsigned char expected[W*H*4], actual[W*H*4], candidate[W*H*4];
    for(int y=0;y<H;y++)for(int x=0;x<W;x++){
        int o=(y*W+x)*4; expected[o]=(x*7+y*3)&255; expected[o+1]=(x*5+y*11)&255; expected[o+2]=(x*13+y*2)&255; expected[o+3]=255;
    }
    memset(actual,0xCD,sizeof(actual));
    GLuint tex=0,fbo=0; glGenTextures(1,&tex); glBindTexture(GL_TEXTURE_2D,tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,W,H,0,GL_RGBA,GL_UNSIGNED_BYTE,expected);
    CHECK(glGetError()==GL_NO_ERROR,"texture upload has no GL error");
    glGenFramebuffers(1,&fbo); glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
    CHECK(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"FBO complete");
    glFinish(); glReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,actual); glFinish();
    CHECK(glGetError()==GL_NO_ERROR,"readback has no GL error");
    int exact=mismatch(expected,actual);
    transform(expected,candidate,1,0); int flip=mismatch(candidate,actual);
    transform(expected,candidate,0,1); int bgra=mismatch(candidate,actual);
    transform(expected,candidate,1,1); int flip_bgra=mismatch(candidate,actual);
    printf("ROUNDTRIP mismatch exact=%d flipY=%d swapRB=%d flipY+swapRB=%d / %d\n",exact,flip,bgra,flip_bgra,W*H*4);
    printf("expected first=(%u,%u,%u,%u) actual first=(%u,%u,%u,%u)\n",expected[0],expected[1],expected[2],expected[3],actual[0],actual[1],actual[2],actual[3]);
    CHECK(exact==0,"GL texture upload -> attached FBO -> glReadPixels is byte-exact");
    return failures?1:0;
}
