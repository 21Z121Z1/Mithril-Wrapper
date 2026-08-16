/* DirectMetal buffer mapping/copy/range + std140 UBO GPU oracle. */
#include <dlfcn.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>
#include <GL/glcorearb.h>

#define W 64
#define H 64
static int checks=0, failures=0;
#define CHECK(c,fmt,...) do { ++checks; if(c) printf("ok  : " fmt "\n", ##__VA_ARGS__); else { printf("FAIL: " fmt "\n", ##__VA_ARGS__); ++failures; } } while(0)

typedef struct API {
 const GLubyte* (*GetString)(GLenum); GLenum (*GetError)(void); void (*GetIntegerv)(GLenum,GLint*); void (*Finish)(void);
 void (*Viewport)(GLint,GLint,GLsizei,GLsizei); void (*ClearColor)(GLfloat,GLfloat,GLfloat,GLfloat); void (*Clear)(GLbitfield);
 void (*ReadPixels)(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void*);
 void (*GenTextures)(GLsizei,GLuint*); void (*BindTexture)(GLenum,GLuint); void (*TexImage2D)(GLenum,GLint,GLint,GLsizei,GLsizei,GLint,GLenum,GLenum,const void*); void (*TexParameteri)(GLenum,GLenum,GLint);
 void (*GenFramebuffers)(GLsizei,GLuint*); void (*BindFramebuffer)(GLenum,GLuint); void (*FramebufferTexture2D)(GLenum,GLenum,GLenum,GLuint,GLint); GLenum (*CheckFramebufferStatus)(GLenum);
 GLuint (*CreateShader)(GLenum); void (*ShaderSource)(GLuint,GLsizei,const GLchar* const*,const GLint*); void (*CompileShader)(GLuint); void (*GetShaderiv)(GLuint,GLenum,GLint*);
 GLuint (*CreateProgram)(void); void (*AttachShader)(GLuint,GLuint); void (*LinkProgram)(GLuint); void (*GetProgramiv)(GLuint,GLenum,GLint*); void (*UseProgram)(GLuint);
 GLuint (*GetUniformBlockIndex)(GLuint,const GLchar*); void (*UniformBlockBinding)(GLuint,GLuint,GLuint);
 void (*GenVertexArrays)(GLsizei,GLuint*); void (*BindVertexArray)(GLuint); void (*DrawArrays)(GLenum,GLint,GLsizei);
 void (*GenBuffers)(GLsizei,GLuint*); void (*BindBuffer)(GLenum,GLuint); void (*BufferData)(GLenum,GLsizeiptr,const void*,GLenum); void (*BufferSubData)(GLenum,GLintptr,GLsizeiptr,const void*);
 void* (*MapBufferRange)(GLenum,GLintptr,GLsizeiptr,GLbitfield); void (*FlushMappedBufferRange)(GLenum,GLintptr,GLsizeiptr); GLboolean (*UnmapBuffer)(GLenum);
 void (*CopyBufferSubData)(GLenum,GLenum,GLintptr,GLintptr,GLsizeiptr); void (*BindBufferRange)(GLenum,GLuint,GLuint,GLintptr,GLsizeiptr);
} API;
#define LOAD(a,h,f) do { (a)->f=(__typeof__((a)->f))dlsym((h),"gl" #f); CHECK((a)->f!=NULL,"resolve gl%s",#f); } while(0)
static int load_api(void* h, API* a){ memset(a,0,sizeof(*a));
 LOAD(a,h,GetString);LOAD(a,h,GetError);LOAD(a,h,GetIntegerv);LOAD(a,h,Finish);LOAD(a,h,Viewport);LOAD(a,h,ClearColor);LOAD(a,h,Clear);LOAD(a,h,ReadPixels);
 LOAD(a,h,GenTextures);LOAD(a,h,BindTexture);LOAD(a,h,TexImage2D);LOAD(a,h,TexParameteri);LOAD(a,h,GenFramebuffers);LOAD(a,h,BindFramebuffer);LOAD(a,h,FramebufferTexture2D);LOAD(a,h,CheckFramebufferStatus);
 LOAD(a,h,CreateShader);LOAD(a,h,ShaderSource);LOAD(a,h,CompileShader);LOAD(a,h,GetShaderiv);LOAD(a,h,CreateProgram);LOAD(a,h,AttachShader);LOAD(a,h,LinkProgram);LOAD(a,h,GetProgramiv);LOAD(a,h,UseProgram);
 LOAD(a,h,GetUniformBlockIndex);LOAD(a,h,UniformBlockBinding);LOAD(a,h,GenVertexArrays);LOAD(a,h,BindVertexArray);LOAD(a,h,DrawArrays);
 LOAD(a,h,GenBuffers);LOAD(a,h,BindBuffer);LOAD(a,h,BufferData);LOAD(a,h,BufferSubData);LOAD(a,h,MapBufferRange);LOAD(a,h,FlushMappedBufferRange);LOAD(a,h,UnmapBuffer);LOAD(a,h,CopyBufferSubData);LOAD(a,h,BindBufferRange);
 return failures==0; }
