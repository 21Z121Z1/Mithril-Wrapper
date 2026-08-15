/* DirectMetal ARB_vertex_attrib_binding GPU correctness oracle. */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>
#include <GL/glcorearb.h>

#ifndef GL_VERTEX_ATTRIB_BINDING
#define GL_VERTEX_ATTRIB_BINDING 0x82D4
#endif
#ifndef GL_VERTEX_ATTRIB_RELATIVE_OFFSET
#define GL_VERTEX_ATTRIB_RELATIVE_OFFSET 0x82D5
#endif
#define W 128
#define H 96

static int checks=0, failures=0;
#define CHECK(c,fmt,...) do{++checks;if(c)printf("ok  : " fmt "\n",##__VA_ARGS__);else{printf("FAIL: " fmt "\n",##__VA_ARGS__);++failures;}}while(0)

typedef struct API {
 const GLubyte*(*GetString)(GLenum); GLenum(*GetError)(void); void(*Finish)(void);
 void(*Viewport)(GLint,GLint,GLsizei,GLsizei); void(*ClearColor)(GLfloat,GLfloat,GLfloat,GLfloat); void(*Clear)(GLbitfield);
 void(*ReadPixels)(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void*);
 void(*GenTextures)(GLsizei,GLuint*); void(*BindTexture)(GLenum,GLuint); void(*TexImage2D)(GLenum,GLint,GLint,GLsizei,GLsizei,GLint,GLenum,GLenum,const void*); void(*TexParameteri)(GLenum,GLenum,GLint);
 void(*GenFramebuffers)(GLsizei,GLuint*); void(*BindFramebuffer)(GLenum,GLuint); void(*FramebufferTexture2D)(GLenum,GLenum,GLenum,GLuint,GLint); GLenum(*CheckFramebufferStatus)(GLenum);
 GLuint(*CreateShader)(GLenum); void(*ShaderSource)(GLuint,GLsizei,const GLchar* const*,const GLint*); void(*CompileShader)(GLuint); void(*GetShaderiv)(GLuint,GLenum,GLint*); void(*GetShaderInfoLog)(GLuint,GLsizei,GLsizei*,GLchar*);
 GLuint(*CreateProgram)(void); void(*AttachShader)(GLuint,GLuint); void(*LinkProgram)(GLuint); void(*GetProgramiv)(GLuint,GLenum,GLint*); void(*GetProgramInfoLog)(GLuint,GLsizei,GLsizei*,GLchar*); void(*UseProgram)(GLuint);
 void(*GenVertexArrays)(GLsizei,GLuint*); void(*BindVertexArray)(GLuint); void(*EnableVertexAttribArray)(GLuint); void(*DisableVertexAttribArray)(GLuint);
 void(*GenBuffers)(GLsizei,GLuint*); void(*BindBuffer)(GLenum,GLuint); void(*BufferData)(GLenum,GLsizeiptr,const void*,GLenum);
 void(*BindVertexBuffer)(GLuint,GLuint,GLintptr,GLsizei); void(*VertexAttribFormat)(GLuint,GLint,GLenum,GLboolean,GLuint); void(*VertexAttribBinding)(GLuint,GLuint); void(*VertexBindingDivisor)(GLuint,GLuint); void(*VertexAttribDivisor)(GLuint,GLuint);
 void(*VertexAttribPointer)(GLuint,GLint,GLenum,GLboolean,GLsizei,const void*); void(*GetVertexAttribiv)(GLuint,GLenum,GLint*);
 void(*DrawArrays)(GLenum,GLint,GLsizei);
} API;
#define LOAD(a,h,f,s) do{(a)->f=(__typeof__((a)->f))dlsym((h),(s));CHECK((a)->f!=NULL,"resolve %s",(s));}while(0)

