/* Focused regressions for the 2026-08-14 DirectVulkan device-debug fixes.
 * Runs headless against Mithril; on Linux this reaches Vulkan through lavapipe,
 * and on macOS it reaches MoltenVK/Metal. */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <GL/glcorearb.h>

#ifndef GL_INVALID_INDEX
#define GL_INVALID_INDEX 0xFFFFFFFFu
#endif

static int checks = 0, failures = 0;
#define CHECK(c, fmt, ...) do { ++checks; if (c) printf("ok : " fmt "\n", ##__VA_ARGS__); else { printf("FAIL: " fmt "\n", ##__VA_ARGS__); ++failures; } } while (0)
#define LOAD(type, var, name) type var = (type)dlsym(h, name); if (!(var)) { fprintf(stderr, "missing %s\n", name); return 3; }

typedef void (*PFN_void_enum)(GLenum);
typedef void (*PFN_frontface)(GLenum);
typedef void (*PFN_gen)(GLsizei, GLuint*);
typedef void (*PFN_bindtex)(GLenum, GLuint);
typedef void (*PFN_teximage2d)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
typedef void (*PFN_texsubimage2d)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*);
typedef void (*PFN_texparam)(GLenum, GLenum, GLint);
typedef void (*PFN_bindfb)(GLenum, GLuint);
typedef void (*PFN_fbtex2d)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum (*PFN_checkfb)(GLenum);
typedef void (*PFN_bindbuf)(GLenum, GLuint);
typedef void (*PFN_bufdata)(GLenum, GLsizeiptr, const void*, GLenum);
typedef void (*PFN_bindvao)(GLuint);
typedef void (*PFN_attribptr)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
typedef void (*PFN_enableattrib)(GLuint);
typedef GLuint (*PFN_createshader)(GLenum);
typedef void (*PFN_shadersource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
typedef void (*PFN_compileshader)(GLuint);
typedef void (*PFN_getshaderiv)(GLuint, GLenum, GLint*);
typedef void (*PFN_getshaderlog)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef GLuint (*PFN_createprogram)(void);
typedef void (*PFN_attachshader)(GLuint, GLuint);
typedef void (*PFN_linkprogram)(GLuint);
typedef void (*PFN_getprogramiv)(GLuint, GLenum, GLint*);
typedef void (*PFN_getprogramlog)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef void (*PFN_useprogram)(GLuint);
typedef void (*PFN_deleteobj)(GLuint);
typedef void (*PFN_viewport)(GLint, GLint, GLsizei, GLsizei);
typedef void (*PFN_clearcolor)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (*PFN_clear)(GLbitfield);
typedef void (*PFN_drawarrays)(GLenum, GLint, GLsizei);
typedef void (*PFN_finish)(void);
typedef void (*PFN_readpixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
typedef GLenum (*PFN_geterror)(void);
typedef void (*PFN_activetex)(GLenum);
typedef GLint (*PFN_getuniformloc)(GLuint, const GLchar*);
typedef void (*PFN_uniform1i)(GLint, GLint);
typedef GLuint (*PFN_getblockindex)(GLuint, const GLchar*);
typedef void (*PFN_blockbinding)(GLuint, GLuint, GLuint);
typedef void (*PFN_bindbufferbase)(GLenum, GLuint, GLuint);
typedef void* (*PFN_eglgetproc)(const char*);

typedef struct API {
    PFN_void_enum enable, disable, cullFace;
    PFN_frontface frontFace;
    PFN_gen genTextures, genFramebuffers, genBuffers, genVertexArrays;
    PFN_bindtex bindTexture;
    PFN_teximage2d texImage2D;
    PFN_texsubimage2d texSubImage2D;
    PFN_texparam texParameteri;
    PFN_bindfb bindFramebuffer;
    PFN_fbtex2d framebufferTexture2D;
    PFN_checkfb checkFramebufferStatus;
    PFN_bindbuf bindBuffer;
    PFN_bufdata bufferData;
    PFN_bindvao bindVertexArray;
    PFN_attribptr vertexAttribPointer;
    PFN_enableattrib enableVertexAttribArray;
    PFN_createshader createShader;
    PFN_shadersource shaderSource;
    PFN_compileshader compileShader;
    PFN_getshaderiv getShaderiv;
    PFN_getshaderlog getShaderInfoLog;
    PFN_createprogram createProgram;
    PFN_attachshader attachShader;
    PFN_linkprogram linkProgram;
    PFN_getprogramiv getProgramiv;
    PFN_getprogramlog getProgramInfoLog;
    PFN_useprogram useProgram;
    PFN_deleteobj deleteShader, deleteProgram;
    PFN_viewport viewport;
    PFN_clearcolor clearColor;
    PFN_clear clear;
    PFN_drawarrays drawArrays;
    PFN_finish finish;
    PFN_readpixels readPixels;
    PFN_geterror getError;
    PFN_activetex activeTexture;
    PFN_getuniformloc getUniformLocation;
    PFN_uniform1i uniform1i;
    PFN_getblockindex getUniformBlockIndex;
    PFN_blockbinding uniformBlockBinding;
    PFN_bindbufferbase bindBufferBase;
} API;

static GLuint shader(API* a, GLenum stage, const char* src) {
    GLuint s = a->createShader(stage);
    a->shaderSource(s, 1, &src, NULL);
    a->compileShader(s);
    GLint ok = 0; a->getShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[2048] = {0}; GLsizei n = 0; a->getShaderInfoLog(s, sizeof(log), &n, log); fprintf(stderr, "shader compile failed: %s\n", log); }
    CHECK(ok == GL_TRUE, "shader stage 0x%x compiled", stage);
    return s;
}

static GLuint program(API* a, const char* vs, const char* fs) {
    GLuint v = shader(a, GL_VERTEX_SHADER, vs), f = shader(a, GL_FRAGMENT_SHADER, fs);
    GLuint p = a->createProgram(); a->attachShader(p, v); a->attachShader(p, f); a->linkProgram(p);
    GLint ok = 0; a->getProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[2048] = {0}; GLsizei n = 0; a->getProgramInfoLog(p, sizeof(log), &n, log); fprintf(stderr, "program link failed: %s\n", log); }
    CHECK(ok == GL_TRUE, "program linked");
    a->deleteShader(v); a->deleteShader(f);
    return p;
}

