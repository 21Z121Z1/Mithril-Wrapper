/*
 * gui_glyph_pipeline_smoke.c
 * End-to-end GUI glyph pipeline gate for DirectVulkan/MoltenVK.
 *
 * A persistent/coherent PBO holds a padded 20x12 source. A 16x8 RGBA atlas
 * containing asymmetric G and I glyphs is selected with ROW_LENGTH and SKIP
 * state, uploaded to a texture, sampled through a real GLSL program, alpha
 * blended onto the default framebuffer, then compared pixel-for-pixel via
 * glReadPixels. Any stale-PBO source, unpack addressing error, sampling error,
 * or vertical default-framebuffer inversion changes the expected bitmap.
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>
#include <GL/glcorearb.h>
#ifndef GL_MAP_PERSISTENT_BIT
#define GL_MAP_PERSISTENT_BIT 0x0040
#endif
#ifndef GL_MAP_COHERENT_BIT
#define GL_MAP_COHERENT_BIT 0x0080
#endif
#define LOAD(h,v,t,s) do { v=(t)dlsym(h,s); if(!(v)){fprintf(stderr,"missing %s\n",s);return 3;} } while(0)

enum { W=16, H=8, ROW=20, SRC_H=12, PREFIX=32 };
static uint8_t expected[W*H*4];
static void put_expected(int x,int y,uint8_t r,uint8_t g,uint8_t b,uint8_t a){uint8_t*p=expected+(y*W+x)*4;p[0]=r;p[1]=g;p[2]=b;p[3]=a;}
static void draw_glyphs(void){
    memset(expected,0,sizeof(expected));
    static const char* G[7]={"11111","10000","10111","10001","10001","10001","11111"};
    static const char* I[7]={"111","010","010","010","010","010","111"};
    for(int y=0;y<7;y++)for(int x=0;x<5;x++)if(G[y][x]=='1')put_expected(1+x,y,255,240,32,255);
    for(int y=0;y<7;y++)for(int x=0;x<3;x++)if(I[y][x]=='1')put_expected(10+x,y,32,220,255,255);
}
static int close_rgb(const uint8_t*a,const uint8_t*b){return abs((int)a[0]-b[0])<=2&&abs((int)a[1]-b[1])<=2&&abs((int)a[2]-b[2])<=2;}

int main(int argc,char**argv){
    if(argc<2)return 2; void*h=dlopen(argv[1],RTLD_NOW|RTLD_GLOBAL); if(!h){fprintf(stderr,"dlopen: %s\n",dlerror());return 2;}
    EGLDisplay(*egD)(EGLNativeDisplayType); EGLBoolean(*egI)(EGLDisplay,EGLint*,EGLint*); EGLBoolean(*egB)(EGLenum); EGLBoolean(*egG)(EGLDisplay,EGLConfig*,EGLint,EGLint*); EGLContext(*egC)(EGLDisplay,EGLConfig,EGLContext,const EGLint*); EGLSurface(*egP)(EGLDisplay,EGLConfig,const EGLint*); EGLBoolean(*egM)(EGLDisplay,EGLSurface,EGLSurface,EGLContext);
    LOAD(h,egD,EGLDisplay(*)(EGLNativeDisplayType),"eglGetDisplay"); LOAD(h,egI,EGLBoolean(*)(EGLDisplay,EGLint*,EGLint*),"eglInitialize"); LOAD(h,egB,EGLBoolean(*)(EGLenum),"eglBindAPI"); LOAD(h,egG,EGLBoolean(*)(EGLDisplay,EGLConfig*,EGLint,EGLint*),"eglGetConfigs"); LOAD(h,egC,EGLContext(*)(EGLDisplay,EGLConfig,EGLContext,const EGLint*),"eglCreateContext"); LOAD(h,egP,EGLSurface(*)(EGLDisplay,EGLConfig,const EGLint*),"eglCreatePbufferSurface"); LOAD(h,egM,EGLBoolean(*)(EGLDisplay,EGLSurface,EGLSurface,EGLContext),"eglMakeCurrent");
    EGLDisplay d=egD(EGL_DEFAULT_DISPLAY); EGLint ma=0,mi=0,n=0; if(d==EGL_NO_DISPLAY||!egI(d,&ma,&mi)||!egB(EGL_OPENGL_API))return 4; EGLConfig cfg=NULL; if(!egG(d,&cfg,1,&n)||n<1)return 5; const EGLint ca[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,EGL_NONE}; const EGLint pa[]={EGL_WIDTH,W,EGL_HEIGHT,H,EGL_NONE}; EGLContext c=egC(d,cfg,EGL_NO_CONTEXT,ca); EGLSurface s=egP(d,cfg,pa); if(c==EGL_NO_CONTEXT||s==EGL_NO_SURFACE||!egM(d,s,s,c))return 6;

    void(*genBuffers)(GLsizei,GLuint*);void(*bindBuffer)(GLenum,GLuint);void(*bufferStorage)(GLenum,GLsizeiptr,const void*,GLbitfield);void*(*mapBufferRange)(GLenum,GLintptr,GLsizeiptr,GLbitfield);void(*genTextures)(GLsizei,GLuint*);void(*bindTexture)(GLenum,GLuint);void(*texImage2D)(GLenum,GLint,GLint,GLsizei,GLsizei,GLint,GLenum,GLenum,const void*);void(*texSubImage2D)(GLenum,GLint,GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,const void*);void(*texParameteri)(GLenum,GLenum,GLint);void(*pixelStorei)(GLenum,GLint);GLuint(*createShader)(GLenum);void(*shaderSource)(GLuint,GLsizei,const GLchar*const*,const GLint*);void(*compileShader)(GLuint);void(*getShaderiv)(GLuint,GLenum,GLint*);GLuint(*createProgram)(void);void(*attachShader)(GLuint,GLuint);void(*linkProgram)(GLuint);void(*getProgramiv)(GLuint,GLenum,GLint*);void(*useProgram)(GLuint);GLint(*getUniformLocation)(GLuint,const GLchar*);void(*uniform1i)(GLint,GLint);void(*genVertexArrays)(GLsizei,GLuint*);void(*bindVertexArray)(GLuint);void(*bindFramebuffer)(GLenum,GLuint);void(*viewport)(GLint,GLint,GLsizei,GLsizei);void(*disable)(GLenum);void(*enable)(GLenum);void(*blendFunc)(GLenum,GLenum);void(*clearColor)(GLfloat,GLfloat,GLfloat,GLfloat);void(*clear)(GLbitfield);void(*drawArrays)(GLenum,GLint,GLsizei);void(*finish)(void);void(*readPixels)(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void*);GLenum(*getError)(void);const GLubyte*(*getString)(GLenum);
#define L(v,t,sym) LOAD(h,v,t,sym)
    L(genBuffers,void(*)(GLsizei,GLuint*),"glGenBuffers");L(bindBuffer,void(*)(GLenum,GLuint),"glBindBuffer");L(bufferStorage,void(*)(GLenum,GLsizeiptr,const void*,GLbitfield),"glBufferStorage");L(mapBufferRange,void*(*)(GLenum,GLintptr,GLsizeiptr,GLbitfield),"glMapBufferRange");L(genTextures,void(*)(GLsizei,GLuint*),"glGenTextures");L(bindTexture,void(*)(GLenum,GLuint),"glBindTexture");L(texImage2D,void(*)(GLenum,GLint,GLint,GLsizei,GLsizei,GLint,GLenum,GLenum,const void*),"glTexImage2D");L(texSubImage2D,void(*)(GLenum,GLint,GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,const void*),"glTexSubImage2D");L(texParameteri,void(*)(GLenum,GLenum,GLint),"glTexParameteri");L(pixelStorei,void(*)(GLenum,GLint),"glPixelStorei");L(createShader,GLuint(*)(GLenum),"glCreateShader");L(shaderSource,void(*)(GLuint,GLsizei,const GLchar*const*,const GLint*),"glShaderSource");L(compileShader,void(*)(GLuint),"glCompileShader");L(getShaderiv,void(*)(GLuint,GLenum,GLint*),"glGetShaderiv");L(createProgram,GLuint(*)(void),"glCreateProgram");L(attachShader,void(*)(GLuint,GLuint),"glAttachShader");L(linkProgram,void(*)(GLuint),"glLinkProgram");L(getProgramiv,void(*)(GLuint,GLenum,GLint*),"glGetProgramiv");L(useProgram,void(*)(GLuint),"glUseProgram");L(getUniformLocation,GLint(*)(GLuint,const GLchar*),"glGetUniformLocation");L(uniform1i,void(*)(GLint,GLint),"glUniform1i");L(genVertexArrays,void(*)(GLsizei,GLuint*),"glGenVertexArrays");L(bindVertexArray,void(*)(GLuint),"glBindVertexArray");L(bindFramebuffer,void(*)(GLenum,GLuint),"glBindFramebuffer");L(viewport,void(*)(GLint,GLint,GLsizei,GLsizei),"glViewport");L(disable,void(*)(GLenum),"glDisable");L(enable,void(*)(GLenum),"glEnable");L(blendFunc,void(*)(GLenum,GLenum),"glBlendFunc");L(clearColor,void(*)(GLfloat,GLfloat,GLfloat,GLfloat),"glClearColor");L(clear,void(*)(GLbitfield),"glClear");L(drawArrays,void(*)(GLenum,GLint,GLsizei),"glDrawArrays");L(finish,void(*)(void),"glFinish");L(readPixels,void(*)(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void*),"glReadPixels");L(getError,GLenum(*)(void),"glGetError");L(getString,const GLubyte*(*)(GLenum),"glGetString");
#undef L
    draw_glyphs();
    enum{BACKING=ROW*SRC_H*4,TOTAL=PREFIX+BACKING}; GLuint pbo=0;genBuffers(1,&pbo);bindBuffer(GL_PIXEL_UNPACK_BUFFER,pbo);bufferStorage(GL_PIXEL_UNPACK_BUFFER,TOTAL,NULL,GL_MAP_WRITE_BIT|GL_MAP_PERSISTENT_BIT|GL_MAP_COHERENT_BIT);uint8_t*m=(uint8_t*)mapBufferRange(GL_PIXEL_UNPACK_BUFFER,0,TOTAL,GL_MAP_WRITE_BIT|GL_MAP_PERSISTENT_BIT|GL_MAP_COHERENT_BIT);if(!m)return 7;memset(m,0x6b,TOTAL);uint8_t*base=m+PREFIX;for(int y=0;y<H;y++)for(int x=0;x<W;x++)memcpy(base+((y+2)*ROW+(x+2))*4,expected+(y*W+x)*4,4);
    GLuint tex=0;genTextures(1,&tex);bindTexture(GL_TEXTURE_2D,tex);texParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);texParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);texParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);texParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);texImage2D(GL_TEXTURE_2D,0,GL_RGBA8,W,H,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);pixelStorei(GL_UNPACK_ALIGNMENT,1);pixelStorei(GL_UNPACK_ROW_LENGTH,ROW);pixelStorei(GL_UNPACK_SKIP_PIXELS,2);pixelStorei(GL_UNPACK_SKIP_ROWS,2);texSubImage2D(GL_TEXTURE_2D,0,0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,(const void*)(uintptr_t)PREFIX);pixelStorei(GL_UNPACK_ROW_LENGTH,0);pixelStorei(GL_UNPACK_SKIP_PIXELS,0);pixelStorei(GL_UNPACK_SKIP_ROWS,0);pixelStorei(GL_UNPACK_ALIGNMENT,4);bindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    const char*vs="#version 330 core\nout vec2 uv;void main(){vec2 p=(gl_VertexID==0)?vec2(-1,-1):((gl_VertexID==1)?vec2(3,-1):vec2(-1,3));uv=p*0.5+0.5;gl_Position=vec4(p,0,1);}";const char*fs="#version 330 core\nin vec2 uv;uniform sampler2D atlas;out vec4 color;void main(){color=texture(atlas,uv);}";GLuint v=createShader(GL_VERTEX_SHADER),f=createShader(GL_FRAGMENT_SHADER);shaderSource(v,1,&vs,NULL);compileShader(v);shaderSource(f,1,&fs,NULL);compileShader(f);GLint ok=0;getShaderiv(v,GL_COMPILE_STATUS,&ok);if(!ok)return 8;getShaderiv(f,GL_COMPILE_STATUS,&ok);if(!ok)return 9;GLuint pr=createProgram();attachShader(pr,v);attachShader(pr,f);linkProgram(pr);getProgramiv(pr,GL_LINK_STATUS,&ok);if(!ok)return 10;useProgram(pr);GLint loc=getUniformLocation(pr,"atlas");if(loc<0)return 11;uniform1i(loc,0);GLuint vao=0;genVertexArrays(1,&vao);bindVertexArray(vao);bindFramebuffer(GL_FRAMEBUFFER,0);viewport(0,0,W,H);disable(GL_SCISSOR_TEST);disable(GL_CULL_FACE);disable(GL_DEPTH_TEST);enable(GL_BLEND);blendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);clearColor(0,0,0,1);clear(GL_COLOR_BUFFER_BIT);bindTexture(GL_TEXTURE_2D,tex);drawArrays(GL_TRIANGLES,0,3);finish();uint8_t out[W*H*4];memset(out,0,sizeof(out));readPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,out);GLenum err=getError();int mismatches=0;for(int i=0;i<W*H;i++){if(!close_rgb(out+i*4,expected+i*4)){if(mismatches<12)fprintf(stderr,"mismatch xy=(%d,%d) got=(%u,%u,%u) exp=(%u,%u,%u)\n",i%W,i/W,out[i*4],out[i*4+1],out[i*4+2],expected[i*4],expected[i*4+1],expected[i*4+2]);mismatches++;}}
    printf("backend: %s\n",getString(GL_VERSION));printf("glyph_rgb_mismatches=%d err=0x%04x\n",mismatches,(unsigned)err);if(err!=GL_NO_ERROR||mismatches){fprintf(stderr,"GUI GLYPH PIPELINE SMOKE FAILED\n");return 1;}printf("GUI GLYPH PIPELINE SMOKE PASSED\n");return 0;
}
