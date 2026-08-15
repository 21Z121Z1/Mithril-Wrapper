/*
 * DirectMetal exact Sodium mc26.2-0.9.1 terrain contract oracle.
 *
 * Sodium's CompactChunkVertex is exactly 20 bytes:
 *   location 0: RG32_UINT   @ 0
 *   location 1: RGBA8_UNORM @ 8
 *   location 2: RG16_UINT   @ 12
 *   location 3: RGBA8_UINT  @ 16
 * Its GL batch submits U32 element indices with byte offsets and per-draw
 * baseVertex through glMultiDrawElementsBaseVertex.  This smoke reproduces
 * that contract on a real Metal GPU, both through legacy pointer setup and
 * GL 4.5 DSA/separate vertex bindings.
 *
 * A second oracle exercises the OpenGL/Metal semantic mismatch where restart
 * is disabled and 0xffff must remain a real U16 vertex index.  Metal always
 * treats the maximum index value as a restart sentinel, so DirectMetal must
 * adapt that case rather than forwarding it unchanged.
 */
#include <dlfcn.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <EGL/egl.h>
#include <GL/glcorearb.h>

#ifndef GL_PRIMITIVE_RESTART_FIXED_INDEX
#define GL_PRIMITIVE_RESTART_FIXED_INDEX 0x8D69
#endif
#ifndef GL_VERTEX_ARRAY_BINDING
#define GL_VERTEX_ARRAY_BINDING 0x85B5
#endif

#define W 160
#define H 120

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
    void (*ClearColor)(GLfloat,GLfloat,GLfloat,GLfloat);
    void (*Clear)(GLbitfield);
    void (*Viewport)(GLint,GLint,GLsizei,GLsizei);
    void (*ReadPixels)(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void*);

    void (*GenTextures)(GLsizei,GLuint*);
    void (*BindTexture)(GLenum,GLuint);
    void (*TexImage2D)(GLenum,GLint,GLint,GLsizei,GLsizei,GLint,GLenum,GLenum,const void*);
    void (*TexParameteri)(GLenum,GLenum,GLint);
    void (*GenFramebuffers)(GLsizei,GLuint*);
    void (*BindFramebuffer)(GLenum,GLuint);
    void (*FramebufferTexture2D)(GLenum,GLenum,GLenum,GLuint,GLint);
    GLenum (*CheckFramebufferStatus)(GLenum);

    GLuint (*CreateShader)(GLenum);
    void (*ShaderSource)(GLuint,GLsizei,const GLchar* const*,const GLint*);
    void (*CompileShader)(GLuint);
    void (*GetShaderiv)(GLuint,GLenum,GLint*);
    void (*GetShaderInfoLog)(GLuint,GLsizei,GLsizei*,GLchar*);
    GLuint (*CreateProgram)(void);
    void (*AttachShader)(GLuint,GLuint);
    void (*LinkProgram)(GLuint);
    void (*GetProgramiv)(GLuint,GLenum,GLint*);
    void (*GetProgramInfoLog)(GLuint,GLsizei,GLsizei*,GLchar*);
    void (*UseProgram)(GLuint);
    void (*DeleteShader)(GLuint);

    void (*GenVertexArrays)(GLsizei,GLuint*);
    void (*BindVertexArray)(GLuint);
    void (*GenBuffers)(GLsizei,GLuint*);
    void (*BindBuffer)(GLenum,GLuint);
    void (*BufferData)(GLenum,GLsizeiptr,const void*,GLenum);
    void (*VertexAttribPointer)(GLuint,GLint,GLenum,GLboolean,GLsizei,const void*);
    void (*VertexAttribIPointer)(GLuint,GLint,GLenum,GLsizei,const void*);
    void (*EnableVertexAttribArray)(GLuint);
    void (*MultiDrawElementsBaseVertex)(GLenum,const GLsizei*,GLenum,const void* const*,GLsizei,const GLint*);
    void (*DrawElements)(GLenum,GLsizei,GLenum,const void*);

    void (*VertexArrayVertexBuffer)(GLuint,GLuint,GLuint,GLintptr,GLsizei);
    void (*VertexArrayAttribFormat)(GLuint,GLuint,GLint,GLenum,GLboolean,GLuint);
    void (*VertexArrayAttribIFormat)(GLuint,GLuint,GLint,GLenum,GLuint);
    void (*VertexArrayAttribBinding)(GLuint,GLuint,GLuint);
    void (*VertexArrayElementBuffer)(GLuint,GLuint);
    void (*EnableVertexArrayAttrib)(GLuint,GLuint);
} Api;