static int load_api(void*h,API*a){memset(a,0,sizeof(*a));
 LOAD(a,h,GetString,"glGetString");LOAD(a,h,GetError,"glGetError");LOAD(a,h,Finish,"glFinish");LOAD(a,h,Viewport,"glViewport");LOAD(a,h,ClearColor,"glClearColor");LOAD(a,h,Clear,"glClear");LOAD(a,h,ReadPixels,"glReadPixels");
 LOAD(a,h,GenTextures,"glGenTextures");LOAD(a,h,BindTexture,"glBindTexture");LOAD(a,h,TexImage2D,"glTexImage2D");LOAD(a,h,TexParameteri,"glTexParameteri");LOAD(a,h,GenFramebuffers,"glGenFramebuffers");LOAD(a,h,BindFramebuffer,"glBindFramebuffer");LOAD(a,h,FramebufferTexture2D,"glFramebufferTexture2D");LOAD(a,h,CheckFramebufferStatus,"glCheckFramebufferStatus");
 LOAD(a,h,CreateShader,"glCreateShader");LOAD(a,h,ShaderSource,"glShaderSource");LOAD(a,h,CompileShader,"glCompileShader");LOAD(a,h,GetShaderiv,"glGetShaderiv");LOAD(a,h,GetShaderInfoLog,"glGetShaderInfoLog");LOAD(a,h,CreateProgram,"glCreateProgram");LOAD(a,h,AttachShader,"glAttachShader");LOAD(a,h,LinkProgram,"glLinkProgram");LOAD(a,h,GetProgramiv,"glGetProgramiv");LOAD(a,h,GetProgramInfoLog,"glGetProgramInfoLog");LOAD(a,h,UseProgram,"glUseProgram");
 LOAD(a,h,GenVertexArrays,"glGenVertexArrays");LOAD(a,h,BindVertexArray,"glBindVertexArray");LOAD(a,h,EnableVertexAttribArray,"glEnableVertexAttribArray");LOAD(a,h,DisableVertexAttribArray,"glDisableVertexAttribArray");LOAD(a,h,GenBuffers,"glGenBuffers");LOAD(a,h,BindBuffer,"glBindBuffer");LOAD(a,h,BufferData,"glBufferData");
 LOAD(a,h,BindVertexBuffer,"glBindVertexBuffer");LOAD(a,h,VertexAttribFormat,"glVertexAttribFormat");LOAD(a,h,VertexAttribBinding,"glVertexAttribBinding");LOAD(a,h,VertexBindingDivisor,"glVertexBindingDivisor");LOAD(a,h,VertexAttribDivisor,"glVertexAttribDivisor");LOAD(a,h,VertexAttribPointer,"glVertexAttribPointer");LOAD(a,h,GetVertexAttribiv,"glGetVertexAttribiv");LOAD(a,h,DrawArrays,"glDrawArrays");return failures==0;}
#undef LOAD

static int egl_setup(void*h){
 EGLDisplay(*gd)(EGLNativeDisplayType)=dlsym(h,"eglGetDisplay");EGLBoolean(*init)(EGLDisplay,EGLint*,EGLint*)=dlsym(h,"eglInitialize");EGLBoolean(*ba)(EGLenum)=dlsym(h,"eglBindAPI");EGLBoolean(*gc)(EGLDisplay,EGLConfig*,EGLint,EGLint*)=dlsym(h,"eglGetConfigs");EGLContext(*cc)(EGLDisplay,EGLConfig,EGLContext,const EGLint*)=dlsym(h,"eglCreateContext");EGLSurface(*ps)(EGLDisplay,EGLConfig,const EGLint*)=dlsym(h,"eglCreatePbufferSurface");EGLBoolean(*mc)(EGLDisplay,EGLSurface,EGLSurface,EGLContext)=dlsym(h,"eglMakeCurrent");
 CHECK(gd&&init&&ba&&gc&&cc&&ps&&mc,"resolve EGL entrypoints");if(!gd||!init||!ba||!gc||!cc||!ps||!mc)return 0;EGLDisplay d=gd(EGL_DEFAULT_DISPLAY);EGLint ma=0,mi=0;CHECK(d!=EGL_NO_DISPLAY&&init(d,&ma,&mi),"initialize EGL");CHECK(ba(EGL_OPENGL_API),"bind GL");EGLConfig cfg=0;EGLint n=0;CHECK(gc(d,&cfg,1,&n)&&n>0,"get EGL config");const EGLint ca[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,EGL_NONE},pa[]={EGL_WIDTH,W,EGL_HEIGHT,H,EGL_NONE};EGLContext c=cc(d,cfg,EGL_NO_CONTEXT,ca);EGLSurface s=ps(d,cfg,pa);CHECK(c!=EGL_NO_CONTEXT&&s!=EGL_NO_SURFACE,"create pbuffer/context");CHECK(mc(d,s,s,c),"make current");return c!=EGL_NO_CONTEXT&&s!=EGL_NO_SURFACE;}

