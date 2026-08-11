/*
 * render_smoke.c — Mithril-Wrapper 真正的离屏渲染冒烟测试。
 *
 * 与 gl_smoke.c（纯状态机断言）不同，本测试驱动完整 GL 4.6 Core 渲染管线：
 *   离屏 FBO（纹理 attachment）→ 纹理上传 → shader 编译/链接 → VAO/VBO →
 *   glDrawArrays → glFinish（backend_commit → vkQueueSubmit）→ glReadPixels
 *   读回像素，验证 GPU 真正执行了绘制（而非仅状态机 no-op）。
 *
 * 依据 backend headless 调研结论：
 *   - proc_init() 在无 EGL 时直接 backend_init()，可 headless 创建完整
 *     Vulkan device（Device.cpp:711/1177/1181/1188），无需 surface/swapchain。
 *   - 管线创建用 VK_KHR_dynamic_rendering（Pipeline.cpp:815 renderPass=NULL），
 *     headless 可行，无需 EGL。
 *   - 离屏渲染必须绑定用户 FBO：glBindFramebuffer(0) 在无 swapchain 时没有
 *     color attachment（Framebuffer.cpp:971-977 eglDefaultColor 为 NULL）。
 *     故全程绑定自建纹理 FBO。
 *   - glFinish → backend_end_render_pass + backend_commit → vkQueueSubmit
 *     （CommandStream.cpp:1583），GL 冒烟因此会真正上 GPU。
 *   - glReadPixels → backend_read_pixels（ImageOps.cpp:886 → read_pixels:481），
 *     内部 vkWaitForFences 同步，可确认 GPU 执行完成。
 *
 * 运行（在有 libmithril.dylib 产物的 macOS 原生构建上）：
 *   clang -std=c11 -O0 -Wall -Wextra -I Mithril-Wrapper-cpp/include \
 *         -o tests/render_smoke tests/render_smoke.c -ldl
 *   DYLD_LIBRARY_PATH="$(brew --prefix)/lib" ./tests/render_smoke build/libmithril.dylib
 * 通过条件：退出码 0 且 stdout 含 "RENDER SMOKE ALL PASSED"。
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <GL/glcorearb.h>

/* ---- 依赖的 GL 函数指针 typedef（与 glcorearb.h 签名一致） -------------- */
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
typedef void      (*drawArrays_fn)(GLenum, GLint, GLsizei);
typedef void      (*finish_fn)(void);
typedef void      (*readPixels_fn)(GLint, GLint, GLsizei, GLsizei,
                                   GLenum, GLenum, void*);
typedef GLenum    (*getError_fn)(void);
typedef void      (*getIntegerv_fn)(GLenum, GLint*);
typedef const GLubyte* (*getString_fn)(GLenum);

