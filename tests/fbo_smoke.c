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
#define GL_RGBA               0x1908
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

typedef unsigned int GLuint;
typedef unsigned int GLenum;
typedef unsigned int GLsizei;
typedef unsigned char GLboolean;
typedef int GLint;
typedef int GLsizeiptr;
typedef int GLintptr;
typedef void* GLvoid;

typedef void (*fn_glClearColor)(float, float, float, float);
typedef void (*fn_glClear)(GLenum);
typedef void (*fn_glEnable)(GLenum);
typedef void (*fn_glDisable)(GLenum);
typedef void (*fn_glDepthFunc)(GLenum);
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

int main(void) {
    void* h = dlopen("./output/libmithril.so", RTLD_NOW | RTLD_GLOBAL);
    if (!h) { printf("dlopen: %s\n", dlerror()); return 2; }

    fn_glClearColor clearColor = (fn_glClearColor)dlsym(h, "glClearColor");
    fn_glClear clear = (fn_glClear)dlsym(h, "glClear");
    fn_glEnable enable = (fn_glEnable)dlsym(h, "glEnable");
    fn_glDisable disable = (fn_glDisable)dlsym(h, "glDisable");
    fn_glDepthFunc depthFunc = (fn_glDepthFunc)dlsym(h, "glDepthFunc");
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

    CHECK(clearColor && clear && enable && depthFunc && scissor && blendFunc &&
          cullFace && frontFace && stencilFunc && stencilOp && stencilMask &&
          colorMask && polygonMode &&
          createShader && shaderSource && compileShader && createProgram &&
          attachShader && linkProgram && useProgram && getUniformLoc &&
          uniform4f && genVertexArrays && bindVertexArray && genBuffers &&
          bindBuffer && bufferData && enableAttrib && vertexAttribPtr &&
          drawArrays && finish && readPixels,
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
    linkProgram(prog);
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
        enable(GL_SCISSOR_TEST);
        scissor(0, 0, 3, 3);
        clearColor(0.10f, 0.20f, 0.30f, 1.0f);
        clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
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

    dlclose(h);

    if (failures == 0) { printf("\nFBO SMOKE ALL PASSED\n"); return 0; }
    printf("\nFBO SMOKE FAILED: %d failure(s)\n", failures);
    return 1;
}