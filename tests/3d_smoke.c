/* 3D smoke test: perspective projection + depth buffering.
 *
 * Two independent 3D checks:
 *   1. Depth ordering -- near quad (z_NDC 0.2) drawn before a far triangle
 *      (z_NDC 0.7): GL_LEQUAL keeps the nearer quad at the centre while the
 *      far triangle shows through at the corners outside the quad.
 *   2. Perspective projection -- a mat4 (glUniformMatrix4fv) applies a proper
 *      GL perspective matrix; a quad at z=cam-2 and a triangle at z=cam-3
 *      project through NDC with correct foreshortening and still depth-sort.
 *
 * Build (from project root):
 *   gcc -o tests/3d_smoke tests/3d_smoke.c -ldl
 *   LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu ./tests/3d_smoke
 */
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/* GL 3.3 core constants (glcorearb.h values) */
#define GL_VERTEX_SHADER   0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_TRIANGLES       0x0004
#define GL_ARRAY_BUFFER    0x8892
#define GL_FLOAT           0x1406
#define GL_FALSE           0
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_TEST      0x0B71
#define GL_LEQUAL          0x0203
#define GL_RGBA            0x1908
#define GL_UNSIGNED_BYTE   0x1401

typedef unsigned int GLuint;
typedef unsigned int GLenum;
typedef unsigned int GLsizei;
typedef unsigned char GLboolean;
typedef int GLint;
typedef int GLsizeiptr;
typedef void* GLvoid;

typedef GLuint (*fn_glCreateShader)(GLenum);
typedef void (*fn_glShaderSource)(GLuint, GLsizei, const char* const*, const GLint*);
typedef void (*fn_glCompileShader)(GLuint);
typedef GLuint (*fn_glCreateProgram)(void);
typedef void (*fn_glAttachShader)(GLuint, GLuint);
typedef void (*fn_glLinkProgram)(GLuint);
typedef void (*fn_glUseProgram)(GLuint);
typedef GLint (*fn_glGetUniformLocation)(GLuint, const char*);
typedef void (*fn_glUniformMatrix4fv)(GLint, GLsizei, GLboolean, const float*);
typedef void (*fn_glGenVertexArrays)(GLsizei, GLuint*);
typedef void (*fn_glBindVertexArray)(GLuint);
typedef void (*fn_glGenBuffers)(GLsizei, GLuint*);
typedef void (*fn_glBindBuffer)(GLenum, GLuint);
typedef void (*fn_glBufferData)(GLenum, GLsizeiptr, const void*, GLenum);
typedef void (*fn_glEnableVertexAttribArray)(GLuint);
typedef void (*fn_glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const GLvoid*);
typedef void (*fn_glEnable)(GLenum);
typedef void (*fn_glDepthFunc)(GLenum);
typedef void (*fn_glClear)(GLenum);
typedef void (*fn_glClearColor)(float, float, float, float);
typedef void (*fn_glDrawArrays)(GLenum, GLint, GLsizei);
typedef void (*fn_glFinish)(void);
typedef void (*fn_glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);

static int failures = 0;