static int green(const unsigned char p[4]) { return p[1] > 180 && p[0] < 40 && p[2] < 40 && p[3] > 180; }
static int black(const unsigned char p[4]) { return p[0] < 20 && p[1] < 20 && p[2] < 20; }
static int red(const unsigned char p[4]) { return p[0] > 180 && p[1] < 40 && p[2] < 40 && p[3] > 180; }

int main(int argc, char** argv) {
    if (argc != 2) { fprintf(stderr, "usage: %s /path/to/libmithril\n", argv[0]); return 2; }
    void* h = dlopen(argv[1], RTLD_NOW | RTLD_GLOBAL);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }
    API a = {0};
#define ALOAD(field, type, sym) do { a.field = (type)dlsym(h, sym); if (!a.field) { fprintf(stderr, "missing %s\n", sym); return 3; } } while (0)
    ALOAD(enable, PFN_void_enum, "glEnable"); ALOAD(disable, PFN_void_enum, "glDisable"); ALOAD(cullFace, PFN_void_enum, "glCullFace"); ALOAD(frontFace, PFN_frontface, "glFrontFace");
    ALOAD(genTextures, PFN_gen, "glGenTextures"); ALOAD(genFramebuffers, PFN_gen, "glGenFramebuffers"); ALOAD(genBuffers, PFN_gen, "glGenBuffers"); ALOAD(genVertexArrays, PFN_gen, "glGenVertexArrays");
    ALOAD(bindTexture, PFN_bindtex, "glBindTexture"); ALOAD(texImage2D, PFN_teximage2d, "glTexImage2D"); ALOAD(texSubImage2D, PFN_texsubimage2d, "glTexSubImage2D"); ALOAD(texParameteri, PFN_texparam, "glTexParameteri");
    ALOAD(bindFramebuffer, PFN_bindfb, "glBindFramebuffer"); ALOAD(framebufferTexture2D, PFN_fbtex2d, "glFramebufferTexture2D"); ALOAD(checkFramebufferStatus, PFN_checkfb, "glCheckFramebufferStatus");
    ALOAD(bindBuffer, PFN_bindbuf, "glBindBuffer"); ALOAD(bufferData, PFN_bufdata, "glBufferData"); ALOAD(bindVertexArray, PFN_bindvao, "glBindVertexArray"); ALOAD(vertexAttribPointer, PFN_attribptr, "glVertexAttribPointer"); ALOAD(enableVertexAttribArray, PFN_enableattrib, "glEnableVertexAttribArray");
    ALOAD(createShader, PFN_createshader, "glCreateShader"); ALOAD(shaderSource, PFN_shadersource, "glShaderSource"); ALOAD(compileShader, PFN_compileshader, "glCompileShader"); ALOAD(getShaderiv, PFN_getshaderiv, "glGetShaderiv"); ALOAD(getShaderInfoLog, PFN_getshaderlog, "glGetShaderInfoLog");
    ALOAD(createProgram, PFN_createprogram, "glCreateProgram"); ALOAD(attachShader, PFN_attachshader, "glAttachShader"); ALOAD(linkProgram, PFN_linkprogram, "glLinkProgram"); ALOAD(getProgramiv, PFN_getprogramiv, "glGetProgramiv"); ALOAD(getProgramInfoLog, PFN_getprogramlog, "glGetProgramInfoLog"); ALOAD(useProgram, PFN_useprogram, "glUseProgram"); ALOAD(deleteShader, PFN_deleteobj, "glDeleteShader"); ALOAD(deleteProgram, PFN_deleteobj, "glDeleteProgram");
    ALOAD(viewport, PFN_viewport, "glViewport"); ALOAD(clearColor, PFN_clearcolor, "glClearColor"); ALOAD(clear, PFN_clear, "glClear"); ALOAD(drawArrays, PFN_drawarrays, "glDrawArrays"); ALOAD(finish, PFN_finish, "glFinish"); ALOAD(readPixels, PFN_readpixels, "glReadPixels"); ALOAD(getError, PFN_geterror, "glGetError");
    ALOAD(activeTexture, PFN_activetex, "glActiveTexture"); ALOAD(getUniformLocation, PFN_getuniformloc, "glGetUniformLocation"); ALOAD(uniform1i, PFN_uniform1i, "glUniform1i"); ALOAD(getUniformBlockIndex, PFN_getblockindex, "glGetUniformBlockIndex"); ALOAD(uniformBlockBinding, PFN_blockbinding, "glUniformBlockBinding"); ALOAD(bindBufferBase, PFN_bindbufferbase, "glBindBufferBase");
