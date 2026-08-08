/* M4-VK texture smoke test: GL texture upload -> sampler -> draw -> readback.
 *
 * Exercises the full M4 chain through the exported GL entry points:
 *   glGenTextures -> glBindTexture -> glTexImage2D -> glTexParameteri
 *     -> glActiveTexture -> glUniform1i(tex, 0)
 *     -> fullscreen quad with uv -> glDrawArrays -> glFinish -> glReadPixels
 *
 * Also checks the 1x1 white dummy fallback (unbound unit), the sampler-state
 * round-trip, and glGenerateMipmap. Requires a Vulkan runtime (lavapipe).
 *
 * Build (from project root):
 *   gcc -o tests/texture_smoke tests/texture_smoke.c -ldl
 *   LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu ./tests/texture_smoke
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* GL 3.3 core constants (values from glcorearb.h) */
#define GL_VERTEX_SHADER      0x8B31
#define GL_FRAGMENT_SHADER    0x8B30
#define GL_TRIANGLES          0x0004
#define GL_TRIANGLE_STRIP     0x0005
#define GL_ARRAY_BUFFER       0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_TEXTURE0           0x84C0
#define GL_TEXTURE_2D         0x0DE1
#define GL_RGBA               0x1908
#define GL_RGBA8              0x8058
#define GL_RGB                0x1907
#define GL_UNSIGNED_BYTE      0x1401
#define GL_FLOAT              0x1406
#define GL_FALSE              0
#define GL_TRUE               1
#define GL_COLOR_BUFFER_BIT   0x00004000
#define GL_NEAREST            0x2600
#define GL_LINEAR             0x2601
#define GL_NEAREST_MIPMAP_LINEAR 0x2702
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S     0x2802
#define GL_TEXTURE_WRAP_T     0x2803
#define GL_CLAMP_TO_EDGE      0x812F
#define GL_REPEAT             0x2901

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
typedef void (*fn_glUniform1i)(GLint, GLint);
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
typedef void (*fn_glGenTextures)(GLsizei, GLuint*);
typedef void (*fn_glDeleteTextures)(GLsizei, const GLuint*);
typedef GLboolean (*fn_glIsTexture)(GLuint);
typedef void (*fn_glBindTexture)(GLenum, GLuint);
typedef void (*fn_glActiveTexture)(GLenum);
typedef void (*fn_glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
typedef void (*fn_glTexParameteri)(GLenum, GLenum, GLint);
typedef void (*fn_glGetTexParameteriv)(GLenum, GLenum, GLint*);
typedef void (*fn_glGenerateMipmap)(GLenum);

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
    "layout(location=0) in vec2 pos;\n"
    "layout(location=1) in vec2 uv;\n"
    "out vec2 vUv;\n"
    "void main() {\n"
    "    vUv = uv;\n"
    "    gl_Position = vec4(pos, 0.0, 1.0);\n"
    "}\n";

static const char* FS =
    "#version 150\n"
    "uniform sampler2D tex;\n"
    "in vec2 vUv;\n"
    "layout(location=0) out vec4 fragColor;\n"
    "void main() {\n"
    "    fragColor = texture(tex, vUv);\n"
    "}\n";

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    void* h = dlopen("./output/libmithril.so", RTLD_NOW | RTLD_GLOBAL);
    if (!h) { printf("dlopen: %s\n", dlerror()); return 2; }

    fn_glClearColor        clearColor   = (fn_glClearColor)dlsym(h, "glClearColor");
    fn_glClear             clear        = (fn_glClear)dlsym(h, "glClear");
    fn_glCreateShader      createShader = (fn_glCreateShader)dlsym(h, "glCreateShader");
    fn_glShaderSource      shaderSource = (fn_glShaderSource)dlsym(h, "glShaderSource");
    fn_glCompileShader     compileShader= (fn_glCompileShader)dlsym(h, "glCompileShader");
    fn_glCreateProgram     createProgram= (fn_glCreateProgram)dlsym(h, "glCreateProgram");
    fn_glAttachShader      attachShader = (fn_glAttachShader)dlsym(h, "glAttachShader");
    fn_glLinkProgram       linkProgram  = (fn_glLinkProgram)dlsym(h, "glLinkProgram");
    fn_glUseProgram        useProgram   = (fn_glUseProgram)dlsym(h, "glUseProgram");
    fn_glGetUniformLocation getUniformLoc=(fn_glGetUniformLocation)dlsym(h, "glGetUniformLocation");
    fn_glUniform1i         uniform1i    = (fn_glUniform1i)dlsym(h, "glUniform1i");
    fn_glGenVertexArrays   genVertexArrays=(fn_glGenVertexArrays)dlsym(h, "glGenVertexArrays");
    fn_glBindVertexArray   bindVertexArray=(fn_glBindVertexArray)dlsym(h, "glBindVertexArray");
    fn_glGenBuffers        genBuffers   = (fn_glGenBuffers)dlsym(h, "glGenBuffers");
    fn_glBindBuffer        bindBuffer   = (fn_glBindBuffer)dlsym(h, "glBindBuffer");
    fn_glBufferData        bufferData   = (fn_glBufferData)dlsym(h, "glBufferData");
    fn_glEnableVertexAttribArray enableAttrib=(fn_glEnableVertexAttribArray)dlsym(h, "glEnableVertexAttribArray");
    fn_glVertexAttribPointer vertexAttribPtr=(fn_glVertexAttribPointer)dlsym(h, "glVertexAttribPointer");
    fn_glDrawArrays        drawArrays   = (fn_glDrawArrays)dlsym(h, "glDrawArrays");
    fn_glFinish            finish       = (fn_glFinish)dlsym(h, "glFinish");
    fn_glReadPixels        readPixels   = (fn_glReadPixels)dlsym(h, "glReadPixels");
    fn_glDeleteProgram     deleteProgram= (fn_glDeleteProgram)dlsym(h, "glDeleteProgram");
    fn_glGenTextures       genTextures  = (fn_glGenTextures)dlsym(h, "glGenTextures");
    fn_glDeleteTextures    deleteTextures=(fn_glDeleteTextures)dlsym(h, "glDeleteTextures");
    fn_glIsTexture         isTexture    = (fn_glIsTexture)dlsym(h, "glIsTexture");
    fn_glBindTexture       bindTexture  = (fn_glBindTexture)dlsym(h, "glBindTexture");
    fn_glActiveTexture     activeTexture= (fn_glActiveTexture)dlsym(h, "glActiveTexture");
    fn_glTexImage2D        texImage2D   = (fn_glTexImage2D)dlsym(h, "glTexImage2D");
    fn_glTexParameteri     texParameteri= (fn_glTexParameteri)dlsym(h, "glTexParameteri");
    fn_glGetTexParameteriv getTexParam  = (fn_glGetTexParameteriv)dlsym(h, "glGetTexParameteriv");
    fn_glGenerateMipmap    generateMipmap=(fn_glGenerateMipmap)dlsym(h, "glGenerateMipmap");

    CHECK(clearColor && clear && createShader && shaderSource && compileShader &&
          createProgram && attachShader && linkProgram && useProgram &&
          getUniformLoc && uniform1i && genVertexArrays && bindVertexArray &&
          genBuffers && bindBuffer && bufferData && enableAttrib &&
          vertexAttribPtr && drawArrays && finish && readPixels &&
          genTextures && deleteTextures && isTexture && bindTexture &&
          activeTexture && texImage2D && texParameteri && getTexParam &&
          generateMipmap,
          "all required texture symbols resolved");

    clearColor(0.10f, 0.20f, 0.30f, 1.0f);

    /* -- program with a sampler2D uniform --------------------------- */
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
    GLint tex_loc = getUniformLoc(prog, "tex");
    CHECK(tex_loc >= 0, "glGetUniformLocation(tex) resolves");

    /* -- fullscreen quad ------------------------------------------- */
    GLuint vao, vbo;
    genVertexArrays(1, &vao);
    bindVertexArray(vao);
    genBuffers(1, &vbo);
    bindBuffer(GL_ARRAY_BUFFER, vbo);
    struct { float x, y; float u, v; } quad[4] = {
        {-1, -1, 0, 0}, {1, -1, 1, 0}, {1, 1, 1, 1}, {-1, 1, 0, 1},
    };
    bufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(quad), quad, 0x88E4);
    enableAttrib(0);
    vertexAttribPtr(0, 2, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(quad[0]), (const GLvoid*)0);
    enableAttrib(1);
    vertexAttribPtr(1, 2, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(quad[0]), (const GLvoid*)8);

    /* -- 4x4 red texture, nearest, clamp --------------------------- */
    GLuint tex = 0;
    genTextures(1, &tex);
    CHECK(tex != 0 && isTexture(tex) == GL_TRUE, "glGenTextures produces a live name");
    activeTexture(GL_TEXTURE0);
    bindTexture(GL_TEXTURE_2D, tex);
    unsigned char red[4 * 4 * 4];
    for (int i = 0; i < 16; ++i) {
        red[i * 4 + 0] = 255; red[i * 4 + 1] = 30; red[i * 4 + 2] = 0;
        red[i * 4 + 3] = 255;
    }
    texImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, red);
    /* sampler state executes while the texture is unbound? no: bound above. */
    texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    uniform1i(tex_loc, 0);   /* sampler -> texture unit 0 */

    /* -- draw + readback ------------------------------------------- */
    /* prime the target with a clear+draw so the background is dirty;
       the tiny triangle stays inside, the corner stays on the clear color */
    clear(GL_COLOR_BUFFER_BIT);
    {
        struct { float x, y, u, v; } tri[3] = {
            {0.45f, -0.05f, 0, 0}, {0.55f, -0.05f, 0, 0}, {0.5f, 0.05f, 0, 0},
        };
        bindBuffer(GL_ARRAY_BUFFER, vbo);
        bufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(tri), tri, 0x88E4);
        drawArrays(GL_TRIANGLES, 0, 3);
    }
    finish();
    unsigned char corner[4];
    readPixels(10, 10, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, corner);
    CHECK(px_match(corner, 26, 51, 77, 255),
          "background corner pixel is the clear color (r=%d g=%d b=%d)",
          corner[0], corner[1], corner[2]);

    /* restore the fullscreen quad */
    bindBuffer(GL_ARRAY_BUFFER, vbo);
    bufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(quad), quad, 0x88E4);
    clear(GL_COLOR_BUFFER_BIT);
    drawArrays(GL_TRIANGLE_STRIP, 0, 4);
    finish();
    unsigned char px[4];
    readPixels(256, 256, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    CHECK(px_match(px, 255, 30, 0, 255),
          "textured quad samples the texture (r=%d g=%d b=%d)", px[0], px[1], px[2]);

    /* -- sampler-state round trip ----------------------------------- */
    GLint got = 0;
    getTexParam(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &got);
    CHECK(got == GL_NEAREST, "glGetTexParameteriv(MIN_FILTER) round-trips");
    getTexParam(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &got);
    CHECK(got == GL_CLAMP_TO_EDGE, "glGetTexParameteriv(WRAP_S) round-trips");

    /* -- mipmap generation keeps the image intact ------------------- */
    texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    generateMipmap(GL_TEXTURE_2D);
    clear(GL_COLOR_BUFFER_BIT);
    drawArrays(GL_TRIANGLE_STRIP, 0, 4);
    finish();
    readPixels(256, 256, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    CHECK(px_match(px, 255, 30, 0, 255),
          "glGenerateMipmap keeps the draw correct (r=%d g=%d b=%d)", px[0], px[1], px[2]);

    /* -- unbound unit falls back to the 1x1 white dummy ------------- */
    bindTexture(GL_TEXTURE_2D, 0);
    clear(GL_COLOR_BUFFER_BIT);
    drawArrays(GL_TRIANGLE_STRIP, 0, 4);
    finish();
    readPixels(256, 256, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    CHECK(px_match(px, 255, 255, 255, 255),
          "unbound texture samples the white dummy (r=%d g=%d b=%d)", px[0], px[1], px[2]);

    deleteTextures(1, &tex);
    CHECK(isTexture(tex) == GL_FALSE, "glDeleteTextures releases the name");
    deleteProgram(prog);
    dlclose(h);

    if (failures == 0) { printf("\nTEXTURE SMOKE ALL PASSED\n"); return 0; }
    printf("\nTEXTURE SMOKE FAILED: %d failure(s)\n", failures);
    return 1;
}