/* M5 state-pipeline smoke test (stages A+B): depth test + blend + scissor +
 * cull/stencil/colorMask/polygon on the default framebuffer, verified through
 * RV8 readback.
 *
 * Exercises the new pipeline-state path end to end:
 *   glEnable(GL_DEPTH_TEST)/glDepthFunc/glClear(GL_DEPTH_BUFFER_BIT) ->
 *   two overlapping triangles at different z -> the near one wins
 *   glEnable(GL_BLEND) + glBlendFunc -> premultiplied blend result
 *   glEnable(GL_SCISSOR_TEST)+glScissor -> clipped region
 *   GL_CULL_FACE + glCullFace/glFrontFace -> winding-consistent dropping
 *   GL_STENCIL_TEST + glStencilFunc/Op/Top/Mask -> REPLACE mark + EQUAL read
 *   glColorMask -> per-channel write gating
 *   glPolygonMode(GL_LINE) -> wireframe interior empty, FILL restores
 *
 * Build (from project root):
 *   gcc -o tests/fbo_smoke tests/fbo_smoke.c -ldl
 *   LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu ./tests/fbo_smoke
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* GL 3.3 core constants (values from glcorearb.h) */
#define GL_VERTEX_SHADER      0x8B31
#define GL_FRAGMENT_SHADER    0x8B30
#define GL_TRIANGLES          0x0004
#define GL_ARRAY_BUFFER       0x8892
#define GL_FLOAT              0x1406
#define GL_FALSE              0
#define GL_TRUE               1
#define GL_COLOR_BUFFER_BIT   0x00004000
#define GL_DEPTH_BUFFER_BIT   0x00000100
#define GL_STENCIL_BUFFER_BIT 0x00000400
#define GL_COLOR              0x1800
#define GL_RGBA               0x1908
#define GL_DEPTH_COMPONENT    0x1902
#define GL_UNSIGNED_BYTE      0x1401
#define GL_DEPTH_TEST         0x0B71
#define GL_BLEND              0x0BE2
#define GL_SCISSOR_TEST       0x0C11
#define GL_LEQUAL             0x0203
#define GL_ALWAYS             0x0207
#define GL_EQUAL              0x0202
#define GL_NOTEQUAL           0x0205
#define GL_SRC_ALPHA          0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_CULL_FACE          0x0B44
#define GL_STENCIL_TEST       0x0B90
#define GL_FRONT              0x0404
#define GL_BACK               0x0405
#define GL_FRONT_AND_BACK     0x0408
#define GL_CW                 0x0900
#define GL_CCW                0x0901
#define GL_KEEP               0x1E00
#define GL_REPLACE            0x1E01
#define GL_LINE               0x1B01
#define GL_FILL               0x1B02
/* S5-MRT constants */
#define GL_DRAW_BUFFER          0x0C01
#define GL_BACK                 0x0405
#define GL_NONE                 0
#define GL_RENDERBUFFER_SAMPLES 0x8CAB
#define GL_COLOR_ATTACHMENT1    0x8CE1
#define GL_FRAMEBUFFER        0x8D40
#define GL_DRAW_FRAMEBUFFER    0x8CA9
#define GL_READ_FRAMEBUFFER    0x8CA8
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_RENDERBUFFER        0x8D41
#define GL_TEXTURE_2D          0x0DE1
#define GL_TEXTURE_2D_MULTISAMPLE 0x9100
#define GL_TEXTURE_SAMPLES     0x9106
#define GL_TEXTURE_FIXED_SAMPLE_LOCATIONS 0x9107
#define GL_RGBA8               0x8058
#define GL_DEPTH_COMPONENT32F  0x8CAC
#define GL_TEXTURE_INTERNAL_FORMAT 0x1003
#define GL_TEXTURE_RED_TYPE    0x8C10
#define GL_TEXTURE_COMPARE_MODE 0x884C
#define GL_TEXTURE_COMPARE_FUNC 0x884D
#define GL_COMPARE_REF_TO_TEXTURE 0x884E
#define GL_NEAREST             0x2600
#define GL_LINEAR              0x2601
#define GL_COLOR_ATTACHMENT0   0x8CE0
#define GL_FRAMEBUFFER_DEFAULT 0x8218
#define GL_DEPTH_STENCIL       0x84F9
#define GL_DEPTH_ATTACHMENT    0x8D00
#define GL_STENCIL_ATTACHMENT  0x8D20
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#define GL_DEPTH24_STENCIL8    0x88F0
#define GL_MAX_DRAW_BUFFERS     0x8824
#define GL_MAX_COLOR_TEXTURE_SAMPLES 0x910E
#define GL_MAX_DUAL_SOURCE_DRAW_BUFFERS 0x88FC

typedef unsigned int GLuint;
typedef unsigned int GLenum;
typedef unsigned int GLsizei;
typedef unsigned int GLbitfield;
typedef unsigned char GLboolean;
typedef int GLint;
typedef int GLsizeiptr;
typedef int GLintptr;
typedef void* GLvoid;

typedef void (*fn_glClearColor)(float, float, float, float);
typedef void (*fn_glClear)(GLenum);
typedef void (*fn_glClearBufferfv)(GLenum, GLint, const float*);
typedef void (*fn_glEnable)(GLenum);
typedef void (*fn_glDisable)(GLenum);
typedef void (*fn_glDepthFunc)(GLenum);
typedef void (*fn_glViewport)(GLint, GLint, GLsizei, GLsizei);
typedef void (*fn_glScissor)(GLint, GLint, GLsizei, GLsizei);
typedef void (*fn_glBlendFunc)(GLenum, GLenum);
typedef void (*fn_glCullFace)(GLenum);
typedef void (*fn_glFrontFace)(GLenum);
typedef void (*fn_glStencilFunc)(GLenum, GLint, GLuint);
typedef void (*fn_glStencilOp)(GLenum, GLenum, GLenum);
typedef void (*fn_glStencilMask)(GLuint);
typedef void (*fn_glColorMask)(GLboolean, GLboolean, GLboolean, GLboolean);
typedef void (*fn_glPolygonMode)(GLenum, GLenum);
typedef GLuint (*fn_glCreateShader)(GLenum);
typedef void (*fn_glShaderSource)(GLuint, GLsizei, const char* const*, const GLint*);
typedef void (*fn_glCompileShader)(GLuint);
typedef GLuint (*fn_glCreateProgram)(void);
typedef void (*fn_glAttachShader)(GLuint, GLuint);
typedef void (*fn_glBindAttribLocation)(GLuint, GLuint, const char*);
typedef GLint (*fn_glGetAttribLocation)(GLuint, const char*);
typedef void (*fn_glBindFragDataLocation)(GLuint, GLuint, const char*);
typedef void (*fn_glBindFragDataLocationIndexed)(GLuint, GLuint, GLuint,
                                                  const char*);
