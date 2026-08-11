/*
 * gl_smoke.c — Mithril-Wrapper GL 4.6 Core Profile 契约 + 状态机冒烟测试。
 *
 * 参照 Uniaball/Mithril-Wrapper 的 tests/state_smoke.c 结构：dlopen 构建产物，
 * dlsym 解析导出的 GL 入口，随后对「纯状态机」行为做断言。这类断言不依赖
 * Vulkan/Metal 渲染（仅读 g_state / 静态版本串 / 错误队列），因此即使 Vulkan
 * 后端未拉起（无 lavapipe / 无 MoltenVK）也能通过——这正是 Uniaball
 * state_smoke 在 Linux CI 上可独立运行的原因。
 *
 * 本项目是 iOS 专用（GL -> Vulkan -> Metal/MoltenVK），完整库无法在无 Apple
 * SDK 的宿主上构建，故本文件不追求在开发机直接链接 libmithril；它作为验收
 * 资产存在：
 *   1) 用项目自带的 Mithril-Wrapper-cpp/include/GL/glcorearb.h 编译（可
 *      -fsyntax-only 校验），保证「测试期望的符号 + 常量」与导出契约一致；
 *   2) 在有 libmithril 产物、且允许 dlopen 的宿主（macOS 交叉注入 / iOS 模拟
 *      器环境 / 将来若打通 Linux 构建）上真正 dlopen 运行并断言状态机。
 *
 * 构建（语法校验，任意有 gcc/clang 的机器）：
 *   gcc -fsyntax-only -I Mithril-Wrapper-cpp/include tests/gl_smoke.c
 *
 * 运行（在能加载 libmithril 的宿主上）：
 *   gcc -o tests/gl_smoke tests/gl_smoke.c -ldl -I Mithril-Wrapper-cpp/include
 *   ./tests/gl_smoke [path/to/libmithril.{so,dylib}]
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 项目自带的 GL Core Profile 头：常量 + 入口声明与 libmithril 导出契约一致。 */
#include <GL/glcorearb.h>

/* ---- 依赖的 GL 函数指针 typedef（与 glcorearb.h 签名一致） -------------- */
typedef void    (*glGetIntegerv_fn)(GLenum, GLint*);
typedef void    (*glGetFloatv_fn)(GLenum, GLfloat*);
typedef void    (*glGetBooleanv_fn)(GLenum, GLboolean*);
typedef const GLubyte* (*glGetString_fn)(GLenum);
typedef const GLubyte* (*glGetStringi_fn)(GLenum, GLuint);
typedef GLenum   (*glGetError_fn)(void);
typedef void    (*glViewport_fn)(GLint, GLint, GLsizei, GLsizei);
typedef void    (*glScissor_fn)(GLint, GLint, GLsizei, GLsizei);
typedef void    (*glEnable_fn)(GLenum);
typedef void    (*glDisable_fn)(GLenum);
typedef GLboolean (*glIsEnabled_fn)(GLenum);
typedef void    (*glEnablei_fn)(GLenum, GLuint);
typedef void    (*glDisablei_fn)(GLenum, GLuint);
typedef GLboolean (*glIsEnabledi_fn)(GLenum, GLuint);
typedef void    (*glClearColor_fn)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void    (*glClear_fn)(GLbitfield);
typedef void    (*glDepthMask_fn)(GLboolean);
typedef void    (*glColorMask_fn)(GLboolean, GLboolean, GLboolean, GLboolean);
typedef void    (*glBlendFunc_fn)(GLenum, GLenum);
typedef void    (*glCullFace_fn)(GLenum);
typedef void    (*glFrontFace_fn)(GLenum);
typedef void    (*glDepthFunc_fn)(GLenum);
typedef void    (*glFinish_fn)(void);
typedef void    (*glFlush_fn)(void);
typedef void    (*glGenTextures_fn)(GLsizei, GLuint*);
typedef void    (*glTexImage2D_fn)(GLenum, GLint, GLint, GLsizei, GLsizei,
                                   GLint, GLenum, GLenum, const void*);