#define LOAD(a,h,f,s) do { \
    (a)->f=(__typeof__((a)->f))dlsym((h),(s)); \
    CHECK((a)->f!=NULL,"resolve %s",(s)); \
} while(0)

static int load_api(void* h, Api* a) {
    memset(a,0,sizeof(*a));
    LOAD(a,h,GetString,"glGetString"); LOAD(a,h,GetError,"glGetError");
    LOAD(a,h,GetIntegerv,"glGetIntegerv"); LOAD(a,h,Finish,"glFinish");
    LOAD(a,h,Enable,"glEnable"); LOAD(a,h,Disable,"glDisable");
    LOAD(a,h,ClearColor,"glClearColor"); LOAD(a,h,Clear,"glClear");
    LOAD(a,h,Viewport,"glViewport"); LOAD(a,h,ReadPixels,"glReadPixels");
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
    LOAD(a,h,BufferData,"glBufferData");
    LOAD(a,h,VertexAttribPointer,"glVertexAttribPointer");
    LOAD(a,h,VertexAttribIPointer,"glVertexAttribIPointer");
    LOAD(a,h,EnableVertexAttribArray,"glEnableVertexAttribArray");
    LOAD(a,h,MultiDrawElementsBaseVertex,"glMultiDrawElementsBaseVertex");
    LOAD(a,h,DrawElements,"glDrawElements");
    LOAD(a,h,VertexArrayVertexBuffer,"glVertexArrayVertexBuffer");
    LOAD(a,h,VertexArrayAttribFormat,"glVertexArrayAttribFormat");
    LOAD(a,h,VertexArrayAttribIFormat,"glVertexArrayAttribIFormat");
    LOAD(a,h,VertexArrayAttribBinding,"glVertexArrayAttribBinding");
    LOAD(a,h,VertexArrayElementBuffer,"glVertexArrayElementBuffer");
    LOAD(a,h,EnableVertexArrayAttrib,"glEnableVertexArrayAttrib");
    return g_failures==0;
}
#undef LOAD

static int setup_egl(void* h) {
    EGLDisplay (*GetDisplay)(EGLNativeDisplayType)=dlsym(h,"eglGetDisplay");
    EGLBoolean (*Initialize)(EGLDisplay,EGLint*,EGLint*)=dlsym(h,"eglInitialize");
    EGLBoolean (*BindAPI)(EGLenum)=dlsym(h,"eglBindAPI");
    EGLBoolean (*GetConfigs)(EGLDisplay,EGLConfig*,EGLint,EGLint*)=dlsym(h,"eglGetConfigs");
    EGLContext (*CreateContext)(EGLDisplay,EGLConfig,EGLContext,const EGLint*)=dlsym(h,"eglCreateContext");
    EGLSurface (*CreatePbuffer)(EGLDisplay,EGLConfig,const EGLint*)=dlsym(h,"eglCreatePbufferSurface");
    EGLBoolean (*MakeCurrent)(EGLDisplay,EGLSurface,EGLSurface,EGLContext)=dlsym(h,"eglMakeCurrent");
    CHECK(GetDisplay&&Initialize&&BindAPI&&GetConfigs&&CreateContext&&CreatePbuffer&&MakeCurrent,
          "resolve EGL pbuffer entry points");
    if(!GetDisplay||!Initialize||!BindAPI||!GetConfigs||!CreateContext||!CreatePbuffer||!MakeCurrent) return 0;
    EGLDisplay d=GetDisplay(EGL_DEFAULT_DISPLAY); EGLint ma=0,mi=0;
    CHECK(d!=EGL_NO_DISPLAY&&Initialize(d,&ma,&mi)==EGL_TRUE,"initialize EGL %d.%d",ma,mi);
    CHECK(BindAPI(EGL_OPENGL_API)==EGL_TRUE,"bind OpenGL API");
    EGLConfig cfg=NULL; EGLint n=0;
    CHECK(GetConfigs(d,&cfg,1,&n)==EGL_TRUE&&n>0,"choose EGL config");
    const EGLint ca[]={EGL_CONTEXT_MAJOR_VERSION,4,EGL_CONTEXT_MINOR_VERSION,6,EGL_NONE};
    const EGLint pa[]={EGL_WIDTH,W,EGL_HEIGHT,H,EGL_NONE};
    EGLContext c=CreateContext(d,cfg,EGL_NO_CONTEXT,ca); EGLSurface s=CreatePbuffer(d,cfg,pa);
    CHECK(c!=EGL_NO_CONTEXT&&s!=EGL_NO_SURFACE,"create pbuffer/context");
    CHECK(MakeCurrent(d,s,s,c)==EGL_TRUE,"make context current");
    return c!=EGL_NO_CONTEXT&&s!=EGL_NO_SURFACE;
}

