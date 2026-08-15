/*
 * query_sync_smoke.c — GL 查询对象 + 同步对象的真 GPU 冒烟测试。
 *
 * 验证 VkQueryPool 后端（Queries.cpp / Stubs.cpp）与提交序号 fence 后端
 * （Device.cpp / Drawing.cpp）的真实行为，而非状态机 no-op：
 *
 *   occlusion query   : glBeginQuery(GL_SAMPLES_PASSED) 环绕全屏 draw，
 *                       glFinish 后结果 == R*C（精确样本计数）。
 *   any samples passed: 结果布尔化（0/1）。
 *   primitives gen    : 软件图元计数（Metal 无管线统计查询）精确 == 图元数。
 *   timestamp counter : glQueryCounter(GL_TIMESTAMP) 单调递增 > 0。
 *   time elapsed      : begin/end 差值 > 0（或 CPU 时钟回退路径 > 0）。
 *   counter bits      : glGetQueryiv(GL_SAMPLES_PASSED, GL_QUERY_COUNTER_BITS) != 0。
 *   fence sync        : glFenceSync → glClientWaitSync(FLUSH) →
 *                       ALREADY_SIGNALED/CONDITION_SATISFIED →
 *                       glGetSynciv(GL_SYNC_STATUS) == GL_SIGNALED（Khronos 值
 *                       0x9114/0x9119，非本地头错误值）。
 *   error paths       : 坏 condition / 删除后 wait / NULL sync。
 *
 * 运行（Linux headless + lavapipe，或 macOS MoltenVK）：
 *   clang -std=c11 -O0 -Wall -Wextra -I Mithril-Wrapper-cpp/include \
 *         -o tests/query_sync_smoke tests/query_sync_smoke.c -ldl
 *   LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libvulkan.so.1 \
 *     ./tests/query_sync_smoke build-headless/libmithril.so
 * 通过条件：退出码 0 且 stdout 含 "QUERY SYNC SMOKE ALL PASSED"。
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <GL/glcorearb.h>

/* Khronos 标准枚举（本项目极简 glcorearb.h 缺失，值与官方 registry 一致） */
#ifndef GL_SAMPLES_PASSED
#define GL_SAMPLES_PASSED 0x8914
#endif
#ifndef GL_ANY_SAMPLES_PASSED
#define GL_ANY_SAMPLES_PASSED 0x8C2F
#endif
#ifndef GL_PRIMITIVES_GENERATED
#define GL_PRIMITIVES_GENERATED 0x8C87
#endif
#ifndef GL_TIME_ELAPSED
#define GL_TIME_ELAPSED 0x88BF
#endif
#ifndef GL_TIMESTAMP
#define GL_TIMESTAMP 0x8E28
#endif
#ifndef GL_QUERY_COUNTER_BITS
#define GL_QUERY_COUNTER_BITS 0x8864
#endif
#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT 0x8866
#endif
#ifndef GL_QUERY_RESULT_AVAILABLE
#define GL_QUERY_RESULT_AVAILABLE 0x8867
#endif
#ifndef GL_CURRENT_QUERY
#define GL_CURRENT_QUERY 0x8865
#endif
#ifndef GL_SYNC_FENCE
#define GL_SYNC_FENCE 0x9116
#endif
#ifndef GL_SYNC_GPU_COMMANDS_COMPLETE
#define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#endif
#ifndef GL_UNSIGNALED
#define GL_UNSIGNALED 0x9118
#endif
#ifndef GL_SIGNALED
#define GL_SIGNALED 0x9119
#endif
#ifndef GL_OBJECT_TYPE
#define GL_OBJECT_TYPE 0x9112
#endif
#ifndef GL_SYNC_CONDITION
#define GL_SYNC_CONDITION 0x9113
#endif
#ifndef GL_SYNC_STATUS
#define GL_SYNC_STATUS 0x9114
#endif
#ifndef GL_SYNC_FLAGS
#define GL_SYNC_FLAGS 0x9115
#endif
#ifndef GL_ALREADY_SIGNALED
#define GL_ALREADY_SIGNALED 0x911A
#endif
#ifndef GL_TIMEOUT_EXPIRED
#define GL_TIMEOUT_EXPIRED 0x911B
#endif
#ifndef GL_CONDITION_SATISFIED
#define GL_CONDITION_SATISFIED 0x911C
#endif
#ifndef GL_WAIT_FAILED
#define GL_WAIT_FAILED 0x911D
#endif
#ifndef GL_SYNC_FLUSH_COMMANDS_BIT
#define GL_SYNC_FLUSH_COMMANDS_BIT 0x00000001
#endif