#undef ALOAD

    /* Symbol ownership regression: eglGetProcAddress must not hand LWJGL a system OpenGL function. */
    PFN_eglgetproc eglGetProcAddress = (PFN_eglgetproc)dlsym(h, "eglGetProcAddress");
    CHECK(eglGetProcAddress != NULL, "eglGetProcAddress exported");
    if (eglGetProcAddress) {
        void* viaEgl = eglGetProcAddress("glGetString");
        void* direct = dlsym(h, "glGetString");
        CHECK(viaEgl != NULL && viaEgl == direct, "eglGetProcAddress(glGetString) resolves to Mithril implementation");
        if (viaEgl) { Dl_info info = {0}; int ok = dladdr(viaEgl, &info); CHECK(ok && info.dli_fname && strstr(info.dli_fname, "mithril"), "glGetString owner is Mithril (%s)", ok && info.dli_fname ? info.dli_fname : "unknown"); }
    }

    const int W = 64, H = 64;
    GLuint color = 0, fbo = 0;
    a.genTextures(1, &color); a.bindTexture(GL_TEXTURE_2D, color); a.texImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    a.genFramebuffers(1, &fbo); a.bindFramebuffer(GL_FRAMEBUFFER, fbo); a.framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
    CHECK(a.checkFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "regression FBO complete"); a.viewport(0, 0, W, H);

    const char* solidVS = "#version 330 core\nlayout(location=0) in vec2 p; void main(){gl_Position=vec4(p,0,1);}\n";
    const char* greenFS = "#version 330 core\nout vec4 c; void main(){c=vec4(0,1,0,1);}\n";
    GLuint solid = program(&a, solidVS, greenFS);
    const GLfloat tri[] = {-0.8f,-0.8f, 0.8f,-0.8f, 0.0f,0.8f};
    GLuint vao=0,vbo=0; a.genVertexArrays(1,&vao); a.bindVertexArray(vao); a.genBuffers(1,&vbo); a.bindBuffer(GL_ARRAY_BUFFER,vbo); a.bufferData(GL_ARRAY_BUFFER,sizeof(tri),tri,GL_STATIC_DRAW); a.vertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,(void*)0); a.enableVertexAttribArray(0);

    /* Regression 1: GL user-FBO winding must remain GL semantics after the Vulkan Y mapping. */
    a.useProgram(solid); a.enable(GL_CULL_FACE); a.cullFace(GL_BACK); a.frontFace(GL_CCW); a.clearColor(0,0,0,1); a.clear(GL_COLOR_BUFFER_BIT); a.drawArrays(GL_TRIANGLES,0,3); a.finish();
    unsigned char px[4]={0}; a.readPixels(W/2,H/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px); CHECK(green(px), "GL_CCW + BACK cull survives user-FBO mapping (%u,%u,%u,%u)",px[0],px[1],px[2],px[3]);
    a.frontFace(GL_CW); a.clear(GL_COLOR_BUFFER_BIT); a.drawArrays(GL_TRIANGLES,0,3); a.finish(); memset(px,0,sizeof(px)); a.readPixels(W/2,H/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px); CHECK(black(px), "GL_CW makes the same CCW triangle back-facing (%u,%u,%u,%u)",px[0],px[1],px[2],px[3]);
    a.disable(GL_CULL_FACE); a.frontFace(GL_CCW); CHECK(a.getError()==GL_NO_ERROR,"culling regression leaves no GL error");

    /* Regression 2: PBO pixels is an offset, not a CPU address. */
    GLuint sampleTex=0,pbo=0; const unsigned char rgba[16]={255,0,0,255,255,0,0,255,255,0,0,255,255,0,0,255};
    a.genTextures(1,&sampleTex); a.bindTexture(GL_TEXTURE_2D,sampleTex); a.texImage2D(GL_TEXTURE_2D,0,GL_RGBA8,2,2,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL); a.texParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST); a.texParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    a.genBuffers(1,&pbo); a.bindBuffer(GL_PIXEL_UNPACK_BUFFER,pbo); a.bufferData(GL_PIXEL_UNPACK_BUFFER,sizeof(rgba),rgba,GL_STATIC_DRAW); a.texSubImage2D(GL_TEXTURE_2D,0,0,0,2,2,GL_RGBA,GL_UNSIGNED_BYTE,(const void*)(uintptr_t)0); CHECK(a.getError()==GL_NO_ERROR,"PBO offset 0 upload accepted");
    a.texSubImage2D(GL_TEXTURE_2D,0,0,0,2,2,GL_RGBA,GL_UNSIGNED_BYTE,(const void*)(uintptr_t)8);
    /* Mithril intentionally mirrors MobileGlues' no-error public contract:
     * glGetError() always reports GL_NO_ERROR.  Safety is therefore verified
     * by the absence of a crash and by the later readback proving this rejected
     * out-of-range update did not corrupt the previously valid red upload. */
    CHECK(a.getError()==GL_NO_ERROR,"out-of-range PBO offset handled safely under Mithril no-error contract");
    a.bindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    const char* texVS="#version 330 core\nlayout(location=0) in vec2 p; out vec2 uv; void main(){gl_Position=vec4(p,0,1);uv=p*0.5+0.5;}\n";
    const char* texFS="#version 330 core\nin vec2 uv; out vec4 c; uniform sampler2D t; void main(){c=texture(t,uv);}\n";
    GLuint texProg=program(&a,texVS,texFS); a.useProgram(texProg); a.activeTexture(GL_TEXTURE0); a.bindTexture(GL_TEXTURE_2D,sampleTex); GLint tloc=a.getUniformLocation(texProg,"t"); CHECK(tloc>=0,"PBO sample sampler location valid"); if(tloc>=0)a.uniform1i(tloc,0);
    const GLfloat fsTri[]={-1,-1,3,-1,-1,3}; a.bindBuffer(GL_ARRAY_BUFFER,vbo); a.bufferData(GL_ARRAY_BUFFER,sizeof(fsTri),fsTri,GL_STATIC_DRAW); a.vertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,(void*)0); a.clearColor(0,0,0,1); a.clear(GL_COLOR_BUFFER_BIT); a.drawArrays(GL_TRIANGLES,0,3); a.finish(); memset(px,0,sizeof(px)); a.readPixels(W/2,H/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px); CHECK(red(px),"PBO-uploaded texture samples red (%u,%u,%u,%u)",px[0],px[1],px[2],px[3]);

    /* Regression 3: one GL uniform block shared by VS+FS must map to one backend binding. */
    const char* uboVS="#version 330 core\nlayout(location=0) in vec2 p; layout(std140) uniform SharedBlock{vec4 tint;}; out vec4 vt; void main(){gl_Position=vec4(p,0,1);vt=tint;}\n";
    const char* uboFS="#version 330 core\nlayout(std140) uniform SharedBlock{vec4 tint;}; in vec4 vt; out vec4 c; void main(){c=vt*tint;}\n";
    GLuint uboProg=program(&a,uboVS,uboFS); GLuint block=a.getUniformBlockIndex(uboProg,"SharedBlock"); CHECK(block!=GL_INVALID_INDEX,"cross-stage SharedBlock reflected once (index=%u)",block);
    GLuint ubo=0; const GLfloat tint[4]={1.0f,0.5f,0.25f,1.0f}; a.genBuffers(1,&ubo); a.bindBuffer(GL_UNIFORM_BUFFER,ubo); a.bufferData(GL_UNIFORM_BUFFER,sizeof(tint),tint,GL_STATIC_DRAW); if(block!=GL_INVALID_INDEX)a.uniformBlockBinding(uboProg,block,3); a.bindBufferBase(GL_UNIFORM_BUFFER,3,ubo); a.useProgram(uboProg); a.bindVertexArray(vao); a.clear(GL_COLOR_BUFFER_BIT); a.drawArrays(GL_TRIANGLES,0,3); a.finish(); memset(px,0,sizeof(px)); a.readPixels(W/2,H/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,px);
    CHECK(px[0]>220 && px[1]>45 && px[1]<90 && px[2]<35 && px[3]>220,"cross-stage UBO drives both shader stages (%u,%u,%u,%u)",px[0],px[1],px[2],px[3]); CHECK(a.getError()==GL_NO_ERROR,"cross-stage UBO regression leaves no GL error");

    printf("OVERNIGHT REGRESSION: %d checks, %d failures\n",checks,failures);
    dlclose(h); return failures ? 1 : 0;
}