static GLuint compile_program(Api* a,const char* vs,const char* fs,const char* label) {
    GLuint sv=a->CreateShader(GL_VERTEX_SHADER), sf=a->CreateShader(GL_FRAGMENT_SHADER);
    a->ShaderSource(sv,1,&vs,NULL); a->ShaderSource(sf,1,&fs,NULL);
    a->CompileShader(sv); a->CompileShader(sf); GLint okv=0,okf=0;
    a->GetShaderiv(sv,GL_COMPILE_STATUS,&okv); a->GetShaderiv(sf,GL_COMPILE_STATUS,&okf);
    if(!okv||!okf){char l[4096];GLsizei n=0;if(!okv){a->GetShaderInfoLog(sv,sizeof(l),&n,l);fprintf(stderr,"%s VS: %.*s\n",label,n,l);}if(!okf){a->GetShaderInfoLog(sf,sizeof(l),&n,l);fprintf(stderr,"%s FS: %.*s\n",label,n,l);}}
    CHECK(okv&&okf,"%s shaders compile",label);
    GLuint p=a->CreateProgram(); a->AttachShader(p,sv); a->AttachShader(p,sf); a->LinkProgram(p);
    GLint linked=0; a->GetProgramiv(p,GL_LINK_STATUS,&linked);
    if(!linked){char l[4096];GLsizei n=0;a->GetProgramInfoLog(p,sizeof(l),&n,l);fprintf(stderr,"%s LINK: %.*s\n",label,n,l);}
    CHECK(linked,"%s program links",label); a->DeleteShader(sv); a->DeleteShader(sf); return p;
}

static GLuint make_target(Api* a) {
    GLuint t=0,f=0; a->GenTextures(1,&t); a->BindTexture(GL_TEXTURE_2D,t);
    a->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    a->TexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    a->TexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,W,H,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    a->GenFramebuffers(1,&f); a->BindFramebuffer(GL_FRAMEBUFFER,f);
    a->FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t,0);
    CHECK(a->CheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"target FBO complete");
    return f;
}

static void clear_target(Api* a,GLuint f) {
    a->BindFramebuffer(GL_FRAMEBUFFER,f); a->Viewport(0,0,W,H);
    a->Disable(GL_SCISSOR_TEST); a->Disable(GL_CULL_FACE); a->Disable(GL_DEPTH_TEST);
    a->Disable(GL_BLEND); a->Disable(GL_PRIMITIVE_RESTART); a->Disable(GL_PRIMITIVE_RESTART_FIXED_INDEX);
    a->ClearColor(0,0,0,1); a->Clear(GL_COLOR_BUFFER_BIT);
}

