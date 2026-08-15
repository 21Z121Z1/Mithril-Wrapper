/*
 * DirectMetal terrain/indexed semantics regression control.
 *
 * This intentionally targets OpenGL semantics heavily used by Minecraft/Sodium
 * terrain rendering but not covered by the broad render_smoke.c control:
 *   - GL_UNSIGNED_BYTE/SHORT/INT indexed draws
 *   - fixed and programmable primitive restart
 *   - multi-draw indirect + baseVertex
 *   - ARB_indirect_parameters parameter-buffer draw counts
 *   - viewport/scissor lower-left GL coordinates
 *   - ARB_clip_control origin/depth conventions and culling parity
 *
 * Every oracle renders to a user FBO and reads it back.  It is therefore an L4
 * GPU/render correctness test and does not depend on WindowServer/presentation.
 */
#include <dlfcn.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <EGL/egl.h>
#include <GL/glcorearb.h>

#ifndef GL_PRIMITIVE_RESTART_FIXED_INDEX
#define GL_PRIMITIVE_RESTART_FIXED_INDEX 0x8D69
#endif
#ifndef GL_PARAMETER_BUFFER_ARB
#define GL_PARAMETER_BUFFER_ARB 0x80EE
#endif
#ifndef GL_CLIP_ORIGIN
#define GL_CLIP_ORIGIN 0x935C
#endif
#ifndef GL_CLIP_DEPTH_MODE
#define GL_CLIP_DEPTH_MODE 0x935D
#endif
#ifndef GL_NEGATIVE_ONE_TO_ONE
#define GL_NEGATIVE_ONE_TO_ONE 0x935E
#endif
#ifndef GL_ZERO_TO_ONE
#define GL_ZERO_TO_ONE 0x935F
#endif
#ifndef GL_LOWER_LEFT
#define GL_LOWER_LEFT 0x8CA1
#endif
#ifndef GL_UPPER_LEFT
#define GL_UPPER_LEFT 0x8CA2
#endif

#define W 128
#define H 128