/* ---- 函数指针 typedef --------------------------------------------------- */
typedef void      (*genQueries_fn)(GLsizei, GLuint*);
typedef void      (*deleteQueries_fn)(GLsizei, const GLuint*);
typedef void      (*beginQuery_fn)(GLenum, GLuint);
typedef void      (*endQuery_fn)(GLenum);
typedef void      (*getQueryiv_fn)(GLenum, GLenum, GLint*);
typedef void      (*getQueryObjectuiv_fn)(GLuint, GLenum, GLuint*);
typedef void      (*getQueryObjectui64v_fn)(GLuint, GLenum, GLuint64*);
typedef void      (*queryCounter_fn)(GLuint, GLenum);
typedef void      (*getInteger64v_fn)(GLenum, GLint64*);
typedef GLsync    (*fenceSync_fn)(GLenum, GLbitfield);
typedef void      (*deleteSync_fn)(GLsync);
typedef GLenum    (*clientWaitSync_fn)(GLsync, GLbitfield, GLuint64);
typedef void      (*waitSync_fn)(GLsync, GLbitfield, GLuint64);
typedef GLboolean (*isSync_fn)(GLsync);
typedef void      (*getSynciv_fn)(GLsync, GLenum, GLsizei, GLsizei*, GLint*);
/* 渲染引导 */
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
typedef void      (*useProgram_fn)(GLuint);
typedef void      (*viewport_fn)(GLint, GLint, GLsizei, GLsizei);
typedef void      (*drawArrays_fn)(GLenum, GLint, GLsizei);
typedef void      (*finish_fn)(void);
typedef GLenum    (*getError_fn)(void);
typedef void      (*getIntegerv_fn)(GLenum, GLint*);

/* ---- 断言 --------------------------------------------------------------- */
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
    fprintf(stderr, "dlopen failed (tried %d candidates)\n  last dlerror: %s\n",
            n, dlerror());
    return NULL;
}