static void read_target(Api* a,uint8_t* px){a->Finish();a->ReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,px);a->Finish();}
static void count_colors(const uint8_t* px,uint32_t* red,uint32_t* green,uint32_t* cyan,uint32_t* magenta){
    *red=*green=*cyan=*magenta=0;
    for(int i=0;i<W*H;i++){
        const uint8_t* p=&px[i*4];
        if(p[0]>180&&p[1]<80&&p[2]<80)(*red)++;
        if(p[1]>180&&p[0]<80&&p[2]<80)(*green)++;
        if(p[1]>150&&p[2]>150&&p[0]<80)(*cyan)++;
        if(p[0]>150&&p[2]>150&&p[1]<80)(*magenta)++;
    }
}

typedef struct PackedVertex {
    uint32_t pos[2];
    uint8_t color[4];
    uint16_t tex[2];
    uint8_t light[4];
} PackedVertex;
_Static_assert(sizeof(PackedVertex)==20,"Sodium CompactChunkVertex must be 20 bytes");
_Static_assert(offsetof(PackedVertex,color)==8,"color offset");
_Static_assert(offsetof(PackedVertex,tex)==12,"tex offset");
_Static_assert(offsetof(PackedVertex,light)==16,"light offset");

static PackedVertex pv(uint32_t x,uint32_t y,uint8_t r,uint8_t g,uint8_t b,uint8_t drawid){
    PackedVertex v; memset(&v,0,sizeof(v));
    v.pos[0]=x; v.pos[1]=y; v.color[0]=r;v.color[1]=g;v.color[2]=b;v.color[3]=255;
    v.tex[0]=101;v.tex[1]=202;v.light[0]=11;v.light[1]=22;v.light[2]=33;v.light[3]=drawid;
    return v;
}

static void fill_sodium_vertices(PackedVertex* v){
    for(int i=0;i<8;i++)v[i]=pv(500,500,0,0,0,0);
    v[2]=pv(50,100,255,0,0,7); v[3]=pv(400,100,255,0,0,7); v[4]=pv(225,900,255,0,0,7);
    v[5]=pv(600,100,0,255,0,13);v[6]=pv(950,100,0,255,0,13);v[7]=pv(775,900,0,255,0,13);
}

static GLuint sodium_program(Api* a){
    const char* vs=
      "#version 330 core\n"
      "layout(location=0) in uvec2 a_Position;\n"
      "layout(location=1) in vec4 a_Color;\n"
      "layout(location=2) in uvec2 a_TexCoord;\n"
      "layout(location=3) in uvec4 a_LightAndData;\n"
      "out vec4 vc;\n"
      "void main(){\n"
      " uint expected=(a_Position.x<500u)?7u:13u;\n"
      " bool ok=all(equal(a_TexCoord,uvec2(101u,202u))) && all(equal(a_LightAndData.xyz,uvec3(11u,22u,33u))) && a_LightAndData.w==expected;\n"
      " vec2 p=vec2(a_Position)/500.0-vec2(1.0);\n"
      " gl_Position=vec4(p,0.0,1.0); vc=ok?a_Color:vec4(1,0,1,1);\n"
      "}\n";
    const char* fs="#version 330 core\nin vec4 vc;out vec4 o;void main(){o=vc;}\n";
    return compile_program(a,vs,fs,"Sodium packed terrain");
}

static void upload_sodium_buffers(Api* a,GLuint* vbo,GLuint* ebo,GLintptr* vertexBase){
    enum{PREFIX=32}; uint8_t blob[PREFIX+8*sizeof(PackedVertex)]; memset(blob,0xCD,sizeof(blob));
    PackedVertex verts[8];fill_sodium_vertices(verts);memcpy(blob+PREFIX,verts,sizeof(verts));
    a->GenBuffers(1,vbo);a->BindBuffer(GL_ARRAY_BUFFER,*vbo);a->BufferData(GL_ARRAY_BUFFER,sizeof(blob),blob,GL_STATIC_DRAW);
    const uint32_t idx[]={99,98,97,96, 0,1,2, 0,1,2};
    a->GenBuffers(1,ebo);a->BindBuffer(GL_ELEMENT_ARRAY_BUFFER,*ebo);a->BufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(idx),idx,GL_STATIC_DRAW);
    *vertexBase=PREFIX;
}

