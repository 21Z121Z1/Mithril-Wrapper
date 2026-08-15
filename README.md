# Mithril-Wrapper

> OpenGL 4.6 Core Profile → **双后端**（DirectMetal / Vulkan+MoltenVK）翻译层，
> 让依赖桌面 OpenGL 的应用能在仅有 Metal 后端的 iOS 上运行。

Mithril-Wrapper 把宿主程序发出的 **OpenGL 4.6 Core Profile** 调用实时翻译为
**Metal** 或 **Vulkan 1.2**（经 MoltenVK 转 Metal）调用。库自带一套基于
**CAMetalLayer** 的 **EGL 1.5** 实现，让 LWJGL 3 / PojavLauncher /
Amethyst-iOS 这类靠 `eglCreateContext` 等 EGL 入口拉起 GL 上下文的启动器
可以直接 `dlopen` 本库。

当前有两条显式 backend 路径：

- **DirectMetal**：GL frontend/state → 共享 SPIR-V → SPIRV-Cross MSL → Metal；
  不调用 Vulkan/MoltenVK。Apple 构建默认选择。
- **Vulkan**：GL → SPIR-V → `vkCreateShaderModule` → MoltenVK 内部 SPIR-V→MSL → Metal；
  可用 `MITHRIL_BACKEND=vulkan` 显式选择。

可用 `MITHRIL_BACKEND=metal|vulkan` 覆盖默认选择。未知名称或非 Apple 构建请求
`metal` 会明确失败，不会静默切换 backend。

```
DirectMetal 路径:
  GLSL 源码 ──glslang──▶ SPIR-V ──SPIRV-Cross──▶ MSL ──▶ MTLLibrary ──▶ Metal

Vulkan 路径:
  GLSL 源码 ──glslang──▶ SPIR-V ──vkCreateShaderModule──▶ [MoltenVK SPIR-V→MSL] ──▶ Metal
```

## 功能概览

- 对外暴露一整套 `extern "C"` 的 OpenGL 4.6 Core 入口（`glDraw*`、
  `glBindBuffer`、`glTexImage2D`、`glUniform*`、`glGetString*` 等，~850 个符号），
  可作为动态库 `libmithril.dylib` 被 `dlopen` 注入。
- `glGetString(GL_VERSION)` 返回
  `4.6 §bMithril-Wrapper§r 1.0 (Vulkan 1.2 / MoltenVK)`，
  `glGetIntegerv(GL_MAJOR_VERSION/GL_MINOR_VERSION)` 返回 `4 / 6`，
  `GL_CONTEXT_PROFILE_MASK` 返回 `GL_CONTEXT_CORE_PROFILE_BIT`。
- **自带 EGL 1.5**：`egl/egl.cpp` 导出 ~44 个 `egl*` 入口
  （EGL 1.5 全套：`eglGetDisplay` / `eglInitialize` / `eglChooseConfig` /
  `eglCreateContext` / `eglCreateWindowSurface` / `eglMakeCurrent` /
  `eglSwapBuffers` + EGL 1.5 Sync / Image / Platform Surface API …）。
- **双后端架构**：
  - **DirectMetal**（`MG_Backend/DirectMetal/`）— 直接创建 `MTLDevice`、
    command queue、offscreen RGBA8 + depth/stencil target，执行
    GLSL→SPIR-V→MSL→`MTLLibrary`/`MTLRenderPipelineState`，支持 clear、
    triangle/line vertex/index/instance draw、自定义 primitive restart、
    `glProvokingVertex`、`GL_SAMPLES_PASSED`/`GL_ANY_SAMPLES_PASSED`、
    loose uniform、真实 GL uniform block、sampler、texture upload、
    FBO render-to-texture + blit、MSAA resolve、compute dispatch。
  - **Vulkan**（`MG_Backend/DirectVulkan/`）— Vulkan 1.2 + MoltenVK 后端，
    使用 `VK_KHR_dynamic_rendering` 动态渲染通道、SPIRV-Cross 反射描述符布局。
- 着色器转译（`MG_Impl/Shader.cpp`）：线程安全地调用 glslang 把 GLSL 4.60
  编译成 Vulkan SPIR-V，并在预处理阶段注入 Z remap / Y flip（GLSL 源码层注入，
  等价于 MobileGL 的 SPIRV-Tools `GlToVulkanPositionFixPass`）。

## 最低硬件 / 系统要求