#define CHECK(cond, fmt, ...) do {                                          \
    if (cond) { printf("ok  : " fmt "\n", ##__VA_ARGS__); }                 \
    else      { printf("FAIL: " fmt "\n", ##__VA_ARGS__); ++failures; }     \
} while (0)

static int px_match(const unsigned char* got, int r, int g, int b) {
    return abs((int)got[0] - r) <= 3 && abs((int)got[1] - g) <= 3 &&
           abs((int)got[2] - b) <= 3;
}

static const char* VS =
    "#version 150\n"
    "layout(location=0) in vec3 pos;\n"
    "layout(location=1) in vec4 col;\n"
    "uniform mat4 uMVP;\n"
    "out vec4 vColor;\n"
    "void main() {\n"
    "    vColor = col;\n"
    "    gl_Position = uMVP * vec4(pos, 1.0);\n"
    "}\n";

static const char* FS =
    "#version 150\n"
    "in vec4 vColor;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    fragColor = vColor;\n"
    "}\n";

/* Column-major perspective matrix: same shape as GL's glFrustum with
 * symmetric fov/aspect; maps (x,y,-z) with z in [near,far] in front. */
static void Perspective(float near, float far, float fov_deg, float aspect,
                        float out[16]) {
    float f = 1.0f / tanf(fov_deg * 0.5f * 3.14159265f / 180.0f);
    for (int i = 0; i < 16; ++i) out[i] = 0;
    out[0]  = f / aspect;
    out[5]  = f;
    out[10] = (far + near) / (near - far);
    out[11] = -1.0f;
    out[14] = (2.0f * far * near) / (near - far);
}

int main(void) {
    void* h = dlopen("./output/libmithril.so", RTLD_NOW | RTLD_GLOBAL);
    if (!h) { printf("dlopen: %s\n", dlerror()); return 2; }

    fn_glCreateShader cs = (fn_glCreateShader)dlsym(h, "glCreateShader");
    fn_glShaderSource ss = (fn_glShaderSource)dlsym(h, "glShaderSource");
    fn_glCompileShader co = (fn_glCompileShader)dlsym(h, "glCompileShader");
    fn_glCreateProgram cp = (fn_glCreateProgram)dlsym(h, "glCreateProgram");
    fn_glAttachShader as = (fn_glAttachShader)dlsym(h, "glAttachShader");
    fn_glLinkProgram lp = (fn_glLinkProgram)dlsym(h, "glLinkProgram");
    fn_glUseProgram up = (fn_glUseProgram)dlsym(h, "glUseProgram");
    fn_glGetUniformLocation gu = (fn_glGetUniformLocation)dlsym(h, "glGetUniformLocation");
    fn_glUniformMatrix4fv um = (fn_glUniformMatrix4fv)dlsym(h, "glUniformMatrix4fv");
    fn_glGenVertexArrays gv = (fn_glGenVertexArrays)dlsym(h, "glGenVertexArrays");
    fn_glBindVertexArray bv = (fn_glBindVertexArray)dlsym(h, "glBindVertexArray");
    fn_glGenBuffers gb = (fn_glGenBuffers)dlsym(h, "glGenBuffers");
    fn_glBindBuffer bb = (fn_glBindBuffer)dlsym(h, "glBindBuffer");
    fn_glBufferData bd = (fn_glBufferData)dlsym(h, "glBufferData");
    fn_glEnableVertexAttribArray ea = (fn_glEnableVertexAttribArray)dlsym(h, "glEnableVertexAttribArray");
    fn_glVertexAttribPointer vp = (fn_glVertexAttribPointer)dlsym(h, "glVertexAttribPointer");
    fn_glEnable en = (fn_glEnable)dlsym(h, "glEnable");
    fn_glDepthFunc df = (fn_glDepthFunc)dlsym(h, "glDepthFunc");
    fn_glClear cl = (fn_glClear)dlsym(h, "glClear");
    fn_glClearColor cc = (fn_glClearColor)dlsym(h, "glClearColor");
    fn_glDrawArrays da = (fn_glDrawArrays)dlsym(h, "glDrawArrays");
    fn_glFinish fi = (fn_glFinish)dlsym(h, "glFinish");
    fn_glReadPixels rp = (fn_glReadPixels)dlsym(h, "glReadPixels");

    CHECK(cs && ss && co && cp && as && lp && up && gu && um &&
          gv && bv && gb && bb && bd && ea && vp && en && df &&
          cl && cc && da && fi && rp,
          "all required GL symbols resolved");

    /* -- program ----------------------------------------------------- */
    GLuint vs = cs(GL_VERTEX_SHADER);
    GLuint fs = cs(GL_FRAGMENT_SHADER);
    ss(vs, 1, &VS, 0);
    ss(fs, 1, &FS, 0);
    co(vs);
    co(fs);
    GLuint prog = cp();
    as(prog, vs);
    as(prog, fs);
    lp(prog);
    up(prog);
    GLint mvp = gu(prog, "uMVP");
    CHECK(mvp >= 0, "mat4 uniform resolved (loc=%d)", mvp);

    /* -- vertex setup ------------------------------------------------ */
    GLuint vao, vbo;
    gv(1, &vao);
    bv(vao);
    gb(1, &vbo);
    bb(GL_ARRAY_BUFFER, vbo);
    ea(0);
    vp(0, 3, GL_FLOAT, GL_FALSE, 7 * 4, 0);
    ea(1);
    vp(1, 4, GL_FLOAT, GL_FALSE, 7 * 4, (const GLvoid*)12);

    en(GL_DEPTH_TEST);
    df(GL_LEQUAL);
    cc(0.1f, 0.2f, 0.3f, 1.0f);
    unsigned char px[4];
    float ident[16]; for (int i = 0; i < 16; ++i) ident[i] = (i % 5) ? 0 : 1;

    /* -- check 1: NDC depth at identity viewport --------------------- */
    {
        /* near quad (red) z=0.2, far triangle (blue) z=0.7, both covering the
           centre. LEQUAL: near survives at the centre, corner is far. */
        float near_q[6][7] = {
            {-0.8f,-0.8f,0.2f, 1,0,0,1},{ 0.8f,-0.8f,0.2f, 1,0,0,1},
            { 0.8f, 0.8f,0.2f, 1,0,0,1},
            {-0.8f,-0.8f,0.2f, 1,0,0,1},{ 0.8f, 0.8f,0.2f, 1,0,0,1},
            {-0.8f, 0.8f,0.2f, 1,0,0,1}
        };
        float far_tri[3][7] = {
            {-1.5f,-1.5f,0.7f, 0,0,1,1},
            { 2.5f,-1.5f,0.7f, 0,0,1,1},
            {-1.5f, 2.5f,0.7f, 0,0,1,1}
        };
        um(mvp, 1, GL_FALSE, ident);
        cl(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        bd(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(near_q), near_q, 0x88E4);
        da(GL_TRIANGLES, 0, 6);
        bd(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(far_tri), far_tri, 0x88E4);
        da(GL_TRIANGLES, 0, 3);
        fi();
        rp(256, 256, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 255, 0, 0),
              "depth sort keeps the NEAR quad at the centre (r=%d g=%d b=%d)",
              px[0], px[1], px[2]);
        rp(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 0, 0, 255),
              "far triangle visible at the corner (r=%d g=%d b=%d)",
              px[0], px[1], px[2]);
    }

    /* -- check: perspective projection (mat4 far behind the eye) ----- */
    {
        /* Camera looks down -Z. Quad at z=-2 (near where near=0.3) red, full
           triangle at z=-6 blue. Both inside the near/far clip volume. The
           projected quad (foreshortened ~3x) sits at the centre; the huge far
           triangle still covers the corners. Depth still LEQUAL-sorts. */
        float persp[16];
        Perspective(0.3f, 40.0f, 60.0f, 1.0f, persp);
        um(mvp, 1, GL_FALSE, persp);

        float near_q[6][7] = {
            {-0.8f,-0.8f,-2.0f, 1,0,0,1},{ 0.8f,-0.8f,-2.0f, 1,0,0,1},
            { 0.8f, 0.8f,-2.0f, 1,0,0,1},
            {-0.8f,-0.8f,-2.0f, 1,0,0,1},{ 0.8f, 0.8f,-2.0f, 1,0,0,1},
            {-0.8f, 0.8f,-2.0f, 1,0,0,1}
        };
        float far_tri[3][7] = {
            {-4.0f,-4.0f,-6.0f, 0,0,1,1},
            {10.0f,-4.0f,-6.0f, 0,0,1,1},
            {-4.0f,10.0f,-6.0f, 0,0,1,1}
        };
        cl(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        bd(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(near_q), near_q, 0x88E4);
        da(GL_TRIANGLES, 0, 6);
        bd(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(far_tri), far_tri, 0x88E4);
        da(GL_TRIANGLES, 0, 3);
        fi();
        rp(256, 256, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 255, 0, 0),
              "perspective: near quad wins at the centre (r=%d g=%d b=%d)",
              px[0], px[1], px[2]);
        rp(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        CHECK(px_match(px, 0, 0, 255),
              "perspective: far triangle fills the frame corner (r=%d g=%d b=%d)",
              px[0], px[1], px[2]);
    }

    dlclose(h);
    if (failures == 0) { printf("\n3D SMOKE ALL PASSED\n"); return 0; }
    printf("\n3D SMOKE FAILED: %d failure(s)\n", failures);
    return 1;
}