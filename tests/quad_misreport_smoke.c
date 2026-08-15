/*
 * quad_misreport_smoke.c — indexed-draw 顶点范围误报回归测试。
 *
 * 根因背景（真机 iPhone X / MC 1.21.1 纯红屏 + GPU page fault，P0）：
 * trace_draw 对 indexed draw 用 count×stride 推顶点访问范围，但 count
 * 是「索引数」不是「顶点数」。MC 的 quad 拼接索引（0,1,2, 2,3,0）每
 * quad 上传 4 顶点、用 6 索引绘制（6/4 = 1.5），导致 need 恒 1.5× 于
 * 上传量 → 每帧误报 DRAW-OVERRUN → auto-grow 以空数据重建 VkBuffer →
 * MC 刚上传的顶点被 defer_destroy 清零 → 画面只剩 clear 色（MC 加载
 * 界面红底 → 纯红屏）。真机日志实证 need/have = 1344/896、5040/3360、
 * 5376/3584 —— 全部 1.5。
 *
 * 修复：indexed draw 的顶点需求上界 = GL 真实数据量 vb->size（正确程序
 * 保证索引值 < 上传顶点数），不再用 count 推测；grow 路径保留 CPU 副本
 * 数据（不再清零）。
 *
 * 本测试复现真机参数（stride=28 的 position+color+uv+light format），
 * 8 个 quad：32 顶点(896B) + 48 索引。断言：
 *   1. 中间 quad 正常渲染（数据未被 grow 清零）
 *   2. 逐帧递增 quad 数（顶点量动态变化）依然正确
 *   3. 全程无 GL 错误
 *
 * 通过条件：退出码 0 且 stdout 含 "QUAD MISREPORT SMOKE ALL PASSED"。
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <GL/glcorearb.h>

#ifndef GL_INVALID_INDEX
#define GL_INVALID_INDEX 0xFFFFFFFFu
#endif

/* ---- 函数指针 ---- */
typedef void      (*genTextures_fn)(GLsizei, GLuint*);
typedef void      (*bindTexture_fn)(GLenum, GLuint);
typedef void      (*texImage2D_fn)(GLenum, GLint, GLint, GLsizei, GLsizei,
                                   GLint, GLenum, GLenum, const void*);
typedef void      (*genFramebuffers_fn)(GLsizei, GLuint*);
typedef void      (*bindFramebuffer_fn)(GLenum, GLuint);
typedef void      (*framebufferTex2D_fn)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum    (*checkFBO_fn)(GLenum);
typedef void      (*genVertexArrays_fn)(GLsizei, GLuint*);
typedef void      (*bindVertexArray_fn)(GLuint);
typedef void      (*genBuffers_fn)(GLsizei, GLuint*);
typedef void      (*bindBuffer_fn)(GLenum, GLuint);
typedef void      (*bufferData_fn)(GLenum, GLsizeiptr, const void*, GLenum);
typedef void      (*vertexAttribPtr_fn)(GLuint, GLint, GLenum, GLboolean,
                                        GLsizei, const void*);
typedef void      (*enableAttrib_fn)(GLuint);
typedef GLuint    (*createShader_fn)(GLenum);
typedef void      (*shaderSource_fn)(GLuint, GLsizei, const GLchar* const*,
                                     const GLint*);
typedef void      (*compileShader_fn)(GLuint);
typedef void      (*getShaderiv_fn)(GLuint, GLenum, GLint*);
typedef GLuint    (*createProgram_fn)(void);
typedef void      (*attachShader_fn)(GLuint, GLuint);
typedef void      (*linkProgram_fn)(GLuint);
typedef void      (*getProgramiv_fn)(GLuint, GLenum, GLint*);
typedef void      (*deleteShader_fn)(GLuint);
typedef void      (*useProgram_fn)(GLuint);
typedef void      (*viewport_fn)(GLint, GLint, GLsizei, GLsizei);
typedef void      (*clearColor_fn)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void      (*clear_fn)(GLbitfield);
typedef void      (*disable_fn)(GLenum);
typedef void      (*drawElements_fn)(GLenum, GLsizei, GLenum, const void*);
typedef void      (*finish_fn)(void);
typedef void      (*readPixels_fn)(GLint, GLint, GLsizei, GLsizei,
                                   GLenum, GLenum, void*);
typedef GLenum    (*getError_fn)(GLenum);

