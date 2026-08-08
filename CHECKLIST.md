# Mithril-Wrapper 实现清单（CHECKLIST）

> 状态：**M4 纹理完成（S4 全组 42 函数，texture_smoke 26 断言通过）**；M3 及以前全部完成（Linux 五冒烟测试通过，含 GL→Vulkan 全链 draw_smoke + texture_smoke）。
> 配套文档：`docs/gl33_core_list.md`（GL 3.3 core 342 函数分组）、`docs/egl_list.md`（EGL 符号清单）。
> 测试：`tests/contract_smoke.c`（EGL 契约）、`tests/state_smoke.c`（GL 状态机）、`tests/shader_smoke.c`（着色器管线）、`tests/draw_smoke.c`（GL→Vulkan→读回全链）、`tests/texture_smoke.c`（M4 纹理全链，需 lavapipe/llvmpipe）。
> M2 已完成：`src/shader/`（Shader/Program 对象表、glslang 编译缓存 .glsl→SPIR-V、SPIRV-Cross 反射 uniform/attrib）、GLSL 150 自动升级到 330 core（含采样器存在时自动升 450 以启用 layout(binding=...) 注入，声明序 1 起绑定）、`gl_VertexID/gl_InstanceID` 重写、松散 uniform 折入合成 UBO（ANGLE 模式）、`glUniform*` 全系 + 矩阵 setter/getter；`src/vk/`（dlsym 动态加载 Vulkan loader/ICD、instance 级函数经真实 instance 句柄解析、UBO 反射 VS+FS 双阶段合并 + 动态 UBO 池、staging 顶点缓冲、renderpass + 清屏 + 帧读回）；`draw_smoke` 全链通过。
> M3 已完成：S3 组 76/114 真实现——顶点属性全家族（glVertexAttribPointer/IPointer、常量系 1-4、Divisor、Enable/Disable）、attribute 查询（fv/dv/iv/Iiv/Iuiv/Pointerv）、buffer 映射/查询家族（MapBuffer/Range、Unmap、FlushMapped、GetBufferParameteriv/i64v/Pointerv/SubData、CopyBufferSubData）、全部 10 个 draw 入口（DrawArrays/Instanced、DrawElements/BaseVertex/Instanced/InstancedBaseVertex、DrawRangeElements/BaseVertex、MultiDrawArrays/Elements/BaseVertex）；引擎新增双顶点流（VERTEX+INSTANCE binding）、索引缓冲（UBYTE/USHORT/UINT 统一扩 UINT32 staging）、TriangleStrip/Fan 拓扑、实例化（CPU 按 divisor 复制行，无需 EXT）；非 4 字节对齐 stride/offset 由 CPU 打包规整；draw_smoke 扩展 8 断言通过。
> **M4 完成（S4 42/42）**：`src/vk/texture.cpp`——staging→CmdCopyBufferToImage 全 mip 逐切片上传、常驻 TexObj 表、1x1 白色 dummy 兜底、采样器描述符绑定；image 类型扩展（2D/1D、3D volume、2D/1D array、cubemap，image/view/sampler 按类型创建，wrap_r 接入）；GL 端 `src/gl/texture.cpp` S4 全量 42 函数真实现——对象表（Gen/Delete/Is/Bind/Active）、TexImage1D/2D/3D + TexSub1D/2D/3D（cubemap face 归位、array 分层）、GetTexImage（PACK 对齐回读）、GenerateMipmap（每切片盒式滤波）、TexParameter/GetTexParameter 全系、GetTexLevelParameter（宽/高/深/压缩）、S3TC（DXT1/3/5）CPU 解压 + 原始压缩镜像 + GetCompressedTexImage、CopyTexImage1D/2D + CopyTexSubImage1D/2D/3D（帧读回）、glTexBuffer（buffer 引用 → 1xN 镜像上传）、glPixelStoref；**texture_smoke 26 断言全过**（采样/mip/dummy 白/GetTexImage 往返/DXT1 解压/3D 切片/数组分层/cubemap 6 面+mip/拷贝/texBuffer）。CI 已接 texture_smoke。
> 待办：M5 状态管线（depth/stencil/blend/cull/FBO/MSAA → pipeline 缓存）。