static int g_checks = 0;
static int g_failures = 0;
#define CHECK(c, fmt, ...) do { \
    ++g_checks; \
    if (c) printf("ok  : " fmt "\n", ##__VA_ARGS__); \
    else { printf("FAIL: " fmt "\n", ##__VA_ARGS__); ++g_failures; } \
} while (0)

typedef struct Api {
    const GLubyte* (*GetString)(GLenum);
    GLenum (*GetError)(void);
    void (*GetIntegerv)(GLenum, GLint*);
    void (*Finish)(void);
    void (*Enable)(GLenum);
    void (*Disable)(GLenum);
    void (*CullFace)(GLenum);
    void (*FrontFace)(GLenum);
    void (*PrimitiveRestartIndex)(GLuint);
    void (*ClipControl)(GLenum, GLenum);
    void (*Viewport)(GLint, GLint, GLsizei, GLsizei);
    void (*Scissor)(GLint, GLint, GLsizei, GLsizei);
    void (*ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
    void (*Clear)(GLbitfield);
    void (*ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
    void (*GenTextures)(GLsizei, GLuint*);
    void (*BindTexture)(GLenum, GLuint);
    void (*TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
    void (*TexParameteri)(GLenum, GLenum, GLint);
    void (*GenFramebuffers)(GLsizei, GLuint*);
    void (*BindFramebuffer)(GLenum, GLuint);
    void (*FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
    GLenum (*CheckFramebufferStatus)(GLenum);
    GLuint (*CreateShader)(GLenum);
    void (*ShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
    void (*CompileShader)(GLuint);
    void (*GetShaderiv)(GLuint, GLenum, GLint*);
    void (*GetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
    GLuint (*CreateProgram)(void);
    void (*AttachShader)(GLuint, GLuint);
    void (*LinkProgram)(GLuint);
    void (*GetProgramiv)(GLuint, GLenum, GLint*);
    void (*GetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
    void (*UseProgram)(GLuint);
    void (*DeleteShader)(GLuint);
    void (*GenVertexArrays)(GLsizei, GLuint*);
    void (*BindVertexArray)(GLuint);
    void (*GenBuffers)(GLsizei, GLuint*);
    void (*BindBuffer)(GLenum, GLuint);
    void (*BufferData)(GLenum, GLsizeiptr, const void*, GLenum);
    void (*VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
    void (*EnableVertexAttribArray)(GLuint);
    void (*DrawArrays)(GLenum, GLint, GLsizei);
    void (*DrawElements)(GLenum, GLsizei, GLenum, const void*);
    void (*DrawElementsBaseVertex)(GLenum, GLsizei, GLenum, const void*, GLint);
    void (*MultiDrawElementsIndirect)(GLenum, GLenum, const void*, GLsizei, GLsizei);
    void (*MultiDrawElementsIndirectCount)(GLenum, GLenum, const void*, GLintptr, GLsizei, GLsizei);
} Api;

#define LOAD(api,h,field,sym) do { \
    (api)->field = (__typeof__((api)->field))dlsym((h), (sym)); \
    CHECK((api)->field != NULL, "resolve %s", (sym)); \
} while (0)

static int load_api(void* h, Api* a) {
    memset(a, 0, sizeof(*a));
    LOAD(a,h,GetString,"glGetString"); LOAD(a,h,GetError,"glGetError");
    LOAD(a,h,GetIntegerv,"glGetIntegerv"); LOAD(a,h,Finish,"glFinish");
    LOAD(a,h,Enable,"glEnable"); LOAD(a,h,Disable,"glDisable");
    LOAD(a,h,CullFace,"glCullFace"); LOAD(a,h,FrontFace,"glFrontFace");
    LOAD(a,h,PrimitiveRestartIndex,"glPrimitiveRestartIndex");
    LOAD(a,h,ClipControl,"glClipControl");
    LOAD(a,h,Viewport,"glViewport"); LOAD(a,h,Scissor,"glScissor");
    LOAD(a,h,ClearColor,"glClearColor"); LOAD(a,h,Clear,"glClear");
    LOAD(a,h,ReadPixels,"glReadPixels");
    LOAD(a,h,GenTextures,"glGenTextures"); LOAD(a,h,BindTexture,"glBindTexture");
    LOAD(a,h,TexImage2D,"glTexImage2D"); LOAD(a,h,TexParameteri,"glTexParameteri");
    LOAD(a,h,GenFramebuffers,"glGenFramebuffers"); LOAD(a,h,BindFramebuffer,"glBindFramebuffer");
    LOAD(a,h,FramebufferTexture2D,"glFramebufferTexture2D");
    LOAD(a,h,CheckFramebufferStatus,"glCheckFramebufferStatus");
    LOAD(a,h,CreateShader,"glCreateShader"); LOAD(a,h,ShaderSource,"glShaderSource");
    LOAD(a,h,CompileShader,"glCompileShader"); LOAD(a,h,GetShaderiv,"glGetShaderiv");
    LOAD(a,h,GetShaderInfoLog,"glGetShaderInfoLog"); LOAD(a,h,CreateProgram,"glCreateProgram");
    LOAD(a,h,AttachShader,"glAttachShader"); LOAD(a,h,LinkProgram,"glLinkProgram");
    LOAD(a,h,GetProgramiv,"glGetProgramiv"); LOAD(a,h,GetProgramInfoLog,"glGetProgramInfoLog");
    LOAD(a,h,UseProgram,"glUseProgram"); LOAD(a,h,DeleteShader,"glDeleteShader");
    LOAD(a,h,GenVertexArrays,"glGenVertexArrays"); LOAD(a,h,BindVertexArray,"glBindVertexArray");
    LOAD(a,h,GenBuffers,"glGenBuffers"); LOAD(a,h,BindBuffer,"glBindBuffer");
    LOAD(a,h,BufferData,"glBufferData"); LOAD(a,h,VertexAttribPointer,"glVertexAttribPointer");
    LOAD(a,h,EnableVertexAttribArray,"glEnableVertexAttribArray");
    LOAD(a,h,DrawArrays,"glDrawArrays"); LOAD(a,h,DrawElements,"glDrawElements");
    LOAD(a,h,DrawElementsBaseVertex,"glDrawElementsBaseVertex");
    LOAD(a,h,MultiDrawElementsIndirect,"glMultiDrawElementsIndirect");
    LOAD(a,h,MultiDrawElementsIndirectCount,"glMultiDrawElementsIndirectCount");
    return g_failures == 0;
}
#undef LOAD

static int setup_egl(void* h) {
    EGLDisplay (*GetDisplay)(EGLNativeDisplayType) = dlsym(h,"eglGetDisplay");
    EGLBoolean (*Initialize)(EGLDisplay,EGLint*,EGLint*) = dlsym(h,"eglInitialize");
    EGLBoolean (*BindAPI)(EGLenum) = dlsym(h,"eglBindAPI");
    EGLBoolean (*GetConfigs)(EGLDisplay,EGLConfig*,EGLint,EGLint*) = dlsym(h,"eglGetConfigs");
    EGLContext (*CreateContext)(EGLDisplay,EGLConfig,EGLContext,const EGLint*) = dlsym(h,"eglCreateContext");
    EGLSurface (*CreatePbuffer)(EGLDisplay,EGLConfig,const EGLint*) = dlsym(h,"eglCreatePbufferSurface");
    EGLBoolean (*MakeCurrent)(EGLDisplay,EGLSurface,EGLSurface,EGLContext) = dlsym(h,"eglMakeCurrent");
    CHECK(GetDisplay&&Initialize&&BindAPI&&GetConfigs&&CreateContext&&CreatePbuffer&&MakeCurrent,
          "resolve EGL pbuffer entry points");
    if (!GetDisplay||!Initialize||!BindAPI||!GetConfigs||!CreateContext||!CreatePbuffer||!MakeCurrent) return 0;
    EGLDisplay d=GetDisplay(EGL_DEFAULT_DISPLAY); EGLint ma=0,mi=0;
    CHECK(d!=EGL_NO_DISPLAY && Initialize(d,&ma,&mi)==EGL_TRUE,"initialize EGL %d.%d",ma,mi);
    CHECK(BindAPI(EGL_OPENGL_API)==EGL_TRUE,"bind OpenGL API");
    EGLConfig cfg=NULL; EGLint n=0; CHECK(GetConfigs(d,&cfg,1,&n)==EGL_TRUE&&n>0,"choose EGL config");
    const EGLint ca[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,EGL_NONE};
    const EGLint pa[]={EGL_WIDTH,W,EGL_HEIGHT,H,EGL_NONE};
    EGLContext c=CreateContext(d,cfg,EGL_NO_CONTEXT,ca); EGLSurface s=CreatePbuffer(d,cfg,pa);
    CHECK(c!=EGL_NO_CONTEXT&&s!=EGL_NO_SURFACE,"create pbuffer/context");
    CHECK(MakeCurrent(d,s,s,c)==EGL_TRUE,"make context current");
    return c!=EGL_NO_CONTEXT && s!=EGL_NO_SURFACE;
}

typedef struct Vertex { float x,y,z,w,r,g,b; } Vertex;

static GLuint compile_program(Api* a) {
    const char* vs=
        "#version 330 core\n"
        "layout(location=0) in vec4 p; layout(location=1) in vec3 c; out vec3 vc;\n"
        "void main(){ gl_Position=p; vc=c; }\n";
    const char* fs=
        "#version 330 core\n"
        "in vec3 vc; out vec4 o; void main(){ o=vec4(vc,1.0); }\n";
    GLuint sv=a->CreateShader(GL_VERTEX_SHADER), sf=a->CreateShader(GL_FRAGMENT_SHADER);
    a->ShaderSource(sv,1,&vs,NULL); a->ShaderSource(sf,1,&fs,NULL);
    a->CompileShader(sv); a->CompileShader(sf); GLint okv=0,okf=0;
    a->GetShaderiv(sv,GL_COMPILE_STATUS,&okv); a->GetShaderiv(sf,GL_COMPILE_STATUS,&okf);
    if (!okv||!okf) { char l[2048]; GLsizei n=0; if(!okv){a->GetShaderInfoLog(sv,sizeof(l),&n,l);fprintf(stderr,"VS: %.*s\n",n,l);} if(!okf){a->GetShaderInfoLog(sf,sizeof(l),&n,l);fprintf(stderr,"FS: %.*s\n",n,l);} }
    CHECK(okv&&okf,"terrain semantics shaders compile");
    GLuint p=a->CreateProgram(); a->AttachShader(p,sv); a->AttachShader(p,sf); a->LinkProgram(p);
    GLint linked=0; a->GetProgramiv(p,GL_LINK_STATUS,&linked);
    if(!linked){char l[2048];GLsizei n=0;a->GetProgramInfoLog(p,sizeof(l),&n,l);fprintf(stderr,"LINK: %.*s\n",n,l);}
    CHECK(linked,"terrain semantics program links"); a->DeleteShader(sv); a->DeleteShader(sf); return p;
}

static GLuint make_target(Api* a) {
    GLuint t=0,f=0; a->GenTextures(1,&t); a->BindTexture(GL_TEXTURE_2D,t);
    a->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST); a->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    a->TexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,W,H,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    a->GenFramebuffers(1,&f); a->BindFramebuffer(GL_FRAMEBUFFER,f);
    a->FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t,0);
    CHECK(a->CheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"terrain target FBO complete");
    return f;
}

static void clear_target(Api* a, GLuint f) {
    a->BindFramebuffer(GL_FRAMEBUFFER,f); a->Viewport(0,0,W,H); a->Disable(GL_SCISSOR_TEST);
    a->Disable(GL_CULL_FACE); a->Disable(GL_DEPTH_TEST); a->Disable(GL_BLEND);
    a->ClearColor(0,0,0,1); a->Clear(GL_COLOR_BUFFER_BIT);
}

static void read_target(Api* a, uint8_t* px) { a->Finish(); a->ReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,px); a->Finish(); }
static int is_lit(const uint8_t* p){return p[0]>32||p[1]>32||p[2]>32;}
static uint32_t lit_rect(const uint8_t* px,int x0,int y0,int x1,int y1){uint32_t n=0;for(int y=y0;y<y1;y++)for(int x=x0;x<x1;x++)n+=is_lit(&px[(y*W+x)*4]);return n;}
static double lit_centroid_y(const uint8_t* px,uint32_t* count){double sy=0;uint32_t n=0;for(int y=0;y<H;y++)for(int x=0;x<W;x++)if(is_lit(&px[(y*W+x)*4])){sy+=y;n++;}*count=n;return n?sy/n:-1.0;}

static void bind_vertices(Api* a,const Vertex* v,size_t n,GLuint* vaoOut){
    GLuint vao=0,vbo=0; a->GenVertexArrays(1,&vao); a->BindVertexArray(vao); a->GenBuffers(1,&vbo); a->BindBuffer(GL_ARRAY_BUFFER,vbo);
    a->BufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(n*sizeof(Vertex)),v,GL_STATIC_DRAW);
    a->VertexAttribPointer(0,4,GL_FLOAT,GL_FALSE,sizeof(Vertex),(const void*)0); a->EnableVertexAttribArray(0);
    a->VertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(Vertex),(const void*)(4*sizeof(float))); a->EnableVertexAttribArray(1);
    *vaoOut=vao;
}

static void fill_two_triangles(Vertex* v,size_t n){
    for(size_t i=0;i<n;i++) v[i]=(Vertex){0,0,0,1,0,0,1};
    v[0]=(Vertex){-.95f,-.75f,0,1,1,0,0}; v[1]=(Vertex){-.18f,-.75f,0,1,1,0,0}; v[2]=(Vertex){-.55f,.75f,0,1,1,0,0};
    v[3]=(Vertex){ .18f,-.75f,0,1,0,1,0}; v[4]=(Vertex){ .95f,-.75f,0,1,0,1,0}; v[5]=(Vertex){ .55f,.75f,0,1,0,1,0};
}

static void assert_two_sides(const char* name,const uint8_t* px){
    uint32_t l=lit_rect(px,3,10,55,115), r=lit_rect(px,73,10,125,115);
    CHECK(l>600,"%s renders left indexed triangle (%u)",name,l); CHECK(r>600,"%s renders right indexed triangle (%u)",name,r);
}

static void basic_index_widths(Api* a,GLuint f,GLuint prog){
    Vertex v[6];fill_two_triangles(v,6);GLuint vao;bind_vertices(a,v,6,&vao);(void)vao;
    const uint8_t i8[]={0,1,2,3,4,5}; const uint16_t i16[]={0,1,2,3,4,5}; const uint32_t i32[]={0,1,2,3,4,5};
    const void* data[3]={i8,i16,i32}; size_t sizes[3]={sizeof(i8),sizeof(i16),sizeof(i32)}; GLenum types[3]={GL_UNSIGNED_BYTE,GL_UNSIGNED_SHORT,GL_UNSIGNED_INT}; const char* names[3]={"U8","U16","U32"};
    uint8_t px[W*H*4];
    for(int k=0;k<3;k++){GLuint e=0;a->GenBuffers(1,&e);a->BindBuffer(GL_ELEMENT_ARRAY_BUFFER,e);a->BufferData(GL_ELEMENT_ARRAY_BUFFER,sizes[k],data[k],GL_STATIC_DRAW);clear_target(a,f);a->UseProgram(prog);a->Disable(GL_PRIMITIVE_RESTART);a->Disable(GL_PRIMITIVE_RESTART_FIXED_INDEX);a->DrawElements(GL_TRIANGLES,6,types[k],0);read_target(a,px);assert_two_sides(names[k],px);CHECK(a->GetError()==GL_NO_ERROR,"%s indexed draw leaves no GL error",names[k]);}
}

static void restart_case(Api* a,GLuint f,GLuint prog,GLenum type,int programmable,int both,const char* label){
    size_t nv=(type==GL_UNSIGNED_BYTE)?256:8; Vertex* v=calloc(nv,sizeof(Vertex)); fill_two_triangles(v,nv); if(nv>7)v[7]=(Vertex){0,0,0,1,0,0,1}; if(nv>255)v[255]=(Vertex){0,0,0,1,0,0,1}; GLuint vao;bind_vertices(a,v,nv,&vao);(void)vao;free(v);
    uint8_t b8[7];uint16_t b16[7];uint32_t b32[7];uint32_t token=programmable?7u:(type==GL_UNSIGNED_BYTE?0xffu:type==GL_UNSIGNED_SHORT?0xffffu:0xffffffffu);uint32_t seq[7]={0,1,2,token,3,4,5};
    void* data=NULL;size_t bytes=0;if(type==GL_UNSIGNED_BYTE){for(int i=0;i<7;i++)b8[i]=(uint8_t)seq[i];data=b8;bytes=sizeof(b8);}else if(type==GL_UNSIGNED_SHORT){for(int i=0;i<7;i++)b16[i]=(uint16_t)seq[i];data=b16;bytes=sizeof(b16);}else{memcpy(b32,seq,sizeof(seq));data=b32;bytes=sizeof(b32);}GLuint e=0;a->GenBuffers(1,&e);a->BindBuffer(GL_ELEMENT_ARRAY_BUFFER,e);a->BufferData(GL_ELEMENT_ARRAY_BUFFER,bytes,data,GL_STATIC_DRAW);
    clear_target(a,f);a->UseProgram(prog);a->Disable(GL_PRIMITIVE_RESTART);a->Disable(GL_PRIMITIVE_RESTART_FIXED_INDEX);a->PrimitiveRestartIndex(7);
    if(programmable)a->Enable(GL_PRIMITIVE_RESTART);else a->Enable(GL_PRIMITIVE_RESTART_FIXED_INDEX);if(both){a->Enable(GL_PRIMITIVE_RESTART);a->Enable(GL_PRIMITIVE_RESTART_FIXED_INDEX);}
    uint8_t px[W*H*4];a->DrawElements(GL_TRIANGLE_STRIP,7,type,0);read_target(a,px);assert_two_sides(label,px);uint32_t center=lit_rect(px,56,20,72,108);CHECK(center<80,"%s restart prevents bridge triangles (center lit=%u)",label,center);CHECK(a->GetError()==GL_NO_ERROR,"%s restart draw leaves no GL error",label);a->Disable(GL_PRIMITIVE_RESTART);a->Disable(GL_PRIMITIVE_RESTART_FIXED_INDEX);
}

static void primitive_restart_matrix(Api* a,GLuint f,GLuint prog){
    restart_case(a,f,prog,GL_UNSIGNED_BYTE,0,0,"fixed restart U8"); restart_case(a,f,prog,GL_UNSIGNED_SHORT,0,0,"fixed restart U16"); restart_case(a,f,prog,GL_UNSIGNED_INT,0,0,"fixed restart U32");
    restart_case(a,f,prog,GL_UNSIGNED_BYTE,1,0,"programmable restart U8"); restart_case(a,f,prog,GL_UNSIGNED_SHORT,1,0,"programmable restart U16"); restart_case(a,f,prog,GL_UNSIGNED_INT,1,0,"programmable restart U32");
    /* Fixed-index must win if both capabilities are enabled. */
    restart_case(a,f,prog,GL_UNSIGNED_BYTE,0,1,"fixed wins U8"); restart_case(a,f,prog,GL_UNSIGNED_SHORT,0,1,"fixed wins U16"); restart_case(a,f,prog,GL_UNSIGNED_INT,0,1,"fixed wins U32");
}

typedef struct Cmd { uint32_t count,instanceCount,firstIndex; int32_t baseVertex; uint32_t baseInstance; } Cmd;
static void mdi_basevertex(Api* a,GLuint f,GLuint prog){
    Vertex v[9];for(int i=0;i<9;i++)v[i]=(Vertex){2,2,0,1,1,1,1};Vertex two[6];fill_two_triangles(two,6);for(int i=0;i<3;i++)v[3+i]=two[i];for(int i=0;i<3;i++)v[6+i]=two[3+i];GLuint vao;bind_vertices(a,v,9,&vao);(void)vao;
    uint16_t idx[6]={0,1,2,0,1,2};Cmd cmds[2]={{3,1,0,3,0},{3,1,3,6,0}};GLuint e=0,ib=0;a->GenBuffers(1,&e);a->BindBuffer(GL_ELEMENT_ARRAY_BUFFER,e);a->BufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(idx),idx,GL_STATIC_DRAW);a->GenBuffers(1,&ib);a->BindBuffer(GL_DRAW_INDIRECT_BUFFER,ib);a->BufferData(GL_DRAW_INDIRECT_BUFFER,sizeof(cmds),cmds,GL_STATIC_DRAW);
    clear_target(a,f);a->UseProgram(prog);a->MultiDrawElementsIndirect(GL_TRIANGLES,GL_UNSIGNED_SHORT,0,2,0);uint8_t px[W*H*4];read_target(a,px);assert_two_sides("MDI baseVertex",px);CHECK(a->GetError()==GL_NO_ERROR,"MDI baseVertex leaves no GL error");

    /* ARB_indirect_parameters: drawcount comes from GL_PARAMETER_BUFFER_ARB, not the indirect buffer. */
    uint32_t one=1;GLuint pb=0;a->GenBuffers(1,&pb);a->BindBuffer(GL_PARAMETER_BUFFER_ARB,pb);a->BufferData(GL_PARAMETER_BUFFER_ARB,sizeof(one),&one,GL_STATIC_DRAW);clear_target(a,f);a->UseProgram(prog);a->BindBuffer(GL_DRAW_INDIRECT_BUFFER,ib);a->MultiDrawElementsIndirectCount(GL_TRIANGLES,GL_UNSIGNED_SHORT,0,0,2,0);read_target(a,px);uint32_t l=lit_rect(px,3,10,55,115),r=lit_rect(px,73,10,125,115);CHECK(l>600,"indirect-count parameter buffer emits first draw (%u)",l);CHECK(r<80,"indirect-count parameter buffer clamps to one draw (%u right pixels)",r);CHECK(a->GetError()==GL_NO_ERROR,"indirect-count valid call leaves no GL error");
}

static void viewport_scissor(Api* a,GLuint f,GLuint prog){
    Vertex v[3]={{-1,-1,0,1,1,1,1},{3,-1,0,1,1,1,1},{-1,3,0,1,1,1,1}};GLuint vao;bind_vertices(a,v,3,&vao);(void)vao;clear_target(a,f);a->UseProgram(prog);a->Viewport(16,24,80,72);a->Enable(GL_SCISSOR_TEST);a->Scissor(32,40,48,32);a->DrawArrays(GL_TRIANGLES,0,3);uint8_t px[W*H*4];read_target(a,px);uint32_t inside=lit_rect(px,32,40,80,72),outside=lit_rect(px,0,0,32,128)+lit_rect(px,80,0,128,128)+lit_rect(px,32,0,80,40)+lit_rect(px,32,72,80,128);CHECK(inside>1400,"viewport/scissor intersection rasterizes in GL lower-left coordinates (%u)",inside);CHECK(outside<40,"viewport/scissor clips outside intersection (%u)",outside);a->Disable(GL_SCISSOR_TEST);CHECK(a->GetError()==GL_NO_ERROR,"viewport/scissor leaves no GL error");
}

static void draw_clip_triangle(Api* a,GLuint f,GLuint prog,GLenum origin,GLenum depth,float y,float z,int cull,uint8_t* px){
    Vertex v[3]={{-.65f,y-.25f,z,1,1,1,1},{.65f,y-.25f,z,1,1,1,1},{0,y+.35f,z,1,1,1,1}};GLuint vao;bind_vertices(a,v,3,&vao);(void)vao;clear_target(a,f);a->ClipControl(origin,depth);a->UseProgram(prog);if(cull){a->Enable(GL_CULL_FACE);a->CullFace(GL_BACK);a->FrontFace(GL_CCW);}a->DrawArrays(GL_TRIANGLES,0,3);read_target(a,px);a->Disable(GL_CULL_FACE);
}

static void clip_control(Api* a,GLuint f,GLuint prog){
    uint8_t lo[W*H*4],up[W*H*4],neg[W*H*4],zero[W*H*4];
    draw_clip_triangle(a,f,prog,GL_LOWER_LEFT,GL_NEGATIVE_ONE_TO_ONE,.45f,0,1,lo);draw_clip_triangle(a,f,prog,GL_UPPER_LEFT,GL_NEGATIVE_ONE_TO_ONE,.45f,0,1,up);uint32_t nl=0,nu=0;double yl=lit_centroid_y(lo,&nl),yu=lit_centroid_y(up,&nu);CHECK(nl>400&&nu>400,"clip-origin keeps front-facing triangle visible with culling (lower=%u upper=%u)",nl,nu);CHECK(fabs((yl+yu)-(H-1))<12.0 && fabs(yl-yu)>25.0,"UPPER_LEFT mirrors NDC Y while readback coordinates stay lower-left (centroids %.2f %.2f)",yl,yu);
    draw_clip_triangle(a,f,prog,GL_LOWER_LEFT,GL_NEGATIVE_ONE_TO_ONE,0,-.5f,0,neg);draw_clip_triangle(a,f,prog,GL_LOWER_LEFT,GL_ZERO_TO_ONE,0,-.5f,0,zero);uint32_t nn=lit_rect(neg,0,0,W,H),nz=lit_rect(zero,0,0,W,H);CHECK(nn>400,"NEGATIVE_ONE_TO_ONE accepts z=-0.5w (%u)",nn);CHECK(nz<20,"ZERO_TO_ONE clips z=-0.5w before viewport depth mapping (%u)",nz);
    GLint o=0,d=0;a->GetIntegerv(GL_CLIP_ORIGIN,&o);a->GetIntegerv(GL_CLIP_DEPTH_MODE,&d);CHECK(o==GL_LOWER_LEFT&&d==GL_ZERO_TO_ONE,"glGetIntegerv exposes active clip-control state (0x%x 0x%x)",o,d);a->ClipControl(GL_LOWER_LEFT,GL_NEGATIVE_ONE_TO_ONE);CHECK(a->GetError()==GL_NO_ERROR,"clip-control valid states leave no GL error");
}

int main(int argc,char**argv){const char* path=argc>1?argv[1]:"./libmithril.dylib";void*h=dlopen(path,RTLD_NOW|RTLD_GLOBAL);CHECK(h!=NULL,"dlopen %s",path);if(!h){fprintf(stderr,"%s\n",dlerror());return 2;}if(!setup_egl(h))return 2;Api a;if(!load_api(h,&a))return 2;const char* ver=(const char*)a.GetString(GL_VERSION);CHECK(ver&&strstr(ver,"Metal 3 (DirectMetal)")&&strstr(ver,"Mithril-Wrapper"),"DirectMetal active (%s)",ver?ver:"null");GLuint f=make_target(&a),p=compile_program(&a);a.BindFramebuffer(GL_FRAMEBUFFER,f);a.Viewport(0,0,W,H);a.UseProgram(p);
    basic_index_widths(&a,f,p);primitive_restart_matrix(&a,f,p);mdi_basevertex(&a,f,p);viewport_scissor(&a,f,p);clip_control(&a,f,p);
    printf("TERRAIN SEMANTICS SMOKE: %d checks, %d failure(s)\n",g_checks,g_failures);if(!g_failures)printf("TERRAIN SEMANTICS SMOKE ALL PASSED\n");return g_failures?1:0;}