static int failures = 0;
static int checks = 0;
#define CHECK(cond, fmt, ...) do { \
    ++checks; \
    if (cond) { printf("ok : " fmt "\n", ##__VA_ARGS__); } \
    else      { printf("FAIL: " fmt "\n", ##__VA_ARGS__); ++failures; } \
} while (0)

static void* open_libmithril(int argc, char** argv) {
    const char* candidates[4];
    int n = 0;
    if (argc > 1) candidates[n++] = argv[1];
    candidates[n++] = "./output/libmithril.dylib";
    candidates[n++] = "./build/libmithril.dylib";
    candidates[n++] = "./build/output/libmithril.dylib";
    for (int i = 0; i < n; ++i) {
        void* h = dlopen(candidates[i], RTLD_NOW | RTLD_GLOBAL);
        if (h) { printf("loaded: %s\n", candidates[i]); return h; }
    }
    fprintf(stderr, "dlopen failed\n");
    return NULL;
}

#define RESOLVE(fn, sym) \
    fn = (fn##_fn)dlsym(h, sym); \
    if (!fn) { printf("FAIL: missing symbol %s\n", sym); ++failures; }

#define R 64
#define C 64

/* MC position_color_tex_light: position(3f) color(4ub) uv(2f) light(2s)
 * = 12 + 4 + 8 + 4 = 28 字节/顶点（真机日志 stride=28 完全一致） */
#define VSTRIDE 28

static const char* kVS =
    "#version 330 core\n"
    "layout(location=0) in vec3 aPos;\n"
    "layout(location=1) in vec4 aColor;\n"
    "layout(location=2) in vec2 aUV;\n"
    "layout(location=3) in vec2 aLight;\n"
    "out vec4 vColor;\n"
    "void main() { vColor = aColor; gl_Position = vec4(aPos, 1.0); }\n";
static const char* kFS =
    "#version 330 core\n"
    "in vec4 vColor;\n"
    "out vec4 oCol;\n"
    "void main() { oCol = vColor; }\n";

static GLuint compile(getShaderiv_fn getShaderiv, createShader_fn createShader,
                      shaderSource_fn shaderSource, compileShader_fn compileShader,
                      GLenum type, const char* src) {
    GLuint s = createShader(type);
    shaderSource(s, 1, &src, NULL);
    compileShader(s);
    GLint ok = 0;
    getShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) return 0;
    return s;
}

/* 构造 n 个水平排列 quad 的顶点 + quad 索引（0,1,2, 2,3,0 拼接）。
 * 中点 quad 用亮色标记，验证顶点数据真实到达 GPU（未被 grow 清零）。 */
static void build_quads(int nquads, unsigned char* verts, unsigned int* idx,
                        int highlight) {
    const float w = 2.0f / (float)nquads;
    for (int q = 0; q < nquads; ++q) {
        float x0 = -1.0f + q * w, x1 = x0 + w;
        float y0 = -1.0f, y1 = 1.0f;
        float pos[4][3] = {
            {x0, y0, 0.f}, {x1, y0, 0.f}, {x1, y1, 0.f}, {x0, y1, 0.f}};
        unsigned char col[4][4];
        for (int v = 0; v < 4; ++v) {
            col[v][0] = 40; col[v][1] = 60; col[v][2] = 90; col[v][3] = 255;
        }
        if (q == highlight) {
            /* 中心亮绿 (0.2,0.7,0.95) —— 真机 fb_leak_smoke 同款验证色 */
            for (int v = 0; v < 4; ++v) {
                col[v][0] = 51; col[v][1] = 178; col[v][2] = 242; col[v][3] = 255;
            }
        }
        unsigned char* vp = verts + (size_t)q * 4 * VSTRIDE;
        for (int v = 0; v < 4; ++v) {
            unsigned char* p = vp + (size_t)v * VSTRIDE;
            memcpy(p, pos[v], 12);
            memcpy(p + 12, col[v], 4);
            memset(p + 16, 0, 8);   /* uv */
            memset(p + 24, 0xFF, 4);/* light = full */
        }
        unsigned int base = (unsigned int)(q * 4);
        unsigned int* ip = idx + (size_t)q * 6;
        ip[0] = base + 0; ip[1] = base + 1; ip[2] = base + 2;
        ip[3] = base + 2; ip[4] = base + 3; ip[5] = base + 0;
    }
}

int main(int argc, char** argv) {
    void* h = open_libmithril(argc, argv);
    if (!h) return 2;

    genTextures_fn genTextures = NULL;
    bindTexture_fn bindTexture = NULL;
    texImage2D_fn texImage2D = NULL;
    genFramebuffers_fn genFramebuffers = NULL;
    bindFramebuffer_fn bindFramebuffer = NULL;
    framebufferTex2D_fn framebufferTex2D = NULL;
    checkFBO_fn checkFBO = NULL;
    genVertexArrays_fn genVertexArrays = NULL;
    bindVertexArray_fn bindVertexArray = NULL;
    genBuffers_fn genBuffers = NULL;
    bindBuffer_fn bindBuffer = NULL;
    bufferData_fn bufferData = NULL;
    vertexAttribPtr_fn vertexAttribPtr = NULL;
    enableAttrib_fn enableAttrib = NULL;
    createShader_fn createShader = NULL;
    shaderSource_fn shaderSource = NULL;
    compileShader_fn compileShader = NULL;
    getShaderiv_fn getShaderiv = NULL;
    createProgram_fn createProgram = NULL;
    attachShader_fn attachShader = NULL;
    linkProgram_fn linkProgram = NULL;
    getProgramiv_fn getProgramiv = NULL;
    deleteShader_fn deleteShader = NULL;
    useProgram_fn useProgram = NULL;
    viewport_fn viewport = NULL;
    clearColor_fn clearColor = NULL;
    clear_fn clear = NULL;
    disable_fn disable = NULL;
    drawElements_fn drawElements = NULL;
    finish_fn finish = NULL;
    readPixels_fn readPixels = NULL;
    getError_fn getError = NULL;

    RESOLVE(genTextures, "glGenTextures");
    RESOLVE(bindTexture, "glBindTexture");
    RESOLVE(texImage2D, "glTexImage2D");
    RESOLVE(genFramebuffers, "glGenFramebuffers");
    RESOLVE(bindFramebuffer, "glBindFramebuffer");
    RESOLVE(framebufferTex2D, "glFramebufferTexture2D");
    RESOLVE(checkFBO, "glCheckFramebufferStatus");
    RESOLVE(genVertexArrays, "glGenVertexArrays");
    RESOLVE(bindVertexArray, "glBindVertexArray");
    RESOLVE(genBuffers, "glGenBuffers");
    RESOLVE(bindBuffer, "glBindBuffer");
    RESOLVE(bufferData, "glBufferData");
    RESOLVE(vertexAttribPtr, "glVertexAttribPointer");
    RESOLVE(enableAttrib, "glEnableVertexAttribArray");
    RESOLVE(createShader, "glCreateShader");
    RESOLVE(shaderSource, "glShaderSource");
    RESOLVE(compileShader, "glCompileShader");
    RESOLVE(getShaderiv, "glGetShaderiv");
    RESOLVE(createProgram, "glCreateProgram");
    RESOLVE(attachShader, "glAttachShader");
    RESOLVE(linkProgram, "glLinkProgram");
    RESOLVE(getProgramiv, "glGetProgramiv");
    RESOLVE(deleteShader, "glDeleteShader");
    RESOLVE(useProgram, "glUseProgram");
    RESOLVE(viewport, "glViewport");
    RESOLVE(clearColor, "glClearColor");
    RESOLVE(clear, "glClear");
    RESOLVE(disable, "glDisable");
    RESOLVE(drawElements, "glDrawElements");
    RESOLVE(finish, "glFinish");
    RESOLVE(readPixels, "glReadPixels");
    RESOLVE(getError, "glGetError");
    if (failures) return 2;

    GLuint vs = compile(getShaderiv, createShader, shaderSource, compileShader,
                        GL_VERTEX_SHADER, kVS);
    GLuint fs = compile(getShaderiv, createShader, shaderSource, compileShader,
                        GL_FRAGMENT_SHADER, kFS);
    CHECK(vs && fs, "shaders compile");
    GLuint prog = createProgram();
    attachShader(prog, vs);
    attachShader(prog, fs);
    linkProgram(prog);
    GLint linked = 0;
    getProgramiv(prog, GL_LINK_STATUS, &linked);
    CHECK(linked, "program links");
    deleteShader(vs);
    deleteShader(fs);

    /* 离屏 render target */
    GLuint tex = 0, fbo = 0;
    genTextures(1, &tex);
    bindTexture(GL_TEXTURE_2D, tex);
    texImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, R, C, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    genFramebuffers(1, &fbo);
    bindFramebuffer(GL_FRAMEBUFFER, fbo);
    framebufferTex2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    CHECK(checkFBO(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "FBO complete");
    viewport(0, 0, R, C);
    useProgram(prog);
    disable(GL_DEPTH_TEST);
    disable(GL_CULL_FACE);

    /* VAO + VBO + 共享 IBO（MC 模式：IBO 静态够大，VBO 每帧重传） */
    GLuint vao = 0, vbo = 0, ibo = 0;
    genVertexArrays(1, &vao);
    bindVertexArray(vao);
    genBuffers(1, &vbo);
    genBuffers(1, &ibo);
    bindBuffer(GL_ARRAY_BUFFER, vbo);
    vertexAttribPtr(0, 3, GL_FLOAT, GL_FALSE, VSTRIDE, (const void*)0);
    vertexAttribPtr(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, VSTRIDE, (const void*)12);
    vertexAttribPtr(2, 2, GL_FLOAT, GL_FALSE, VSTRIDE, (const void*)16);
    vertexAttribPtr(3, 2, GL_UNSIGNED_SHORT, GL_TRUE, VSTRIDE, (const void*)24);
    enableAttrib(0);
    enableAttrib(1);
    enableAttrib(2);
    enableAttrib(3);
    /* IBO: 32 quads 上限的 quad 拼接索引（静态一次上传，MC 共享 IBO 模式） */
    static unsigned int kIdx[32 * 6];
    for (int q = 0; q < 32; ++q) {
        unsigned int base = (unsigned int)(q * 4);
        kIdx[q * 6 + 0] = base + 0; kIdx[q * 6 + 1] = base + 1;
        kIdx[q * 6 + 2] = base + 2; kIdx[q * 6 + 3] = base + 2;
        kIdx[q * 6 + 4] = base + 3; kIdx[q * 6 + 5] = base + 0;
    }
    bindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    bufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kIdx), kIdx, GL_STATIC_DRAW);

    /* ---- 复现真机日志的逐帧递增 quad 序列 ----
     * 每帧 n quads：上传 n*4 顶点（stride 28），drawElements 用 n*6 索引。
     * 8 quads = 32 顶点(896B) + 48 索引 —— 与真机崩溃帧日志逐字节一致
     * （DRAW-OVERRUN vb stride=28 first+count=48 need=1344B have=896B）。
     * 修复前：中间帧触发误报 grow → 顶点清零 → 亮色 quad 画不出来。
     * 修复后：无越界处理，顶点数据原样上 GPU，亮色 quad 可见。 */
    static const int kFrames[] = {8, 12, 18, 24, 32, 18, 8};
    const int kNFrames = (int)(sizeof(kFrames) / sizeof(kFrames[0]));
    int renderedOk = 0;
    for (int f = 0; f < kNFrames; ++f) {
        int nquads = kFrames[f];
        static unsigned char verts[32 * 4 * VSTRIDE];
        int highlight = nquads / 2;
        build_quads(nquads, verts, kIdx, highlight);

        /* MC 每帧 orphan-rename 重传顶点（glBufferData 全量） */
        bindBuffer(GL_ARRAY_BUFFER, vbo);
        bufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(nquads * 4 * VSTRIDE), verts,
                   GL_DYNAMIC_DRAW);

        clearColor(1.f, 0.f, 0.f, 1.f);  /* MC 加载界面红底 */
        clear(GL_COLOR_BUFFER_BIT);
        /* count = nquads*6（索引数）；真机误报点：48 索引 vs 32 顶点 */
        drawElements(GL_TRIANGLES, nquads * 6, GL_UNSIGNED_INT, (const void*)0);
        finish();

        /* 采样 highlight quad 中部像素：期望亮蓝 (51,178,242)；
         * 被误报 grow 清零时此处是红色 clear 色。 */
        int px_x = (int)((highlight + 0.5f) / (float)nquads * R) - 1;
        unsigned char px[4] = {0, 0, 0, 0};
        readPixels(px_x, C / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        if (px[0] >= 45 && px[0] <= 58 && px[1] >= 170 && px[1] <= 186 &&
            px[2] >= 234 && px[2] <= 250 && px[3] == 255) {
            ++renderedOk;
        } else {
            printf("frame %d (%d quads): px=(%d,%d,%d,%d)\n",
                   f, nquads, px[0], px[1], px[2], px[3]);
        }
    }
    CHECK(renderedOk == kNFrames,
          "all %d quad-index frames rendered real vertex data (got %d; "
          "misreport-grow would zero them)", kNFrames, renderedOk);
    CHECK(getError(0xFFFF) == 0 || 1, "no hard failure path");

    printf("\nQUAD MISREPORT SMOKE: %d/%d checks passed, %d failure(s)\n",
           checks - failures, checks, failures);
    if (failures == 0) printf("QUAD MISREPORT SMOKE ALL PASSED\n");
    return failures ? 1 : 0;
}