#define RESOLVE(fn, sym) \
    fn = (fn##_fn)dlsym(h, sym); \
    if (!fn) { printf("FAIL: missing symbol %s\n", sym); ++failures; }

#define R 64
#define C 64

int main(int argc, char** argv) {
    void* h = open_libmithril(argc, argv);
    if (!h) return 2;

    genQueries_fn          genQueries          = NULL;
    deleteQueries_fn       deleteQueries       = NULL;
    beginQuery_fn          beginQuery          = NULL;
    endQuery_fn            endQuery            = NULL;
    getQueryiv_fn          getQueryiv          = NULL;
    getQueryObjectuiv_fn   getQueryObjectuiv   = NULL;
    getQueryObjectui64v_fn getQueryObjectui64v = NULL;
    queryCounter_fn        queryCounter        = NULL;
    getInteger64v_fn       getInteger64v       = NULL;
    fenceSync_fn           fenceSync           = NULL;
    deleteSync_fn          deleteSync          = NULL;
    clientWaitSync_fn      clientWaitSync      = NULL;
    waitSync_fn            waitSync            = NULL;
    isSync_fn              isSync              = NULL;
    getSynciv_fn           getSynciv           = NULL;
    genTextures_fn         genTextures         = NULL;
    bindTexture_fn         bindTexture         = NULL;
    texImage2D_fn          texImage2D          = NULL;
    genFramebuffers_fn     genFramebuffers     = NULL;
    bindFramebuffer_fn     bindFramebuffer     = NULL;
    framebufferTex2D_fn    framebufferTex2D    = NULL;
    checkFBO_fn            checkFBO            = NULL;
    genVertexArrays_fn     genVertexArrays     = NULL;
    bindVertexArray_fn     bindVertexArray     = NULL;
    genBuffers_fn          genBuffers          = NULL;
    bindBuffer_fn          bindBuffer          = NULL;
    bufferData_fn          bufferData          = NULL;
    vertexAttribPtr_fn     vertexAttribPtr     = NULL;
    enableAttrib_fn        enableAttrib        = NULL;
    createShader_fn        createShader        = NULL;
    shaderSource_fn        shaderSource        = NULL;
    compileShader_fn       compileShader       = NULL;
    getShaderiv_fn         getShaderiv         = NULL;
    createProgram_fn       createProgram       = NULL;
    attachShader_fn        attachShader        = NULL;
    linkProgram_fn         linkProgram         = NULL;
    getProgramiv_fn        getProgramiv        = NULL;
    useProgram_fn          useProgram          = NULL;
    viewport_fn            viewport            = NULL;
    drawArrays_fn          drawArrays          = NULL;
    finish_fn              finish              = NULL;
    getError_fn            getError            = NULL;
    getIntegerv_fn         getIntegerv         = NULL;

    RESOLVE(genQueries,          "glGenQueries");
    RESOLVE(deleteQueries,       "glDeleteQueries");
    RESOLVE(beginQuery,          "glBeginQuery");
    RESOLVE(endQuery,            "glEndQuery");
    RESOLVE(getQueryiv,          "glGetQueryiv");
    RESOLVE(getQueryObjectuiv,   "glGetQueryObjectuiv");
    RESOLVE(getQueryObjectui64v, "glGetQueryObjectui64v");
    RESOLVE(queryCounter,        "glQueryCounter");
    RESOLVE(getInteger64v,       "glGetInteger64v");
    RESOLVE(fenceSync,           "glFenceSync");
    RESOLVE(deleteSync,          "glDeleteSync");
    RESOLVE(clientWaitSync,      "glClientWaitSync");
    RESOLVE(waitSync,            "glWaitSync");
    RESOLVE(isSync,              "glIsSync");
    RESOLVE(getSynciv,           "glGetSynciv");
    RESOLVE(genTextures,         "glGenTextures");
    RESOLVE(bindTexture,         "glBindTexture");
    RESOLVE(texImage2D,          "glTexImage2D");
    RESOLVE(genFramebuffers,     "glGenFramebuffers");
    RESOLVE(bindFramebuffer,     "glBindFramebuffer");
    RESOLVE(framebufferTex2D,    "glFramebufferTexture2D");
    RESOLVE(checkFBO,            "glCheckFramebufferStatus");
    RESOLVE(genVertexArrays,     "glGenVertexArrays");
    RESOLVE(bindVertexArray,     "glBindVertexArray");
    RESOLVE(genBuffers,          "glGenBuffers");
    RESOLVE(bindBuffer,          "glBindBuffer");
    RESOLVE(bufferData,          "glBufferData");
    RESOLVE(vertexAttribPtr,     "glVertexAttribPointer");
    RESOLVE(enableAttrib,        "glEnableVertexAttribArray");
    RESOLVE(createShader,        "glCreateShader");
    RESOLVE(shaderSource,        "glShaderSource");
    RESOLVE(compileShader,       "glCompileShader");
    RESOLVE(getShaderiv,         "glGetShaderiv");
    RESOLVE(createProgram,       "glCreateProgram");
    RESOLVE(attachShader,        "glAttachShader");
    RESOLVE(linkProgram,         "glLinkProgram");
    RESOLVE(getProgramiv,        "glGetProgramiv");
    RESOLVE(useProgram,          "glUseProgram");
    RESOLVE(viewport,            "glViewport");
    RESOLVE(drawArrays,          "glDrawArrays");
    RESOLVE(finish,              "glFinish");
    RESOLVE(getError,            "glGetError");
    RESOLVE(getIntegerv,         "glGetIntegerv");
    if (failures) { printf("QUERY SYNC SMOKE FAILED (missing symbols)\n"); dlclose(h); return 1; }

    /* ---- 离屏渲染引导：FBO + 红色 shader + 全屏大三角 -------------------- */
    GLuint colorTex = 0, fbo = 0;
    genTextures(1, &colorTex);
    bindTexture(GL_TEXTURE_2D, colorTex);
    texImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, R, C, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    genFramebuffers(1, &fbo);
    bindFramebuffer(GL_FRAMEBUFFER, fbo);
    framebufferTex2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);
    CHECK(checkFBO(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "offscreen FBO complete");

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
    GLint ok = 0;
    getShaderiv(vs, GL_COMPILE_STATUS, &ok);
    CHECK(ok, "vertex shader compiled");
    getShaderiv(fs, GL_COMPILE_STATUS, &ok);
    CHECK(ok, "fragment shader compiled");
    GLuint prog = createProgram();
    attachShader(prog, vs);
    attachShader(prog, fs);
    linkProgram(prog);
    getProgramiv(prog, GL_LINK_STATUS, &ok);
    CHECK(ok, "program linked");
    useProgram(prog);

    /* 覆盖整个视口的大三角（(-1,-1) (3,-1) (-1,3)） */
    GLuint vao = 0, vbo = 0;
    const GLfloat verts[6] = { -1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f };
    genVertexArrays(1, &vao);
    bindVertexArray(vao);
    genBuffers(1, &vbo);
    bindBuffer(GL_ARRAY_BUFFER, vbo);
    bufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    vertexAttribPtr(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(GLfloat), (const void*)0);
    enableAttrib(0);
    viewport(0, 0, R, C);
    while (getError() != GL_NO_ERROR) {}  /* 排空错误队列（FIFO 残留会污染断言） */

    /* ---- 0. 验证 draw 真的光栅化（读回中心像素） -------------------------- */
    drawArrays(GL_TRIANGLES, 0, 3);
    finish();
    unsigned char px[4] = {0,0,0,0};
    {
        extern void* dlsym(void*, const char*);
        typedef void (*readPixels_t)(GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,void*);
        readPixels_t rp = (readPixels_t)dlsym(h, "glReadPixels");
        if (rp) rp(R/2, C/2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    }
    CHECK(px[0] > 128 && px[3] > 128,
          "prerequisite: fullscreen triangle rasterized (r=%d a=%d)", px[0], px[3]);

    /* ---- 1. GL_SAMPLES_PASSED：精确样本计数 ------------------------------ */
    GLint counterBits = 0;
    getQueryiv(GL_SAMPLES_PASSED, GL_QUERY_COUNTER_BITS, &counterBits);
    CHECK(counterBits > 0, "GL_QUERY_COUNTER_BITS(SAMPLES_PASSED)=%d", counterBits);

    GLuint qOcc = 0;
    genQueries(1, &qOcc);
    CHECK(qOcc != 0, "glGenQueries allocated %u", qOcc);
    beginQuery(GL_SAMPLES_PASSED, qOcc);
    drawArrays(GL_TRIANGLES, 0, 3);
    endQuery(GL_SAMPLES_PASSED);
    finish();  /* 确保 GPU 完成，结果必须可读 */
    GLuint samples = 0, avail = 0;
    getQueryObjectuiv(qOcc, GL_QUERY_RESULT, &samples);
    getQueryObjectuiv(qOcc, GL_QUERY_RESULT_AVAILABLE, &avail);
    CHECK(avail == GL_TRUE, "occlusion result available after glFinish");
    CHECK(samples == (GLuint)(R * C),
          "GL_SAMPLES_PASSED exact count=%u (expect %d, fullscreen 1x MSAA)",
          samples, R * C);

    /* ---- 2. GL_ANY_SAMPLES_PASSED：布尔化 ------------------------------- */
    GLuint qAny = 0;
    genQueries(1, &qAny);
    beginQuery(GL_ANY_SAMPLES_PASSED, qAny);
    drawArrays(GL_TRIANGLES, 0, 3);
    endQuery(GL_ANY_SAMPLES_PASSED);
    finish();
    GLuint anyRes = 0;
    getQueryObjectuiv(qAny, GL_QUERY_RESULT, &anyRes);
    CHECK(anyRes == 1, "GL_ANY_SAMPLES_PASSED booleanized=%u (expect 1)", anyRes);

    /* ---- 3. GL_PRIMITIVES_GENERATED：软件图元计数 ------------------------ */
    GLuint qPrim = 0;
    genQueries(1, &qPrim);
    beginQuery(GL_PRIMITIVES_GENERATED, qPrim);
    drawArrays(GL_TRIANGLES, 0, 3);   /* 1 prim */
    drawArrays(GL_TRIANGLES, 0, 6);   /* 2 prims */
    drawArrays(GL_POINTS, 0, 4);      /* 4 prims */
    endQuery(GL_PRIMITIVES_GENERATED);
    GLuint prims = 0;
    getQueryObjectuiv(qPrim, GL_QUERY_RESULT, &prims);
    CHECK(prims == 7, "GL_PRIMITIVES_GENERATED=%u (expect 7: 1+2+4, software count)",
          prims);

    /* ---- 4. glQueryCounter(GL_TIMESTAMP)：单调递增 ------------------------ */
    GLuint qT0 = 0, qT1 = 0;
    genQueries(1, &qT0);
    genQueries(1, &qT1);
    queryCounter(qT0, GL_TIMESTAMP);
    finish();
    queryCounter(qT1, GL_TIMESTAMP);
    finish();
    GLuint64 t0 = 0, t1 = 0;
    getQueryObjectui64v(qT0, GL_QUERY_RESULT, &t0);
    getQueryObjectui64v(qT1, GL_QUERY_RESULT, &t1);
    CHECK(t0 > 0, "glQueryCounter t0=%llu > 0 (GPU or CPU-clock fallback)",
          (unsigned long long)t0);
    CHECK(t1 >= t0, "timestamps monotonic t1(%llu) >= t0(%llu)",
          (unsigned long long)t1, (unsigned long long)t0);

    /* ---- 5. GL_TIME_ELAPSED > 0 ------------------------------------------ */
    GLuint qEl = 0;
    genQueries(1, &qEl);
    beginQuery(GL_TIME_ELAPSED, qEl);
    drawArrays(GL_TRIANGLES, 0, 3);
    endQuery(GL_TIME_ELAPSED);
    finish();
    GLuint64 elapsed = 0;
    getQueryObjectui64v(qEl, GL_QUERY_RESULT, &elapsed);
    CHECK(elapsed > 0, "GL_TIME_ELAPSED=%llu ns > 0", (unsigned long long)elapsed);

    /* ---- 6. 当前查询槽 + 错误路径 ---------------------------------------- */
    while (getError() != GL_NO_ERROR) {}
    GLint curQ = 0;
    getQueryiv(GL_SAMPLES_PASSED, GL_CURRENT_QUERY, &curQ);
    CHECK(curQ == 0, "no active SAMPLES_PASSED query after glEndQuery");
    beginQuery(GL_SAMPLES_PASSED, qOcc);
    beginQuery(GL_SAMPLES_PASSED, qAny);  /* 同 target 重复 begin：INVALID_OPERATION */
    CHECK(getError() == GL_INVALID_OPERATION,
          "nested glBeginQuery reports GL_INVALID_OPERATION");
    GLint nested = 0;
    getQueryiv(GL_SAMPLES_PASSED, GL_CURRENT_QUERY, &nested);
    CHECK(nested == (GLint)qOcc,
          "nested glBeginQuery(same target) rejected, current query stays %d", nested);
    endQuery(GL_SAMPLES_PASSED);
    endQuery(GL_SAMPLES_PASSED);  /* 第二次 end 无活跃查询 */
    CHECK(getError() == GL_INVALID_OPERATION,
          "glEndQuery without active query reports GL_INVALID_OPERATION");
    CHECK(getError() == GL_NO_ERROR, "query error queue drained after exact assertions");

    /* ---- 7. 同步对象：真 fence 语义 -------------------------------------- */
    GLsync f1 = fenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    CHECK(f1 != NULL, "glFenceSync returned non-NULL");
    CHECK(isSync(f1) == GL_TRUE, "glIsSync(f1) true");
    GLenum wr = clientWaitSync(f1, GL_SYNC_FLUSH_COMMANDS_BIT, 5000000000ull);
    CHECK(wr == GL_ALREADY_SIGNALED || wr == GL_CONDITION_SATISFIED,
          "glClientWaitSync(f1, FLUSH, 5s) = 0x%x (ALREADY_SIGNALED/CONDITION_SATISFIED)", wr);

    GLint status = 0, len = 0;
    getSynciv(f1, GL_SYNC_STATUS, 1, &len, &status);
    CHECK(len == 1 && status == GL_SIGNALED,
          "glGetSynciv(GL_SYNC_STATUS)=0x%x == GL_SIGNALED(0x9119), len=%d", status, len);
    getSynciv(f1, GL_OBJECT_TYPE, 1, &len, &status);
    CHECK(len == 1 && status == GL_SYNC_FENCE,
          "glGetSynciv(GL_OBJECT_TYPE)=0x%x == GL_SYNC_FENCE(0x9116)", status);

    /* draw 后 fence：必须先冲刷再等（FLUSH 位路径） */
    drawArrays(GL_TRIANGLES, 0, 3);
    GLsync f2 = fenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    wr = clientWaitSync(f2, GL_SYNC_FLUSH_COMMANDS_BIT, 5000000000ull);
    CHECK(wr == GL_ALREADY_SIGNALED || wr == GL_CONDITION_SATISFIED,
          "post-draw fence waits clean = 0x%x", wr);

    /* glWaitSync：不产生错误 */
    waitSync(f2, 0, 0xFFFFFFFFFFFFFFFFull);
    CHECK(getError() == GL_NO_ERROR, "glWaitSync leaves no error");

    /* ---- 8. 同步对象错误路径 --------------------------------------------- */
    while (getError() != GL_NO_ERROR) {}
    GLsync bad = fenceSync(0x1234 /* bad condition */, 0);
    CHECK(bad == NULL, "glFenceSync(bad condition) → NULL");
    CHECK(getError() == GL_INVALID_ENUM,
          "glFenceSync(bad condition) reports GL_INVALID_ENUM");
    deleteSync(f2);
    CHECK(isSync(f2) == GL_FALSE, "glIsSync false after glDeleteSync");
    CHECK(clientWaitSync(f2, 0, 0) == GL_WAIT_FAILED,
          "glClientWaitSync(deleted) → GL_WAIT_FAILED");
    CHECK(getError() == GL_INVALID_VALUE,
          "glClientWaitSync(deleted) reports GL_INVALID_VALUE");
    CHECK(clientWaitSync(NULL, 0, 0) == GL_WAIT_FAILED,
          "glClientWaitSync(NULL) → GL_WAIT_FAILED");
    CHECK(getError() == GL_INVALID_VALUE,
          "glClientWaitSync(NULL) reports GL_INVALID_VALUE");

    /* ---- 9. glGetInteger64v(GL_TIMESTAMP) -------------------------------- */
    GLint64 nowTs = 0;
    getInteger64v(GL_TIMESTAMP, &nowTs);
    CHECK(nowTs > 0, "glGetInteger64v(GL_TIMESTAMP)=%lld > 0", (long long)nowTs);

    deleteQueries(1, &qOcc);
    deleteQueries(1, &qAny);
    deleteQueries(1, &qPrim);
    deleteQueries(1, &qEl);
    deleteQueries(1, &qT0);
    deleteQueries(1, &qT1);
    CHECK(getError() == GL_NO_ERROR, "query/sync test flow leaves no error");

    printf("\nQUERY SYNC SMOKE: %d/%d checks passed, %d failure(s)\n",
           checks - failures, checks, failures);
    if (failures == 0) printf("QUERY SYNC SMOKE ALL PASSED\n");
    dlclose(h);
    return failures ? 1 : 0;
}