| 项 | 要求 | 说明 |
|---|---|---|
| SoC | **Apple A11** 及以上 | iPhone 8 / 8 Plus / X 起步 |
| 系统 | **iOS / iPadOS 15.0** 及以上 | CI 默认部署目标 `15.0` |
| 架构 | **arm64** | CI 仅构建 `PLATFORM=OS64` |
| Metal | **Metal 2.3**（MSL 2.3） | iOS 15 对应的 Metal Shading Language 版本 |
| Vulkan | Vulkan 1.2（仅 Vulkan 后端需要） | MoltenVK 静态链接；DirectMetal 后端不需要 |

## 架构分层

### MG_Impl/ — OpenGL 4.6 Core Profile 入口点

GL 调用的具体实现层。每个 `gl*` 函数通过 `MG_Backend/Backend.h` 定义的 C API
调用后端。主要文件：

- `gl.cpp` — 核心 GL 状态切换入口
- `Buffer.cpp` — `glGenBuffers`、`glBindBuffer`、`glBufferData` 等
- `Texture.cpp` — `glGenTextures`、`glTexImage2D`、`glTexParameter` 等
- `Drawing.cpp` — `glDrawArrays`、`glDrawElements`、`glClear` 等
- `Program.cpp` — `glCreateProgram`、`glLinkProgram`、`glUseProgram` 等
- `Shader.cpp` — `glCreateShader`、`glShaderSource`、`glCompileShader`（含 GLSL→SPIR-V）
- `Framebuffer.cpp` — `glGenFramebuffers`、`glFramebufferTexture2D` 等
- `VertexArray.cpp` — `glGenVertexArrays`、`glVertexAttribPointer` 等
- `Getter.cpp` / `Getter_gpu.mm` — `glGetString`、`glGetIntegerv` 等查询
- `GL46_Compat.cpp` — OpenGL 4.3-4.6 Core Profile DSA / packed vertex / indexed getter
- `Stubs.cpp` — 废弃 GL 1.x-2.x 入口桩（符号存在性）
- `lookup.cpp` — `glXGetProcAddress` 入口查找

### MG_State/ — GL 状态机

`GLState` 结构体持有所有 GL 状态、对象表（buffer、texture、shader、program、
framebuffer、VAO），以及 EGL 默认帧缓冲的附件。每个 `EGLContext`
拥有独立的 `GLState`，`eglMakeCurrent` 切换 `mithril::g_state` 全局指针。

### MG_Backend/DirectMetal/ — DirectMetal 后端

直接 Metal 后端，不经过 Vulkan/MoltenVK：

- `MetalDevice.{h,mm}` — `MTLDevice` / command queue / UBO arena / 设备限制查询
- `MetalPipeline.{h,mm}` — SPIR-V→MSL（SPIRV-Cross）+ `MTLRenderPipelineState` 缓存
- `MetalCommandStream.{h,mm}` — render pass 编排 + draw call 调度
- `MetalResources.{h,mm}` — `MTLBuffer` / `MTLTexture` / `MTLSamplerState` 管理
- `MetalFormat.{h,mm}` — VkFormat→MTLPixelFormat / GL→MTLVertexFormat 映射
- `MetalSwapchain.{h,mm}` — `CAMetalLayer` drawable 管理
- `MetalQueries.{h,mm}` — occlusion query / timer query
- `MetalBackend.mm` — C API 入口（`dmt_*` 函数，对接 Backend.h 契约）

### MG_Backend/DirectVulkan/ — Vulkan 1.2 + MoltenVK 后端

- `Device.{h,cpp}` — `VkInstance`/`VkDevice`/`VkQueue`/`VkCommandPool` 生命周期
- `Resources.{h,cpp}` — `VkBuffer`/`VkImage`/`VkImageView`/`VkSampler` 管理
- `Pipeline.{h,cpp}` — `VkShaderModule` 构建 + `VkPipeline` 哈希缓存
- `CommandStream.{h,cpp}` — `VK_KHR_dynamic_rendering` 动态渲染通道编排
- `DescriptorSet.{h,cpp}` — SPIRV-Cross 反射 UBO/sampler 绑定
- `SwapchainCommon.cpp` / `SwapchainMetal.mm` — swapchain 逻辑
- `FormatMap.{h,cpp}` — GL internalFormat → VkFormat 映射
- `Std140.{h,cpp}` — std140 UBO 打包（与 DirectMetal 共享）
- `Reflect.{h,cpp}` — SPIR-V 反射辅助（与 DirectMetal 共享）

### egl/ — EGL 1.5 实现