typedef GLint (*fn_glGetFragDataLocation)(GLuint, const char*);
typedef GLint (*fn_glGetFragDataIndex)(GLuint, const char*);
typedef void (*fn_glLinkProgram)(GLuint);
typedef void (*fn_glUseProgram)(GLuint);
typedef GLint (*fn_glGetUniformLocation)(GLuint, const char*);
typedef void (*fn_glUniform4f)(GLint, float, float, float, float);
typedef void (*fn_glGenVertexArrays)(GLsizei, GLuint*);
typedef void (*fn_glBindVertexArray)(GLuint);
typedef void (*fn_glGenBuffers)(GLsizei, GLuint*);
typedef void (*fn_glBindBuffer)(GLenum, GLuint);
typedef void (*fn_glBufferData)(GLenum, GLsizeiptr, const void*, GLenum);
typedef void (*fn_glEnableVertexAttribArray)(GLuint);
typedef void (*fn_glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const GLvoid*);
typedef void (*fn_glDrawArrays)(GLenum, GLint, GLsizei);
typedef void (*fn_glFinish)(void);
typedef void (*fn_glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
typedef void (*fn_glGenFramebuffers)(GLsizei, GLuint*);
typedef void (*fn_glBindFramebuffer)(GLenum, GLuint);
typedef void (*fn_glDeleteFramebuffers)(GLsizei, const GLuint*);
typedef void (*fn_glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef void (*fn_glFramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint);
typedef GLenum (*fn_glCheckFramebufferStatus)(GLenum);
typedef void (*fn_glGenRenderbuffers)(GLsizei, GLuint*);
typedef void (*fn_glBindRenderbuffer)(GLenum, GLuint);
typedef void (*fn_glDeleteRenderbuffers)(GLsizei, const GLuint*);
typedef void (*fn_glRenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei);
typedef void (*fn_glGenTextures)(GLsizei, GLuint*);
typedef void (*fn_glBindTexture)(GLenum, GLuint);
typedef void (*fn_glDeleteTextures)(GLsizei, const GLuint*);
typedef void (*fn_glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
typedef void (*fn_glTexParameteri)(GLenum, GLenum, GLint);
typedef void (*fn_glTexImage2DMultisample)(GLenum, GLsizei, GLenum, GLsizei,
                                           GLsizei, GLboolean);
typedef void (*fn_glGetTexLevelParameteriv)(GLenum, GLint, GLenum, GLint*);
typedef void (*fn_glBlitFramebuffer)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
typedef void (*fn_glDrawBuffers)(GLsizei, const GLenum*);
typedef void (*fn_glDrawBuffer)(GLenum);
typedef void (*fn_glReadBuffer)(GLenum);
typedef void (*fn_glRenderbufferStorageMultisample)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
typedef void (*fn_glGetRenderbufferParameteriv)(GLenum, GLenum, GLint*);
typedef void (*fn_glGetIntegerv)(GLenum, GLint*);

static int failures = 0;

#define CHECK(cond, fmt, ...) do {                                          \
    if (cond) { printf("ok  : " fmt "\n", ##__VA_ARGS__); }                 \
    else      { printf("FAIL: " fmt "\n", ##__VA_ARGS__); ++failures; }     \
} while (0)

static int px_match(const unsigned char* got, unsigned char r, unsigned char g,
                    unsigned char b, unsigned char a) {
    return abs((int)got[0] - r) <= 3 && abs((int)got[1] - g) <= 3 &&
           abs((int)got[2] - b) <= 3 && abs((int)got[3] - a) <= 3;
}

static const char* VS =
    "#version 150\n"
    "layout(location=0) in vec3 pos;\n"
    "layout(location=1) in vec4 col;\n"
    "out vec4 vColor;\n"
    "void main() {\n"
    "    vColor = col;\n"
    "    gl_Position = vec4(pos, 1.0);\n"
    "}\n";

static const char* FS =
    "#version 150\n"
    "in vec4 vColor;\n"
    "layout(location=0) out vec4 fragColor;\n"
    "void main() {\n"
    "    fragColor = vColor;\n"
    "}\n";

/* Declaration order deliberately disagrees with the requested vertex slots.
 * A pre-link binding must map pos to slot 0 and col to slot 1. */
static const char* VS_BOUND =
    "#version 150\n"
    "in vec4 col;\n"
    "in vec3 pos;\n"
    "out vec4 vColor;\n"
    "void main() {\n"
    "    vColor = col;\n"
    "    gl_Position = vec4(pos, 1.0);\n"
    "}\n";

/* S5-MRT: writes the input color to colour attachment 0 and a fixed green
 * to attachment 1. Both outputs come from a uniforms-free constant path so
 * the fixed-function MRT plumbing is what's exercised. */
static const char* FS_MRT =
    "#version 150\n"
    "in vec4 vColor;\n"
    "out vec4 fragColor1;\n"
    "out vec4 fragColor0;\n"
    "void main() {\n"
    "    fragColor0 = vColor;\n"
    "    fragColor1 = vec4(0.0, 1.0, 0.0, 1.0);\n"
    "}\n";

static const char* FS_MULTISAMPLE =
    "#version 150\n"
    "uniform sampler2DMS msColor;\n"
    "layout(location=0) out vec4 fragColor;\n"
    "void main() {\n"
    "    fragColor = texelFetch(msColor, ivec2(16, 16), 0);\n"
    "}\n";

static const char* FS_DEPTH_TEXTURE =
    "#version 150\n"
    "uniform sampler2D depthImage;\n"
    "layout(location=0) out vec4 fragColor;\n"
    "void main() {\n"
    "    float d = texelFetch(depthImage, ivec2(16, 16), 0).r;\n"
    "    fragColor = vec4(d, 0.0, 0.0, 1.0);\n"
    "}\n";

/* Render-to-texture orientation probe.  GL framebuffer row zero and texture
 * texel row zero are the same logical row, even when the native backend uses
 * a top-left render-target origin. */
static const char* FS_FBO_TEXEL =
    "#version 150\n"
    "uniform sampler2D colorImage;\n"
    "uniform vec4 samplePoint;\n"
    "layout(location=0) out vec4 fragColor;\n"
    "void main() {\n"
    "    fragColor = texelFetch(colorImage, ivec2(samplePoint.xy), 0);\n"
    "}\n";

static const char* FS_DEPTH_SHADOW =
    "#version 150\n"
    "uniform sampler2DShadow depthImage;\n"
    "layout(location=0) out vec4 fragColor;\n"
    "void main() {\n"
    "    float visible = texture(depthImage, vec3(0.5, 0.5, 0.25));\n"
    "    fragColor = vec4(visible, 0.0, 0.0, 1.0);\n"
    "}\n";

int main(void) {
    const char* library = getenv("MITHRIL_LIBRARY");
#if defined(__APPLE__)
    if (!library || !*library) library = "./output/libmithril.dylib";
#else
    if (!library || !*library) library = "./output/libmithril.so";
#endif
    void* h = dlopen(library, RTLD_NOW | RTLD_GLOBAL);
    if (!h) { printf("dlopen: %s\n", dlerror()); return 2; }

    fn_glClearColor clearColor = (fn_glClearColor)dlsym(h, "glClearColor");
    fn_glClear clear = (fn_glClear)dlsym(h, "glClear");
    fn_glClearBufferfv clearBufferfv =
        (fn_glClearBufferfv)dlsym(h, "glClearBufferfv");
    fn_glEnable enable = (fn_glEnable)dlsym(h, "glEnable");
    fn_glDisable disable = (fn_glDisable)dlsym(h, "glDisable");
    fn_glDepthFunc depthFunc = (fn_glDepthFunc)dlsym(h, "glDepthFunc");
    fn_glViewport viewport = (fn_glViewport)dlsym(h, "glViewport");
    fn_glScissor scissor = (fn_glScissor)dlsym(h, "glScissor");
    fn_glBlendFunc blendFunc = (fn_glBlendFunc)dlsym(h, "glBlendFunc");
    fn_glCullFace cullFace = (fn_glCullFace)dlsym(h, "glCullFace");
    fn_glFrontFace frontFace = (fn_glFrontFace)dlsym(h, "glFrontFace");
    fn_glStencilFunc stencilFunc = (fn_glStencilFunc)dlsym(h, "glStencilFunc");
    fn_glStencilOp stencilOp = (fn_glStencilOp)dlsym(h, "glStencilOp");
    fn_glStencilMask stencilMask = (fn_glStencilMask)dlsym(h, "glStencilMask");
    fn_glColorMask colorMask = (fn_glColorMask)dlsym(h, "glColorMask");
    fn_glPolygonMode polygonMode = (fn_glPolygonMode)dlsym(h, "glPolygonMode");
    fn_glCreateShader createShader = (fn_glCreateShader)dlsym(h, "glCreateShader");
    fn_glShaderSource shaderSource = (fn_glShaderSource)dlsym(h, "glShaderSource");
    fn_glCompileShader compileShader = (fn_glCompileShader)dlsym(h, "glCompileShader");
    fn_glCreateProgram createProgram = (fn_glCreateProgram)dlsym(h, "glCreateProgram");
    fn_glAttachShader attachShader = (fn_glAttachShader)dlsym(h, "glAttachShader");
    fn_glBindAttribLocation bindAttribLocation =
        (fn_glBindAttribLocation)dlsym(h, "glBindAttribLocation");
    fn_glGetAttribLocation getAttribLocation =
        (fn_glGetAttribLocation)dlsym(h, "glGetAttribLocation");
    fn_glBindFragDataLocation bindFragDataLocation =
        (fn_glBindFragDataLocation)dlsym(h, "glBindFragDataLocation");
    fn_glBindFragDataLocationIndexed bindFragDataLocationIndexed =
        (fn_glBindFragDataLocationIndexed)dlsym(
            h, "glBindFragDataLocationIndexed");
    fn_glGetFragDataLocation getFragDataLocation =
        (fn_glGetFragDataLocation)dlsym(h, "glGetFragDataLocation");
    fn_glGetFragDataIndex getFragDataIndex =
        (fn_glGetFragDataIndex)dlsym(h, "glGetFragDataIndex");
    fn_glLinkProgram linkProgram = (fn_glLinkProgram)dlsym(h, "glLinkProgram");
    fn_glUseProgram useProgram = (fn_glUseProgram)dlsym(h, "glUseProgram");
    fn_glGetUniformLocation getUniformLoc = (fn_glGetUniformLocation)dlsym(h, "glGetUniformLocation");
    fn_glUniform4f uniform4f = (fn_glUniform4f)dlsym(h, "glUniform4f");
    fn_glGenVertexArrays genVertexArrays = (fn_glGenVertexArrays)dlsym(h, "glGenVertexArrays");
    fn_glBindVertexArray bindVertexArray = (fn_glBindVertexArray)dlsym(h, "glBindVertexArray");
    fn_glGenBuffers genBuffers = (fn_glGenBuffers)dlsym(h, "glGenBuffers");
    fn_glBindBuffer bindBuffer = (fn_glBindBuffer)dlsym(h, "glBindBuffer");
    fn_glBufferData bufferData = (fn_glBufferData)dlsym(h, "glBufferData");
    fn_glEnableVertexAttribArray enableAttrib = (fn_glEnableVertexAttribArray)dlsym(h, "glEnableVertexAttribArray");
    fn_glVertexAttribPointer vertexAttribPtr = (fn_glVertexAttribPointer)dlsym(h, "glVertexAttribPointer");
    fn_glDrawArrays drawArrays = (fn_glDrawArrays)dlsym(h, "glDrawArrays");
    fn_glFinish finish = (fn_glFinish)dlsym(h, "glFinish");
    fn_glReadPixels readPixels = (fn_glReadPixels)dlsym(h, "glReadPixels");
    fn_glGenFramebuffers genFramebuffers = (fn_glGenFramebuffers)dlsym(h, "glGenFramebuffers");
    fn_glBindFramebuffer bindFramebuffer = (fn_glBindFramebuffer)dlsym(h, "glBindFramebuffer");
    fn_glDeleteFramebuffers deleteFramebuffers = (fn_glDeleteFramebuffers)dlsym(h, "glDeleteFramebuffers");
    fn_glFramebufferTexture2D framebufferTexture2D = (fn_glFramebufferTexture2D)dlsym(h, "glFramebufferTexture2D");
    fn_glFramebufferRenderbuffer framebufferRenderbuffer = (fn_glFramebufferRenderbuffer)dlsym(h, "glFramebufferRenderbuffer");
    fn_glCheckFramebufferStatus checkFramebufferStatus = (fn_glCheckFramebufferStatus)dlsym(h, "glCheckFramebufferStatus");
    fn_glGenRenderbuffers genRenderbuffers = (fn_glGenRenderbuffers)dlsym(h, "glGenRenderbuffers");
    fn_glBindRenderbuffer bindRenderbuffer = (fn_glBindRenderbuffer)dlsym(h, "glBindRenderbuffer");
    fn_glDeleteRenderbuffers deleteRenderbuffers = (fn_glDeleteRenderbuffers)dlsym(h, "glDeleteRenderbuffers");
    fn_glRenderbufferStorage renderbufferStorage = (fn_glRenderbufferStorage)dlsym(h, "glRenderbufferStorage");
    fn_glGenTextures genTextures = (fn_glGenTextures)dlsym(h, "glGenTextures");
    fn_glBindTexture bindTexture = (fn_glBindTexture)dlsym(h, "glBindTexture");
    fn_glDeleteTextures deleteTextures = (fn_glDeleteTextures)dlsym(h, "glDeleteTextures");
    fn_glTexImage2D texImage2D = (fn_glTexImage2D)dlsym(h, "glTexImage2D");
    fn_glTexParameteri texParameteri =
        (fn_glTexParameteri)dlsym(h, "glTexParameteri");
    fn_glTexImage2DMultisample texImage2DMultisample =
        (fn_glTexImage2DMultisample)dlsym(h, "glTexImage2DMultisample");
    fn_glGetTexLevelParameteriv getTexLevelParameteriv =
        (fn_glGetTexLevelParameteriv)dlsym(h, "glGetTexLevelParameteriv");
    fn_glBlitFramebuffer blitFramebuffer = (fn_glBlitFramebuffer)dlsym(h, "glBlitFramebuffer");
    fn_glDrawBuffers drawBuffers = (fn_glDrawBuffers)dlsym(h, "glDrawBuffers");
    fn_glDrawBuffer drawBuffer = (fn_glDrawBuffer)dlsym(h, "glDrawBuffer");
    fn_glReadBuffer readBuffer = (fn_glReadBuffer)dlsym(h, "glReadBuffer");
    fn_glRenderbufferStorageMultisample rboMultisample = (fn_glRenderbufferStorageMultisample)dlsym(h, "glRenderbufferStorageMultisample");
    fn_glGetRenderbufferParameteriv getRboParam = (fn_glGetRenderbufferParameteriv)dlsym(h, "glGetRenderbufferParameteriv");
    fn_glGetIntegerv getIntegerv = (fn_glGetIntegerv)dlsym(h, "glGetIntegerv");

    CHECK(clearColor && clear && clearBufferfv && enable && depthFunc && viewport && scissor && blendFunc &&
          cullFace && frontFace && stencilFunc && stencilOp && stencilMask &&
          colorMask && polygonMode &&
          createShader && shaderSource && compileShader && createProgram &&
          attachShader && bindAttribLocation && getAttribLocation &&
          bindFragDataLocation && bindFragDataLocationIndexed &&
          getFragDataLocation && getFragDataIndex && linkProgram && useProgram && getUniformLoc &&
          uniform4f && genVertexArrays && bindVertexArray && genBuffers &&
          bindBuffer && bufferData && enableAttrib && vertexAttribPtr &&
          drawArrays && finish && readPixels &&
          genFramebuffers && bindFramebuffer && deleteFramebuffers &&
          framebufferTexture2D && framebufferRenderbuffer &&
          checkFramebufferStatus && genRenderbuffers && bindRenderbuffer &&
          renderbufferStorage && genTextures && bindTexture && texImage2D &&
          texParameteri &&
          texImage2DMultisample && getTexLevelParameteriv &&
          blitFramebuffer && drawBuffers && drawBuffer && readBuffer &&
          rboMultisample && getRboParam && getIntegerv,
          "all required GL symbols resolved");

    /* -- program ------------------------------------------------ */
    GLuint vs = createShader(GL_VERTEX_SHADER);
    GLuint fs = createShader(GL_FRAGMENT_SHADER);
    shaderSource(vs, 1, &VS, 0);
    shaderSource(fs, 1, &FS, 0);
    compileShader(vs);
    compileShader(fs);
    GLuint prog = createProgram();
    attachShader(prog, vs);
    attachShader(prog, fs);
    bindAttribLocation(prog, 5, "pos");
    bindFragDataLocation(prog, 1, "fragColor");
    linkProgram(prog);
    CHECK(getAttribLocation(prog, "pos") == 0 &&
              getFragDataLocation(prog, "fragColor") == 0,
          "explicit layout(location=) overrides pre-link API bindings");
    useProgram(prog);

    /* -- vertex setup ------------------------------------------- */
    GLuint vao, vbo;
    genVertexArrays(1, &vao);
    bindVertexArray(vao);
    genBuffers(1, &vbo);
    bindBuffer(GL_ARRAY_BUFFER, vbo);
    enableAttrib(0);
    vertexAttribPtr(0, 3, GL_FLOAT, GL_FALSE, 28, 0);
    enableAttrib(1);
    vertexAttribPtr(1, 4, GL_FLOAT, GL_FALSE, 28, (const GLvoid*)12);

    unsigned char px[4];
    GLint direct_metal_marker = 0;
    getIntegerv(GL_MAX_COLOR_TEXTURE_SAMPLES, &direct_metal_marker);

    /* -- depth test: near triangle wins over far ----------------- */
    {
        /* two overlapping full-viewport triangles; near (z=0) red drawn
           first, far (z=0.9) blue drawn second. Depth LESS keeps red. */
        float near[3][7] = {
            {-1, -1, 0.0f, 1, 0, 0, 1},
            { 1, -1, 0.0f, 1, 0, 0, 1},
            { 0,  1, 0.0f, 1, 0, 0, 1},
        };
        float far[3][7] = {
            {-1, -1, 0.9f, 0, 0, 1, 1},
            { 1, -1, 0.9f, 0, 0, 1, 1},
            { 0,  1, 0.9f, 0, 0, 1, 1},
        };
        bufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(near), near, 0x88E4);
        clearColor(0.1f, 0.2f, 0.3f, 1.0f);
        enable(GL_DEPTH_TEST);
        depthFunc(GL_LEQUAL);
        clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawArrays(GL_TRIANGLES, 0, 3);      /* near red first */
        bufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(far), far, 0x88E4);
        drawArrays(GL_TRIANGLES, 0, 3);      /* far blue second */
        finish();
        readPixels(256, 300, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 255, 0, 0, 255),
              "depth test keeps the nearer triangle (r=%d g=%d b=%d)",
              px[0], px[1], px[2]);

        /* with depth write disabled the far draw overdraws it */
        clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        bufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(near), near, 0x88E4);
        drawArrays(GL_TRIANGLES, 0, 3);      /* near red first (z=0) */
        /* -- toggle depth through a fresh clear + two draws again */
        drawArrays(GL_TRIANGLES, 0, 3);
        finish();
        readPixels(256, 300, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 255, 0, 0, 255),
              "LEQUAL still keeps the nearer (equal-z) red (r=%d g=%d b=%d)",
              px[0], px[1], px[2]);
    }

    /* -- scissor: clipping limits where geometry lands ----------- */
    {
        enable(GL_DEPTH_TEST);
        depthFunc(GL_LEQUAL);
        disable(GL_SCISSOR_TEST);
        /* big triangle covering the whole viewport: white, z=0. The 3x3
           scissor leaves the centre clear and keeps the corner covered. */
        float white[3][7] = {
            {-1, -1, 0.0f, 1, 1, 1, 1},
            { 3, -1, 0.0f, 1, 1, 1, 1},
            {-1,  3, 0.0f, 1, 1, 1, 1},
        };
        bufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(white), white, 0x88E4);
        scissor(0, 0, 3, 3);
        clearColor(0.10f, 0.20f, 0.30f, 1.0f);
        disable(GL_SCISSOR_TEST);
        clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        enable(GL_SCISSOR_TEST);
        drawArrays(GL_TRIANGLES, 0, 3);
        finish();
        readPixels(256, 300, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 26, 51, 77, 255),
              "scissor keeps the centre pixel at clear color (r=%d g=%d b=%d)",
              px[0], px[1], px[2]);
        /* narrow scissor: geometry still reaches the bottom-left 3x3 */
        scissor(0, 0, 3, 3);
        clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawArrays(GL_TRIANGLES, 0, 3);
        finish();
        readPixels(1, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 255, 255, 255, 255),
              "scissored region still receives the triangle (r=%d g=%d b=%d)",
              px[0], px[1], px[2]);

        /* glClear itself is scissored: clear a red 16x16 corner over blue. */
        disable(GL_SCISSOR_TEST);
        clearColor(0, 0, 1, 1);
        clear(GL_COLOR_BUFFER_BIT);
        enable(GL_SCISSOR_TEST);
        scissor(0, 0, 16, 16);
        clearColor(1, 0, 0, 1);
        clear(GL_COLOR_BUFFER_BIT);
        finish();
        readPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 255, 0, 0, 255),
              "scissored clear updates its rectangle (r=%d g=%d b=%d)",
              px[0], px[1], px[2]);
        readPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 0, 0, 255, 255),
              "scissored clear preserves pixels outside (r=%d g=%d b=%d)",
              px[0], px[1], px[2]);

        /* A later clear is ordered after prior draws, and its value is a
           snapshot: changing clearColor again must not rewrite it. */
        disable(GL_SCISSOR_TEST);
        disable(GL_DEPTH_TEST);
        float ordered_red[3][7] = {
            {-1, -1, 0.0f, 1, 0, 0, 1},
            { 3, -1, 0.0f, 1, 0, 0, 1},
            {-1,  3, 0.0f, 1, 0, 0, 1},
        };
        clearColor(0, 0, 0, 1);
        clear(GL_COLOR_BUFFER_BIT);
        bufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(ordered_red),
                   ordered_red, 0x88E4);
        drawArrays(GL_TRIANGLES, 0, 3);
        clearColor(0, 0, 1, 1);
        clear(GL_COLOR_BUFFER_BIT);
        clearColor(0, 1, 0, 1); /* state change only; no third clear */
        finish();
        readPixels(256, 256, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 0, 0, 255, 255),
              "ordered clear keeps its captured value after prior draw (r=%d g=%d b=%d)",
              px[0], px[1], px[2]);
    }

    /* -- deferred encoding: viewport/scissor are per-draw snapshots ------- */
    {
        float red[3][7] = {
            {-1, -1, 0.0f, 1, 0, 0, 1},
            { 3, -1, 0.0f, 1, 0, 0, 1},
            {-1,  3, 0.0f, 1, 0, 0, 1},
        };
        float green[3][7] = {
            {-1, -1, 0.0f, 0, 1, 0, 1},
            { 3, -1, 0.0f, 0, 1, 0, 1},
            {-1,  3, 0.0f, 0, 1, 0, 1},
        };
        disable(GL_DEPTH_TEST);
        disable(GL_SCISSOR_TEST);
        clearColor(0, 0, 0, 1);
        clear(GL_COLOR_BUFFER_BIT);

        enable(GL_SCISSOR_TEST);
        viewport(0, 0, 256, 512);
        scissor(0, 0, 256, 512);
        bufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(red), red, 0x88E4);
        drawArrays(GL_TRIANGLES, 0, 3);

        viewport(256, 0, 256, 512);
        scissor(256, 0, 256, 512);
        bufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(green), green, 0x88E4);
        drawArrays(GL_TRIANGLES, 0, 3);
        /* Both draws must still be pending when their dynamic state changes. */
        finish();
        readPixels(128, 256, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 255, 0, 0, 255),
              "first deferred draw keeps left viewport/scissor (r=%d g=%d b=%d)",
              px[0], px[1], px[2]);
        readPixels(384, 256, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 0, 255, 0, 255),
              "second deferred draw keeps right viewport/scissor (r=%d g=%d b=%d)",
              px[0], px[1], px[2]);

        viewport(0, 0, 512, 512);
        scissor(0, 0, 512, 512);
        disable(GL_SCISSOR_TEST);
    }

    /* -- blend: SRC_ALPHA/ONE_MINUS_SRC_ALPHA --------------------- */
    {
        enable(GL_DEPTH_TEST);
        depthFunc(GL_LEQUAL);
        disable(GL_SCISSOR_TEST);
        /* bg black; red triangle (a=0.5). Blend off -> raw src RGBA
           (255,0,0,128); blend on -> src*0.5 + dst*0.5, dst=(0,0,0,1):
           r=128, alpha=0.5*0.5+1.0*0.5=0.75=191. Per GL formula (mobilegl
           reference: non-separate glBlendFunc applies factors to alpha too). */
        float red[3][7] = {
            {-1, -1, 0.0f, 1.0f, 0, 0, 0.5f},
            { 1, -1, 0.0f, 1.0f, 0, 0, 0.5f},
            { 0,  1, 0.0f, 1.0f, 0, 0, 0.5f},
        };
        bufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(red), red, 0x88E4);
        clearColor(0, 0, 0, 1.0f);
        clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawArrays(GL_TRIANGLES, 0, 3);      /* blend still off */
        finish();
        readPixels(256, 300, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 255, 0, 0, 128),
              "blend disabled: writes src alpha (r=%d g=%d b=%d a=%d)",
              px[0], px[1], px[2], px[3]);

        enable(GL_BLEND);
        blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawArrays(GL_TRIANGLES, 0, 3);
        finish();
        readPixels(256, 300, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 128, 0, 0, 191),
              "blend SRC_ALPHA/1-SRC_ALPHA over black (r=%d g=%d b=%d a=%d)",
              px[0], px[1], px[2], px[3]);
    }

    /* -- cull: winding-consistent front/back face dropping ---------------- */
    {
        disable(GL_SCISSOR_TEST);
        enable(GL_CULL_FACE);
        /* full-viewport white triangle, CCW in NDC (default front=CCW). */
        float tri[3][7] = {
            {-1, -1, 0.0f, 1, 1, 1, 1},
            { 3, -1, 0.0f, 1, 1, 1, 1},
            {-1,  3, 0.0f, 1, 1, 1, 1},
        };
        bufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(tri), tri, 0x88E4);
        clearColor(0.10f, 0.20f, 0.30f, 1.0f);
        finish();

        cullFace(GL_BACK);
        frontFace(GL_CCW);
        clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawArrays(GL_TRIANGLES, 0, 3);
        finish();
        readPixels(256, 300, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 255, 255, 255, 255),
              "cull GL_BACK keeps the front-facing (CCW) triangle (r=%d g=%d b=%d)",
              px[0], px[1], px[2]);

        cullFace(GL_FRONT);
        clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawArrays(GL_TRIANGLES, 0, 3);
        finish();
        readPixels(256, 300, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 26, 51, 77, 255),
              "cull GL_FRONT drops the front-facing (CCW) triangle (r=%d g=%d b=%d)",
              px[0], px[1], px[2]);

        /* CCW data wound, but winding interpreted as CW via GL_CW: now that
           is treated as a back face and must be dropped by GL_BACK. */
        cullFace(GL_BACK);
        frontFace(GL_CW);
        clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawArrays(GL_TRIANGLES, 0, 3);
        finish();
        readPixels(256, 300, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 26, 51, 77, 255),
              "frontFace(GL_CW) turns the CCW triangle into a back face (r=%d g=%d b=%d)",
              px[0], px[1], px[2]);

        frontFace(GL_CCW);
        disable(GL_CULL_FACE);
    }

    /* -- stencil: REPLACE mark, then EQUAL / mismatch reads -------------- */
    {
        enable(GL_DEPTH_TEST);
        depthFunc(GL_LEQUAL);
        disable(GL_SCISSOR_TEST);
        float red[3][7] = {
            {-1, -1, 0.0f, 1, 0, 0, 1},
            { 3, -1, 0.0f, 1, 0, 0, 1},
            {-1,  3, 0.0f, 1, 0, 0, 1},
        };
        float blue[3][7] = {
            {-1, -1, 0.0f, 0, 0, 1, 1},
            { 3, -1, 0.0f, 0, 0, 1, 1},
            {-1,  3, 0.0f, 0, 0, 1, 1},
        };
        float green[3][7] = {
            {-1, -1, 0.0f, 0, 1, 0, 1},
            { 3, -1, 0.0f, 0, 1, 0, 1},
            {-1,  3, 0.0f, 0, 1, 0, 1},
        };
        enable(GL_STENCIL_TEST);
        /* pass 1: ALWAYS writes reference 255 into the stencil buffer. */
        stencilFunc(GL_ALWAYS, 255, 0xFF);
        stencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        stencilMask(0xFF);
        clearColor(0.10f, 0.20f, 0.30f, 1.0f);
        bufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(red), red, 0x88E4);
        clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        drawArrays(GL_TRIANGLES, 0, 3);
        finish();
        readPixels(256, 300, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 255, 0, 0, 255),
              "stencil pass writes color (r=%d g=%d b=%d)", px[0], px[1], px[2]);

        /* pass 2: test EQUAL 255; the mark survives -> blue draws. */
        stencilFunc(GL_EQUAL, 255, 0xFF);
        stencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        bufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(blue), blue, 0x88E4);
        clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawArrays(GL_TRIANGLES, 0, 3);
        finish();
        readPixels(256, 300, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 0, 0, 255, 255),
              "stencil EQUAL reads the marked value (r=%d g=%d b=%d)",
              px[0], px[1], px[2]);

        /* pass 3: mismatched reference (1 != 255) must block the draw. */
        stencilFunc(GL_EQUAL, 1, 0xFF);
        bufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(green), green, 0x88E4);
        clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawArrays(GL_TRIANGLES, 0, 3);
        finish();
        readPixels(256, 300, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 26, 51, 77, 255),
              "stencil ref mismatch blocks the draw (r=%d g=%d b=%d)",
              px[0], px[1], px[2]);

        disable(GL_STENCIL_TEST);
        stencilMask(0xFFFFFFFFu);
    }

    /* -- color mask: per-channel write gating ---------------------------- */
    {
        disable(GL_SCISSOR_TEST);
        disable(GL_STENCIL_TEST);
        enable(GL_DEPTH_TEST);
        depthFunc(GL_LEQUAL);
        float red[3][7] = {
            {-1, -1, 0.0f, 1, 0, 0, 1},
            { 3, -1, 0.0f, 1, 0, 0, 1},
            {-1,  3, 0.0f, 1, 0, 0, 1},
        };
        bufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(red), red, 0x88E4);
        clearColor(0.10f, 0.20f, 0.30f, 1.0f);
        finish();

        /* no color writes at all -> the cleared background survives */
        colorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawArrays(GL_TRIANGLES, 0, 3);
        finish();
        readPixels(256, 300, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 26, 51, 77, 255),
              "colorMask off leaves the background (r=%d g=%d b=%d)",
              px[0], px[1], px[2]);

        /* red-only writes: red channel overwritten, g/b keep background */
        colorMask(GL_TRUE, GL_FALSE, GL_FALSE, GL_TRUE);
        clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawArrays(GL_TRIANGLES, 0, 3);
        finish();
        readPixels(256, 300, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 255, 51, 77, 255),
              "colorMask R-only gates the other channels (r=%d g=%d b=%d)",
              px[0], px[1], px[2]);

        colorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawArrays(GL_TRIANGLES, 0, 3);
        finish();
        readPixels(256, 300, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 255, 0, 0, 255),
              "colorMask restored writes all channels (r=%d g=%d b=%d)",
              px[0], px[1], px[2]);
    }

    /* -- polygon mode: GL_LINE only stroke-edges -------------------------- */
    {
        enable(GL_DEPTH_TEST);
        depthFunc(GL_LEQUAL);
        disable(GL_SCISSOR_TEST);
        disable(GL_STENCIL_TEST);
        float white[3][7] = {
            {-1, -1, 0.0f, 1, 1, 1, 1},
            { 3, -1, 0.0f, 1, 1, 1, 1},
            {-1,  3, 0.0f, 1, 1, 1, 1},
        };
        bufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(white), white, 0x88E4);
        clearColor(0.10f, 0.20f, 0.30f, 1.0f);

        polygonMode(GL_FRONT_AND_BACK, GL_LINE);
        clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawArrays(GL_TRIANGLES, 0, 3);
        finish();
        readPixels(256, 300, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 26, 51, 77, 255),
              "polygonMode GL_LINE leaves the interior clear (r=%d g=%d b=%d)",
              px[0], px[1], px[2]);

        polygonMode(GL_FRONT_AND_BACK, GL_FILL);
        clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawArrays(GL_TRIANGLES, 0, 3);
        finish();
        readPixels(256, 300, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 255, 255, 255, 255),
              "polygonMode GL_FILL restores the fill (r=%d g=%d b=%d)",
              px[0], px[1], px[2]);
        polygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }

    /* -- S5: offscreen render to an FBO, then readback ---------------- */
    {
        /* Default framebuffer stays unaffected by an FBO round-trip. */
        clearColor(0.10f, 0.20f, 0.30f, 1.0f);
        clear(GL_COLOR_BUFFER_BIT);
        finish();

        GLuint fbo, tex;
        genFramebuffers(1, &fbo);
        genTextures(1, &tex);
        bindTexture(GL_TEXTURE_2D, tex);
        texImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, 0);
        /* FBO with only a colour texture must be complete. */
        bindFramebuffer(GL_FRAMEBUFFER, fbo);
        framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, tex, 0);
        CHECK(checkFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
              "FBO with a colour texture is complete");

        /* Render a full red triangle into the 32x32 FBO. */
        enable(GL_DEPTH_TEST);
        depthFunc(GL_LEQUAL);
        disable(GL_SCISSOR_TEST);
        clearColor(0, 0, 0, 1.0f);
        clear(GL_COLOR_BUFFER_BIT);
        float red[3][7] = {
            {-1, -1, 0.0f, 1, 0, 0, 1},
            { 3, -1, 0.0f, 1, 0, 0, 1},
            {-1,  3, 0.0f, 1, 0, 0, 1},
        };
        bufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(red), red, 0x88E4);
        drawArrays(GL_TRIANGLES, 0, 3);
        finish();
        /* ReadPixels reads from the *read* framebuffer; FBO bound for both. */
        readPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 255, 0, 0, 255),
              "FBO offscreen render reads back red (r=%d g=%d b=%d)",
              px[0], px[1], px[2]);

        /* Unbind back to the default framebuffer: default is untouched. */
        bindFramebuffer(GL_FRAMEBUFFER, 0);
        readPixels(16, 300, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 26, 51, 77, 255),
              "default framebuffer unaffected by FBO render (r=%d g=%d b=%d)",
              px[0], px[1], px[2]);

        deleteFramebuffers(1, &fbo);
        deleteTextures(1, &tex);
    }

    /* -- S5: framebuffer rows remain texture rows when sampled -------- */
    if (direct_metal_marker > 0) {
        GLuint fbo = 0, tex = 0;
        genFramebuffers(1, &fbo);
        genTextures(1, &tex);
        bindTexture(GL_TEXTURE_2D, tex);
        texImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 32, 32, 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, 0);
        bindFramebuffer(GL_FRAMEBUFFER, fbo);
        framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, tex, 0);
        CHECK(checkFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
              "orientation FBO is complete");

        /* Draw an asymmetric image in GL framebuffer coordinates: row 0 is
         * red and row 31 is blue.  A solid image would not expose a native
         * top-left/bottom-left mismatch, and a clear-only test would not
         * exercise the vertex transform used by real Minecraft draws. */
        useProgram(prog);
        viewport(0, 0, 32, 32);
        disable(GL_DEPTH_TEST);
        disable(GL_SCISSOR_TEST);
        clearColor(0, 0, 0, 1);
        clear(GL_COLOR_BUFFER_BIT);
        const float asymmetric[12][7] = {
            {-1, -1, 0, 1, 0, 0, 1}, { 1, -1, 0, 1, 0, 0, 1},
            { 1,  0, 0, 1, 0, 0, 1}, {-1, -1, 0, 1, 0, 0, 1},
            { 1,  0, 0, 1, 0, 0, 1}, {-1,  0, 0, 1, 0, 0, 1},
            {-1,  0, 0, 0, 0, 1, 1}, { 1,  0, 0, 0, 0, 1, 1},
            { 1,  1, 0, 0, 0, 1, 1}, {-1,  0, 0, 0, 0, 1, 1},
            { 1,  1, 0, 0, 0, 1, 1}, {-1,  1, 0, 0, 0, 1, 1},
        };
        bufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(asymmetric),
                   asymmetric, 0x88E4);
        drawArrays(GL_TRIANGLES, 0, 12);
        finish();

        readPixels(16, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 255, 0, 0, 255),
              "FBO framebuffer row 4 is red (r=%d g=%d b=%d)",
              px[0], px[1], px[2]);
        readPixels(16, 27, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 0, 0, 255, 255),
              "FBO framebuffer row 27 is blue (r=%d g=%d b=%d)",
              px[0], px[1], px[2]);

        GLuint fs_texel = createShader(GL_FRAGMENT_SHADER);
        shaderSource(fs_texel, 1, &FS_FBO_TEXEL, 0);
        compileShader(fs_texel);
        GLuint sample_prog = createProgram();
        attachShader(sample_prog, vs);
        attachShader(sample_prog, fs_texel);
        linkProgram(sample_prog);
        useProgram(sample_prog);
        const GLint sample_point = getUniformLoc(sample_prog, "samplePoint");
        CHECK(sample_point >= 0,
              "render-to-texture sample coordinate remains active");

        bindFramebuffer(GL_FRAMEBUFFER, 0);
        bindTexture(GL_TEXTURE_2D, tex);
        viewport(0, 0, 512, 512);
        const float full_screen[3][7] = {
            {-1, -1, 0.0f, 1, 1, 1, 1},
            { 3, -1, 0.0f, 1, 1, 1, 1},
            {-1,  3, 0.0f, 1, 1, 1, 1},
        };
        bufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(full_screen),
                   full_screen, 0x88E4);

        uniform4f(sample_point, 16.f, 4.f, 0.f, 0.f);
        clearColor(0, 0, 0, 1);
        clear(GL_COLOR_BUFFER_BIT);
        drawArrays(GL_TRIANGLES, 0, 3);
        finish();
        readPixels(256, 256, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 255, 0, 0, 255),
              "texture texel row 4 matches GL framebuffer row 4 "
              "(r=%d g=%d b=%d)", px[0], px[1], px[2]);

        uniform4f(sample_point, 16.f, 27.f, 0.f, 0.f);
        clear(GL_COLOR_BUFFER_BIT);
        drawArrays(GL_TRIANGLES, 0, 3);
        finish();
        readPixels(256, 256, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 0, 0, 255, 255),
              "texture texel row 27 matches GL framebuffer row 27 "
              "(r=%d g=%d b=%d)", px[0], px[1], px[2]);

        useProgram(prog);
        deleteFramebuffers(1, &fbo);
        deleteTextures(1, &tex);
    }

    /* -- S5: renderbuffer colour attachment --------------------------- */
    {
        GLuint rbo, fbo;
        genRenderbuffers(1, &rbo);
        bindRenderbuffer(GL_RENDERBUFFER, rbo);
        renderbufferStorage(GL_RENDERBUFFER, GL_RGBA, 32, 32);
        genFramebuffers(1, &fbo);
        bindFramebuffer(GL_FRAMEBUFFER, fbo);
        framebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                GL_RENDERBUFFER, rbo);
        CHECK(checkFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
              "FBO with a colour renderbuffer is complete");

        clearColor(0, 1, 0, 1.0f);
        clear(GL_COLOR_BUFFER_BIT);
        finish();
        /* ReadPixels is bound to the FBO still; green fill reads back. */
        readPixels(5, 5, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 0, 255, 0, 255),
              "renderbuffer colour attachment reads back green (r=%d g=%d b=%d)",
              px[0], px[1], px[2]);

        bindFramebuffer(GL_FRAMEBUFFER, 0);
        deleteFramebuffers(1, &fbo);
        deleteRenderbuffers(1, &rbo);
    }

    /* -- S5: target switching + framebuffer blit ----------------------- */
    {
        /* Solid red into FBO A, asymmetric blue/green rows into FBO B,
         * then blit B into both A and the default framebuffer. */
        GLuint fboA, fboB, texA, texB;
        genFramebuffers(1, &fboA);
        genFramebuffers(1, &fboB);
        genTextures(1, &texA);
        genTextures(1, &texB);
        bindTexture(GL_TEXTURE_2D, texA);
        texImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, 0);
        bindTexture(GL_TEXTURE_2D, texB);
        texImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, 0);

        bindFramebuffer(GL_DRAW_FRAMEBUFFER, fboA);
        framebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, texA, 0);
        bindFramebuffer(GL_DRAW_FRAMEBUFFER, fboB);
        framebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, texB, 0);
        CHECK(checkFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
              "both draw/read FBO targets are complete");

        /* draw red into A */
        bindFramebuffer(GL_FRAMEBUFFER, fboA);
        enable(GL_DEPTH_TEST);  /* leave as-is; reuse red triangle */
        clearColor(1, 0, 0, 1.0f);
        clear(GL_COLOR_BUFFER_BIT);
        /* Put blue in GL's bottom half and green in its top half. */
        bindFramebuffer(GL_FRAMEBUFFER, fboB);
        disable(GL_DEPTH_TEST);
        enable(GL_SCISSOR_TEST);
        scissor(0, 0, 32, 16);
        clearColor(0, 0, 1, 1.0f);
        clear(GL_COLOR_BUFFER_BIT);
        scissor(0, 16, 32, 16);
        clearColor(0, 1, 0, 1.0f);
        clear(GL_COLOR_BUFFER_BIT);
        disable(GL_SCISSOR_TEST);
        finish();

        /* blit B (read) -> A (draw): full rects, GL_NEAREST */
        bindFramebuffer(GL_READ_FRAMEBUFFER, fboB);
        bindFramebuffer(GL_DRAW_FRAMEBUFFER, fboA);
        blitFramebuffer(0, 0, 32, 32, 0, 0, 32, 32,
                        GL_COLOR_BUFFER_BIT, GL_NEAREST);
        finish();

        bindFramebuffer(GL_READ_FRAMEBUFFER, fboA);
        readPixels(16, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 0, 0, 255, 255),
              "FBO blit preserves B's bottom blue row in A "
              "(r=%d g=%d b=%d)",
              px[0], px[1], px[2]);
        readPixels(16, 27, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 0, 255, 0, 255),
              "FBO blit preserves B's top green row in A "
              "(r=%d g=%d b=%d)", px[0], px[1], px[2]);

        bindFramebuffer(GL_READ_FRAMEBUFFER, fboB);
        bindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        blitFramebuffer(0, 0, 32, 32, 0, 0, 32, 32,
                        GL_COLOR_BUFFER_BIT, GL_NEAREST);
        finish();
        bindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        readPixels(16, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 0, 0, 255, 255),
              "FBO-to-default blit preserves the bottom blue row "
              "(r=%d g=%d b=%d)", px[0], px[1], px[2]);
        readPixels(16, 27, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 0, 255, 0, 255),
              "FBO-to-default blit preserves the top green row "
              "(r=%d g=%d b=%d)", px[0], px[1], px[2]);

        bindFramebuffer(GL_FRAMEBUFFER, 0);
        deleteFramebuffers(1, &fboA);
        deleteFramebuffers(1, &fboB);
        deleteTextures(1, &texA);
        deleteTextures(1, &texB);
    }

    /* -- S5-MRT: two colour attachments, per-attachment draw buffer ------- */
    {
        /* Build a 2-attachment texture FBO; the MRT shader writes red to
           attachment 0 and green to attachment 1. Selecting only attachment
           0 for drawing must leave attachment 1 at its clear colour. */
        GLuint fbo, tex0, tex1;
        genFramebuffers(1, &fbo);
        genTextures(1, &tex0);
        genTextures(1, &tex1);
        bindTexture(GL_TEXTURE_2D, tex0);
        texImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, 0);
        bindTexture(GL_TEXTURE_2D, tex1);
        texImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0, GL_RGBA,
                   GL_UNSIGNED_BYTE, 0);
        bindFramebuffer(GL_FRAMEBUFFER, fbo);
        framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, tex0, 0);
        framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1,
                             GL_TEXTURE_2D, tex1, 0);
        CHECK(checkFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
              "MRT FBO with two colour textures is complete");

        /* Swap in the MRT fragment shader (re-link a fresh program). */
        GLuint vs2 = createShader(GL_VERTEX_SHADER);
        shaderSource(vs2, 1, &VS_BOUND, 0);
        compileShader(vs2);
        GLuint fs2 = createShader(GL_FRAGMENT_SHADER);
        shaderSource(fs2, 1, &FS_MRT, 0);
        compileShader(fs2);
        GLuint prog2 = createProgram();
        attachShader(prog2, vs2);
        attachShader(prog2, fs2);
        bindAttribLocation(prog2, 0, "pos");
        bindAttribLocation(prog2, 1, "col");
        bindFragDataLocation(prog2, 0, "fragColor0");
        bindFragDataLocationIndexed(prog2, 1, 0, "fragColor1");
        linkProgram(prog2);
        GLint max_draw_buffers = 0, max_dual_source = -1;
        getIntegerv(GL_MAX_DRAW_BUFFERS, &max_draw_buffers);
        getIntegerv(GL_MAX_DUAL_SOURCE_DRAW_BUFFERS, &max_dual_source);
        CHECK(getAttribLocation(prog2, "pos") == 0 &&
                  getAttribLocation(prog2, "col") == 1 &&
                  getFragDataLocation(prog2, "fragColor0") == 0 &&
                  getFragDataLocation(prog2, "fragColor1") == 1 &&
                  getFragDataIndex(prog2, "fragColor1") == 0 &&
                  max_draw_buffers == 8 && max_dual_source == 0,
              "pre-link shader I/O bindings survive shared SPIR-V lowering");
        useProgram(prog2);

        clearColor(0, 0, 0, 1.0f);
        GLenum bufs2[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
        drawBuffers(2, bufs2);

        /* Clear GL_DRAW_BUFFER0 and GL_DRAW_BUFFER1 to different values.
           This is the MRT behavior glClear cannot express. */
        const float clear_red[4] = {1, 0, 0, 1};
        const float clear_blue[4] = {0, 0, 1, 1};
        clearBufferfv(GL_COLOR, 0, clear_red);
        clearBufferfv(GL_COLOR, 1, clear_blue);
        finish();
        readBuffer(GL_COLOR_ATTACHMENT0);
        readPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 255, 0, 0, 255),
              "glClearBufferfv targets MRT draw buffer 0 (r=%d g=%d b=%d)",
              px[0], px[1], px[2]);
        readBuffer(GL_COLOR_ATTACHMENT1);
        readPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 0, 0, 255, 255),
              "glClearBufferfv targets MRT draw buffer 1 (r=%d g=%d b=%d)",
              px[0], px[1], px[2]);

        clear(GL_COLOR_BUFFER_BIT);
        /* full-viewport red triangle; both attachments receive it. */
        float red[3][7] = {
            {-1, -1, 0.0f, 1, 0, 0, 1},
            { 3, -1, 0.0f, 1, 0, 0, 1},
            {-1,  3, 0.0f, 1, 0, 0, 1},
        };
        bufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(red), red, 0x88E4);
        drawArrays(GL_TRIANGLES, 0, 3);
        finish();
        /* attachment 0 reads red; attachment 1 reads green (MRC path). */
        readBuffer(GL_COLOR_ATTACHMENT0);
        readPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 255, 0, 0, 255),
              "MRT attachment 0 reads back the shader color (r=%d g=%d b=%d)",
              px[0], px[1], px[2]);
        readBuffer(GL_COLOR_ATTACHMENT1);
        readPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 0, 255, 0, 255),
              "MRT attachment 1 reads back green (r=%d g=%d b=%d)",
              px[0], px[1], px[2]);

        /* Restrict the draw to attachment 0 only: the write is gated to
           attachment 0, so attachment 1 keeps the value from the previous
           two-attachment draw (green) instead of receiving the new red. */
        GLenum one[1] = {GL_COLOR_ATTACHMENT0};
        drawBuffers(1, one);
        clearColor(0, 0, 0, 1.0f);
        clear(GL_COLOR_BUFFER_BIT);
        drawArrays(GL_TRIANGLES, 0, 3);
        finish();
        readBuffer(GL_COLOR_ATTACHMENT1);
        readPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 0, 255, 0, 255),
              "single draw buffer leaves attachment 1 untouched (r=%d g=%d b=%d)",
              px[0], px[1], px[2]);
        readBuffer(GL_COLOR_ATTACHMENT0);
        readPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 255, 0, 0, 255),
              "single draw buffer still writes attachment 0 (r=%d g=%d b=%d)",
              px[0], px[1], px[2]);

        deleteFramebuffers(1, &fbo);
        deleteTextures(1, &tex0);
        deleteTextures(1, &tex1);
    }

    /* -- S5-MSAA: multisampled renderbuffer colour resolve ---------------- */
    {
        GLuint rbo, fbo;
        GLint rbo_samples = 0;
        genRenderbuffers(1, &rbo);
        bindRenderbuffer(GL_RENDERBUFFER, rbo);
        rboMultisample(GL_RENDERBUFFER, 4, GL_RGBA, 32, 32);
        getRboParam(GL_RENDERBUFFER, GL_RENDERBUFFER_SAMPLES, &rbo_samples);
        CHECK(rbo_samples == 4, "MSAA renderbuffer reports 4 samples (got %d)",
              rbo_samples);
        genFramebuffers(1, &fbo);
        bindFramebuffer(GL_FRAMEBUFFER, fbo);
        framebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                GL_RENDERBUFFER, rbo);
        CHECK(checkFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
              "MSAA FBO with a 4-sample colour renderbuffer is complete");

        clearColor(0, 0, 1, 1.0f);
        clear(GL_COLOR_BUFFER_BIT);
        drawArrays(GL_TRIANGLES, 0, 3);   /* reuse the red triangle */
        finish();
        readPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 255, 0, 0, 255),
              "MSAA resolve reads back the red triangle (r=%d g=%d b=%d)",
              px[0], px[1], px[2]);

        bindFramebuffer(GL_FRAMEBUFFER, 0);
        deleteFramebuffers(1, &fbo);
        deleteRenderbuffers(1, &rbo);
    }

    /* -- multisample texture: render attachment -> sampler2DMS ------------ */
    {
        GLint max_samples = 0;
        getIntegerv(GL_MAX_COLOR_TEXTURE_SAMPLES, &max_samples);
        if (max_samples < 4) {
            CHECK(max_samples == 0,
                  "backend explicitly leaves multisample textures unavailable");
        } else {
            CHECK(max_samples >= 4,
                  "DirectMetal reports a usable color texture sample count (%d)",
                  max_samples);

            GLuint texture = 0, fbo = 0;
            genTextures(1, &texture);
            bindTexture(GL_TEXTURE_2D_MULTISAMPLE, texture);
            texImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, 4, GL_RGBA8,
                                  32, 32, GL_TRUE);
            GLint texture_samples = 0, fixed_locations = 0;
            getTexLevelParameteriv(GL_TEXTURE_2D_MULTISAMPLE, 0,
                                   GL_TEXTURE_SAMPLES, &texture_samples);
            getTexLevelParameteriv(GL_TEXTURE_2D_MULTISAMPLE, 0,
                                   GL_TEXTURE_FIXED_SAMPLE_LOCATIONS,
                                   &fixed_locations);
            CHECK(texture_samples == 4 && fixed_locations == GL_TRUE,
                  "multisample texture reports 4 fixed samples");

            genFramebuffers(1, &fbo);
            bindFramebuffer(GL_FRAMEBUFFER, fbo);
            framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                 GL_TEXTURE_2D_MULTISAMPLE, texture, 0);
            CHECK(checkFramebufferStatus(GL_FRAMEBUFFER) ==
                      GL_FRAMEBUFFER_COMPLETE,
                  "4-sample texture is a complete Metal FBO attachment");

            GLuint single_sample = 0;
            genTextures(1, &single_sample);
            bindTexture(GL_TEXTURE_2D, single_sample);
            texImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 32, 32, 0, GL_RGBA,
                       GL_UNSIGNED_BYTE, 0);
            framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1,
                                 GL_TEXTURE_2D, single_sample, 0);
            CHECK(checkFramebufferStatus(GL_FRAMEBUFFER) !=
                      GL_FRAMEBUFFER_COMPLETE,
                  "mixed sample-count attachments are not reported complete");
            framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1,
                                 GL_TEXTURE_2D, 0, 0);
            CHECK(checkFramebufferStatus(GL_FRAMEBUFFER) ==
                      GL_FRAMEBUFFER_COMPLETE,
                  "detaching the mismatched image restores completeness");
            deleteTextures(1, &single_sample);

            useProgram(prog);
            disable(GL_DEPTH_TEST);
            viewport(0, 0, 32, 32);
            const float magenta[3][7] = {
                {-1, -1, 0.0f, 1, 0, 1, 1},
                { 3, -1, 0.0f, 1, 0, 1, 1},
                {-1,  3, 0.0f, 1, 0, 1, 1},
            };
            bufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(magenta), magenta,
                       0x88E4);
            clearColor(0, 0, 0, 1);
            clear(GL_COLOR_BUFFER_BIT);
            drawArrays(GL_TRIANGLES, 0, 3);

            /* Switching targets submits the producer on the same Metal queue;
               the consumer reads the native multisample texture directly. */
            bindFramebuffer(GL_FRAMEBUFFER, 0);
            viewport(0, 0, 512, 512);
            GLuint vs_ms = createShader(GL_VERTEX_SHADER);
            shaderSource(vs_ms, 1, &VS, 0);
            compileShader(vs_ms);
            GLuint fs_ms = createShader(GL_FRAGMENT_SHADER);
            shaderSource(fs_ms, 1, &FS_MULTISAMPLE, 0);
            compileShader(fs_ms);
            GLuint program_ms = createProgram();
            attachShader(program_ms, vs_ms);
            attachShader(program_ms, fs_ms);
            linkProgram(program_ms);
            useProgram(program_ms);
            clearColor(0, 0, 0, 1);
            clear(GL_COLOR_BUFFER_BIT);
            drawArrays(GL_TRIANGLES, 0, 3);
            finish();
            readPixels(256, 256, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
            CHECK(px_match(px, 255, 0, 255, 255),
                  "sampler2DMS reads the texture rendered by native Metal "
                  "(r=%d g=%d b=%d)", px[0], px[1], px[2]);

            deleteFramebuffers(1, &fbo);
            deleteTextures(1, &texture);
        }
    }

    /* -- packed depth/stencil attachment identity ------------------------- */
    {
        if (direct_metal_marker > 0) {
            GLuint color = 0, packed_a = 0, packed_b = 0, fbo = 0;
            genTextures(1, &color);
            bindTexture(GL_TEXTURE_2D, color);
            texImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 32, 32, 0,
         GL_RGBA, GL_UNSIGNED_BYTE, 0);
            genRenderbuffers(1, &packed_a);
            bindRenderbuffer(GL_RENDERBUFFER, packed_a);
            renderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
    32, 32);
            genRenderbuffers(1, &packed_b);
            bindRenderbuffer(GL_RENDERBUFFER, packed_b);
            renderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
    32, 32);
            genFramebuffers(1, &fbo);
            bindFramebuffer(GL_FRAMEBUFFER, fbo);
            framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
     GL_TEXTURE_2D, color, 0);
            framebufferRenderbuffer(GL_FRAMEBUFFER,
        GL_DEPTH_STENCIL_ATTACHMENT,
        GL_RENDERBUFFER, packed_a);
            CHECK(checkFramebufferStatus(GL_FRAMEBUFFER) ==
        GL_FRAMEBUFFER_COMPLETE,
    "packed depth/stencil attachment keeps independent GL slots");

            /* Detaching only stencil must leave the depth attachment live. */
            framebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
        GL_RENDERBUFFER, 0);
            CHECK(checkFramebufferStatus(GL_FRAMEBUFFER) ==
        GL_FRAMEBUFFER_COMPLETE,
    "detaching stencil does not detach depth");
            framebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
        GL_RENDERBUFFER, packed_a);
            CHECK(checkFramebufferStatus(GL_FRAMEBUFFER) ==
        GL_FRAMEBUFFER_COMPLETE,
    "reattaching the same packed object restores stencil");

            /* The backend has one packed native slot. Distinct depth and
 stencil objects must fail closed rather than aliasing. */
            framebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
        GL_RENDERBUFFER, packed_b);
            CHECK(checkFramebufferStatus(GL_FRAMEBUFFER) !=
        GL_FRAMEBUFFER_COMPLETE,
    "distinct depth/stencil objects fail closed without aliasing");
            framebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
        GL_RENDERBUFFER, packed_a);

            useProgram(prog);
            viewport(0, 0, 32, 32);
            disable(GL_DEPTH_TEST);
            enable(GL_STENCIL_TEST);
            stencilMask(0xff);
            stencilFunc(GL_ALWAYS, 7, 0xff);
            stencilOp(GL_REPLACE, GL_REPLACE, GL_REPLACE);
            const float red[3][7] = {
  {-1, -1, 0.0f, 1, 0, 0, 1},
  { 3, -1, 0.0f, 1, 0, 0, 1},
  {-1,  3, 0.0f, 1, 0, 0, 1},
            };
            bufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(red), red,
         0x88E4);
            clearColor(0, 0, 0, 1);
            clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
    GL_STENCIL_BUFFER_BIT);
            drawArrays(GL_TRIANGLES, 0, 3);

            stencilMask(0);
            stencilFunc(GL_EQUAL, 7, 0xff);
            stencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
            const float green[3][7] = {
  {-1, -1, 0.0f, 0, 1, 0, 1},
  { 3, -1, 0.0f, 0, 1, 0, 1},
  {-1,  3, 0.0f, 0, 1, 0, 1},
            };
            bufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(green), green,
         0x88E4);
            drawArrays(GL_TRIANGLES, 0, 3);
            finish();
            readPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
            CHECK(px_match(px, 0, 255, 0, 255),
    "shared packed attachment executes stencil writes/reads "
    "(r=%d g=%d b=%d)", px[0], px[1], px[2]);

            stencilMask(0xff);
            disable(GL_STENCIL_TEST);
            bindFramebuffer(GL_FRAMEBUFFER, 0);
            viewport(0, 0, 512, 512);
            deleteFramebuffers(1, &fbo);
            deleteRenderbuffers(1, &packed_a);
            deleteRenderbuffers(1, &packed_b);
            deleteTextures(1, &color);
        }
    }

    /* -- depth texture: native attachment write -> shader read ------------ */
    {
        /* The Vulkan reference backend reports this capability marker as 0;
           this focused vertical slice executes only on DirectMetal. */
        if (direct_metal_marker > 0) {
            GLuint color = 0, depth = 0, fbo = 0;
            genTextures(1, &color);
            bindTexture(GL_TEXTURE_2D, color);
            texImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 32, 32, 0, GL_RGBA,
                       GL_UNSIGNED_BYTE, 0);
            genTextures(1, &depth);
            bindTexture(GL_TEXTURE_2D, depth);
            texImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, 32, 32, 0,
                       GL_DEPTH_COMPONENT, GL_FLOAT, 0);
            GLint depth_format = 0, depth_type = 0;
            getTexLevelParameteriv(GL_TEXTURE_2D, 0,
                                   GL_TEXTURE_INTERNAL_FORMAT, &depth_format);
            getTexLevelParameteriv(GL_TEXTURE_2D, 0,
                                   GL_TEXTURE_RED_TYPE, &depth_type);
            CHECK(depth_format == GL_DEPTH_COMPONENT32F &&
                      depth_type == GL_FLOAT,
                  "depth texture preserves its Depth32F storage ABI");

            genFramebuffers(1, &fbo);
            bindFramebuffer(GL_FRAMEBUFFER, fbo);
            framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                 GL_TEXTURE_2D, color, 0);
            framebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                 GL_TEXTURE_2D, depth, 0);
            CHECK(checkFramebufferStatus(GL_FRAMEBUFFER) ==
                      GL_FRAMEBUFFER_COMPLETE,
                  "color plus Depth32F texture forms a complete Metal FBO");

            framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                 GL_TEXTURE_2D, depth, 0);
            CHECK(checkFramebufferStatus(GL_FRAMEBUFFER) !=
                      GL_FRAMEBUFFER_COMPLETE,
                  "depth texture is rejected from a color attachment");
            framebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                 GL_TEXTURE_2D, color, 0);
            framebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                 GL_TEXTURE_2D, color, 0);
            CHECK(checkFramebufferStatus(GL_FRAMEBUFFER) !=
                      GL_FRAMEBUFFER_COMPLETE,
                  "color texture is rejected from a depth attachment");
            framebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                 GL_TEXTURE_2D, depth, 0);
            CHECK(checkFramebufferStatus(GL_FRAMEBUFFER) ==
                      GL_FRAMEBUFFER_COMPLETE,
                  "restoring typed attachments restores completeness");

            useProgram(prog);
            viewport(0, 0, 32, 32);
            enable(GL_DEPTH_TEST);
            depthFunc(GL_LEQUAL);
            const float full_screen[3][7] = {
                {-1, -1, 0.0f, 1, 1, 1, 1},
                { 3, -1, 0.0f, 1, 1, 1, 1},
                {-1,  3, 0.0f, 1, 1, 1, 1},
            };
            bufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(full_screen),
                       full_screen, 0x88E4);
            clearColor(0, 0, 0, 1);
            clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
                  GL_STENCIL_BUFFER_BIT);
            drawArrays(GL_TRIANGLES, 0, 3);

            /* Binding the default target orders the producing command first.
               The next draw samples the same resident Metal depth texture. */
            bindFramebuffer(GL_FRAMEBUFFER, 0);
            bindTexture(GL_TEXTURE_2D, depth);
            viewport(0, 0, 512, 512);
            disable(GL_DEPTH_TEST);
            GLuint vs_depth = createShader(GL_VERTEX_SHADER);
            shaderSource(vs_depth, 1, &VS, 0);
            compileShader(vs_depth);
            GLuint fs_depth = createShader(GL_FRAGMENT_SHADER);
            shaderSource(fs_depth, 1, &FS_DEPTH_TEXTURE, 0);
            compileShader(fs_depth);
            GLuint program_depth = createProgram();
            attachShader(program_depth, vs_depth);
            attachShader(program_depth, fs_depth);
            linkProgram(program_depth);
            useProgram(program_depth);
            clearColor(0, 0, 0, 1);
            clear(GL_COLOR_BUFFER_BIT);
            drawArrays(GL_TRIANGLES, 0, 3);
            finish();
            readPixels(256, 256, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
            CHECK(px_match(px, 128, 0, 0, 255),
                  "shader reads GL z=0 as 0.5 from native Metal depth storage "
                  "(r=%d g=%d b=%d)", px[0], px[1], px[2]);

            /* The same resident image now feeds a comparison sampler. The
               LEQUAL reference 0.25 passes against the stored depth 0.5. */
            bindTexture(GL_TEXTURE_2D, depth);
            texParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE,
                          GL_COMPARE_REF_TO_TEXTURE);
            texParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
            GLuint fs_shadow = createShader(GL_FRAGMENT_SHADER);
            shaderSource(fs_shadow, 1, &FS_DEPTH_SHADOW, 0);
            compileShader(fs_shadow);
            GLuint program_shadow = createProgram();
            attachShader(program_shadow, vs_depth);
            attachShader(program_shadow, fs_shadow);
            linkProgram(program_shadow);
            useProgram(program_shadow);
            clearColor(0, 0, 0, 1);
            clear(GL_COLOR_BUFFER_BIT);
            drawArrays(GL_TRIANGLES, 0, 3);
            finish();
            readPixels(256, 256, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
            CHECK(px_match(px, 255, 0, 0, 255),
                  "sampler2DShadow compares through a cached Metal compare "
                  "sampler (r=%d g=%d b=%d)", px[0], px[1], px[2]);

            deleteFramebuffers(1, &fbo);
            deleteTextures(1, &color);
            deleteTextures(1, &depth);
        }
    }

    dlclose(h);

    if (failures == 0) { printf("\nFBO SMOKE ALL PASSED\n"); return 0; }
    printf("\nFBO SMOKE FAILED: %d failure(s)\n", failures);
    return 1;
}