---

## 1. 项目定位

在 iOS 上为 Minecraft Java（LWJGL 3）提供自研 OpenGL 实现：

- **目标 GL**：OpenGL 3.3 Core Profile（仅实现必要的子集，由 Amethyst 启动器注释确认：`gl_bridge.m:127` "Mithril 是 OpenGL 3.3 Core Profile"）
- **渲染后端**：自研 GL → Vulkan → Metal（MoltenVK），无 OpenGL ES 参与
- **交付物**：`libmithril.dylib`（arm64），由 GitHub Actions macOS runner 构建，集成进 Amethyst 启动器
- **开发循环**：本地 Linux 编译 `.so` 验证逻辑，CI 出 dylib
- **参考实现**（仅参考，从零实现）：`~/Desktop/projects/dg/DesktopGlues`、`~/Desktop/opencode/mobilegl/MobileGL`（MobileGL，GL→Vulkan 完整实现）、`~/Desktop/opencode/air/Amethyst-iOS-MyRemastered`（启动器桥接契约）

## 2. 桥接契约（Amethyst 启动器侧，硬性要求）

来源：`Natives/ctxbridges/gl_bridge.m`（已逐行核对）。

### 2.1 必须导出的 EGL 符号（gl_bridge.m:73-97 逐个 dlsym）

18 个：`eglBindAPI eglChooseConfig eglCreateContext eglCreateWindowSurface eglDestroyContext eglDestroySurface eglGetConfigAttrib eglGetCurrentContext eglGetCurrentSurface eglGetDisplay eglGetError eglGetPlatformDisplay eglInitialize eglMakeCurrent eglReleaseThread eglSwapBuffers eglSwapInterval eglTerminate`

另需：`eglGetProcAddress`（LWJGL/GLFW/SDL3 解析 GL 扩展函数）、`eglGetConfigs`、`eglQueryString`（SDL3 dump）。建议完整导出 EGL 1.5 全 44 个 + EXT 别名（见 `docs/egl_list.md`）。

### 2.2 行为契约（gl_bridge.m:110-257）

1. `eglGetDisplay(EGL_DEFAULT_DISPLAY)` → 非 NULL display
2. `eglInitialize(display, NULL, NULL)` → 成功
3. `eglChooseConfig` 对以下属性**必须返回 ≥1 个配置**（gl_bridge.m:133-141，否则 `assert(bundle->config)` 崩溃）：
   - `EGL_RED_SIZE=8, GREEN=8, BLUE=8, ALPHA=8, DEPTH=24`
   - `EGL_SURFACE_TYPE = EGL_WINDOW_BIT | EGL_PBUFFER_BIT`
   - `EGL_RENDERABLE_TYPE = EGL_OPENGL_BIT`（desktop GL 分支，Mithril 走此路径）
4. `eglGetConfigAttrib(EGL_NATIVE_VISUAL_ID)` → 成功
5. `eglBindAPI(EGL_OPENGL_API)` → EGL_TRUE（Mithril 走 desktop GL 分支 gl_bridge.m:161-163）
6. `eglCreateWindowSurface(display, config, (EGLNativeWindowType)CALayer*, NULL)` — **native window 类型 = CALayer***（gl_bridge.m:171）
7. `eglCreateContext(display, config, share_ctx, {EGL_CONTEXT_CLIENT_VERSION, 3})`（gl_bridge.m:178-183）
8. `eglMakeCurrent` 支持解绑：`eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT)`（gl_bridge.m:194-200）
9. `eglSwapBuffers` / `eglSwapInterval`（interval 受 `POJAV_DISABLE_VSYNC=1` 强制为 0，gl_bridge.m:224-231）
10. 终止顺序：`eglMakeCurrent(EGL_NO_SURFACE)` → `eglDestroySurface` → `eglDestroyContext` → `eglTerminate` → `eglReleaseThread`（gl_bridge.m:249-257）