/* ---- 断言基础设施（Uniaball 风格） -------------------------------------- */
static int failures = 0;
static int checks = 0;
#define CHECK(cond, fmt, ...) do { \
    ++checks; \
    if (cond) { printf("ok : " fmt "\n", ##__VA_ARGS__); } \
    else      { printf("FAIL: " fmt "\n", ##__VA_ARGS__); ++failures; } \
} while (0)

/* ---- dlopen 库路径解析（与 gl_smoke.c 一致） ----------------------------- */
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
    fprintf(stderr, "dlopen failed (tried %d candidates):\n", n);
    for (int i = 0; i < n; ++i) fprintf(stderr, "  %s\n", candidates[i]);
    fprintf(stderr, "  last dlerror: %s\n", dlerror());
    return NULL;
}

#define RESOLVE(fn, sym) \
    fn = (fn##_fn)dlsym(h, sym); \
    if (!fn) { printf("FAIL: missing symbol %s\n", sym); ++failures; }

/* 离屏渲染尺寸 */
#define R 64   /* render width  */
#define C 64   /* render height */

int main(int argc, char** argv) {
    void* h = open_libmithril(argc, argv);
    if (!h) return 2;

    genTextures_fn        genTextures        = NULL;
    bindTexture_fn        bindTexture        = NULL;
    texImage2D_fn         texImage2D         = NULL;
    genFramebuffers_fn    genFramebuffers    = NULL;
    bindFramebuffer_fn    bindFramebuffer    = NULL;
    framebufferTex2D_fn framebufferTex2D = NULL;
    checkFBO_fn checkFBO       = NULL;
    genVertexArrays_fn    genVertexArrays    = NULL;
    bindVertexArray_fn    bindVertexArray    = NULL;
    genBuffers_fn         genBuffers         = NULL;
    bindBuffer_fn         bindBuffer         = NULL;
    bufferData_fn         bufferData         = NULL;
    vertexAttribPtr_fn vertexAttribPtr   = NULL;
    enableAttrib_fn enableAttrib  = NULL;
    createShader_fn       createShader       = NULL;
    shaderSource_fn       shaderSource       = NULL;
    compileShader_fn      compileShader      = NULL;
    getShaderiv_fn        getShaderiv        = NULL;
    createProgram_fn      createProgram      = NULL;
    attachShader_fn       attachShader       = NULL;
    linkProgram_fn        linkProgram        = NULL;
    getProgramiv_fn       getProgramiv       = NULL;
    deleteShader_fn       deleteShader       = NULL;
    useProgram_fn         useProgram         = NULL;
    viewport_fn           viewport           = NULL;
    clearColor_fn         clearColor         = NULL;
    clear_fn              clear              = NULL;
    drawArrays_fn         drawArrays         = NULL;
    finish_fn             finish             = NULL;
    readPixels_fn         readPixels         = NULL;
    getError_fn           getError           = NULL;
    getIntegerv_fn        getIntegerv        = NULL;
    getString_fn          getString          = NULL;

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
    RESOLVE(drawArrays, "glDrawArrays");
    RESOLVE(finish, "glFinish");
    RESOLVE(readPixels, "glReadPixels");
    RESOLVE(getError, "glGetError");
    RESOLVE(getIntegerv, "glGetIntegerv");
    RESOLVE(getString, "glGetString");
    if (failures) { printf("RENDER SMOKE FAILED (missing symbols)\n"); dlclose(h); return 1; }

    /* ---- 版本（走 backend 起来后的 glGetIntegerv，验证偏移修复） ---------- */
    GLint major = 0, minor = 0;
    getIntegerv(GL_MAJOR_VERSION, &major);
    getIntegerv(GL_MINOR_VERSION, &minor);
    CHECK(major == 4 && minor == 6,
          "GL version %d.%d (backend-up, glGetIntegerv un-hijacked)", major, minor);
    const char* ver = (const char*)getString(GL_VERSION);
    CHECK(ver && strstr(ver, "4.6"), "glGetString(GL_VERSION): %s", ver ? ver : "(null)");
    if (getError() != GL_NO_ERROR) { printf("FAIL: GL error before setup\n"); ++failures; }

    /* ---- 离屏 FBO：RGBA8 纹理作为 color attachment ------------------------ */
    GLuint colorTex = 0, fbo = 0;
    genTextures(1, &colorTex);
    CHECK(colorTex != 0, "genTextures allocated color texture (%u)", colorTex);
    bindTexture(GL_TEXTURE_2D, colorTex);
    texImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, R, C, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    genFramebuffers(1, &fbo);
    bindFramebuffer(GL_FRAMEBUFFER, fbo);
    framebufferTex2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);
    GLenum fboStatus = checkFBO(GL_FRAMEBUFFER);
    CHECK(fboStatus == GL_FRAMEBUFFER_COMPLETE,
          "offscreen FBO complete (status=0x%x)", fboStatus);

    viewport(0, 0, R, C);
    clearColor(0.0f, 0.0f, 0.0f, 0.0f);
    clear(GL_COLOR_BUFFER_BIT);
    CHECK(getError() == GL_NO_ERROR, "clear on offscreen FBO leaves no error");

    /* ---- 控制组诊断：clear 到已知非零颜色并读回，隔离 readback/clear 路径 ----
     * 若此读回返回 (25,51,76,255)（=0.1/0.2/0.3*255），说明 glClear+glReadPixels
     * 整条 GPU 写读路径正常，后续 draw 读回全黑就指向 draw 本身；若此读回也是全 0，
     * 说明 clear/readback 或离屏渲染目标本身有问题。 */
    clearColor(0.1f, 0.2f, 0.3f, 1.0f);
    clear(GL_COLOR_BUFFER_BIT);
    finish();  /* 提交 clear 的 command buffer，确保 GPU 执行完毕 */
    unsigned char ctrl[4] = {0,0,0,0};
    readPixels(R / 2, C / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, ctrl);
    CHECK(ctrl[0] == 25 && ctrl[1] == 51 && ctrl[2] == 76 && ctrl[3] == 255,
          "CONTROL clear-to-(0.1,0.2,0.3,1.0) readback=(%d,%d,%d,%d) — clear+readback path %s",
          ctrl[0], ctrl[1], ctrl[2], ctrl[3],
          (ctrl[0]==25&&ctrl[1]==51&&ctrl[2]==76&&ctrl[3]==255) ? "OK" : "BROKEN");
    /* 恢复黑色背景再继续 draw 测试 */
    clearColor(0.0f, 0.0f, 0.0f, 0.0f);
    clear(GL_COLOR_BUFFER_BIT);
    finish();
    CHECK(getError() == GL_NO_ERROR, "control clear/readback leaves no error");

    /* ---- shader 编译 + program 链接（glslang -> SPIR-V -> pipeline）-------- */
    const char* vsSrc = "#version 330 core\n"
                        "layout(location=0) in vec2 aPos;\n"
                        "void main(){ gl_Position = vec4(aPos, 0.0, 1.0); }\n";
    const char* fsSrc = "#version 330 core\n"
                        "out vec4 fragColor;\n"
                        "void main(){ fragColor = vec4(1.0, 0.0, 0.0, 1.0); }\n";
    GLuint vs = createShader(GL_VERTEX_SHADER);
    GLuint fs = createShader(GL_FRAGMENT_SHADER);
    shaderSource(vs, 1, &vsSrc, NULL);
    shaderSource(fs, 1, &fsSrc, NULL);
    compileShader(vs);
    compileShader(fs);
    GLint vsOk = 0, fsOk = 0;
    getShaderiv(vs, GL_COMPILE_STATUS, &vsOk);
    getShaderiv(fs, GL_COMPILE_STATUS, &fsOk);
    CHECK(vsOk == GL_TRUE, "vertex shader compiled");
    CHECK(fsOk == GL_TRUE, "fragment shader compiled");
    GLuint prog = createProgram();
    attachShader(prog, vs);
    attachShader(prog, fs);
    linkProgram(prog);
    GLint linkOk = 0;
    getProgramiv(prog, GL_LINK_STATUS, &linkOk);
    CHECK(linkOk == GL_TRUE, "program linked (GL_LINK_STATUS=%d)", linkOk);
    useProgram(prog);
    deleteShader(vs);
    deleteShader(fs);
    CHECK(getError() == GL_NO_ERROR, "shader compile/link leaves no error");

    /* ---- VAO + VBO：全屏三角形（三个顶点覆盖中心区） ---------------------- */
    GLuint vao = 0, vbo = 0;
    const GLfloat verts[6] = {
        -1.0f, -1.0f,
         3.0f, -1.0f,
        -1.0f,  3.0f,
    };
    genVertexArrays(1, &vao);
    bindVertexArray(vao);
    genBuffers(1, &vbo);
    bindBuffer(GL_ARRAY_BUFFER, vbo);
    bufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    vertexAttribPtr(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), (const void*)0);
    enableAttrib(0);
    CHECK(getError() == GL_NO_ERROR, "VAO/VBO setup leaves no error");

    /* ---- 绘制 + 提交（glFinish -> backend_commit -> vkQueueSubmit）--------- */
    drawArrays(GL_TRIANGLES, 0, 3);
    CHECK(getError() == GL_NO_ERROR, "glDrawArrays leaves no error");
    finish();
    CHECK(getError() == GL_NO_ERROR, "glFinish leaves no error");

    /* ---- 读回像素，验证 GPU 真正执行绘制 ---------------------------------- */
    /* 全屏三角形覆盖整个 viewport：中心应是红色，四角也应是红色（三角形盖满）。 */
    unsigned char px[4] = {0,0,0,0};
    readPixels(R / 2, C / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    CHECK(px[0] > 128 && px[3] > 128,
          "center pixel is red (r=%d g=%d b=%d a=%d) — GPU draw executed", px[0], px[1], px[2], px[3]);
    readPixels(1, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    CHECK(px[0] > 128 && px[3] > 128,
          "corner pixel is red (r=%d g=%d b=%d a=%d) — full-screen triangle rasterized",
          px[0], px[1], px[2], px[3]);
    CHECK(getError() == GL_NO_ERROR, "glReadPixels leaves no error");

    dlclose(h);

    printf("\nRENDER SMOKE: %d/%d checks passed, %d failure(s)\n", checks - failures, checks, failures);
    if (failures == 0) {
        printf("RENDER SMOKE ALL PASSED\n");
        return 0;
    }
    printf("RENDER SMOKE FAILED\n");
    return 1;
}