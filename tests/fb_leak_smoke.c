/*
 * fb_leak_smoke.c — framebuffer/renderpass 缓存生命周期压力测试。
 *
 * 根因背景（iOS OOM → device-lost，P0）：framebuffer cache 以
 * (renderPass, views, extent) 为键、无淘汰。每次纹理 re-spec（MC 的
 * atlas rebuild / 资源包重载 / 维度切换）都会产生新的 VkImageView，
 * 从而键入新的 FramebufferKey 条目；旧 view 被 deferred-destroy 后，
 * 其键下的 VkFramebuffer 条目永远留在 cache 里 —— 每个 VkFramebuffer
 * 都包着一个 Metal 对象，长会话累积数千个，最终把显存吃穿。
 *
 * 修复：retire_framebuffers_referencing(view) —— view 进 disposal queue
 * 前，先把引用它的 framebuffer 条目从 cache 逐出，VkFramebuffer 走同
 * 一条延迟销毁路径（在途 command buffer 可能还握着它的
 * vkCmdBeginRenderPass）。
 *
 * 本测试模拟 MC 的循环模式，反复执行：
 *   创建纹理(re-spec) → 建 FBO 附着 → 离屏绘制 → 读回验证 →
 *   删 FBO → 删纹理（触发 retire + deferred destroy）
 * 300 轮后验证渲染路径依然正确（retire 没有误杀活着的 framebuffer）。
 *
 * 通过条件：退出码 0 且 stdout 含 "FB LEAK SMOKE ALL PASSED"。
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
typedef void      (*deleteTextures_fn)(GLsizei, const GLuint*);
typedef void      (*bindTexture_fn)(GLenum, GLuint);
typedef void      (*texImage2D_fn)(GLenum, GLint, GLint, GLsizei, GLsizei,
                                   GLint, GLenum, GLenum, const void*);
typedef void      (*genFramebuffers_fn)(GLsizei, GLuint*);
typedef void      (*deleteFramebuffers_fn)(GLsizei, const GLuint*);
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
typedef void      (*drawArrays_fn)(GLenum, GLint, GLsizei);
typedef void      (*finish_fn)(void);
typedef void      (*readPixels_fn)(GLint, GLint, GLsizei, GLsizei,
                                   GLenum, GLenum, void*);
typedef GLenum    (*getError_fn)(void);

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

static const char* kVS =
    "#version 330 core\n"
    "layout(location=0) in vec2 aPos;\n"
    "void main() { gl_Position = vec4(aPos, 0.0, 1.0); }\n";
static const char* kFS =
    "#version 330 core\n"
    "out vec4 oCol;\n"
    "void main() { oCol = vec4(0.2, 0.7, 0.95, 1.0); }\n";

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

int main(int argc, char** argv) {
    void* h = open_libmithril(argc, argv);
    if (!h) return 2;

    genTextures_fn genTextures = NULL;
    deleteTextures_fn deleteTextures = NULL;
    bindTexture_fn bindTexture = NULL;
    texImage2D_fn texImage2D = NULL;
    genFramebuffers_fn genFramebuffers = NULL;
    deleteFramebuffers_fn deleteFramebuffers = NULL;
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
    drawArrays_fn drawArrays = NULL;
    finish_fn finish = NULL;
    readPixels_fn readPixels = NULL;
    getError_fn getError = NULL;

    RESOLVE(genTextures, "glGenTextures");
    RESOLVE(deleteTextures, "glDeleteTextures");
    RESOLVE(bindTexture, "glBindTexture");
    RESOLVE(texImage2D, "glTexImage2D");
    RESOLVE(genFramebuffers, "glGenFramebuffers");
    RESOLVE(deleteFramebuffers, "glDeleteFramebuffers");
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
    RESOLVE(drawArrays, "glDrawArrays");
    RESOLVE(finish, "glFinish");
    RESOLVE(readPixels, "glReadPixels");
    RESOLVE(getError, "glGetError");
    if (failures) return 2;

    /* 静态 pipeline：一个 program + VAO/VBO，跨全部迭代复用 */
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

    GLuint vao = 0, vbo = 0;
    genVertexArrays(1, &vao);
    genBuffers(1, &vbo);
    bindBuffer(GL_ARRAY_BUFFER, vbo);
    /* 超大三角形覆盖整个 render target（与 render_smoke 的验证几何一致） */
    static const float kTri[6] = {
        -1.0f, -1.0f,
         3.0f, -1.0f,
        -1.0f,  3.0f,
    };
    bufferData(GL_ARRAY_BUFFER, sizeof(kTri), kTri, GL_STATIC_DRAW);
    vertexAttribPtr(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), (const void*)0);
    enableAttrib(0);
    disable(GL_DEPTH_TEST);
    disable(GL_CULL_FACE);

    /* ---- 压力循环：MC 资源包重载模式 ----
     * 每轮一个全新纹理（= 新 VkImageView = 新 FramebufferKey），
     * 绘制后立即整体删除。修复前 cache 无界累积；修复后每轮 retire。 */
    const int kIters = 300;
    int renderedOk = 0;
    for (int i = 0; i < kIters; ++i) {
        GLuint tex = 0, fbo = 0;
        genTextures(1, &tex);
        bindTexture(GL_TEXTURE_2D, tex);
        texImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, R, C, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

        genFramebuffers(1, &fbo);
        bindFramebuffer(GL_FRAMEBUFFER, fbo);
        framebufferTex2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        if (checkFBO(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            CHECK(0, "iter %d: FBO incomplete", i);
            break;
        }

        viewport(0, 0, R, C);
        useProgram(prog);
        clearColor(1.f, 0.f, 0.f, 1.f);
        clear(GL_COLOR_BUFFER_BIT);
        drawArrays(GL_TRIANGLES, 0, 3);
        finish();

        unsigned char px[4] = {0, 0, 0, 0};
        readPixels(R / 2, C / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        /* FS 输出 (0.2, 0.7, 0.95, 1.0) → uint8 ≈ (51, 178, 242, 255) */
        if (px[0] >= 45 && px[0] <= 58 && px[1] >= 170 && px[1] <= 186 &&
            px[2] >= 234 && px[2] <= 250 && px[3] == 255) ++renderedOk;

        /* 删除顺序与 MC 一致：FBO 先走，纹理随后（触发 view 的
         * defer_destroy → retire_framebuffers_referencing） */
        bindFramebuffer(GL_FRAMEBUFFER, 0);
        deleteFramebuffers(1, &fbo);
        deleteTextures(1, &tex);

        if ((i & 7) == 7) finish();  /* 周期性排空 disposal queue */
    }
    CHECK(renderedOk == kIters, "all %d re-spec iterations rendered correctly (got %d)",
          kIters, renderedOk);

    /* retire 不能误杀：全新 FBO 在大量 retire 之后仍须可用 */
    GLuint tex = 0, fbo = 0;
    genTextures(1, &tex);
    bindTexture(GL_TEXTURE_2D, tex);
    texImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, R, C, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    genFramebuffers(1, &fbo);
    bindFramebuffer(GL_FRAMEBUFFER, fbo);
    framebufferTex2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    CHECK(checkFBO(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
          "post-stress FBO complete");
    viewport(0, 0, R, C);
    useProgram(prog);
    clearColor(1.f, 0.f, 0.f, 1.f);
    clear(GL_COLOR_BUFFER_BIT);
    drawArrays(GL_TRIANGLES, 0, 3);
    finish();
    unsigned char px[4] = {0, 0, 0, 0};
    readPixels(R / 2, C / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    CHECK(px[0] >= 45 && px[0] <= 58 && px[1] >= 170 && px[1] <= 186 &&
          px[2] >= 234 && px[2] <= 250 && px[3] == 255,
          "post-stress render correct (rgba=%u,%u,%u,%u)", px[0], px[1], px[2], px[3]);

    while (getError() != GL_NO_ERROR) {}
    printf("checks=%d failures=%d\n", checks, failures);
    if (failures) { printf("FB LEAK SMOKE FAILED\n"); return 1; }
    printf("FB LEAK SMOKE ALL PASSED\n");
    return 0;
}