### 2.3 GL 侧要求

- LWJGL 通过 `-Dorg.lwjgl.opengl.libname=libmithril.dylib` **直接 dlsym GL 符号**（JavaLauncher.m:825-870），因此所有需用 GL 函数必须真实导出为符号（default visibility）
- `eglGetProcAddress` + `glXGetProcAddress`/`glXGetProcAddressARB` 作为 LWJGL 解析回退
- `GL_VERSION` 字符串需含 `3.3 Core Profile`（MC 1.17+ 拒绝无 Core Profile 的实现）；`glGetStringi` 需可用（扩展查询，Sodium/Iris）
- 关键扩展声明（Sodium/Iris 依赖）：`GL_ARB_buffer_storage`、`GL_ARB_draw_buffers_blend` 等（LTW 注释 gl_bridge.m:60-68 明确）

### 2.4 环境变量与路径

| 变量 | 值 | 位置 |
|---|---|---|
| `AMETHYST_RENDERER` | `libmithril.dylib`（触发 Mithril 路径的唯一值） | gl_bridge.m:31 |
| `SDL_EGL_LIBRARY` / `SDL_OPENGL_LIBRARY` | `@rpath/libmithril.dylib`（SDL3 窗口后端时） | JavaLauncher.m:945-949 |
| `POJAV_DISABLE_VSYNC` | `1` → swap interval 0 | gl_bridge.m:224 |

- 产物位置：app bundle `Frameworks/libmithril.dylib`；加载用 `@rpath/libmithril.dylib`
- Mach-O 要求：arm64、TWOLEVEL、`install name = @rpath/libmithril.dylib`、符号 default visibility
- **macOS 侧导出白名单**：参考 MobileGL `ExportedSymbols.txt` 模式：`_egl*`、`_gl[A-Z0-9]*`、`_glX*`，隐藏 glslang/spirv 等内部 C++ 符号，避免污染 host（`-exported_symbols_list`）

## 3. GL 函数子集（已生成，见 docs/gl33_core_list.md）

- **基础**：从 MobileGL 自带 Khronos `glcorearb.h` 提取 `GL_VERSION_1_0`..`GL_VERSION_3_3` 累计 → **342 个函数**
- **交叉验证**：
  - desktopglues（libdesktopglues.so，3062 gl 导出）：342 全部覆盖 ✅
  - MobileGL `Definitions.cpp`（2766 导出）：342 全部覆盖 ✅（glFinish/glFlush 为独立导出形式）
  - LWJGL core 类（GL11C..GL33C，712 函数）含 4.x 函数，超出范围剔除
- 分组（docs/gl33_core_list.md，按实现里程碑）：
    - S1 状态/使能/基础查询：55（glEnable/glClear/glViewport/glBlend/glDepth/glStencil/glGet*…）
    - S2 着色器/程序/Uniform：71（glCreateShader→glUseProgram、glUniform* 全系、程序反射）
    - S3 Buffer/VAO/顶点/Draw：114（glGenBuffers→glDrawArrays/glDrawElements 全系、glVertexAttrib* 全系）
    - S4 纹理：42（glGenTextures→glTexImage2D/glTexSubImage2D/glGenerateMipmap；**S4 全量 42 真实现完成，texture_smoke 26 断言通过**）
    - S5 FBO/渲染缓冲：24（glGenFramebuffers→glReadPixels/glBlitFramebuffer）
    - S6 同步/Query/Sampler：36（glGenQueries/glFenceSync/glSamplerParameter 系）
  - 342 全部归入以上六组，无遗漏
- 未实现函数：导出为 stub，返回 `GL_INVALID_OPERATION` + 日志（MobileGL `DECLARE_GL_FUNCTION_STUB_HEAD` 模式）
- 最终裁剪依据：M7 用 apitrace 抓 Minecraft 真实调用 → 与 desktopglues 清单交叉

## 4. 架构设计（从零实现）