- `egl.cpp` — 跨平台 EGL 1.5 核心（~44 个 `egl*` 入口）
- `SurfaceMetal.mm` — Apple `CAMetalLayer` surface 创建

## 后端选择

构建时通过 CMake 选项控制：

```bash
# Apple 构建：默认 DirectMetal（不需要 MoltenVK）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Apple 构建：显式选择 Vulkan 后端（需要 MoltenVK.xcframework）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMITHRIL_BACKEND=vulkan

# Apple 构建：双后端（运行时通过 MITHRIL_BACKEND 环境变量选择）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMITHRIL_BACKEND=dual
```

运行时通过环境变量选择：

```bash
export MITHRIL_BACKEND=metal   # DirectMetal（默认）
export MITHRIL_BACKEND=vulkan  # Vulkan + MoltenVK
```

## 目录结构

```
.
├── CMakeLists.txt                 # 顶层构建脚本
├── .github/workflows/build.yml    # CI：iOS 交叉编译 + macOS 冒烟测试
├── Mithril-Wrapper-cpp/
│   ├── MG_Impl/                   # OpenGL 4.6 Core Profile 实现
│   ├── MG_State/                  # GL 状态机
│   ├── MG_Backend/
│   │   ├── Backend.h              # 后端抽象 C API 契约
│   │   ├── BackendTypes.h         # 共享类型 / 限制常量
│   │   ├── DirectMetal/           # DirectMetal 后端（SPIR-V→MSL→Metal）
│   │   └── DirectVulkan/          # Vulkan 1.2 + MoltenVK 后端
│   ├── egl/                       # EGL 1.5 实现
│   ├── include/                   # 对外公共头
│   └── 3rdparty/                  # Git 子模块（glslang、SPIRV-Cross、SPIRV-Headers）
```

## 依赖

- **CMake ≥ 3.22**
- **C++20** 编译器（clang / Apple clang）
- **Metal 框架**（DirectMetal 后端）— 直接使用 `MTLDevice` / `MTLCommandQueue` 等
- **MoltenVK.xcframework**（仅 Vulkan 后端）— CI 自动下载 v1.4.2
- Git 子模块：glslang、SPIRV-Cross、SPIRV-Headers

## 本地构建

### 1. 克隆（带子模块）

```bash
git clone --recursive https://github.com/MithrilWrapper-Dev/Mithril-Wrapper.git
cd Mithril-Wrapper
```

### 2. 配置 & 构建（macOS 原生，DirectMetal 后端）

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
# 产物：build/libmithril.dylib
```

### 3. 交叉编译 iOS arm64

```bash
curl -fsSL -o ios.toolchain.cmake \
  https://raw.githubusercontent.com/leetal/ios-cmake/master/ios.toolchain.cmake

cmake -S . -B build-ios \
  -DCMAKE_TOOLCHAIN_FILE=ios.toolchain.cmake \
  -DPLATFORM=OS64 \
  -DDEPLOYMENT_TARGET=15.0 \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build-ios -j
# 产物：build-ios/libmithril.dylib（arm64 iOS）
```

## CI

GitHub Actions 工作流 [`.github/workflows/build.yml`](.github/workflows/build.yml)
在 `macos-latest` runner 上：

1. 交叉编译 iOS arm64 `libmithril.dylib`（部署目标 15.0）
2. macOS 原生构建 + dlopen 冒烟测试（`tests/gl_smoke.c` + `tests/render_smoke.c`）

## 致谢

- [MobileGlues](https://github.com/MobileGL-Dev/MobileGlues) — 目录结构与 GL 状态管理参考
- [MobileGL](https://github.com/MobileGL-Dev/MobileGL) — Vulkan 渲染器架构参考
- [KhronosGroup/MoltenVK](https://github.com/KhronosGroup/MoltenVK) — Vulkan 1.2 over Metal（Vulkan 后端）
- [KhronosGroup/glslang](https://github.com/KhronosGroup/glslang) — GLSL→SPIR-V 编译器前端
- [KhronosGroup/SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross) — SPIR-V 反射 + MSL 翻译（DirectMetal 后端）
- [KhronosGroup/SPIRV-Headers](https://github.com/KhronosGroup/SPIRV-Headers) — SPIR-V 头文件
- [leetal/ios-cmake](https://github.com/leetal/ios-cmake) — iOS CMake 工具链

## 开发者

- **EternityQwQ**
- **yitenchen123**
- **Uniaball**

## 许可

详见 [LICENSE](LICENSE)。