static GLuint program(API*a){const char*vs="#version 330 core\nlayout(location=0) in vec4 p; layout(location=1) in vec3 c; out vec3 vc; void main(){gl_Position=p;vc=c;}\n";const char*fs="#version 330 core\nin vec3 vc; out vec4 o; void main(){o=vec4(vc,1.0);}\n";GLuint sv=a->CreateShader(GL_VERTEX_SHADER),sf=a->CreateShader(GL_FRAGMENT_SHADER);a->ShaderSource(sv,1,&vs,0);a->ShaderSource(sf,1,&fs,0);a->CompileShader(sv);a->CompileShader(sf);GLint v=0,f=0;a->GetShaderiv(sv,GL_COMPILE_STATUS,&v);a->GetShaderiv(sf,GL_COMPILE_STATUS,&f);CHECK(v&&f,"compile shaders");GLuint p=a->CreateProgram();a->AttachShader(p,sv);a->AttachShader(p,sf);a->LinkProgram(p);GLint l=0;a->GetProgramiv(p,GL_LINK_STATUS,&l);if(!l){char b[2048];GLsizei n=0;a->GetProgramInfoLog(p,sizeof(b),&n,b);fprintf(stderr,"LINK: %.*s\n",n,b);}CHECK(l,"link program");return p;}
static GLuint target(API*a){GLuint t=0,f=0;a->GenTextures(1,&t);a->BindTexture(GL_TEXTURE_2D,t);a->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);a->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);a->TexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,W,H,0,GL_RGBA,GL_UNSIGNED_BYTE,0);a->GenFramebuffers(1,&f);a->BindFramebuffer(GL_FRAMEBUFFER,f);a->FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t,0);CHECK(a->CheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"FBO complete");return f;}
static uint32_t lit(const uint8_t*p,int x0,int y0,int x1,int y1){uint32_t n=0;for(int y=y0;y<y1;y++)for(int x=x0;x<x1;x++){const uint8_t*q=&p[(y*W+x)*4];if(q[0]>32||q[1]>32||q[2]>32)n++;}return n;}

typedef struct V{float x,y,z,w,r,g,b;}V;
static void separate_binding_case(API*a,GLuint f,GLuint p){
 const V v[6]={{-.9f,-.75f,0,1,1,0,0},{-.15f,-.75f,0,1,1,0,0},{-.52f,.72f,0,1,1,0,0},{.15f,-.75f,0,1,0,1,0},{.9f,-.75f,0,1,0,1,0},{.52f,.72f,0,1,0,1,0}};enum{PAD=64};uint8_t blob[PAD+sizeof(v)];memset(blob,0xA5,sizeof(blob));memcpy(blob+PAD,v,sizeof(v));GLuint vao=0,b=0;a->GenVertexArrays(1,&vao);a->BindVertexArray(vao);a->GenBuffers(1,&b);a->BindBuffer(GL_ARRAY_BUFFER,b);a->BufferData(GL_ARRAY_BUFFER,sizeof(blob),blob,GL_STATIC_DRAW);
 a->BindVertexBuffer(5,b,PAD,sizeof(V));a->VertexAttribFormat(0,4,GL_FLOAT,GL_FALSE,0);a->VertexAttribBinding(0,5);a->EnableVertexAttribArray(0);a->VertexAttribFormat(1,3,GL_FLOAT,GL_FALSE,4*sizeof(float));a->VertexAttribBinding(1,5);a->EnableVertexAttribArray(1);
 GLint bi0=-1,bi1=-1,rel1=-1,buf1=-1;a->GetVertexAttribiv(0,GL_VERTEX_ATTRIB_BINDING,&bi0);a->GetVertexAttribiv(1,GL_VERTEX_ATTRIB_BINDING,&bi1);a->GetVertexAttribiv(1,GL_VERTEX_ATTRIB_RELATIVE_OFFSET,&rel1);a->GetVertexAttribiv(1,GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,&buf1);CHECK(bi0==5&&bi1==5,"separate attribs query binding 5 (%d,%d)",bi0,bi1);CHECK(rel1==16,"relative offset query preserves 16 (%d)",rel1);CHECK(buf1==(GLint)b,"buffer query follows attrib->binding mapping (%d vs %u)",buf1,b);
 a->BindFramebuffer(GL_FRAMEBUFFER,f);a->Viewport(0,0,W,H);a->ClearColor(0,0,0,1);a->Clear(GL_COLOR_BUFFER_BIT);a->UseProgram(p);a->DrawArrays(GL_TRIANGLES,0,6);a->Finish();uint8_t px[W*H*4];a->ReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,px);a->Finish();uint32_t left=lit(px,2,5,58,91),right=lit(px,70,5,126,91);CHECK(left>1000&&right>1000,"binding offset+relative offset feed both interleaved attributes (%u,%u)",left,right);CHECK(a->GetError()==GL_NO_ERROR,"separate binding draw no GL error");
}

