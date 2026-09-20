/* Backend draw smoke test: GL-driven colored triangle with pixel readback.
 *
 * Exercises the full chain end to end through the exported GL entry points:
 *   glCreateShader -> glShaderSource -> glCompileShader -> glAttachShader
 *     -> glLinkProgram -> glUseProgram -> glUniform4f
 *     -> glGenVertexArrays/glBindVertexArray
 *     -> glGenBuffers/glBindBuffer/glBufferData
 *     -> glEnableVertexAttribArray/glVertexAttribPointer
 *     -> glDrawArrays -> glFinish -> glReadPixels
 *
 * Linux normally exercises Vulkan/lavapipe. On Apple, setting
 * MITHRIL_BACKEND=metal exercises the native DirectMetal backend.
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
#define GL_UNSIGNED_SHORT     0x1403
#define GL_UNSIGNED_INT       0x1405
#define GL_NO_ERROR           0
#define GL_TRIANGLE_STRIP     0x0005
#define GL_TRIANGLE_FAN       0x0006
#define GL_LINES              0x0001
#define GL_LINE_LOOP          0x0002
#define GL_LINE_STRIP         0x0003
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_BUFFER_SIZE        0x8764
#define GL_VERTEX_ATTRIB_ARRAY_SIZE   0x8623
#define GL_VERTEX_ATTRIB_ARRAY_STRIDE 0x8624
#define GL_READ_WRITE         0x88BA
#define GL_PRIMITIVE_RESTART  0x8F9D
#define GL_PRIMITIVE_RESTART_INDEX 0x8F9E

typedef unsigned int GLuint;
typedef unsigned int GLenum;
typedef unsigned int GLsizei;
typedef unsigned char GLboolean;
typedef int GLint;
typedef int GLsizeiptr;
typedef int GLintptr;
typedef void* GLvoid;
typedef unsigned short GLushort;
typedef unsigned int GLbitfield;

typedef void (*fn_glClearColor)(float, float, float, float);
typedef void (*fn_glClear)(GLenum);
typedef void (*fn_glEnable)(GLenum);
typedef void (*fn_glDisable)(GLenum);
typedef GLboolean (*fn_glIsEnabled)(GLenum);
typedef void (*fn_glGetIntegerv)(GLenum, GLint*);
typedef void (*fn_glPrimitiveRestartIndex)(GLuint);
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
typedef void (*fn_glDisableVertexAttribArray)(GLuint);
typedef void (*fn_glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const GLvoid*);
typedef void (*fn_glVertexAttrib4f)(GLuint, float, float, float, float);
typedef void (*fn_glDrawArrays)(GLenum, GLint, GLsizei);
typedef void (*fn_glDrawElements)(GLenum, GLsizei, GLenum, const GLvoid*);
typedef void (*fn_glDrawArraysInstanced)(GLenum, GLint, GLsizei, GLsizei);
typedef void (*fn_glDrawElementsInstanced)(GLenum, GLsizei, GLenum, const GLvoid*, GLsizei);
typedef void (*fn_glVertexAttribDivisor)(GLuint, GLuint);
typedef void (*fn_glGetBufferParameteriv)(GLenum, GLenum, GLint*);
typedef void (*fn_glGetVertexAttribiv)(GLuint, GLenum, GLint*);
typedef void* (*fn_glMapBufferRange)(GLenum, GLintptr, GLsizeiptr, GLbitfield);
typedef GLboolean (*fn_glUnmapBuffer)(GLenum);
typedef void (*fn_glBufferSubData)(GLenum, GLintptr, GLsizeiptr, const void*);
typedef void (*fn_glFinish)(void);
typedef void (*fn_glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
typedef void (*fn_glDeleteProgram)(GLuint);
typedef const unsigned char* (*fn_glGetString)(GLenum);

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

static int patch_has(const unsigned char* pixels, int count,
                     unsigned char r, unsigned char g, unsigned char b,
                     unsigned char a) {
    for (int i = 0; i < count; ++i)
        if (px_match(pixels + i * 4, r, g, b, a)) return 1;
    return 0;
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
    const char* library = getenv("MITHRIL_LIBRARY");
#if defined(__APPLE__)
    if (!library || !*library) library = "./output/libmithril.dylib";
#else
    if (!library || !*library) library = "./output/libmithril.so";
#endif
    void* h = dlopen(library, RTLD_NOW | RTLD_GLOBAL);
    if (!h) { printf("dlopen: %s\n", dlerror()); return 2; }

    fn_glClearColor        clearColor        = (fn_glClearColor)dlsym(h, "glClearColor");
    fn_glClear             clear             = (fn_glClear)dlsym(h, "glClear");
    fn_glEnable            enable            = (fn_glEnable)dlsym(h, "glEnable");
    fn_glDisable           disable           = (fn_glDisable)dlsym(h, "glDisable");
    fn_glIsEnabled         isEnabled         = (fn_glIsEnabled)dlsym(h, "glIsEnabled");
    fn_glGetIntegerv       getIntegerv       = (fn_glGetIntegerv)dlsym(h, "glGetIntegerv");
    fn_glPrimitiveRestartIndex primitiveRestartIndex =
        (fn_glPrimitiveRestartIndex)dlsym(h, "glPrimitiveRestartIndex");
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
    fn_glDisableVertexAttribArray disableAttrib = (fn_glDisableVertexAttribArray)dlsym(h, "glDisableVertexAttribArray");
    fn_glVertexAttribPointer vertexAttribPtr = (fn_glVertexAttribPointer)dlsym(h, "glVertexAttribPointer");
    fn_glVertexAttrib4f     vertexAttrib4f    = (fn_glVertexAttrib4f)dlsym(h, "glVertexAttrib4f");
    fn_glDrawArrays        drawArrays        = (fn_glDrawArrays)dlsym(h, "glDrawArrays");
    fn_glDrawElements      drawElements      = (fn_glDrawElements)dlsym(h, "glDrawElements");
    fn_glDrawArraysInstanced drawArraysInst = (fn_glDrawArraysInstanced)dlsym(h, "glDrawArraysInstanced");
    fn_glDrawElementsInstanced drawElementsInst = (fn_glDrawElementsInstanced)dlsym(h, "glDrawElementsInstanced");
    fn_glVertexAttribDivisor vertexAttribDivisor = (fn_glVertexAttribDivisor)dlsym(h, "glVertexAttribDivisor");
    fn_glGetBufferParameteriv getBufferParam = (fn_glGetBufferParameteriv)dlsym(h, "glGetBufferParameteriv");
    fn_glGetVertexAttribiv getVertexAttrib  = (fn_glGetVertexAttribiv)dlsym(h, "glGetVertexAttribiv");
    fn_glMapBufferRange   mapBufferRange    = (fn_glMapBufferRange)dlsym(h, "glMapBufferRange");
    fn_glUnmapBuffer      unmapBuffer       = (fn_glUnmapBuffer)dlsym(h, "glUnmapBuffer");
    fn_glFinish            finish            = (fn_glFinish)dlsym(h, "glFinish");
    fn_glReadPixels        readPixels        = (fn_glReadPixels)dlsym(h, "glReadPixels");
    fn_glDeleteProgram     deleteProgram     = (fn_glDeleteProgram)dlsym(h, "glDeleteProgram");
    fn_glGetString         getString         = (fn_glGetString)dlsym(h, "glGetString");

    CHECK(clearColor && clear && createShader && shaderSource && compileShader &&
          createProgram && attachShader && linkProgram && useProgram &&
          getUniformLoc && uniform4f && genVertexArrays && bindVertexArray &&
          genBuffers && bindBuffer && bufferData && enableAttrib &&
          disableAttrib && vertexAttribPtr && vertexAttrib4f && drawArrays &&
          finish && readPixels && enable &&
          disable && isEnabled && getIntegerv && primitiveRestartIndex,
          "all required GL symbols resolved");

    const char* expected_renderer = getenv("MITHRIL_EXPECT_RENDERER");
    const char* renderer = getString
        ? (const char*)getString(0x1F01 /* GL_RENDERER */) : NULL;
    CHECK(renderer && (!expected_renderer || strstr(renderer, expected_renderer)),
          "selected renderer is explicit (%s)", renderer ? renderer : "null");

    CHECK(drawElements && drawArraysInst && drawElementsInst && vertexAttribDivisor &&
          getBufferParam && getVertexAttrib && mapBufferRange && unmapBuffer,
          "M3 draw/query symbols resolved");

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

    /* -- disabled array uses the generic current attribute --------- */
    disableAttrib(1);
    /* Pointer format changes must not implicitly re-enable the array. */
    vertexAttribPtr(1, 4, GL_FLOAT, GL_FALSE, sizeof(struct Vertex),
                    (const GLvoid*)12);
    vertexAttrib4f(1, 0.0f, 1.0f, 0.0f, 1.0f);
    uniform4f(tint, 1.0f, 1.0f, 1.0f, 1.0f);
    clear(GL_COLOR_BUFFER_BIT);
    drawArrays(GL_TRIANGLES, 0, 3);
    finish();
    readPixels(256, 300, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    CHECK(px_match(px, 0, 255, 0, 255),
          "disabled colour array uses its current value without vertex repack "
          "(r=%d g=%d b=%d)", px[0], px[1], px[2]);
    enableAttrib(1);

    /* -- M3: object/state queries ----------------------------------------- */
    {
        GLint sz = 0, n = 0, stride = 0;
        getBufferParam(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &sz);
        CHECK(sz == (GLint)sizeof(verts),
              "glGetBufferParameteriv reports VBO size (%d)", sz);
        getVertexAttrib(0, GL_VERTEX_ATTRIB_ARRAY_SIZE, &n);
        getVertexAttrib(0, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &stride);

        CHECK(n == 3 && stride == (GLint)sizeof(struct Vertex),
              "glGetVertexAttribiv reflects pointer state (size=%d stride=%d)", n, stride);
    }

    /* -- M3: indexed draw (UINT16) ----------------------------------------- */
    {
        const GLushort idx[3] = {0, 1, 2};
        GLuint ibo = 0;
        genBuffers(1, &ibo);
        bindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
        bufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)sizeof(idx), idx, 0x88E4);
        uniform4f(tint, 1.0f, 1.0f, 1.0f, 1.0f);
        clear(GL_COLOR_BUFFER_BIT);
        drawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, (const GLvoid*)0);
        finish();
        readPixels(256, 300, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 255, 255, 255, 255),
              "glDrawElements(UINT16) renders white triangle (r=%d g=%d b=%d)",
              px[0], px[1], px[2]);
    }

    /* -- M3: map-range write + redraw -------------------------------------- */
    {
        struct Vertex mv[3] = {{-0.5f, -0.6f, 0.0f, 1, 1, 1, 1},
                               { 0.5f, -0.6f, 0.0f, 1, 1, 1, 1},
                               { 0.0f,  0.8f, 0.0f, 1, 1, 1, 1}};
        void* map = mapBufferRange(GL_ARRAY_BUFFER, 0, sizeof(mv), GL_READ_WRITE);
        CHECK(map != NULL, "glMapBufferRange returns writable pointer");
        memcpy(map, mv, sizeof(mv));
        CHECK(unmapBuffer(GL_ARRAY_BUFFER) == GL_TRUE, "glUnmapBuffer succeeds");
        uniform4f(tint, 0.0f, 1.0f, 0.0f, 1.0f);
        clear(GL_COLOR_BUFFER_BIT);
        drawArrays(GL_TRIANGLES, 0, 3);
        finish();
        readPixels(256, 300, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 0, 255, 0, 255),
              "mapped-write vertex data renders green (r=%d g=%d b=%d)",
              px[0], px[1], px[2]);
        uniform4f(tint, 1.0f, 1.0f, 1.0f, 1.0f);
    }

    /* -- M3: non-4-byte-aligned interleaved stride -------------------------- */
    {
        /* 3 floats pos (0..12) + 2 pad + 4 normalized u8 color at offset 14:
           stride 18 (18 % 4 == 2), color offset 14 (14 % 4 == 2). */
        unsigned char data[18 * 3] = {0};
        float tri[3][3] = {{-0.5f, -0.5f, 0.0f}, {0.5f, -0.5f, 0.0f},
                           {0.0f, 0.6f, 0.0f}};
        for (int v = 0; v < 3; ++v) {
            memcpy(data + v * 18, tri[v], 12);
            data[v * 18 + 14] = 255; data[v * 18 + 15] = 255;
            data[v * 18 + 16] = 255; data[v * 18 + 17] = 255;
        }
        GLuint vbo2 = 0;
        genBuffers(1, &vbo2);
        bindBuffer(GL_ARRAY_BUFFER, vbo2);
        bufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(data), data, 0x88E4);
        enableAttrib(0);
        vertexAttribPtr(0, 3, GL_FLOAT, GL_FALSE, 18, (const GLvoid*)0);
        enableAttrib(1);
        vertexAttribPtr(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, 18, (const GLvoid*)14);
        clear(GL_COLOR_BUFFER_BIT);
        drawArrays(GL_TRIANGLES, 0, 3);
        finish();
        readPixels(256, 300, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 255, 255, 255, 255),
              "non-4-aligned stride/offset renders white (r=%d g=%d b=%d)",
              px[0], px[1], px[2]);
    }

    /* -- M3: instanced draw (divisor=1) ------------------------------------ */
    {
        /* per-vertex: pos from vbo (loc 0); per-instance: color (loc 1) from
           a second buffer with glVertexAttribDivisor(1, 1). Two instances:
           red then green; the green instance is drawn last and wins. */
        struct { float x, y, z; } ipos[3] = {{-0.5f, -0.5f, 0}, {0.5f, -0.5f, 0}, {0, 0.6f, 0}};
        float instcol[2][4] = {{1.0f, 0, 0, 1}, {0, 1.0f, 0, 1}};
        GLuint vbo3, vbo4;
        genBuffers(1, &vbo3);
        bindBuffer(GL_ARRAY_BUFFER, vbo3);
        bufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(ipos), ipos, 0x88E4);
        enableAttrib(0);
        vertexAttribPtr(0, 3, GL_FLOAT, GL_FALSE, (GLsizei)sizeof(ipos[0]), 0);
        genBuffers(1, &vbo4);
        bindBuffer(GL_ARRAY_BUFFER, vbo4);
        bufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(instcol), instcol, 0x88E4);
        enableAttrib(1);
        vertexAttribPtr(1, 4, GL_FLOAT, GL_FALSE, 0, 0);
        vertexAttribDivisor(1, 1);
        clear(GL_COLOR_BUFFER_BIT);
        drawArraysInst(GL_TRIANGLES, 0, 3, 2);
        finish();
        readPixels(256, 300, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 0, 255, 0, 255),
              "glDrawArraysInstanced picks per-instance data (r=%d g=%d b=%d)",
              px[0], px[1], px[2]);
    }

    /* -- M3: triangle-strip topology --------------------------------------- */
    {
        GLuint vao2 = 0;
        genVertexArrays(1, &vao2);
        bindVertexArray(vao2);
        genBuffers(1, &vbo);
        bindBuffer(GL_ARRAY_BUFFER, vbo);
        bufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(verts), verts, 0x88E4);
        enableAttrib(0);
        vertexAttribPtr(0, 3, GL_FLOAT, GL_FALSE, sizeof(verts[0]), 0);
        enableAttrib(1);
        vertexAttribPtr(1, 4, GL_FLOAT, GL_FALSE, sizeof(verts[0]), (const GLvoid*)12);
        clear(GL_COLOR_BUFFER_BIT);
        drawArrays(GL_TRIANGLE_STRIP, 0, 3);
        finish();
        readPixels(256, 300, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 255, 255, 255, 255),
              "GL_TRIANGLE_STRIP renders the same triangle (r=%d g=%d b=%d)",
              px[0], px[1], px[2]);
        bindVertexArray(vao);
    }

    /* -- GL 3.1: custom primitive restart on indexed strip ----------------- */
    {
        const struct Vertex restart_verts[6] = {
            {-0.90f, -0.60f, 0, 1, 0, 0, 1},
            {-0.20f, -0.60f, 0, 1, 0, 0, 1},
            {-0.55f,  0.60f, 0, 1, 0, 0, 1},
            { 0.20f, -0.60f, 0, 0, 1, 0, 1},
            { 0.90f, -0.60f, 0, 0, 1, 0, 1},
            { 0.55f,  0.60f, 0, 0, 1, 0, 1},
        };
        const GLushort restart_indices[7] = {0, 1, 2, 42, 3, 4, 5};
        GLuint restart_vao = 0, restart_vbo = 0, restart_ibo = 0;
        genVertexArrays(1, &restart_vao);
        bindVertexArray(restart_vao);
        genBuffers(1, &restart_vbo);
        bindBuffer(GL_ARRAY_BUFFER, restart_vbo);
        bufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(restart_verts),
                   restart_verts, 0x88E4);
        enableAttrib(0);
        vertexAttribPtr(0, 3, GL_FLOAT, GL_FALSE,
                        sizeof(restart_verts[0]), 0);
        enableAttrib(1);
        vertexAttribPtr(1, 4, GL_FLOAT, GL_FALSE,
                        sizeof(restart_verts[0]), (const GLvoid*)12);
        genBuffers(1, &restart_ibo);
        bindBuffer(GL_ELEMENT_ARRAY_BUFFER, restart_ibo);
        bufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)sizeof(restart_indices),
                   restart_indices, 0x88E4);

        primitiveRestartIndex(42);
        enable(GL_PRIMITIVE_RESTART);
        GLint queried_restart = -1;
        getIntegerv(GL_PRIMITIVE_RESTART_INDEX, &queried_restart);
        CHECK(queried_restart == 42 && isEnabled(GL_PRIMITIVE_RESTART),
              "custom primitive restart state is observable (index=%d)",
              queried_restart);
        clear(GL_COLOR_BUFFER_BIT);
        drawElements(GL_TRIANGLE_STRIP, 7, GL_UNSIGNED_SHORT,
                     (const GLvoid*)0);
        finish();
        unsigned char left[4], right[4];
        readPixels(115, 256, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, left);
        readPixels(397, 256, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, right);
        CHECK(px_match(left, 255, 0, 0, 255) &&
                  px_match(right, 0, 255, 0, 255),
              "custom restart becomes native Metal sentinel "
              "(left=%d,%d,%d right=%d,%d,%d)",
              left[0], left[1], left[2], right[0], right[1], right[2]);
        disable(GL_PRIMITIVE_RESTART);
        bindVertexArray(vao);
    }

    /* -- native Metal line strip + shared GL_LINE_LOOP lowering ----------- */
    {
        const struct Vertex line_verts[4] = {
            {-0.80f, -0.40f, 0, 1, 0, 0, 1},
            {-0.20f, -0.40f, 0, 1, 0, 0, 1},
            { 0.20f,  0.40f, 0, 0, 1, 0, 1},
            { 0.80f,  0.40f, 0, 0, 1, 0, 1},
        };
        const GLushort line_indices[5] = {0, 1, 42, 2, 3};
        GLuint line_vao = 0, line_vbo = 0, line_ibo = 0;
        genVertexArrays(1, &line_vao);
        bindVertexArray(line_vao);
        genBuffers(1, &line_vbo);
        bindBuffer(GL_ARRAY_BUFFER, line_vbo);
        bufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(line_verts),
                   line_verts, 0x88E4);
        enableAttrib(0);
        vertexAttribPtr(0, 3, GL_FLOAT, GL_FALSE,
                        sizeof(line_verts[0]), 0);
        enableAttrib(1);
        vertexAttribPtr(1, 4, GL_FLOAT, GL_FALSE,
                        sizeof(line_verts[0]), (const GLvoid*)12);
        genBuffers(1, &line_ibo);
        bindBuffer(GL_ELEMENT_ARRAY_BUFFER, line_ibo);
        bufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)sizeof(line_indices),
                   line_indices, 0x88E4);

        clear(GL_COLOR_BUFFER_BIT);
        primitiveRestartIndex(42);
        enable(GL_PRIMITIVE_RESTART);
        drawElements(GL_LINE_STRIP, 5, GL_UNSIGNED_SHORT, (const GLvoid*)0);
        finish();
        unsigned char red_patch[3 * 3 * 4], green_patch[3 * 3 * 4];
        readPixels(127, 152, 3, 3, GL_RGBA, GL_UNSIGNED_BYTE, red_patch);
        readPixels(383, 357, 3, 3, GL_RGBA, GL_UNSIGNED_BYTE, green_patch);
        CHECK(patch_has(red_patch, 9, 255, 0, 0, 255) &&
                  patch_has(green_patch, 9, 0, 255, 0, 255),
              "GL_LINE_STRIP restart produces two native Metal line segments");
        disable(GL_PRIMITIVE_RESTART);

        const struct Vertex loop_verts[4] = {
            {-0.50f, -0.50f, 0, 0, 0, 1, 1},
            { 0.50f, -0.50f, 0, 0, 0, 1, 1},
            { 0.50f,  0.50f, 0, 0, 0, 1, 1},
            {-0.50f,  0.50f, 0, 0, 0, 1, 1},
        };
        bindBuffer(GL_ARRAY_BUFFER, line_vbo);
        bufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(loop_verts),
                   loop_verts, 0x88E4);
        clear(GL_COLOR_BUFFER_BIT);
        drawArrays(GL_LINE_LOOP, 0, 4);
        finish();
        unsigned char loop_patch[3 * 3 * 4];
        readPixels(127, 255, 3, 3, GL_RGBA, GL_UNSIGNED_BYTE, loop_patch);
        CHECK(patch_has(loop_patch, 9, 0, 0, 255, 255),
              "GL_LINE_LOOP closes through shared line-list lowering");
        bindVertexArray(vao);
    }

    deleteProgram(prog);
    dlclose(h);

    if (failures == 0) { printf("\nDRAW SMOKE ALL PASSED\n"); return 0; }
    printf("\nDRAW SMOKE FAILED: %d failure(s)\n", failures);
    return 1;
}