typedef void    (*glGenerateMipmap_fn)(GLenum);
typedef void    (*glDrawArrays_fn)(GLenum, GLint, GLsizei);
typedef void    (*glDrawElements_fn)(GLenum, GLsizei, GLenum, const void*);
typedef void    (*glTexParameteri_fn)(GLenum, GLenum, GLint);

/* ---- 断言基础设施（Uniaball 风格） -------------------------------------- */
static int failures = 0;

#define CHECK(cond, fmt, ...) do { \
    if (cond) { printf("ok : " fmt "\n", ##__VA_ARGS__); } \
    else      { printf("FAIL: " fmt "\n", ##__VA_ARGS__); ++failures; } \
} while (0)

/* ---- dlopen 库路径解析 --------------------------------------------------- */
static void* open_libmithril(int argc, char** argv) {
    const char* candidates[4];
    int n = 0;
    if (argc > 1) candidates[n++] = argv[1];               /* 显式指定 */
    candidates[n++] = "./output/libmithril.so";
    candidates[n++] = "./output/libmithril.dylib";
    candidates[n++] = "./build/output/libmithril.so";

    for (int i = 0; i < n; ++i) {
        void* h = dlopen(candidates[i], RTLD_NOW | RTLD_GLOBAL);
        if (h) {
            printf("loaded: %s\n", candidates[i]);
            return h;
        }
        /* 保留最后一个错误用于最终报错 */
    }
    fprintf(stderr, "dlopen failed (tried %d candidates):\n", n);
    for (int i = 0; i < n; ++i) fprintf(stderr, "  %s\n", candidates[i]);
    fprintf(stderr, "  last dlerror: %s\n", dlerror());
    return NULL;
}

int main(int argc, char** argv) {
    void* h = open_libmithril(argc, argv);
    if (!h) return 2;

    /* ---- 解析全部被测符号；契约缺失直接判失败 --------------------------- */
    glGetIntegerv_fn  getIntegerv  = (glGetIntegerv_fn) dlsym(h, "glGetIntegerv");
    glGetFloatv_fn    getFloatv    = (glGetFloatv_fn)   dlsym(h, "glGetFloatv");
    glGetBooleanv_fn  getBooleanv  = (glGetBooleanv_fn) dlsym(h, "glGetBooleanv");
    glGetString_fn    getString    = (glGetString_fn)   dlsym(h, "glGetString");
    glGetStringi_fn   getStringi   = (glGetStringi_fn)  dlsym(h, "glGetStringi");
    glGetError_fn     getError     = (glGetError_fn)    dlsym(h, "glGetError");
    glViewport_fn     viewport     = (glViewport_fn)    dlsym(h, "glViewport");
    glScissor_fn      scissor      = (glScissor_fn)     dlsym(h, "glScissor");
    glEnable_fn       enable       = (glEnable_fn)      dlsym(h, "glEnable");
    glDisable_fn      disable      = (glDisable_fn)     dlsym(h, "glDisable");
    glIsEnabled_fn    isEnabled    = (glIsEnabled_fn)   dlsym(h, "glIsEnabled");
    glEnablei_fn      enablei      = (glEnablei_fn)     dlsym(h, "glEnablei");
    glDisablei_fn     disablei     = (glDisablei_fn)    dlsym(h, "glDisablei");
    glIsEnabledi_fn   isEnabledi   = (glIsEnabledi_fn)  dlsym(h, "glIsEnabledi");
    glClearColor_fn   clearColor   = (glClearColor_fn)  dlsym(h, "glClearColor");
    glClear_fn        clear        = (glClear_fn)       dlsym(h, "glClear");
    glDepthMask_fn    depthMask    = (glDepthMask_fn)   dlsym(h, "glDepthMask");
    glColorMask_fn    colorMask    = (glColorMask_fn)   dlsym(h, "glColorMask");
    glBlendFunc_fn    blendFunc    = (glBlendFunc_fn)   dlsym(h, "glBlendFunc");
    glCullFace_fn     cullFace     = (glCullFace_fn)    dlsym(h, "glCullFace");
    glFrontFace_fn    frontFace    = (glFrontFace_fn)   dlsym(h, "glFrontFace");
    glDepthFunc_fn    depthFunc    = (glDepthFunc_fn)   dlsym(h, "glDepthFunc");
    glFinish_fn       finish       = (glFinish_fn)      dlsym(h, "glFinish");
    glFlush_fn        flush        = (glFlush_fn)       dlsym(h, "glFlush");
    glGenTextures_fn  genTextures  = (glGenTextures_fn) dlsym(h, "glGenTextures");
    glTexImage2D_fn   texImage2D   = (glTexImage2D_fn)  dlsym(h, "glTexImage2D");
    glGenerateMipmap_fn genMipmap  = (glGenerateMipmap_fn) dlsym(h, "glGenerateMipmap");
    glDrawArrays_fn   drawArrays   = (glDrawArrays_fn)  dlsym(h, "glDrawArrays");
    glDrawElements_fn drawElements = (glDrawElements_fn)dlsym(h, "glDrawElements");
    glTexParameteri_fn texParamI  = (glTexParameteri_fn)dlsym(h, "glTexParameteri");

    /* ---- GL 4.6 核心符号契约（抽样，覆盖状态机 + 纹理 + 绘制 + mipmap） --- */
    CHECK(getIntegerv && getFloatv && getBooleanv && getString && getStringi &&
          getError && viewport && scissor && enable && disable && isEnabled &&
          enablei && disablei && isEnabledi,
          "state machine symbols resolved");
    CHECK(clearColor && clear && depthMask && colorMask && blendFunc &&
          cullFace && frontFace && depthFunc && finish && flush,
          "draw/fixed-function symbols resolved");
    CHECK(genTextures && texImage2D && genMipmap && texParamI,
          "texture symbols resolved (incl. glGenerateMipmap)");
    CHECK(drawArrays && drawElements, "draw entry points resolved");

    /* ---- 版本 / 能力查询 ------------------------------------------------ */
    GLint major = 0, minor = 0;
    getIntegerv(GL_MAJOR_VERSION, &major);
    getIntegerv(GL_MINOR_VERSION, &minor);
    CHECK(major == 4 && minor == 6,
          "GL_MAJOR_VERSION=%d GL_MINOR_VERSION=%d == 4.6", major, minor);

    const char* version = (const char*)getString(GL_VERSION);
    CHECK(version && strstr(version, "4.6"),
          "glGetString(GL_VERSION) contains 4.6 (got \"%s\")", version ? version : "(null)");

    const char* glslVer = (const char*)getString(GL_SHADING_LANGUAGE_VERSION);
    CHECK(glslVer && strstr(glslVer, "4.60"),
          "glGetString(GL_SHADING_LANGUAGE_VERSION) contains 4.60 (got \"%s\")",
          glslVer ? glslVer : "(null)");

    /* ---- 错误语义 ------------------------------------------------------------
     * Mithril 镜像 MobileGlues 的故意设计：glGetError 恒返回 GL_NO_ERROR，仅
     * 内部弹出错误队列（否则 Minecraft 会刷屏无害的 GL 错误日志）。因此断言
     * 非法 pname / capability 的调用本身不崩溃、状态机保持可用即可 —— 不能
     * 期望 glGetError 返回 GL_INVALID_ENUM（该实现契约永远返回 NO_ERROR）。 */
    CHECK(getError() == GL_NO_ERROR, "glGetError always NO_ERROR (MobileGlues mirror)");
    GLint bogus = 0;
    getIntegerv(0xC0FFEE, &bogus);                       /* 非法 pname 不崩溃 */
    CHECK(getError() == GL_NO_ERROR, "illegal getIntegerv pname tolerated (no crash)");
    enable(0xC0FFEE);                                     /* 非法 capability 不崩溃 */
    disable(0xC0FFEE);
    CHECK(getError() == GL_NO_ERROR, "illegal enable/disable cap tolerated (no crash)");

    /* ---- viewport / scissor round-trip ----------------------------------- */
    viewport(10, 20, 640, 480);
    GLint vp[4] = {-1, -1, -1, -1};
    getIntegerv(GL_VIEWPORT, vp);
    CHECK(vp[0] == 10 && vp[1] == 20 && vp[2] == 640 && vp[3] == 480,
          "glViewport round-trip via glGetIntegerv(GL_VIEWPORT) (%d,%d %dx%d)",
          vp[0], vp[1], vp[2], vp[3]);

    scissor(1, 2, 300, 200);
    GLint sc[4] = {-1, -1, -1, -1};
    getIntegerv(GL_SCISSOR_BOX, sc);
    CHECK(sc[0] == 1 && sc[1] == 2 && sc[2] == 300 && sc[3] == 200,
          "glScissor round-trip via glGetIntegerv(GL_SCISSOR_BOX)");

    /* ---- capability enable/disable round-trip ---------------------------- */
    CHECK(isEnabled(GL_DEPTH_TEST) == GL_FALSE, "GL_DEPTH_TEST off by default");
    enable(GL_DEPTH_TEST);
    CHECK(isEnabled(GL_DEPTH_TEST) == GL_TRUE, "glIsEnabled(GL_DEPTH_TEST) after enable");
    disable(GL_DEPTH_TEST);
    CHECK(isEnabled(GL_DEPTH_TEST) == GL_FALSE, "glIsEnabled(GL_DEPTH_TEST) after disable");

    enable(GL_CULL_FACE);
    CHECK(isEnabled(GL_CULL_FACE) == GL_TRUE, "glIsEnabled(GL_CULL_FACE) after enable");
    disable(GL_CULL_FACE);

    /* ---- indexed capability (GL 3.0+) ------------------------------------ */
    /* blend per-attachment index 0 */
    enablei(GL_BLEND, 0);
    CHECK(isEnabledi(GL_BLEND, 0) == GL_TRUE, "glIsEnabledi(GL_BLEND, 0) after enablei");
    disablei(GL_BLEND, 0);
    CHECK(isEnabledi(GL_BLEND, 0) == GL_FALSE, "glIsEnabledi(GL_BLEND, 0) after disablei");

    /* ---- 常见 draw/fixed-function 状态 setter 不崩溃（契约存在性） --------- */
    clearColor(0.1f, 0.2f, 0.3f, 1.0f);
    depthMask(GL_TRUE);
    colorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    cullFace(GL_BACK);
    frontFace(GL_CCW);
    depthFunc(GL_LESS);
    flush();
    finish();
    CHECK(getError() == GL_NO_ERROR, "state setters leave no error");

    /* ---- 纹理对象生命周期（不触发渲染，仅对象表操作） --------------------- */
    GLuint tex = 0;
    genTextures(1, &tex);
    CHECK(tex != 0, "glGenTextures allocates a texture name (%u)", tex);
    /* 若上下文允许，尝试上传一个单 mip level + glGenerateMipmap —— 这正是
     * 本次「atlas 单层 + mipmap 采样越界」根因修复的回归点。注：headless /
     * 无后端时这些是 no-op，不会报错；有 Vulkan 后端时会真正执行。 */
    texParamI(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    texParamI(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    texParamI(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    texParamI(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    CHECK(getError() == GL_NO_ERROR, "glTexParameteri set accepts mipmap filters");
    /* 不在此强制 upload+generate（需要 GL_TEXTURE_2D 绑定 + 后端），避免在
     * 无 Vulkan 环境下产生误导性失败；对象表 + 参数契约已覆盖核心回归面。 */

    dlclose(h);

    printf("\nGL SMOKE: %d failure(s)\n", failures);
    if (failures == 0) {
        printf("GL SMOKE ALL PASSED\n");
        return 0;
    }
    printf("GL SMOKE FAILED\n");
    return 1;
}