```
LWJGL (Minecraft Java) — dlsym libmithril.dylib
        │
        ▼
GL 分发表（src/gl/gl_dispatch.cpp，手写 gl* 符号，未实现返回 INVALID_OPERATION+日志）
EGL 层（src/egl/，44 符号，display/config/context/surface 生命周期 + thread_local 当前状态）
        │  桥接契约：CAMetalLayer 作为 native window（iOS）
        ▼
对象与状态管理层（src/state/）
   ├─ 对象表：VAO/VBO/IBO/Texture/FBO/Program/Query → Vulkan 对象 + 名称池（虚拟 ID，参照 MobileGL buffer.cpp 模式）
   ├─ 状态跟踪：blend/depth/stencil/cull/viewport/scissor/顶点布局/当前 program/纹理单元（参照 MobileGL GLState）
   ├─ 错误队列：GLenum 错误栈（glGetError 语义）
   └─ 着色器：glslang GLSL→SPIR-V；SPIRV-Cross 反射 → uniform/采样器绑定（iOS 无 GLSL 编译能力，必须走 glslang）
        │
        ▼
Vulkan 后端（src/vk/，经 MoltenVK → Metal → CAMetalLayer）
   ├─ pipeline 工厂：状态哈希 key → VkPipeline 缓存（program_hash, blend, depth, stencil, raster, 顶点布局, 附件格式, sample_count, cull）
   ├─ 命令记录：每帧一个 VkCommandBuffer，draw 时录制，present 前提交；双缓冲 + fence
   ├─ 显式同步：upload→shader barrier、帧 fence
   ├─ 差异修复：非 4 字节对齐 stride/offset → staging 规整（MoltenVK portability 限制）
   └─ swapchain：CAMetalLayer → VkSwapchain（VK_EXT_metal_surface）
```

### 4.1 关键设计决策

| 决策 | 选择 |
|---|---|
| GL 版本 | 3.3 Core Profile，仅实现必要子集 |
| EGL 虚拟后端 | 自实现 44 符号，config 生成器（单 display、单 config 满足契约属性即可） |
| 上下文 | 单渲染上下文（MC 单窗口场景） |
| 着色器 | glslang（vendored 或系统库）编译 GLSL→SPIR-V，SPIRV-Cross 做反射；无运行时 GLSL 编译 |
| 正确性标准 | MC 画面跑通即可，不做 CTS |
| 对象 ID | 虚拟 ID → 驱动 ID 映射，名称池复用（MobileGL buffer.cpp 模式） |
| 未实现函数 | stub 返回 GL_INVALID_OPERATION + 日志（不产生崩溃） |

### 4.2 源码目录规划

```
~/Desktop/projects/mithril/
├── CMakeLists.txt            # 双分支：Linux .so / macOS(CI) dylib
├── src/
│   ├── egl/                  # EGL 实现 + 导出符号
│   ├── gl/                   # gl* 分发表 + stub
│   ├── state/                # 对象表 + 状态跟踪 + 错误栈
│   ├── vk/                   # Vulkan 后端（pipeline 工厂/命令记录/swapchain/同步）
│   ├── shader/               # glslang 集成 + SPIRV-Cross 反射
│   └── util/                 # 日志、哈希、平台封装
├── third_party/              # glslang、SPIRV-Cross、Vulkan 头（vendored）
├── .github/workflows/build.yml
├── docs/                     # 本清单 + 分组清单
└── output/                   # CI 产物归档（本机占位）
```

## 5. 里程碑

