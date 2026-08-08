/* M2-VK draw smoke test: GL-driven colored triangle with pixel readback.
 *
 * Exercises the full chain end to end through the exported GL entry points:
 *   glCreateShader -> glShaderSource -> glCompileShader -> glAttachShader
 *     -> glLinkProgram -> glUseProgram -> glUniform4f
 *     -> glGenVertexArrays/glBindVertexArray
 *     -> glGenBuffers/glBindBuffer/glBufferData
 *     -> glEnableVertexAttribArray/glVertexAttribPointer
 *     -> glDrawArrays -> glFinish -> glReadPixels
 *
 * Requires a Vulkan runtime (lavapipe on Linux CI/dev). When no Vulkan
 * loader is present the draw is a no-op and the pixel assertions fail,
 * which is the intended behavior for this milestone.
 *
 * Build (from project root):
 *   gcc -o tests/draw_smoke tests/draw_smoke.c -ldl
 *   LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu ./tests/draw_smoke
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* GL 3.3 core constants (values from glcorearb.h) */
#define GL_VERTEX_SHADER      0x8B31
#define GL_FRAGMENT_SHADER    0x8B30
#define GL_COMPILE_STATUS     0x8B81
#define GL_LINK_STATUS        0x8B82
#define GL_TRIANGLES          0x0004
#define GL_ARRAY_BUFFER       0x8892
#define GL_FLOAT              0x1406
#define GL_FALSE              0
#define GL_TRUE               1
#define GL_COLOR_BUFFER_BIT   0x00004000
#define GL_RGBA               0x1908
#define GL_UNSIGNED_BYTE      0x1401
#define GL_NO_ERROR           0

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
typedef void (*fn_glDeleteProgram)(GLuint);

static int failures = 0;