static void run_sodium_legacy(Api* a,GLuint f,GLuint prog,uint8_t* px){
    clear_target(a,f);a->UseProgram(prog);
    GLuint vao=0,vbo=0,ebo=0;GLintptr base=0;a->GenVertexArrays(1,&vao);a->BindVertexArray(vao);
    upload_sodium_buffers(a,&vbo,&ebo,&base);
    a->BindBuffer(GL_ARRAY_BUFFER,vbo);a->BindBuffer(GL_ELEMENT_ARRAY_BUFFER,ebo);
    a->VertexAttribIPointer(0,2,GL_UNSIGNED_INT,20,(const void*)(uintptr_t)(base+0));a->EnableVertexAttribArray(0);
    a->VertexAttribPointer(1,4,GL_UNSIGNED_BYTE,GL_TRUE,20,(const void*)(uintptr_t)(base+8));a->EnableVertexAttribArray(1);
    a->VertexAttribIPointer(2,2,GL_UNSIGNED_SHORT,20,(const void*)(uintptr_t)(base+12));a->EnableVertexAttribArray(2);
    a->VertexAttribIPointer(3,4,GL_UNSIGNED_BYTE,20,(const void*)(uintptr_t)(base+16));a->EnableVertexAttribArray(3);
    const GLsizei count[2]={3,3};const void* offsets[2]={(const void*)(uintptr_t)16,(const void*)(uintptr_t)28};const GLint bv[2]={2,5};
    a->MultiDrawElementsBaseVertex(GL_TRIANGLES,count,GL_UNSIGNED_INT,offsets,2,bv);
    CHECK(a->GetError()==GL_NO_ERROR,"legacy Sodium multi-draw has no GL error");read_target(a,px);
    uint32_t r,g,c,m;count_colors(px,&r,&g,&c,&m);
    CHECK(r>900,"legacy packed U32/baseVertex/byte-offset renders red section (%u)",r);
    CHECK(g>900,"legacy packed U32/baseVertex/byte-offset renders green section (%u)",g);
    CHECK(m<20,"legacy mixed integer/normalized attributes preserve section metadata (magenta=%u)",m);
}

static void run_sodium_dsa(Api* a,GLuint f,GLuint prog,uint8_t* px){
    clear_target(a,f);a->UseProgram(prog);
    GLuint sentinel=0,target=0,vbo=0,ebo=0;GLintptr base=0;a->GenVertexArrays(1,&sentinel);a->GenVertexArrays(1,&target);
    a->BindVertexArray(sentinel);upload_sodium_buffers(a,&vbo,&ebo,&base);
    /* Rebind sentinel after upload_sodium_buffers touched its element binding. */
    a->BindVertexArray(sentinel);
    a->VertexArrayVertexBuffer(target,5,vbo,base,20);
    a->VertexArrayAttribIFormat(target,0,2,GL_UNSIGNED_INT,0);
    a->VertexArrayAttribFormat(target,1,4,GL_UNSIGNED_BYTE,GL_TRUE,8);
    a->VertexArrayAttribIFormat(target,2,2,GL_UNSIGNED_SHORT,12);
    a->VertexArrayAttribIFormat(target,3,4,GL_UNSIGNED_BYTE,16);
    for(GLuint i=0;i<4;i++){a->VertexArrayAttribBinding(target,i,5);a->EnableVertexArrayAttrib(target,i);}
    a->VertexArrayElementBuffer(target,ebo);
    GLint still=0;a->GetIntegerv(GL_VERTEX_ARRAY_BINDING,&still);
    CHECK((GLuint)still==sentinel,"DSA setup preserves currently bound VAO (%d)",still);
    a->BindVertexArray(target);
    const GLsizei count[2]={3,3};const void* offsets[2]={(const void*)(uintptr_t)16,(const void*)(uintptr_t)28};const GLint bv[2]={2,5};
    a->MultiDrawElementsBaseVertex(GL_TRIANGLES,count,GL_UNSIGNED_INT,offsets,2,bv);
    CHECK(a->GetError()==GL_NO_ERROR,"DSA Sodium multi-draw has no GL error");read_target(a,px);
    uint32_t r,g,c,m;count_colors(px,&r,&g,&c,&m);
    CHECK(r>900,"DSA shared binding/base offset renders red section (%u)",r);
    CHECK(g>900,"DSA shared binding/base offset renders green section (%u)",g);
    CHECK(m<20,"DSA relative offsets preserve integer section metadata (magenta=%u)",m);
}

