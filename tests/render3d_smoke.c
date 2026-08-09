/* 3D scene smoke test: perspective + top-down camera + floor grid.
 *
 * Renders a cube standing on a checkered floor slab with a proper
 * perspective projection and a gluLookAt-style camera pitched DOWN at the
 * scene (camera above the cube, floor filling the lower half of the frame).
 * Verifies the readback through pixel assertions, then exports the
 * framebuffer to a PPM (use scripts/ppm_to_png.py to make a PNG).
 *
 * Build (from project root):
 *   gcc -o tests/render3d_smoke tests/render3d_smoke.c -ldl -lm
 *   LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu ./tests/render3d_smoke
 */
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* GL 3.3 core constants (glcorearb.h values) */
#define GL_VERTEX_SHADER    0x8B31
#define GL_FRAGMENT_SHADER  0x8B30
#define GL_TRIANGLES        0x0004
#define GL_ARRAY_BUFFER     0x8892
#define GL_FLOAT            0x1406
#define GL_FALSE            0
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_TEST       0x0B71
#define GL_LEQUAL           0x0203
#define GL_RGBA             0x1908
#define GL_UNSIGNED_BYTE    0x1401

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

#define W 512
#define H 512

static int failures = 0;

#define CHECK(cond, fmt, ...) do {                                          \
    if (cond) { printf("ok  : " fmt "\n", ##__VA_ARGS__); }                 \
    else      { printf("FAIL: " fmt "\n", ##__VA_ARGS__); ++failures; }     \
} while (0)

static const char* VS =
    "#version 150\n"
    "layout(location=0) in vec3 pos;\n"
    "layout(location=1) in vec4 col;\n"
    "uniform mat4 uMVP;\n"
    "out vec4 vColor;\n"
    "void main() { vColor = col; gl_Position = uMVP * vec4(pos, 1.0); }\n";

static const char* FS =
    "#version 150\n"
    "in vec4 vColor;\n"
    "out vec4 fragColor;\n"
    "void main() { fragColor = vColor; }\n";

static void MatMul(const float a[16], const float b[16], float out[16]) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a[k * 4 + r] * b[c * 4 + k];
            out[c * 4 + r] = s;
        }
}
static void MatIdent(float m[16]) { for (int i = 0; i < 16; ++i) m[i] = (i % 5) ? 0 : 1; }

static void MatRotXY(float ang, float m[16]) {
    float c = cosf(ang), s = sinf(ang);
    for (int i = 0; i < 16; ++i) m[i] = (i % 5) ? 0 : 1;
    m[5] = c; m[9] = s; m[6] = -s; m[10] = c;   /* rotate around X */
    float cx = cosf(ang * 0.8f), sx = sinf(ang * 0.8f);
    float m2[16]; for (int i = 0; i < 16; ++i) m2[i] = m[i];
    for (int i = 0; i < 16; ++i) m[i] = (i % 5) ? 0 : 1;
    m[0] = cx; m[8] = -sx; m[2] = sx; m[10] = cx; /* rotate around Y */
    MatMul(m, m2, m);
}

/* Column-major gluLookAt. Camera at (ex,ey,ez) looking at (tx,ty,tz),
 * up = +Y. GL convention: column 3 holds the translation. */
static void LookAt(float ex, float ey, float ez,
                   float tx, float ty, float tz, float out[16]) {
    float fw[3] = {tx-ex, ty-ey, tz-ez};
    float fl = sqrtf(fw[0]*fw[0]+fw[1]*fw[1]+fw[2]*fw[2]);
    fw[0]/=fl; fw[1]/=fl; fw[2]/=fl;
    float up[3] = {0,1,0};
    float s[3] = { fw[1]*up[2]-fw[2]*up[1],
                   fw[2]*up[0]-fw[0]*up[2],
                   fw[0]*up[1]-fw[1]*up[0] };
    float sl = 1.0f/(sqrtf(s[0]*s[0]+s[1]*s[1]+s[2]*s[2])+1e-9f);
    s[0]*=sl; s[1]*=sl; s[2]*=sl;
    float u[3] = { s[1]*fw[2]-s[2]*fw[1],
                   s[2]*fw[0]-s[0]*fw[2],
                   s[0]*fw[1]-s[1]*fw[0] };
    out[0]=s[0]; out[4]=s[1]; out[8]=s[2];
    out[1]=u[0]; out[5]=u[1]; out[9]=u[2];
    out[2]=-fw[0]; out[6]=-fw[1]; out[10]=-fw[2];
    out[12]=-(s[0]*ex+s[1]*ey+s[2]*ez);
    out[13]=-(u[0]*ex+u[1]*ey+u[2]*ez);
    out[14]= fw[0]*ex+fw[1]*ey+fw[2]*ez;
    out[3]=out[7]=out[11]=0; out[15]=1;
}

static void Perspective(float near, float far, float fov_deg, float aspect, float out[16]) {
    float f = 1.0f / tanf(fov_deg * 0.5f * 3.14159265f / 180.0f);
    for (int i = 0; i < 16; ++i) out[i] = 0;
    out[0] = f / aspect; out[5] = f;
    out[10] = (far + near) / (near - far);
    out[11] = -1.0f;
    out[14] = (2.0f * far * near) / (near - far);
}

/* 6 faces * 6 verts (pos3 + rgba4), cube centred on (cx,cy,cz), half-size s */
static void BuildCube(float s, float cx, float cy, float cz, float (*v)[7]) {
    static const float col[6][4] = {
        {1,0,0,1},{0,1,0,1},{0,0,1,1},
        {1,1,0,1},{1,0,1,1},{0,1,1,1}};
    static const float n[6][3] = {
        {0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
    int vi = 0;
    for (int f = 0; f < 6; ++f) {
        float a[3] = {n[f][0], n[f][1], n[f][2]};
        float u[3] = {0,0,0}, w[3] = {0,0,0};
        if (a[0] != 0) u[1] = 1;
        else if (a[1] != 0) u[2] = 1;
        else u[0] = 1;
        for (int k = 0; k < 3; ++k)
            w[k] = a[(k + 1) % 3] * u[(k + 2) % 3] - a[(k + 2) % 3] * u[(k + 1) % 3];
        float c[3] = {cx + a[0] * s, cy + a[1] * s, cz + a[2] * s};
        float corners[4][3] = {
            {c[0] - u[0] * s - w[0] * s, c[1] - u[1] * s - w[1] * s, c[2] - u[2] * s - w[2] * s},
            {c[0] + u[0] * s - w[0] * s, c[1] + u[1] * s - w[1] * s, c[2] + u[2] * s - w[2] * s},
            {c[0] + u[0] * s + w[0] * s, c[1] + u[1] * s + w[1] * s, c[2] + u[2] * s + w[2] * s},
            {c[0] - u[0] * s + w[0] * s, c[1] - u[1] * s + w[1] * s, c[2] - u[2] * s + w[2] * s}};
        int order[6] = {0,1,2, 0,2,3};
        for (int t = 0; t < 6; ++t) {
            int i = order[t];
            for (int k = 0; k < 3; ++k) v[vi][k] = corners[i][k];
            for (int k = 0; k < 4; ++k) v[vi][3 + k] = col[f][k];
            ++vi;
        }
    }
}

int main(void) {
    void* h = dlopen("./output/libmithril.so", RTLD_NOW | RTLD_GLOBAL);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }

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
    CHECK(cs && ss && co && cp && as && lp && up && gu && um && gv && bv && gb && bb && bd &&
          ea && vp && en && df && cl && cc && da && fi && rp,
          "all required GL symbols resolved");

    GLuint vs = cs(GL_VERTEX_SHADER), fs = cs(GL_FRAGMENT_SHADER);
    ss(vs, 1, &VS, 0); ss(fs, 1, &FS, 0); co(vs); co(fs);
    GLuint prog = cp(); as(prog, vs); as(prog, fs); lp(prog); up(prog);
    GLint mvp = gu(prog, "uMVP");
    CHECK(mvp >= 0, "scene mat4 uniform resolved (loc=%d)", mvp);

    GLuint vao, vbo; gv(1, &vao); bv(vao);
    gb(1, &vbo); bb(GL_ARRAY_BUFFER, vbo);
    ea(0); vp(0, 3, GL_FLOAT, GL_FALSE, 7 * 4, 0);
    ea(1); vp(1, 4, GL_FLOAT, GL_FALSE, 7 * 4, (const GLvoid*)12);

    en(GL_DEPTH_TEST); df(GL_LEQUAL);
    cc(0.08f, 0.09f, 0.12f, 1.0f);

    /* floor slab under and around the cube */
    float grid[24 * 24 * 6][7]; int gi = 0;
    for (int i = 0; i < 24; ++i) {
        float x0 = -12.0f + i * 1.0f;
        for (int j = 0; j < 24; ++j) {
            float z0 = 6.0f - j * 1.0f, z1 = z0 - 1.0f;
            float g = ((i + j) % 2) ? 0.30f : 0.14f;
            float quad[6][7] = {
                {x0, -1.0f, z0, g,g,g,1},{x0 + 1, -1.0f, z0, g,g,g,1},
                {x0 + 1, -1.0f, z1, g,g,g,1},
                {x0, -1.0f, z0, g,g,g,1},{x0 + 1, -1.0f, z1, g,g,g,1},
                {x0, -1.0f, z1, g,g,g,1}};
            memcpy(grid + gi, quad, sizeof quad); gi += 6;
        }
    }

    /* cube on the slab (bottom at y=-0.85, floor at y=-1.0) */
    float cube[36][7]; BuildCube(0.85f, 0, 0, 0, cube);

    float persp[16]; Perspective(0.1f, 100.0f, 55.0f, 1.0f, persp);
    float rot[16], trl[16], cam[16], model[16], mvpTmp[16], mvpM[16];
    MatRotXY(0.5f, rot);
    MatIdent(trl); trl[14] = -4.0f;          /* cube placed at z=-4 */
    MatMul(trl, rot, model);
    LookAt(0.0f, 5.0f, 7.0f,  0.0f, 0.0f, -4.0f, cam);  /* pitched DOWN, sky on top */
    MatMul(cam, model, mvpTmp);
    MatMul(persp, mvpTmp, mvpM);

    cl(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* floor: flat world-space slab (persp*cam only, no model rotation) */
    float floorMVP[16];
    MatMul(persp, cam, floorMVP);
    um(mvp, 1, GL_FALSE, floorMVP);
    bd(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(grid), grid, 0x88E4);
    da(GL_TRIANGLES, 0, gi);

    /* cube: rotate + translate onto the slab */
    um(mvp, 1, GL_FALSE, mvpM);
    bd(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(cube), cube, 0x88E4);
    da(GL_TRIANGLES, 0, 36);
    fi();

    unsigned char* px = malloc((size_t)W * H * 4);
    rp(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);

    /* -- pixel assertions (top row is y=0 in readback, matches PNG export) - */
    int nbg = 0, nred = 0, nblue = 0, ngray = 0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            unsigned char* p = px + ((size_t)y * W + x) * 4;
            if (p[0] == 20 && p[1] == 23 && p[2] == 31) ++nbg;
            else if (p[2] < 60 && p[1] < 60 && p[0] > 200) ++nred;
            else if (p[0] < 60 && p[2] > 200 && p[1] < 60) ++nblue;
            else if (p[0] > 30 && p[0] < 100 && p[1] > 30 && p[1] < 100 &&
                     p[2] > 30 && p[2] < 100) ++ngray;
        }
    CHECK(nbg > 3000, "background (dark clear) present: %d px", nbg);
    CHECK(nred > 300, "cube red face visible in top half: %d px", nred);
    CHECK(nblue > 100, "cube blue face visible: %d px", nblue);
    CHECK(ngray > 3000, "floor grid fills the lower half: %d px", ngray);

    /* write the framebuffer as a PPM (convert with scripts/ppm_render.py) */
    FILE* f = fopen("tests/render3d.ppm", "wb");
    if (f) {
        fprintf(f, "P6\n%d %d\n255\n", W, H);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                unsigned char* p = px + ((size_t)y * W + x) * 4;
                fputc(p[0], f); fputc(p[1], f); fputc(p[2], f);
            }
        fclose(f);
        printf("wrote tests/render3d.ppm (see scripts/ppm_render.py for PNG)\n");
    }

    dlclose(h);
    if (failures == 0) { printf("\nRENDER SMOKE ALL PASSED\n"); return 0; }
    printf("\nRENDER SMOKE FAILED: %d failure(s)\n", failures);
    return 1;
}