#define CHECK(cond, fmt, ...) do {                                          \
    if (cond) { printf("ok  : " fmt "\n", ##__VA_ARGS__); }                 \
    else      { printf("FAIL: " fmt "\n", ##__VA_ARGS__); ++failures; }     \
} while (0)

/* tolerant RGBA comparison (R8 conversions round, off-by-one allowed) */
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
    "uniform vec4 tint;\n"
    "in vec4 vColor;\n"
    "layout(location=0) out vec4 fragColor;\n"
    "void main() {\n"
    "    fragColor = vColor * tint;\n"
    "}\n";

int main(void) {
    void* h = dlopen("./output/libmithril.so", RTLD_NOW | RTLD_GLOBAL);
    if (!h) { printf("dlopen: %s\n", dlerror()); return 2; }

    fn_glClearColor        clearColor        = (fn_glClearColor)dlsym(h, "glClearColor");
    fn_glClear             clear             = (fn_glClear)dlsym(h, "glClear");
    fn_glCreateShader      createShader      = (fn_glCreateShader)dlsym(h, "glCreateShader");
    fn_glShaderSource      shaderSource      = (fn_glShaderSource)dlsym(h, "glShaderSource");
    fn_glCompileShader     compileShader     = (fn_glCompileShader)dlsym(h, "glCompileShader");
    fn_glCreateProgram     createProgram     = (fn_glCreateProgram)dlsym(h, "glCreateProgram");
    fn_glAttachShader      attachShader      = (fn_glAttachShader)dlsym(h, "glAttachShader");
    fn_glLinkProgram       linkProgram       = (fn_glLinkProgram)dlsym(h, "glLinkProgram");
    fn_glUseProgram        useProgram        = (fn_glUseProgram)dlsym(h, "glUseProgram");
    fn_glGetUniformLocation getUniformLoc    = (fn_glGetUniformLocation)dlsym(h, "glGetUniformLocation");
    fn_glUniform4f         uniform4f         = (fn_glUniform4f)dlsym(h, "glUniform4f");
    fn_glGenVertexArrays   genVertexArrays   = (fn_glGenVertexArrays)dlsym(h, "glGenVertexArrays");
    fn_glBindVertexArray   bindVertexArray   = (fn_glBindVertexArray)dlsym(h, "glBindVertexArray");
    fn_glGenBuffers        genBuffers        = (fn_glGenBuffers)dlsym(h, "glGenBuffers");
    fn_glBindBuffer        bindBuffer        = (fn_glBindBuffer)dlsym(h, "glBindBuffer");
    fn_glBufferData        bufferData        = (fn_glBufferData)dlsym(h, "glBufferData");
    fn_glEnableVertexAttribArray enableAttrib = (fn_glEnableVertexAttribArray)dlsym(h, "glEnableVertexAttribArray");
    fn_glVertexAttribPointer vertexAttribPtr = (fn_glVertexAttribPointer)dlsym(h, "glVertexAttribPointer");
    fn_glDrawArrays        drawArrays        = (fn_glDrawArrays)dlsym(h, "glDrawArrays");
    fn_glFinish            finish            = (fn_glFinish)dlsym(h, "glFinish");
    fn_glReadPixels        readPixels        = (fn_glReadPixels)dlsym(h, "glReadPixels");
    fn_glDeleteProgram     deleteProgram     = (fn_glDeleteProgram)dlsym(h, "glDeleteProgram");

    CHECK(clearColor && clear && createShader && shaderSource && compileShader &&
          createProgram && attachShader && linkProgram && useProgram &&
          getUniformLoc && uniform4f && genVertexArrays && bindVertexArray &&
          genBuffers && bindBuffer && bufferData && enableAttrib &&
          vertexAttribPtr && drawArrays && finish && readPixels,
          "all required GL symbols resolved");

    /* -- background ------------------------------------------------ */
    clearColor(0.10f, 0.20f, 0.30f, 1.0f);
    clear(GL_COLOR_BUFFER_BIT);

    /* -- program ---------------------------------------------------- */
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
    GLint tint = getUniformLoc(prog, "tint");
    CHECK(tint >= 0, "glGetUniformLocation(tint) resolves");
    uniform4f(tint, 1.0f, 1.0f, 1.0f, 1.0f);   /* multiply colors through */

    /* -- vertex data (interleaved pos+color) ------------------------ */
    struct Vertex { float x, y, z; float r, g, b, a; };
    const struct Vertex verts[3] = {
        {-0.5f, -0.5f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f},
        { 0.5f, -0.5f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f},
        { 0.0f,  0.6f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f},
    };
    GLuint vao, vbo;
    genVertexArrays(1, &vao);
    bindVertexArray(vao);
    genBuffers(1, &vbo);
    bindBuffer(GL_ARRAY_BUFFER, vbo);
    bufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(verts), verts, 0x88E4 /*GL_STATIC_DRAW*/);
    enableAttrib(0);
    vertexAttribPtr(0, 3, GL_FLOAT, GL_FALSE, sizeof(struct Vertex), 0);
    enableAttrib(1);
    vertexAttribPtr(1, 4, GL_FLOAT, GL_FALSE, sizeof(struct Vertex),
                    (const GLvoid*)12);

    /* -- draw ------------------------------------------------------- */
    drawArrays(GL_TRIANGLES, 0, 3);
    finish();

    /* -- pixel assertions ------------------------------------------- */
    unsigned char px[4];
    readPixels(256, 300, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    CHECK(px_match(px, 255, 255, 255, 255),
          "triangle interior is white with identity tint (r=%d g=%d b=%d a=%d)",
          px[0], px[1], px[2], px[3]);

    unsigned char corner[4];
    readPixels(10, 10, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, corner);
    CHECK(px_match(corner, 26, 51, 77, 255),
          "background corner pixel is the clear color (r=%d g=%d b=%d)",
          corner[0], corner[1], corner[2]);

    /* -- second frame: tint multiply changes colour ----------------- */
    uniform4f(tint, 0.0f, 0.0f, 1.0f, 1.0f);
    clear(GL_COLOR_BUFFER_BIT);
    drawArrays(GL_TRIANGLES, 0, 3);
    finish();
    readPixels(256, 300, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    CHECK(px_match(px, 0, 0, 255, 255),
          "tint uniform drives the pixel colour (r=%d g=%d b=%d a=%d)",
          px[0], px[1], px[2], px[3]);

    deleteProgram(prog);
    dlclose(h);

    if (failures == 0) { printf("\nDRAW SMOKE ALL PASSED\n"); return 0; }
    printf("\nDRAW SMOKE FAILED: %d failure(s)\n", failures);
    return 1;
}
