# Mithril-Wrapper

> OpenGL 3.3 Core Profile → Vulkan 1.2 (via MoltenVK) → Metal 翻译层，让依赖桌面
> OpenGL 的应用能在仅有 Metal 后端的 iOS 上运行。

Mithril-Wrapper 把宿主程序发出的 **OpenGL 3.3 Core Profile** 调用实时翻译成
**Vulkan 1.2** 调用，再由 **MoltenVK**（静态链接）将 Vulkan 调用交叉翻译为
**Metal** 调用。库自带一套基于 **Vulkan + CAMetalLayer** 的 **EGL 1.5** 实现，
让 LWJGL 3 / PojavLauncher / Amethyst-iOS 这类靠 `eglCreateContext` 等 EGL 入口
拉起 GL 上下文的启动器可以直接 `dlopen` 本库。着色器走
`GLSL → SPIR-V` 即时转译管线 —— MoltenVK 在 `vkCreateShaderModule` 时内部把
SPIR-V 交叉翻译成 MSL：

```
GLSL 源码  ──glslang──▶  SPIR-V  ──vkCreateShaderModule──▶  [MoltenVK SPIR-V→MSL]  ──▶  MTLLibrary
```

项目结构参考了 [MobileGlues](https://github.com/MobileGL-Dev/MobileGlues) 的
`MobileGlues-cpp/` 布局，但目标 API 不同：MobileGlues 做的是
`桌面 GLSL → GLSL ES` ；Mithril-Wrapper 直接落到 **Vulkan 1.2**，
通过 MoltenVK 静态链接到 Metal，且自带 EGL，不再依赖 ANGLE 的
`libEGL.framework`。

## 功能概览

- 对外暴露一整套 `extern "C"` 的 OpenGL 3.3 Core 入口（`glDraw*`、
  `glBindBuffer`、`glTexImage2D`、`glUniform*`、`glGetString*` 等），
  可作为动态库 `libmithril.dylib` 被 `dlopen` 注入。
- `glGetString(GL_VERSION)` 返回
  `3.3 §bMithril-Wrapper§r 1.0 (Vulkan 1.2 / MoltenVK)`，
  `glGetIntegerv(GL_MAJOR_VERSION/GL_MINOR_VERSION)` 返回 `3 / 3`，
  `GL_CONTEXT_PROFILE_MASK` 返回 `GL_CONTEXT_CORE_PROFILE_BIT`。
- **自带 EGL 1.5（Vulkan 后端）**：`egl/egl.cpp` 导出 ~44 个 `egl*` 入口
  （EGL 1.5 全套：`eglGetDisplay` / `eglInitialize` / `eglChooseConfig` /
  `eglCreateContext` / `eglCreateWindowSurface` / `eglMakeCurrent` /
  `eglSwapBuffers` + EGL 1.5 Sync / Image / Platform Surface API …），宿主启动器
  （如 Amethyst-iOS 的 `Natives/ctxbridges/gl_bridge.m`）可直接 `dlsym` 解析。
  EGLDisplay 映射到单例 Vulkan 实例/设备；EGLSurface 包装原生窗口
  （`CAMetalLayer`）+ `mithril::vk::Swapchain*`，
  每帧由 `vkAcquireNextImageKHR` 拉取的 `VkImageView` 直接挂到 GL 状态机的默认
  帧缓冲（FBO 0）上，GL 绘制命令因此直接渲染到屏幕 drawable；EGLContext 各自
  持有独立的 `mithril::GLState`，`eglMakeCurrent` 切换 `mithril::g_state` 指向
  当前上下文。新增的 EGL 1.5 Sync/Image API 为**影子实现**（参考 MobileGL，仅
  维护状态层，不创建真实 `VkFence` / `VkImage`）；平台相关 surface 创建由
  `SurfaceMetal.mm` 承担，覆盖 Apple (Metal) 平台。
- Vulkan 后端（`MG_Backend/DirectVulkan/`）：
  - `Device` —— `VkInstance` / `VkPhysicalDevice` / `VkDevice` / `VkQueue` /
    `VkCommandPool` 生命周期与端口性枚举（`VK_KHR_portability_enumeration` +
    `VK_KHR_portability_subset` + `VK_KHR_swapchain` + `VK_KHR_dynamic_rendering`）。
  - `Resources` —— `VkBuffer` / `VkImage` / `VkImageView` / `VkSampler` 按
    GL 名字托管 + 暂存上传路径 + GL internalFormat → VkFormat 映射。
  - `Pipeline` —— 从 SPIR-V 构建 `VkShaderModule`，并按
    `(程序, 顶点格式, 附件格式, 混合状态, 图元模式)` 的哈希签名缓存
    `VkGraphicsPipeline`。
  - `Swapchain` —— 拆分为 `SwapchainCommon.cpp`（共享后处理：格式查询 /
    `vkCreateSwapchainKHR` / image views / 深度模板）+ `SwapchainMetal.mm`
    （Apple，`VK_EXT_metal_surface` 把 `CAMetalLayer` 包成 `VkSurfaceKHR`）。
    深度/模板格式 `VK_FORMAT_D32_SFLOAT_S8_UINT`。`SwapchainMetal.mm` 编译为
    Objective-C++（`.mm`），因为需要 `VK_USE_PLATFORM_METAL_EXT` 宏定义。
  - `CommandStream` —— Vulkan dynamic rendering (`VK_KHR_dynamic_rendering`)
    的渲染通道编排与命令缓冲区管理。
  - `DescriptorSet` —— 通过 SPIRV-Cross 反射 VS+FS 的 SPIR-V，发现 UBO 和
    sampled image 绑定，构建 `VkDescriptorSetLayout` / `VkPipelineLayout` /
    `VkDescriptorPool`。
  - `FormatMap` —— GL internalFormat 到 VkFormat 的映射表。
  - `ImageOps` —— 纹理的 blit、mipmap 生成、readPixels 等图像操作。
  - `Reflect` —— SPIR-V 反射辅助工具，封装 SPIRV-Cross 的常用反射查询。
- 着色器转译（`MG_Impl/Shader.cpp`）：线程安全地调用 glslang 把 GLSL 3.30
  编译成 Vulkan SPIR-V（`EShClientVulkan` + `EShTargetVulkan_1_2` +
  `EShTargetSpv_1_5`），并在预处理阶段注入 `MG_MITHRIL` /
  `MG_MITHRIL_VERSION` 宏以及 `glBindAttribLocation` 映射的
  `layout(location=N)`。自动将 GLSL 版本升级到 330+。MoltenVK 在
  `vkCreateShaderModule` 内部把 SPIR-V 交叉翻译成 MSL。

## 最低硬件 / 系统要求

| 项 | 要求 | 说明 |
|---|---|---|
| SoC | **Apple A11** 及以上 | iPhone 8 / 8 Plus / X 起步；A11 是首个支持 Metal 的芯片，也是 MoltenVK 1.2 portability subset 的最低起步 |
| 系统 | **iOS / iPadOS 15.0** 及以上 | CI 默认部署目标 `15.0`（MoltenVK v1.4.x 已提升最低版本） |
| 架构 | **arm64** | CI 仅构建 `PLATFORM=OS64`；不支持 armv7/armv7s |
| Vulkan | **Vulkan 1.2 (MoltenVK 静态链接)** | CI 从 KhronosGroup/MoltenVK release 拉取 `MoltenVK.xcframework`，CMake 用 `find_library` 解析 |
| MSL | 目标 **MSL 2.3** | iOS 15 对应的 Metal Shading Language 版本；MoltenVK 自动选择 |
| 宿主 | 任意支持 `dlopen` 注入渲染器的启动器 | 已验证可对接 Amethyst-iOS（`ui/fcl-versionmgr` 系） |

Vulkan 后端只使用 Vulkan 1.2 核心 + `VK_KHR_dynamic_rendering` +
portability subset 所需的最小扩展集，**不依赖 Vulkan 1.3 的同步、动态渲染
核心提升等可选特性**，因此 A11 / iOS 15 设备上可完整运行 Minecraft Java
Edition 的现代渲染管线。

> [!WARNING]
> **低于 A11 的设备（A7 / A8 / A8X / A9 / A10）不受支持：A7–A8 仅支持
> Metal 1.x，MoltenVK 1.2 portability subset 在 A9–A10 上虽可启动但缺少
> 本实现依赖的若干 `VK_FORMAT_D32_SFLOAT_S8_UINT` 性能优化路径。最低起步即
> iPhone 8 / iPhone X（A11, Metal, iOS 15）**。

## 架构分层

### MG_Impl/ — OpenGL 3.3 Core Profile 入口点

GL 调用的具体实现层。每个 `gl*` 函数通过 `MG_Backend/Backend.h` 定义的 C API
调用 Vulkan 后端。主要文件：

- `gl.cpp` —— 核心 GL 状态切换入口（`glBind*`、`glEnable/Disable` 等）
- `Buffer.cpp` —— `glGenBuffers`、`glBindBuffer`、`glBufferData` 等
- `Texture.cpp` —— `glGenTextures`、`glTexImage2D`、`glTexParameter` 等
- `Drawing.cpp` —— `glDrawArrays`、`glDrawElements`、`glClear` 等
- `Program.cpp` —— `glCreateProgram`、`glLinkProgram`、`glUseProgram` 等
- `Shader.cpp` —— `glCreateShader`、`glShaderSource`、`glCompileShader`（含 GLSL→SPIR-V 编译）
- `Framebuffer.cpp` —— `glGenFramebuffers`、`glFramebufferTexture2D` 等
- `VertexArray.cpp` —— `glGenVertexArrays`、`glVertexAttribPointer` 等
- `Getter.cpp` / `Getter_gpu.mm` —— `glGetString`、`glGetIntegerv` 等查询
- `EGLConfig.cpp` —— EGL 配置匹配表（纯逻辑，可单元测试）
- `init.cpp` —— 全局初始化与清理
- `Stubs.cpp` —— 未实现的 GL 入口桩
- `lookup.cpp` —— `glXGetProcAddress` 入口查找
- `Log.cpp` / `Log.h` —— 日志基础设施
- `Debug.cpp` —— 调试辅助

### MG_State/ — GL 状态机

`GLState` 结构体持有所有 GL 状态、对象表（buffer、texture、shader、program、
framebuffer、VAO），以及 EGL 默认帧缓冲的 `VkImageView`。每个 `EGLContext`
拥有独立的 `GLState`，`eglMakeCurrent` 切换 `mithril::g_state` 全局指针。

### MG_Backend/DirectVulkan/ — Vulkan 1.2 + MoltenVK 后端实现

- `Device.cpp/h` —— `VkInstance`/`VkDevice`/`VkQueue`/`VkCommandPool` 生命周期
- `Resources.cpp/h` —— `VkBuffer`/`VkImage`/`VkImageView`/`VkSampler` 管理
- `Pipeline.cpp/h` —— `VkShaderModule` 构建 + `VkPipeline` 哈希缓存
- `CommandStream.cpp/h` —— `VK_KHR_dynamic_rendering` 动态渲染通道编排
- `DescriptorSet.cpp/h` —— SPIRV-Cross 反射 UBO/sampler 绑定，构建描述符布局
- `Swapchain.h` / `SwapchainCommon.cpp` —— 共享 swapchain 逻辑（格式查询 /
  `vkCreateSwapchainKHR` / image views / 深度模板）
- `SwapchainMetal.mm` —— Apple 路径（`VK_EXT_metal_surface`，Objective-C++）
- `FormatMap.cpp/h` —— GL internalFormat → VkFormat 映射
- `ImageOps.cpp` —— blit、mipmap 生成、readPixels 等图像操作
- `Reflect.cpp/h` —— SPIR-V 反射辅助（封装 SPIRV-Cross）

### egl/ — EGL 1.5 实现

- `egl.cpp` —— 跨平台 EGL 1.5 核心（~44 个 `egl*` 入口 + 影子 sync/image 对象）。
  无平台相关 include；surface 创建通过 `surface_create()` / `surface_get_size()`
  分派到平台文件。EGLDisplay → 单例 Vulkan 实例/设备；EGLSurface → 原生窗口 +
  `VkSwapchainKHR`；EGLContext → 独立 `GLState`。
- `SurfaceMetal.mm` —— Apple `CAMetalLayer` 强制转换（`CALayer` → `CAMetalLayer`）
  + drawable 尺寸查询（Objective-C++）

## 实现细节

### GL 状态机

`GLState` 结构体（`MG_State/State.h`）包含：
- 所有 GL 开关状态（blend、depth test、cull face、stencil 等）
- 对象表：`std::unordered_map<GLuint, ...>` 管理 buffers、textures、shaders、
  programs、framebuffers、VAOs、samplers
- EGL 默认帧缓冲 `VkImageView`（color + depth/stencil）
- 每上下文隔离，`eglMakeCurrent` 切换全局 `g_state` 指针

### 着色器翻译

`Shader.cpp` 实现 GLSL → SPIR-V 编译管线：
1. 预处理阶段注入 `MG_MITHRIL` / `MG_MITHRIL_VERSION` 宏
2. 自动升级 GLSL 版本到 330+（Vulkan GLSL 最低要求）
3. 应用 `glBindAttribLocation` 映射，注入 `layout(location=N)`
4. 调用 glslang 编译（`EShClientVulkan` + `EShTargetVulkan_1_2` +
   `EShTargetSpv_1_5`）
5. 线程安全（`std::mutex` 保护），带缓存

MoltenVK 在 `vkCreateShaderModule` 时自动将 SPIR-V 交叉翻译为 MSL。

### SPIRV-Cross 用法

SPIRV-Cross **仅用于 SPIR-V 反射**（非翻译）。`DescriptorSet.cpp` 在程序链接后
反射 VS+FS 的 SPIR-V，发现 UBO 和 sampled image 的 binding 信息，进而构建
`VkDescriptorSetLayout` / `VkPipelineLayout` / `VkDescriptorPool`。
实际的 SPIR-V → MSL 翻译仍由 MoltenVK 在 `vkCreateShaderModule` 时内部完成。

### Vulkan 动态渲染

`CommandStream.cpp` 使用 `VK_KHR_dynamic_rendering` 扩展替代传统
`VkRenderPass` / `VkFramebuffer` 对象，简化渲染通道管理。

### 管线缓存

`Pipeline.cpp` 按 `(program, 顶点格式, 附件 VkFormat 列表, 混合状态, 图元模式)`
的哈希签名缓存 `VkGraphicsPipeline`，避免重复创建。

### EGL 实现

`egl/egl.cpp` 提供完整 EGL 1.5 实现（~44 个 `egl*` 入口，EGL 1.5 全套）：
- `EGLDisplay` → 单例 Vulkan 实例/设备
- `EGLSurface` → 原生窗口 + `VkSwapchainKHR`（平台对应：Apple
  `VK_EXT_metal_surface`）
- `EGLContext` → 独立 `GLState`，`eglMakeCurrent` 切换上下文
- `eglSwapBuffers` → 提交命令 + `vkAcquireNextImageKHR` + 呈现
- EGL 1.5 Sync API（`eglCreateSync` / `eglClientWaitSync` / …）—— **影子实现**，
  参考 MobileGL：仅维护状态层（始终 signaled），不创建真实 `VkFence`
- EGL 1.5 Image API（`eglCreateImage` / `eglDestroyImage`）—— **影子实现**，
  不创建真实 `VkImage`，仅返回进程内句柄

项目**不直接使用 Metal 框架**——所有 Metal 交互通过 MoltenVK 内部完成。
`CAMetalLayer` 仅用于 `VK_EXT_metal_surface` 创建 Vulkan surface。

#### 平台支持矩阵

| 平台 | Vulkan surface 扩展 | 原生窗口 | 备注 |
|---|---|---|---|
| iOS / macOS | `VK_EXT_metal_surface` + `CAMetalLayer` | `CAMetalLayer*` | 主路径，CI 实际构建 |

`egl/Surface<Platform>.cpp/mm` 提供 `surface_create()` / `surface_get_size()`，
由 CMake 按 `APPLE` 守卫选择。

## 目录结构

```
.
├── CMakeLists.txt                 # 顶层构建脚本（add_subdirectory glslang/SPIRV-Cross + find MoltenVK.xcframework）
├── .gitmodules                    # 三个子模块：glslang、SPIRV-Cross、SPIRV-Headers
├── .github/workflows/
│   └── build.yml                  # CI：macOS arm64 交叉编译 iOS dylib
├── Mithril-Wrapper-cpp/           # 源码根（参考 MobileGlues 的布局）
│   ├── MG_Impl/                   # OpenGL 3.3 Core Profile 实现（Vulkan 后端）
│   │   ├── includes.h             #   全局内部头
│   │   ├── init.cpp  gl.cpp  Getter.cpp  Program.cpp  Shader.cpp
│   │   ├── Buffer.cpp  Texture.cpp  Framebuffer.cpp  Drawing.cpp
│   │   ├── VertexArray.cpp  Stubs.cpp  Debug.cpp  lookup.cpp
│   │   ├── EGLConfig.{h,cpp}      #   EGL 配置匹配表
│   │   ├── Getter_gpu.mm          #   GPU 名称字符串构建（读 VkPhysicalDeviceProperties）
│   │   ├── Log.{h,cpp}  Shader.{h,cpp}  Framebuffer.h
│   ├── MG_State/                  # GL 状态机
│   │   └── State.{h,cpp}
│   ├── MG_Backend/                # 抽象后端 C API（Backend.h）
│   │   ├── Backend.h
│   │   └── DirectVulkan/          # Vulkan 1.2 + MoltenVK 直接后端
│   │       ├── Device.{h,cpp}     #   VkInstance/Device/Queue/CommandPool
│   │       ├── Resources.{h,cpp}  #   VkBuffer/Image/ImageView/Sampler
│   │       ├── Pipeline.{h,cpp}   #   VkShaderModule + VkPipeline 缓存
│   │       ├── CommandStream.{h,cpp}  # 动态渲染通道编排（VK_KHR_dynamic_rendering）
│   │       ├── DescriptorSet.{h,cpp}  # SPIRV-Cross 反射描述符布局
│   │       ├── FormatMap.{h,cpp}  #   GL → VkFormat 映射
│   │       ├── ImageOps.cpp       #   图像操作（blit/mipmap/readPixels）
│   │       ├── Reflect.{h,cpp}    #   SPIR-V 反射辅助
│   │       ├── Swapchain.h            #   Swapchain 接口（平台无关）
│   │       ├── SwapchainCommon.cpp    #   共享 swapchain 逻辑（格式查询 / vkCreateSwapchainKHR / image views / 深度模板）
│   │       └── SwapchainMetal.mm      #   Apple 路径（VK_EXT_metal_surface，.mm）
│   ├── egl/                       # EGL 1.5（跨平台核心 + 平台 surface 分裂）
│   │   ├── egl.cpp                #   ~44 个 egl* 入口 + 影子 sync/image 对象（跨平台 C++）
│   │   └── SurfaceMetal.mm        #   Apple CAMetalLayer 强制转换 + 尺寸查询（.mm）
│   ├── include/                   # 对外公共头
│   │   ├── GL/                    #   gl.h、glcorearb.h
│   │   ├── KHR/                   #   khrplatform.h
│   │   └── EGL/                   #   egl.h（自带 EGL 类型 + PFNEGL*PROC typedef）
│   └── 3rdparty/                  # Git 子模块
│       ├── glslang                # GLSL → SPIR-V 前端
│       ├── SPIRV-Cross            # SPIR-V 反射（仅用于反射，非翻译）
│       └── SPIRV-Headers          # SPIR-V 头文件（spv:: 枚举）
```

## 依赖

- **CMake ≥ 3.22**
- **C++20** 编译器（clang / Apple clang）
- **MoltenVK.xcframework**（静态，Vulkan 1.2 over Metal）— CI 自动下载
  v1.4.2，本地构建请从 [KhronosGroup/MoltenVK releases](https://github.com/KhronosGroup/MoltenVK/releases)
  下载 `MoltenVK-ios.tar` 解压后放到仓库根目录的 `MoltenVK.xcframework` +
  `MoltenVK-Headers`，或通过 `-DMOLTENVK_ROOT=/path/to/extracted` 指定。
- **Metal 框架**——项目不直接使用 Metal API，仅通过 MoltenVK 内部调用。
  CAMetalLayer 用于 `VK_EXT_metal_surface` 创建 Vulkan surface。
- Git 子模块（glslang、SPIRV-Cross、SPIRV-Headers），见 `.gitmodules`
- **SPIRV-Cross** —— 运行时用于 SPIR-V 反射（`DescriptorSet.cpp` 通过
  `spirv_cross::Compiler` + `get_shader_resources` 发现 UBO/sampler 绑定）
- **SPIRV-Headers** —— 提供 `<spirv.h>`（`spv::Decoration*` 等枚举）

## 本地构建

### 1. 克隆（带子模块）

```bash
git clone --recursive https://github.com/EternityQwQ/Mithril-Wrapper.git
cd Mithril-Wrapper

# 如果已经克隆但忘了带 --recursive：
git submodule update --init --recursive
```

### 2. 准备 MoltenVK.xcframework

```bash
# 选一个 tagged release（示例用 v1.4.2）
MOLTENVK_TAG="v1.4.2"
curl -fsSL -o MoltenVK-ios.tar \
  "https://github.com/KhronosGroup/MoltenVK/releases/download/${MOLTENVK_TAG}/MoltenVK-ios.tar"
tar -xf MoltenVK-ios.tar
# 解压后找到 static MoltenVK.xcframework 和 MoltenVK-Headers 并移到仓库根目录
mv MoltenVK/MoltenVK/static/MoltenVK.xcframework ./MoltenVK.xcframework
mv MoltenVK/MoltenVK/include ./MoltenVK-Headers
```

### 3. 配置 & 构建（macOS 原生）

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
# 产物：build/libmithril.dylib
```

### 4. 交叉编译 iOS arm64 dylib

CI 使用的就是这条路径。借助
[leetal/ios-cmake](https://github.com/leetal/ios-cmake) 工具链：

```bash
# 下载工具链
curl -fsSL -o ios.toolchain.cmake \
  https://raw.githubusercontent.com/leetal/ios-cmake/master/ios.toolchain.cmake

# 配置（iOS arm64，部署目标 15.0）
cmake -S . -B build-ios \
  -DCMAKE_TOOLCHAIN_FILE=ios.toolchain.cmake \
  -DPLATFORM=OS64 \
  -DDEPLOYMENT_TARGET=15.0 \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build-ios -j
# 产物：build-ios/libmithril.dylib （arm64 iOS）
```


构建产物是单个共享库（`libmithril.dylib`），可注入到目标进程的 OpenGL / EGL
加载路径中。该库同时导出 `gl*`（OpenGL 3.3 Core）与 `egl*`（EGL 1.5，~44 个入口）
符号，宿主启动器只需 `dlopen("@rpath/libmithril.dylib", RTLD_NOW)` 即可同时拿到
两套入口。MoltenVK 静态链接进 dylib（仅 Apple 构建），所以**不需要在目标设备上
额外安装 Vulkan ICD 或 `VK_ICD_FILENAMES`**。

## 与 Amethyst-iOS 集成

本仓库的 EGL 实现专门用于对接
[Amethyst-iOS](https://github.com/EternityQwQ/Amethyst-iOS) 的 Mithril 渲染器
（`eglCreateContext` dlsym 路径）。集成步骤：

1. 把 CI 产物 `libmithril.dylib` 放到 Amethyst 应用的 `Frameworks/` 目录。
2. 在 Amethyst 的渲染器选项里选择 `Mithril`（即 `RENDERER_NAME_MTL_ANGLE`
   对应的入口），让 `gl_bridge.m` 把 `libmithril.dylib` 当作 EGL 宿主加载。
3. `egl_bridge.m` / `gl_bridge.m` 调用 `eglGetDisplay(EGL_DEFAULT_DISPLAY)` →
   `eglInitialize` → `eglChooseConfig` → `eglCreateWindowSurface(layer)` →
   `eglCreateContext` → `eglMakeCurrent`，全部由本 dylib 解析并落到
   Vulkan 1.2 → MoltenVK → Metal。

对应分支：[`Amethyst-IOS`](https://github.com/EternityQwQ/Amethyst-iOS/tree/Amethyst-IOS)
（基于 `herbrine8403/Amethyst-iOS-MyRemastered@ui/fcl-versionmgr`）。

## CI

### iOS 构建（build.yml）

GitHub Actions 工作流 [`.github/workflows/build.yml`](.github/workflows/build.yml)
会在 `macos-latest` runner 上：

1. 检出仓库（带子模块）。
2. 下载 `ios.toolchain.cmake` 工具链。
3. 下载 `MoltenVK.xcframework`（v1.4.2 静态）到仓库根目录。
4. 用 ios-cmake 工具链交叉编译 iOS arm64 的 `libmithril.dylib`（部署目标 15.0）。
5. 用 `nm -gU` 校验所有 `egl*` 入口都进入导出表。
6. 上传 `libmithril.dylib` 为 artifact。

每次推送到 `main` 都会触发。

## 致谢

- [MobileGlues](https://github.com/MobileGL-Dev/MobileGlues) —— 目录结构与
  GL 状态管理思路的参考。
- [KhronosGroup/MoltenVK](https://github.com/KhronosGroup/MoltenVK) ——
  Vulkan 1.2 over Metal 实现，本项目静态链接其 .xcframework。
- [KhronosGroup/glslang](https://github.com/KhronosGroup/glslang) ——
  GLSL 参考编译器前端，用于 GLSL → SPIR-V 即时翻译。
- [KhronosGroup/SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross) ——
  SPIR-V 反射库，用于描述符布局构建。
- [KhronosGroup/SPIRV-Headers](https://github.com/KhronosGroup/SPIRV-Headers) ——
  SPIR-V 头文件。
- [KhronosGroup/Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) ——
  Vulkan 头文件（随 MoltenVK.xcframework 一起分发）。
- [leetal/ios-cmake](https://github.com/leetal/ios-cmake) —— iOS CMake 工具链。

## 开发者

- **EternityQwQ**
- **yitenchen123**
- **Uniaball**

## 许可

详见 [LICENSE](LICENSE)。