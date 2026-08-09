/* M5 state-pipeline smoke test (stage A): depth test + blend + scissor
 * on the default framebuffer, verified through RV8 readback.
 *
 * Exercises the new pipeline-state path end to end:
 *   glEnable(GL_DEPTH_TEST)/glDepthFunc/glClear(GL_DEPTH_BUFFER_BIT) ->
 *   two overlapping triangles at different z -> the near one wins
 *   glEnable(GL_BLEND) + glBlendFunc -> premultiplied blend result
 *   glEnable(GL_SCISSOR_TEST)+glScissor -> clipped region
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
#define GL_SRC_ALPHA          0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303

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

    dlclose(h);

    if (failures == 0) { printf("\nFBO SMOKE ALL PASSED\n"); return 0; }
    printf("\nFBO SMOKE FAILED: %d failure(s)\n", failures);
    return 1;
}