| 里程碑 | 内容 | 验收 |
|---|---|---|
| **M0 基建** | CMake 双分支工程；EGL 44 符号骨架 + config 契约；GL 342 stub 分发表；CI build.yml（macOS runner + MoltenVK + glslang）；Linux 编 .so 验证；iOS 分支 CAMetalLayer→swapchain 纯色 | Linux .so 可加载、EGL 契约通过、CI 产出 dylib |
| **M1 注入与 Context** | eglMakeCurrent 语义完整；glClear/glViewport/glClearColor 实现；GL_VERSION="3.3 Core Profile" 字符串 | 真机 demo 背景色正确 |
| **M2 Shader 管线** | glShaderSource→glslang→SPIR-V→vkShaderModule；UBO 管理；glDrawArrays 三角形 | 带色三角形 | ✅ S2 层完成（glslang+SPIRV-Cross、shader_smoke 通过）+ Vulkan 后端接通（`src/vk/`：dlsym 加载器、instance GIPA 解析、UBO 双阶段反射、动态 UBO 池、staging 顶点缓冲、renderpass/清屏/读回）；draw_smoke 全链通过（llvmpipe） |
| **M3 顶点数据** | VAO/VBO、stride/offset 规整、glDrawElements | 用 App 数据绘制 | ✅ S3 76/114 完成（属性全系/映射查询/draw 十入口/双流+索引+拓扑+实例化）；draw_smoke 扩展 8 断言通过 |
| **M4 纹理** | glTexImage2D 上传、格式/swizzle、采样器、mipmap | 贴图正确 |
| **M5 状态与管线** | depth/stencil/blend/cull/FBO/MSAA → pipeline 缓存 | 完整三维画面 |
| **M6 同步** | 双 CMD buffer、barrier、glFinish/glFlush | 多帧不花屏 |
| **M7 收窄** | apitrace 实测 MC 调用 → 裁剪/补漏 | 与 GL 参考输出截图一致 |
| **M8 集成** | 打包进 Amethyst，真机验证 | 完整可玩 |

## 6. 技术风险与应对

| 风险 | 应对 |
|---|---|
| MoltenVK portability（4 字节对齐、动态状态缺失、VK 1.3 不全） | staging 规整；只用 MoltenVK 支持子集；验证层排查 |
| glslang 输出与 MoltenVK 后端不兼容 | 参考 desktopglues/MobileGL 已验证着色器路径；必要时 MSL 变通 |
| iOS 无系统 GL 可回落 | 全部 proc/surface/present 自处理；详细日志 |
| LWJGL 大量 Get/查询函数需"诚实作答" | 参照 desktopglues getter.cpp 的虚值策略（MobileGL 更完整） |
| Amethyst 版本耦合 | 严格对照 gl_bridge.m 契约；保持 ABI 稳定 |

## 7. 关键参考（文件:行号索引）

| 参考点 | 位置 |
|---|---|
| EGL 18 符号 dlsym | Amethyst `Natives/ctxbridges/gl_bridge.m:73-97` |
| eglChooseConfig 契约属性 | 同上 `:133-141` |
| desktop GL 分支（EGL_OPENGL_BIT/API） | 同上 `:126-167` |
| CALayer 作为 native window | 同上 `:171` |
| context 属性 {CLIENT_VERSION,3} | 同上 `:178-183` |
| 终止顺序 | 同上 `:249-257` |
| LWJGL libname 注入 | Amethyst `Natives/JavaLauncher.m:825-870` |
| SDL3 EGL 重定向 | 同上 `:928-964` |
| GL 3.3 core 官方函数表 | MobileGL `include/GL/glcorearb.h`（GL_VERSION_1_0..3_3） |
| GL 符号导出宏模式 | MobileGL `MG_Impl/GLImpl/Exporting/Definitions.cpp:25-58` |
| EGL 导出模式 | MobileGL `MG_Impl/EGLImpl/Exporting/Definitions.cpp` |
| macOS 导出白名单 | MobileGL `MG_Impl/DyldInterpose/ExportedSymbols.txt` + CMakeLists.txt:462-473 |
| iOS 构建分支 | MobileGL `CMakeLists.txt:493-502`（MOBILEGL_IOS + MOBILEGL_VULKAN_LIBRARY） |
| 对象表/状态管理 | MobileGL `MG_State/`（buffer.cpp 虚拟 ID 模式、GLState/EGLState 分离） |
| getter 虚值策略 | DesktopGlues `MobileGlues-cpp/gl/getter.cpp` |
