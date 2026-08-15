/* DirectMetal OpenGL 4.6 draw-parameter conformance oracle.
 * Covers shader-visible gl_VertexID/gl_BaseVertex/gl_BaseInstance/gl_DrawID,
 * non-zero MDI stride/firstIndex, and primitive-restart + baseVertex/MDI.
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>
#include <GL/glcorearb.h>

#ifndef GL_PRIMITIVE_RESTART_FIXED_INDEX
#define GL_PRIMITIVE_RESTART_FIXED_INDEX 0x8D69
#endif
#define W 192
#define H 128
static int checks, failures;
#define CHECK(c,fmt,...) do{++checks;if(c)printf("ok  : " fmt "\n",##__VA_ARGS__);else{printf("FAIL: " fmt "\n",##__VA_ARGS__);++failures;}}while(0)

typedef struct Api {
 const GLubyte*(*GetString)(GLenum); GLenum(*GetError)(void); void(*Finish)(void);
 void(*Enable)(GLenum);void(*Disable)(GLenum);void(*Viewport)(GLint,GLint,GLsizei,GLsizei);
 void(*ClearColor)(GLfloat,GLfloat,GLfloat,GLfloat);void(*Clear)(GLbitfield);
 void(*ReadPixels)(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void*);
 void(*GenTextures)(GLsizei,GLuint*);void(*BindTexture)(GLenum,GLuint);void(*TexParameteri)(GLenum,GLenum,GLint);
 void(*TexImage2D)(GLenum,GLint,GLint,GLsizei,GLsizei,GLint,GLenum,GLenum,const void*);
 void(*GenFramebuffers)(GLsizei,GLuint*);void(*BindFramebuffer)(GLenum,GLuint);void(*FramebufferTexture2D)(GLenum,GLenum,GLenum,GLuint,GLint);GLenum(*CheckFramebufferStatus)(GLenum);
 GLuint(*CreateShader)(GLenum);void(*ShaderSource)(GLuint,GLsizei,const GLchar*const*,const GLint*);void(*CompileShader)(GLuint);void(*GetShaderiv)(GLuint,GLenum,GLint*);void(*GetShaderInfoLog)(GLuint,GLsizei,GLsizei*,GLchar*);
 GLuint(*CreateProgram)(void);void(*AttachShader)(GLuint,GLuint);void(*LinkProgram)(GLuint);void(*GetProgramiv)(GLuint,GLenum,GLint*);void(*GetProgramInfoLog)(GLuint,GLsizei,GLsizei*,GLchar*);void(*UseProgram)(GLuint);
 void(*GenVertexArrays)(GLsizei,GLuint*);void(*BindVertexArray)(GLuint);void(*GenBuffers)(GLsizei,GLuint*);void(*BindBuffer)(GLenum,GLuint);void(*BufferData)(GLenum,GLsizeiptr,const void*,GLenum);
 void(*VertexAttribPointer)(GLuint,GLint,GLenum,GLboolean,GLsizei,const void*);void(*EnableVertexAttribArray)(GLuint);
 void(*DrawElementsBaseVertex)(GLenum,GLsizei,GLenum,const void*,GLint);
 void(*DrawElementsInstancedBaseVertexBaseInstance)(GLenum,GLsizei,GLenum,const void*,GLsizei,GLint,GLuint);
 void(*MultiDrawElementsIndirect)(GLenum,GLenum,const void*,GLsizei,GLsizei);
} Api;
#define LOAD(a,h,f) do{(a)->f=(__typeof__((a)->f))dlsym((h),"gl"#f);CHECK((a)->f!=NULL,"resolve gl%s",#f);}while(0)
static void load(void*h,Api*a){memset(a,0,sizeof(*a));LOAD(a,h,GetString);LOAD(a,h,GetError);LOAD(a,h,Finish);LOAD(a,h,Enable);LOAD(a,h,Disable);LOAD(a,h,Viewport);LOAD(a,h,ClearColor);LOAD(a,h,Clear);LOAD(a,h,ReadPixels);LOAD(a,h,GenTextures);LOAD(a,h,BindTexture);LOAD(a,h,TexParameteri);LOAD(a,h,TexImage2D);LOAD(a,h,GenFramebuffers);LOAD(a,h,BindFramebuffer);LOAD(a,h,FramebufferTexture2D);LOAD(a,h,CheckFramebufferStatus);LOAD(a,h,CreateShader);LOAD(a,h,ShaderSource);LOAD(a,h,CompileShader);LOAD(a,h,GetShaderiv);LOAD(a,h,GetShaderInfoLog);LOAD(a,h,CreateProgram);LOAD(a,h,AttachShader);LOAD(a,h,LinkProgram);LOAD(a,h,GetProgramiv);LOAD(a,h,GetProgramInfoLog);LOAD(a,h,UseProgram);LOAD(a,h,GenVertexArrays);LOAD(a,h,BindVertexArray);LOAD(a,h,GenBuffers);LOAD(a,h,BindBuffer);LOAD(a,h,BufferData);LOAD(a,h,VertexAttribPointer);LOAD(a,h,EnableVertexAttribArray);LOAD(a,h,DrawElementsBaseVertex);LOAD(a,h,DrawElementsInstancedBaseVertexBaseInstance);LOAD(a,h,MultiDrawElementsIndirect);}
#undef LOAD
static int egl_setup(void*h){
 EGLDisplay(*gd)(EGLNativeDisplayType)=dlsym(h,"eglGetDisplay");EGLBoolean(*init)(EGLDisplay,EGLint*,EGLint*)=dlsym(h,"eglInitialize");EGLBoolean(*ba)(EGLenum)=dlsym(h,"eglBindAPI");EGLBoolean(*gc)(EGLDisplay,EGLConfig*,EGLint,EGLint*)=dlsym(h,"eglGetConfigs");EGLContext(*cc)(EGLDisplay,EGLConfig,EGLContext,const EGLint*)=dlsym(h,"eglCreateContext");EGLSurface(*cp)(EGLDisplay,EGLConfig,const EGLint*)=dlsym(h,"eglCreatePbufferSurface");EGLBoolean(*mc)(EGLDisplay,EGLSurface,EGLSurface,EGLContext)=dlsym(h,"eglMakeCurrent");
 CHECK(gd&&init&&ba&&gc&&cc&&cp&&mc,"resolve EGL");if(!gd||!init||!ba||!gc||!cc||!cp||!mc)return 0;EGLDisplay d=gd(EGL_DEFAULT_DISPLAY);EGLint x=0,y=0;CHECK(d!=EGL_NO_DISPLAY&&init(d,&x,&y),"initialize EGL");CHECK(ba(EGL_OPENGL_API),"bind GL");EGLConfig c=0;EGLint n=0;CHECK(gc(d,&c,1,&n)&&n,"get config");const EGLint ca[]={EGL_CONTEXT_MAJOR_VERSION,4,EGL_CONTEXT_MINOR_VERSION,6,EGL_NONE},pa[]={EGL_WIDTH,W,EGL_HEIGHT,H,EGL_NONE};EGLContext ctx=cc(d,c,EGL_NO_CONTEXT,ca);EGLSurface s=cp(d,c,pa);CHECK(ctx!=EGL_NO_CONTEXT&&s!=EGL_NO_SURFACE&&mc(d,s,s,ctx),"make current");return ctx!=EGL_NO_CONTEXT&&s!=EGL_NO_SURFACE;
}
static GLuint program(Api*a,const char*vs,const char*fs,const char*name){GLuint v=a->CreateShader(GL_VERTEX_SHADER),f=a->CreateShader(GL_FRAGMENT_SHADER);a->ShaderSource(v,1,&vs,0);a->ShaderSource(f,1,&fs,0);a->CompileShader(v);a->CompileShader(f);GLint vo=0,fo=0;a->GetShaderiv(v,GL_COMPILE_STATUS,&vo);a->GetShaderiv(f,GL_COMPILE_STATUS,&fo);if(!vo||!fo){char l[4096];GLsizei n=0;if(!vo){a->GetShaderInfoLog(v,sizeof l,&n,l);fprintf(stderr,"%s VS %.*s\n",name,n,l);}if(!fo){a->GetShaderInfoLog(f,sizeof l,&n,l);fprintf(stderr,"%s FS %.*s\n",name,n,l);}}CHECK(vo&&fo,"%s shaders compile",name);GLuint p=a->CreateProgram();a->AttachShader(p,v);a->AttachShader(p,f);a->LinkProgram(p);GLint ok=0;a->GetProgramiv(p,GL_LINK_STATUS,&ok);if(!ok){char l[4096];GLsizei n=0;a->GetProgramInfoLog(p,sizeof l,&n,l);fprintf(stderr,"%s LINK %.*s\n",name,n,l);}CHECK(ok,"%s links",name);return p;}
static GLuint target(Api*a){GLuint t,f;a->GenTextures(1,&t);a->BindTexture(GL_TEXTURE_2D,t);a->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);a->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);a->TexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,W,H,0,GL_RGBA,GL_UNSIGNED_BYTE,0);a->GenFramebuffers(1,&f);a->BindFramebuffer(GL_FRAMEBUFFER,f);a->FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t,0);CHECK(a->CheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"FBO complete");return f;}
static void clear(Api*a,GLuint f){a->BindFramebuffer(GL_FRAMEBUFFER,f);a->Viewport(0,0,W,H);a->Disable(GL_BLEND);a->Disable(GL_DEPTH_TEST);a->Disable(GL_CULL_FACE);a->Disable(GL_SCISSOR_TEST);a->Disable(GL_PRIMITIVE_RESTART);a->Disable(GL_PRIMITIVE_RESTART_FIXED_INDEX);a->ClearColor(0,0,0,1);a->Clear(GL_COLOR_BUFFER_BIT);}
static void pixels(Api*a,uint8_t*p){a->Finish();a->ReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,p);a->Finish();}
static void count(const uint8_t*p,unsigned*r,unsigned*g,unsigned*b,unsigned*m){*r=*g=*b=*m=0;for(int i=0;i<W*H;i++){const uint8_t*q=p+i*4;if(q[0]>180&&q[1]<80&&q[2]<80)(*r)++;if(q[1]>180&&q[0]<80&&q[2]<80)(*g)++;if(q[2]>180&&q[0]<80&&q[1]<80)(*b)++;if(q[0]>150&&q[2]>150&&q[1]<80)(*m)++;}}
static void geometry(Api*a,GLuint*vao,GLuint*vbo,GLuint*ebo){
 static const float v[]={-0.88f,-0.65f,-0.18f,-0.65f,-0.53f,0.65f, 0.18f,-0.65f,0.88f,-0.65f,0.53f,0.65f};
 static const uint32_t e[]={99,98,0,1,2,0,1,2};a->GenVertexArrays(1,vao);a->BindVertexArray(*vao);a->GenBuffers(1,vbo);a->BindBuffer(GL_ARRAY_BUFFER,*vbo);a->BufferData(GL_ARRAY_BUFFER,sizeof v,v,GL_STATIC_DRAW);a->VertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,8,0);a->EnableVertexAttribArray(0);a->GenBuffers(1,ebo);a->BindBuffer(GL_ELEMENT_ARRAY_BUFFER,*ebo);a->BufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof e,e,GL_STATIC_DRAW);
}
static void test_direct(Api*a,GLuint f,uint8_t*p){
 const char*vs="#version 460 core\nlayout(location=0)in vec2 p;out vec4 c;void main(){bool ok=(gl_BaseVertex==3)&&(gl_VertexID>=3&&gl_VertexID<=5);gl_Position=vec4(p,0,1);c=ok?vec4(0,1,0,1):vec4(1,0,1,1);}";
 const char*fs="#version 460 core\nin vec4 c;out vec4 o;void main(){o=c;}";GLuint pr=program(a,vs,fs,"baseVertex/VertexID");GLuint va,vb,eb;geometry(a,&va,&vb,&eb);clear(a,f);a->UseProgram(pr);a->DrawElementsBaseVertex(GL_TRIANGLES,3,GL_UNSIGNED_INT,(void*)(uintptr_t)(5*4),3);CHECK(a->GetError()==GL_NO_ERROR,"direct baseVertex draw no error");pixels(a,p);unsigned r,g,b,m;count(p,&r,&g,&b,&m);CHECK(g>2000&&m<20,"gl_VertexID includes baseVertex and gl_BaseVertex is visible (green=%u magenta=%u)",g,m);
}
static void test_base_instance(Api*a,GLuint f,uint8_t*p){
 const char*vs="#version 460 core\nlayout(location=0)in vec2 p;out vec4 c;void main(){bool ok=(gl_BaseVertex==0)&&(gl_BaseInstance==7);gl_Position=vec4(p,0,1);c=ok?vec4(0,0,1,1):vec4(1,0,1,1);}";
 const char*fs="#version 460 core\nin vec4 c;out vec4 o;void main(){o=c;}";GLuint pr=program(a,vs,fs,"BaseInstance");GLuint va,vb,eb;geometry(a,&va,&vb,&eb);clear(a,f);a->UseProgram(pr);a->DrawElementsInstancedBaseVertexBaseInstance(GL_TRIANGLES,3,GL_UNSIGNED_INT,(void*)(uintptr_t)(2*4),1,0,7);CHECK(a->GetError()==GL_NO_ERROR,"baseInstance draw no error");pixels(a,p);unsigned r,g,b,m;count(p,&r,&g,&b,&m);CHECK(b>2000&&m<20,"gl_BaseInstance is visible to shader (blue=%u magenta=%u)",b,m);
}
typedef struct Cmd32{uint32_t count,instances,firstIndex;int32_t baseVertex;uint32_t baseInstance;uint32_t pad[3];}Cmd32;
_Static_assert(sizeof(Cmd32)==32,"MDI stride fixture");
static void test_mdi(Api*a,GLuint f,uint8_t*p){
 const char*vs="#version 460 core\nlayout(location=0)in vec2 p;out vec4 c;void main(){int want=(p.x<0.0)?0:1;int bv=(want==0)?0:3;int bi=(want==0)?11:17;bool ok=(gl_DrawID==want)&&(gl_BaseVertex==bv)&&(gl_BaseInstance==bi)&&(gl_VertexID>=bv&&gl_VertexID<=bv+2);gl_Position=vec4(p,0,1);c=ok?(want==0?vec4(1,0,0,1):vec4(0,1,0,1)):vec4(1,0,1,1);}";
 const char*fs="#version 460 core\nin vec4 c;out vec4 o;void main(){o=c;}";GLuint pr=program(a,vs,fs,"MDI draw parameters");GLuint va,vb,eb;geometry(a,&va,&vb,&eb);Cmd32 cmds[2]={{3,1,2,0,11,{0xdeadbeef,1,2}},{3,1,5,3,17,{0xcafebabe,3,4}}};GLuint ib;a->GenBuffers(1,&ib);a->BindBuffer(GL_DRAW_INDIRECT_BUFFER,ib);a->BufferData(GL_DRAW_INDIRECT_BUFFER,sizeof cmds,cmds,GL_STATIC_DRAW);clear(a,f);a->UseProgram(pr);a->MultiDrawElementsIndirect(GL_TRIANGLES,GL_UNSIGNED_INT,0,2,sizeof(Cmd32));CHECK(a->GetError()==GL_NO_ERROR,"MDI nonzero-stride draw no error");pixels(a,p);unsigned r,g,b,m;count(p,&r,&g,&b,&m);CHECK(r>2000&&g>2000,"MDI nonzero stride + nonzero firstIndex + baseVertex renders both draws (%u,%u)",r,g);CHECK(m<20,"CPU-expanded MDI preserves gl_DrawID/gl_BaseVertex/gl_BaseInstance/gl_VertexID (magenta=%u)",m);
}
static void test_restart_basevertex(Api*a,GLuint f,uint8_t*p){
 const char*vs="#version 460 core\nlayout(location=0)in vec2 p;out vec4 c;void main(){gl_Position=vec4(p,0,1);c=(gl_BaseVertex==4&&gl_VertexID>=4)?vec4(0,0,1,1):vec4(1,0,1,1);}";const char*fs="#version 460 core\nin vec4 c;out vec4 o;void main(){o=c;}";GLuint pr=program(a,vs,fs,"restart+baseVertex");
 float v[11*2];for(int i=0;i<22;i++)v[i]=3.0f;float p6[]={-0.88f,-.65f,-.18f,-.65f,-.53f,.65f,.18f,-.65f,.88f,-.65f,.53f,.65f};memcpy(v+8,p6,sizeof p6);uint16_t idx[]={0,1,2,0xffff,3,4,5};GLuint va,vb,eb;a->GenVertexArrays(1,&va);a->BindVertexArray(va);a->GenBuffers(1,&vb);a->BindBuffer(GL_ARRAY_BUFFER,vb);a->BufferData(GL_ARRAY_BUFFER,sizeof v,v,GL_STATIC_DRAW);a->VertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,8,0);a->EnableVertexAttribArray(0);a->GenBuffers(1,&eb);a->BindBuffer(GL_ELEMENT_ARRAY_BUFFER,eb);a->BufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof idx,idx,GL_STATIC_DRAW);clear(a,f);a->UseProgram(pr);a->Enable(GL_PRIMITIVE_RESTART_FIXED_INDEX);a->DrawElementsBaseVertex(GL_TRIANGLE_STRIP,7,GL_UNSIGNED_SHORT,0,4);CHECK(a->GetError()==GL_NO_ERROR,"restart+baseVertex draw no error");pixels(a,p);unsigned r,g,b,m;count(p,&r,&g,&b,&m);CHECK(b>3500&&m<20,"fixed restart composes with nonzero baseVertex (%u blue, %u magenta)",b,m);
}
static void test_restart_mdi(Api*a,GLuint f,uint8_t*p){
 const char*vs="#version 460 core\nlayout(location=0)in vec2 p;out vec4 c;void main(){gl_Position=vec4(p,0,1);c=vec4(0,1,0,1);}";const char*fs="#version 460 core\nin vec4 c;out vec4 o;void main(){o=c;}";GLuint pr=program(a,vs,fs,"restart+MDI");
 float v[]={-0.88f,-.65f,-.18f,-.65f,-.53f,.65f,.18f,-.65f,.88f,-.65f,.53f,.65f};uint16_t idx[]={99,99,0,1,2,0xffff, 3,4,5};GLuint va,vb,eb;a->GenVertexArrays(1,&va);a->BindVertexArray(va);a->GenBuffers(1,&vb);a->BindBuffer(GL_ARRAY_BUFFER,vb);a->BufferData(GL_ARRAY_BUFFER,sizeof v,v,GL_STATIC_DRAW);a->VertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,8,0);a->EnableVertexAttribArray(0);a->GenBuffers(1,&eb);a->BindBuffer(GL_ELEMENT_ARRAY_BUFFER,eb);a->BufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof idx,idx,GL_STATIC_DRAW);Cmd32 cmd={7,1,2,0,0,{9,8,7}};GLuint ib;a->GenBuffers(1,&ib);a->BindBuffer(GL_DRAW_INDIRECT_BUFFER,ib);a->BufferData(GL_DRAW_INDIRECT_BUFFER,sizeof cmd,&cmd,GL_STATIC_DRAW);clear(a,f);a->UseProgram(pr);a->Enable(GL_PRIMITIVE_RESTART_FIXED_INDEX);a->MultiDrawElementsIndirect(GL_TRIANGLE_STRIP,GL_UNSIGNED_SHORT,0,1,sizeof(Cmd32));CHECK(a->GetError()==GL_NO_ERROR,"restart+MDI draw no error");pixels(a,p);unsigned r,g,b,m;count(p,&r,&g,&b,&m);CHECK(g>3500,"fixed restart composes with MDI firstIndex/stride (%u green)",g);
}
int main(int argc,char**argv){if(argc!=2){fprintf(stderr,"usage: %s libmithril.dylib\n",argv[0]);return 2;}void*h=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);CHECK(h!=0,"dlopen Mithril");if(!h)return 2;if(!egl_setup(h))return 1;Api a;load(h,&a);printf("GL_VERSION=%s\n",a.GetString(GL_VERSION));printf("GL_RENDERER=%s\n",a.GetString(GL_RENDERER));GLuint f=target(&a);uint8_t*p=malloc(W*H*4);CHECK(p!=0,"allocate readback");if(!p)return 2;test_direct(&a,f,p);test_base_instance(&a,f,p);test_mdi(&a,f,p);test_restart_basevertex(&a,f,p);test_restart_mdi(&a,f,p);printf("DRAW_PARAMETERS checks=%d failures=%d\n",checks,failures);free(p);return failures?1:0;}