static void run_restart_disabled_u16_max(Api* a,GLuint f,uint8_t* px){
    const char* vs="#version 330 core\nlayout(location=0) in vec2 p;void main(){gl_Position=vec4(p,0,1);}\n";
    const char* fs="#version 330 core\nout vec4 o;void main(){o=vec4(0,1,1,1);}\n";
    GLuint prog=compile_program(a,vs,fs,"restart-disabled U16 sentinel");
    clear_target(a,f);a->UseProgram(prog);
    float* verts=(float*)calloc(65536u*2u,sizeof(float));CHECK(verts!=NULL,"allocate 65536-vertex U16 sentinel fixture");if(!verts)return;
    verts[0]=-0.75f;verts[1]=-0.7f;verts[2]=0.75f;verts[3]=-0.7f;verts[65535u*2u]=0.0f;verts[65535u*2u+1u]=0.7f;
    GLuint vao=0,vbo=0,ebo=0;a->GenVertexArrays(1,&vao);a->BindVertexArray(vao);
    a->GenBuffers(1,&vbo);a->BindBuffer(GL_ARRAY_BUFFER,vbo);a->BufferData(GL_ARRAY_BUFFER,65536u*2u*sizeof(float),verts,GL_STATIC_DRAW);free(verts);
    a->VertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),(const void*)0);a->EnableVertexAttribArray(0);
    const uint16_t idx[]={0,1,0xffffu};a->GenBuffers(1,&ebo);a->BindBuffer(GL_ELEMENT_ARRAY_BUFFER,ebo);a->BufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(idx),idx,GL_STATIC_DRAW);
    a->Disable(GL_PRIMITIVE_RESTART);a->Disable(GL_PRIMITIVE_RESTART_FIXED_INDEX);a->DrawElements(GL_TRIANGLES,3,GL_UNSIGNED_SHORT,(const void*)0);
    CHECK(a->GetError()==GL_NO_ERROR,"restart-disabled U16 max-index draw has no GL error");read_target(a,px);
    uint32_t r,g,c,m;count_colors(px,&r,&g,&c,&m);
    CHECK(c>1500,"restart-disabled 0xffff remains a real U16 index (%u cyan pixels)",c);
}

int main(int argc,char** argv){
    if(argc!=2){fprintf(stderr,"usage: %s /path/to/libmithril.dylib\n",argv[0]);return 2;}
    void* h=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);if(!h){fprintf(stderr,"dlopen: %s\n",dlerror());return 2;}
    CHECK(setup_egl(h),"EGL setup succeeds");Api a;if(!load_api(h,&a))return 1;
    printf("GL_VERSION=%s\n",a.GetString(GL_VERSION));printf("GL_RENDERER=%s\n",a.GetString(GL_RENDERER));
    GLuint f=make_target(&a),prog=sodium_program(&a);uint8_t* px=(uint8_t*)malloc((size_t)W*H*4);CHECK(px!=NULL,"allocate readback");if(!px)return 2;
    run_sodium_legacy(&a,f,prog,px);run_sodium_dsa(&a,f,prog,px);run_restart_disabled_u16_max(&a,f,px);
    printf("SODIUM_TERRAIN_CONTRACT checks=%d failures=%d\n",g_checks,g_failures);free(px);dlclose(h);return g_failures?1:0;
}