#undef LOAD

static int egl_setup(void* h){
 EGLDisplay (*GetDisplay)(EGLNativeDisplayType)=dlsym(h,"eglGetDisplay"); EGLBoolean (*Initialize)(EGLDisplay,EGLint*,EGLint*)=dlsym(h,"eglInitialize"); EGLBoolean (*BindAPI)(EGLenum)=dlsym(h,"eglBindAPI"); EGLBoolean (*GetConfigs)(EGLDisplay,EGLConfig*,EGLint,EGLint*)=dlsym(h,"eglGetConfigs"); EGLContext (*CreateContext)(EGLDisplay,EGLConfig,EGLContext,const EGLint*)=dlsym(h,"eglCreateContext"); EGLSurface (*CreatePbufferSurface)(EGLDisplay,EGLConfig,const EGLint*)=dlsym(h,"eglCreatePbufferSurface"); EGLBoolean (*MakeCurrent)(EGLDisplay,EGLSurface,EGLSurface,EGLContext)=dlsym(h,"eglMakeCurrent");
 CHECK(GetDisplay&&Initialize&&BindAPI&&GetConfigs&&CreateContext&&CreatePbufferSurface&&MakeCurrent,"resolve EGL entry points"); if(!GetDisplay||!Initialize||!BindAPI||!GetConfigs||!CreateContext||!CreatePbufferSurface||!MakeCurrent) return 0;
 EGLDisplay d=GetDisplay(EGL_DEFAULT_DISPLAY); EGLint ma=0,mi=0; CHECK(d!=EGL_NO_DISPLAY&&Initialize(d,&ma,&mi),"initialize EGL"); CHECK(BindAPI(EGL_OPENGL_API),"bind OpenGL API"); EGLConfig cfg=0; EGLint n=0; CHECK(GetConfigs(d,&cfg,1,&n)&&n>0,"get EGL config");
 const EGLint ca[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,EGL_NONE}; const EGLint pa[]={EGL_WIDTH,W,EGL_HEIGHT,H,EGL_NONE}; EGLContext c=CreateContext(d,cfg,EGL_NO_CONTEXT,ca); EGLSurface s=CreatePbufferSurface(d,cfg,pa); CHECK(c!=EGL_NO_CONTEXT&&s!=EGL_NO_SURFACE,"create pbuffer/context"); CHECK(MakeCurrent(d,s,s,c),"make current"); return c!=EGL_NO_CONTEXT&&s!=EGL_NO_SURFACE;
}
static GLuint make_program(API* a){
 const char* vs="#version 330 core\nvoid main(){ const vec2 p[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3)); gl_Position=vec4(p[gl_VertexID],0,1);}\n";
 const char* fs="#version 330 core\nlayout(std140) uniform Colors { vec4 uColor; }; out vec4 o; void main(){o=uColor;}\n";
 GLuint sv=a->CreateShader(GL_VERTEX_SHADER),sf=a->CreateShader(GL_FRAGMENT_SHADER); a->ShaderSource(sv,1,&vs,0);a->ShaderSource(sf,1,&fs,0);a->CompileShader(sv);a->CompileShader(sf); GLint vc=0,fc=0;a->GetShaderiv(sv,GL_COMPILE_STATUS,&vc);a->GetShaderiv(sf,GL_COMPILE_STATUS,&fc);CHECK(vc&&fc,"UBO shader pair compiles"); GLuint p=a->CreateProgram();a->AttachShader(p,sv);a->AttachShader(p,sf);a->LinkProgram(p);GLint linked=0;a->GetProgramiv(p,GL_LINK_STATUS,&linked);CHECK(linked,"UBO program links");return p;
}
static GLuint make_target(API* a){ GLuint t=0,f=0;a->GenTextures(1,&t);a->BindTexture(GL_TEXTURE_2D,t);a->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);a->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);a->TexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,W,H,0,GL_RGBA,GL_UNSIGNED_BYTE,0);a->GenFramebuffers(1,&f);a->BindFramebuffer(GL_FRAMEBUFFER,f);a->FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t,0);CHECK(a->CheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"FBO complete");return f; }
static int color_near(const uint8_t* p,int r,int g,int b){ return abs((int)p[0]-r)<=5&&abs((int)p[1]-g)<=5&&abs((int)p[2]-b)<=5&&p[3]>=245; }
static void read_center(API* a,uint8_t p[4]){ a->Finish();a->ReadPixels(W/2,H/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);a->Finish(); }
int main(int argc,char**argv){ const char* path=argc>1?argv[1]:"./libmithril.dylib";void*h=dlopen(path,RTLD_NOW|RTLD_GLOBAL);CHECK(h!=NULL,"dlopen %s",path);if(!h)return 2;if(!egl_setup(h))return 2;API a;if(!load_api(h,&a))return 2;const char*v=(const char*)a.GetString(GL_VERSION);CHECK(v&&strstr(v,"3.3.0")&&strstr(v,"DirectMetal"),"DirectMetal GL 3.3 active (%s)",v?v:"null");while(a.GetError()!=GL_NO_ERROR){}
 GLuint f=make_target(&a),prog=make_program(&a),vao=0;a.GenVertexArrays(1,&vao);a.BindVertexArray(vao);a.UseProgram(prog);a.Viewport(0,0,W,H);
 GLuint bi=a.GetUniformBlockIndex(prog,"Colors");CHECK(bi!=GL_INVALID_INDEX,"reflect Colors block (%u)",bi);a.UniformBlockBinding(prog,bi,3);
 GLint align=0;a.GetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT,&align);if(align<16)align=16;CHECK((align&(align-1))==0,"UBO offset alignment sane (%d)",align);
 GLuint src=0,dst=0;a.GenBuffers(1,&src);a.GenBuffers(1,&dst);size_t bytes=(size_t)align+64;uint8_t*zero=calloc(1,bytes);a.BindBuffer(GL_COPY_READ_BUFFER,src);a.BufferData(GL_COPY_READ_BUFFER,(GLsizeiptr)bytes,zero,GL_DYNAMIC_DRAW);a.BindBuffer(GL_COPY_WRITE_BUFFER,dst);a.BufferData(GL_COPY_WRITE_BUFFER,(GLsizeiptr)bytes,zero,GL_DYNAMIC_DRAW);free(zero);
 a.BindBuffer(GL_COPY_READ_BUFFER,src);float*mp=(float*)a.MapBufferRange(GL_COPY_READ_BUFFER,0,16,GL_MAP_WRITE_BIT|GL_MAP_FLUSH_EXPLICIT_BIT);CHECK(mp!=NULL,"map source buffer range");if(mp){mp[0]=0;mp[1]=1;mp[2]=0;mp[3]=1;a.FlushMappedBufferRange(GL_COPY_READ_BUFFER,0,16);CHECK(a.UnmapBuffer(GL_COPY_READ_BUFFER)==GL_TRUE,"unmap explicitly flushed source range");}
 a.BindBuffer(GL_COPY_WRITE_BUFFER,dst);a.CopyBufferSubData(GL_COPY_READ_BUFFER,GL_COPY_WRITE_BUFFER,0,align,16);a.BindBufferRange(GL_UNIFORM_BUFFER,3,dst,align,16);
 a.BindFramebuffer(GL_FRAMEBUFFER,f);a.ClearColor(0,0,0,1);a.Clear(GL_COLOR_BUFFER_BIT);a.DrawArrays(GL_TRIANGLES,0,3);uint8_t px[4]={0};read_center(&a,px);CHECK(color_near(px,0,255,0),"mapped+flushed+copied UBO range drives green GPU output = (%u,%u,%u,%u)",px[0],px[1],px[2],px[3]);
 const float red[4]={1,0,0,1};a.BindBuffer(GL_COPY_READ_BUFFER,src);a.BufferSubData(GL_COPY_READ_BUFFER,0,16,red);a.BindBuffer(GL_COPY_WRITE_BUFFER,dst);a.CopyBufferSubData(GL_COPY_READ_BUFFER,GL_COPY_WRITE_BUFFER,0,align,16);a.Clear(GL_COLOR_BUFFER_BIT);a.DrawArrays(GL_TRIANGLES,0,3);read_center(&a,px);CHECK(color_near(px,255,0,0),"BufferSubData+CopyBufferSubData updates bound UBO range to red = (%u,%u,%u,%u)",px[0],px[1],px[2],px[3]);
 CHECK(a.GetError()==GL_NO_ERROR,"buffer/UBO oracle ends without GL error");printf("BUFFER UBO SEMANTICS SMOKE: %d checks, %d failure(s)\n",checks,failures);if(!failures)printf("BUFFER UBO SEMANTICS SMOKE ALL PASSED\n");return failures?1:0; }