static void legacy_rebind_case(API*a,GLuint f,GLuint p){
 const V v[3]={{-.7f,-.65f,0,1,1,1,1},{.7f,-.65f,0,1,1,1,1},{0,.7f,0,1,1,1,1}};enum{PAD=48};uint8_t blob[PAD+sizeof(v)];memset(blob,0,sizeof(blob));memcpy(blob+PAD,v,sizeof(v));GLuint vao=0,b=0;a->GenVertexArrays(1,&vao);a->BindVertexArray(vao);a->GenBuffers(1,&b);a->BindBuffer(GL_ARRAY_BUFFER,b);a->BufferData(GL_ARRAY_BUFFER,sizeof(blob),blob,GL_STATIC_DRAW);
 /* Poison mapping first; legacy pointer must reset attrib 0 -> binding 0. */a->BindVertexBuffer(7,b,0,sizeof(V));a->VertexAttribBinding(0,7);a->VertexAttribPointer(0,4,GL_FLOAT,GL_FALSE,sizeof(V),(const void*)(uintptr_t)PAD);a->EnableVertexAttribArray(0);a->VertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(V),(const void*)(uintptr_t)(PAD+4*sizeof(float)));a->EnableVertexAttribArray(1);
 GLint b0=-1,b1=-1,rel0=-1;a->GetVertexAttribiv(0,GL_VERTEX_ATTRIB_BINDING,&b0);a->GetVertexAttribiv(1,GL_VERTEX_ATTRIB_BINDING,&b1);a->GetVertexAttribiv(0,GL_VERTEX_ATTRIB_RELATIVE_OFFSET,&rel0);CHECK(b0==0&&b1==1,"legacy VertexAttribPointer resets identity mappings (%d,%d)",b0,b1);CHECK(rel0==0,"legacy VertexAttribPointer resets relative offset (%d)",rel0);
 a->BindFramebuffer(GL_FRAMEBUFFER,f);a->Viewport(0,0,W,H);a->ClearColor(0,0,0,1);a->Clear(GL_COLOR_BUFFER_BIT);a->UseProgram(p);a->DrawArrays(GL_TRIANGLES,0,3);a->Finish();uint8_t px[W*H*4];a->ReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,px);a->Finish();CHECK(lit(px,20,10,108,88)>1500,"legacy pointer binding offset still renders after separate-state poison");
 /* ARB_vertex_attrib_binding: glVertexAttribDivisor(i,d) maps i back to binding i. */a->VertexAttribBinding(0,7);a->VertexAttribDivisor(0,3);a->GetVertexAttribiv(0,GL_VERTEX_ATTRIB_BINDING,&b0);CHECK(b0==0,"glVertexAttribDivisor resets attrib mapping to same-number binding (%d)",b0);CHECK(a->GetError()==GL_NO_ERROR,"legacy/separate coexistence no GL error");
}

int main(int argc,char**argv){const char*path=argc>1?argv[1]:"./libmithril.dylib";void*h=dlopen(path,RTLD_NOW|RTLD_GLOBAL);CHECK(h!=0,"dlopen %s",path);if(!h){fprintf(stderr,"%s\n",dlerror());return 2;}if(!egl_setup(h))return 2;API a;if(!load_api(h,&a))return 2;const char*ver=(const char*)a.GetString(GL_VERSION);CHECK(ver&&strstr(ver,"Metal 3 (DirectMetal)"),"DirectMetal active (%s)",ver?ver:"null");GLuint f=target(&a),p=program(&a);separate_binding_case(&a,f,p);legacy_rebind_case(&a,f,p);printf("VERTEX BINDING SMOKE: %d checks, %d failure(s)\n",checks,failures);if(!failures)printf("VERTEX BINDING SMOKE ALL PASSED\n");return failures?1:0;}
