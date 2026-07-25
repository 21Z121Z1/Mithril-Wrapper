# Mithril-Wrapper

> OpenGL 3.3 Core Profile → Vulkan 1.2 (via MoltenVK) → Metal 翻译层，让依赖桌面
> OpenGL 的应用能在仅有 Metal 后端的 iOS 上运行。

Mithril-Wrapper 把宿主程序发出的 **OpenGL 3.3 Core Profile** 调用实时翻译成
**Vulkan 1.2** 调用，再由 **MoltenVK**（静态链接）将 Vulkan 调用交叉翻译为
**Metal 2** 调用。库自带一套基于 **Vulkan + CAMetalLayer** 的 **EGL 1.5** 实现，
让 LWJGL 3 / PojavLauncher / Amethyst-iOS 这类靠 `eglCreateContext` 等 EGL 入口
拉起 GL 上下文的启动器可以直接 `dlopen` 本库。着色器走
`GLSL → SPIR-V` 即时转译管线 —— MoltenVK 在 `vkCreateShaderModule` 时内部把
SPIR-V 交叉翻译成 MSL，无需在项目里再集成 SPIRV-Cross：

```
GLSL 源码  ──glslang──▶  SPIR-V  ──vkCreateShaderModule──▶  [MoltenVK SPIR-V→MSL]  ──▶  MTLLibrary
```

项目结构参考了 [MobileGlues](https://github.com/MobileGL-Dev/MobileGlues) 的
`MobileGlues-cpp/` 布局，但目标 API 不同：MobileGlues 做的是
`桌面 GLSL → GLSL ES` ；Mithril-Wrapper 直接落到 **Vulkan 1.2**，
通过 MoltenVK 静态链接到 Metal 2，且自带 EGL，不再依赖 ANGLE 的
`libEGL.framework`。

## 功能概览

- 对外暴露一整套 `extern "C"` 的 OpenGL 3.3 Core 入口（`glDraw*`、
  `glBindBuffer`、`glTexImage2D`、`glUniform*`、`glGetString*` 等），
  可作为动态库 `libmithril.dylib` 被 `dlopen` 注入。
- `glGetString(GL_VERSION)` 返回
  `3.3 §bMithril-Wrapper§r 1.0 (Vulkan 1.2 / MoltenVK)`，
  `glGetIntegerv(GL_MAJOR_VERSION/GL_MINOR_VERSION)` 返回 `3 / 3`，
  `GL_CONTEXT_PROFILE_MASK` 返回 `GL_CONTEXT_CORE_PROFILE_BIT`。
- **自带 EGL 1.5（Vulkan 后端）**：`egl/egl.mm` 导出 21 个 `egl*` 入口
  （`eglGetDisplay` / `eglInitialize` / `eglChooseConfig` / `eglCreateContext`
  / `eglCreateWindowSurface` / `eglMakeCurrent` / `eglSwapBuffers` …），宿主启动器
  （如 Amethyst-iOS 的 `Natives/ctxbridges/gl_bridge.m`）可直接 `dlsym` 解析。
  EGLDisplay 映射到单例 Vulkan 实例/设备；EGLSurface 包装 `CAMetalLayer` +
  `mithril::vk::Swapchain*`，每帧由 `vkAcquireNextImageKHR` 拉取的
  `VkImageView` 直接挂到 GL 状态机的默认帧缓冲（FBO 0）上，GL 绘制命令因此直接
  渲染到屏幕 drawable；EGLContext 各自持有独立的 `mithril::GLState`，
  `eglMakeCurrent` 切换 `mithril::g_state` 指向当前上下文。
- Vulkan 后端（`MG_Backend/DirectVulkan/`）：
  - `Device` —— `VkInstance` / `VkPhysicalDevice` / `VkDevice` / `VkQueue` /
    `VkCommandPool` 生命周期与端口性枚举（`VK_KHR_portability_enumeration` +
    `VK_KHR_portability_subset` + `VK_KHR_swapchain` + `VK_KHR_dynamic_rendering`）。
  - `Resources` —— `VkBuffer` / `VkImage` / `VkImageView` / `VkSampler` 按
    GL 名字托管 + 暂存上传路径 + GL internalFormat → VkFormat 映射。
  - `Pipeline` —— 从 SPIR-V 构建 `VkShaderModule`，并按
    `(程序, 顶点格式, 附件格式, 混合状态, 图元模式)` 的哈希签名缓存
    `VkGraphicsPipeline`。
  - `Swapchain` —— 通过 `VK_EXT_metal_surface` (`vkCreateMetalSurfaceEXT`) 把
    `CAMetalLayer` 包成 `VkSurfaceKHR` + `VkSwapchainKHR`，并管理深度/模板
    `VkImage`/`VkImageView`（`VK_FORMAT_D32_SFLOAT_S8_UINT`）。
  - `CommandStream` —— Vulkan dynamic rendering (`VK_KHR_dynamic_rendering`)
    的渲染通道编排与命令缓冲区管理。
- 着色器转译（`MG_Impl/Shader.cpp`）：线程安全地调用 glslang 把 GLSL 3.30
  编译成 Vulkan SPIR-V（`EShClientVulkan` + `EShTargetVulkan_1_2` +
  `EShTargetSpv_1_5`），并在预处理阶段注入 `MG_MITHRIL` /
  `MG_MITHRIL_VERSION` 宏以及 `glBindAttribLocation` 映射的
  `layout(location=N)`。MoltenVK 在 `vkCreateShaderModule` 内部把 SPIR-V
  交叉翻译成 MSL，所以项目里不再需要 SPIRV-Cross。

## 最低硬件 / 系统要求

| 项 | 要求 | 说明 |
|---|---|---|
| SoC | **Apple A11** 及以上 | iPhone 8 / 8 Plus / X 起步；A11 是首个支持 Metal 2 的芯片，也是 MoltenVK 1.2 portability subset 的最低起步 |
| 系统 | **iOS / iPadOS 14.0** 及以上 | CI 默认部署目标 `14.0`，对应 MSL 2.3 |
| 架构 | **arm64** | CI 仅构建 `PLATFORM=OS64`；不支持 armv7/armv7s |
| Vulkan | **Vulkan 1.2 (MoltenVK 静态链接)** | CI 从 KhronosGroup/MoltenVK release 拉取 `MoltenVK.xcframework`，CMake 用 `find_library` 解析 |
| MSL | 目标 **MSL 2.3** | iOS 14 对应的 Metal Shading Language 版本；MoltenVK 自动选择 |
| 宿主 | 任意支持 `dlopen` 注入渲染器的启动器 | 已验证可对接 Amethyst-iOS（`ui/fcl-versionmgr` 系） |

Vulkan 后端只使用 Vulkan 1.2 核心 + `VK_KHR_dynamic_rendering` +
portability subset 所需的最小扩展集，**不依赖 Vulkan 1.3 的同步、动态渲染
核心提升等可选特性**，因此 A11 / iOS 14 设备上可完整运行 Minecraft Java
Edition 的现代渲染管线。

> [!WARNING]
> **低于 A11 的设备（A7 / A8 / A8X / A9 / A10）不受支持：A7–A8 仅支持
> Metal 1.x，MoltenVK 1.2 portability subset 在 A9–A10 上虽可启动但缺少
> 本实现依赖的若干 `VK_FORMAT_D32_SFLOAT_S8_UINT` 性能优化路径。最低起步即
> iPhone 8 / iPhone X（A11, Metal 2, iOS 14）**。

## 目录结构

```
.
├── CMakeLists.txt                 # 顶层构建脚本（add_subdirectory glslang + find MoltenVK.xcframework）
├── .gitmodules                    # glslang 子模块
├── .github/workflows/build.yml    # CI：macOS arm64 交叉编译 iOS dylib
├── Mithril-Wrapper-cpp/           # 源码根（参考 MobileGlues 的布局）
│   ├── MG_Impl/                   # OpenGL 3.3 Core Profile 实现（Vulkan 后端）
│   │   ├── includes.h             #   新版全局内部头
│   │   ├── init.cpp  gl.cpp  Getter.cpp  Program.cpp  Shader.cpp
│   │   ├── Buffer.cpp  Texture.cpp  Framebuffer.cpp  Drawing.cpp
│   │   ├── VertexArray.cpp  Stubs.cpp  Debug.cpp  lookup.cpp
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
│   │       ├── Swapchain.{h,cpp}  #   VK_EXT_metal_surface + VkSwapchainKHR
│   │       └── CommandStream.{h,cpp}  # 动态渲染通道编排
│   ├── egl/                       # EGL 1.5（Vulkan 后端，Objective-C++ .mm）
│   │   └── egl.mm                 #   21 个 egl* 入口 + VK_EXT_metal_surface 包装
│   ├── include/                   # 对外公共头
│   │   ├── GL/                    #   gl.h、glcorearb.h
│   │   ├── KHR/                   #   khrplatform.h
│   │   └── EGL/                   #   egl.h（自带 EGL 类型 + PFNEGL*PROC typedef）
│   └── 3rdparty/                  # Git 子模块
│       └── glslang                # GLSL → SPIR-V 前端（SPIRV-Cross 已不再需要）
```

## 依赖

- **CMake ≥ 3.22**
- **C++20** 编译器（clang / Apple clang）
- **MoltenVK.xcframework**（静态，Vulkan 1.2 over Metal 2）— CI 自动下载，
  本地构建请从 [KhronosGroup/MoltenVK releases](https://github.com/KhronosGroup/MoltenVK/releases)
  下载 `MoltenVK-vX.Y.Z.tar.gz` 解压后放到仓库根目录的 `MoltenVK.xcframework`
  或通过 `-DMOLTENVK_ROOT=/path/to/extracted` 指定。
- **Apple Metal 框架**（仅用于 `CAMetalLayer`，由 MoltenVK 内部调用）
- Git 子模块（glslang），见 `.gitmodules`

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
# 选一个 tagged release（示例用 v1.2.9）
MOLTENVK_TAG="v1.2.9"
curl -fsSL -o MoltenVK.tar.gz \
  "https://github.com/KhronosGroup/MoltenVK/releases/download/${MOLTENVK_TAG}/MoltenVK-${MOLTENVK_TAG}.tar.gz"
tar -xzf MoltenVK.tar.gz
mv "MoltenVK-${MOLTENVK_TAG}/MoltenVK.xcframework" ./MoltenVK.xcframework
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

# 配置（iOS arm64，默认仅设备架构）
cmake -S . -B build-ios \
  -DCMAKE_TOOLCHAIN_FILE=../ios.toolchain.cmake \
  -DPLATFORM=OS64 \
  -DDEPLOYMENT_TARGET=14.0 \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build-ios -j
# 产物：build-ios/libmithril.dylib （arm64 iOS）
```

构建产物是单个 `libmithril.dylib`，可注入到目标进程的 OpenGL / EGL 加载路径中。
该 dylib 同时导出 `gl*`（OpenGL 3.3 Core）与 `egl*`（EGL 1.5）符号，宿主启动器
只需 `dlopen("@rpath/libmithril.dylib", RTLD_NOW)` 即可同时拿到两套入口。
MoltenVK 静态链接进 dylib，所以**不需要在目标设备上额外安装 Vulkan ICD 或
`VK_ICD_FILENAMES`**。

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
   Vulkan 1.2 → MoltenVK → Metal 2。

对应分支：[`Amethyst-IOS`](https://github.com/EternityQwQ/Amethyst-iOS/tree/Amethyst-IOS)
（基于 `herbrine8403/Amethyst-iOS-MyRemastered@ui/fcl-versionmgr`）。

## CI

GitHub Actions 工作流 [`.github/workflows/build.yml`](.github/workflows/build.yml)
会在 `macos-latest` runner 上：

1. 检出仓库（带子模块）。
2. 下载 `ios.toolchain.cmake` 工具链。
3. 下载 `MoltenVK.xcframework`（静态）到仓库根目录。
4. 用 ios-cmake 工具链交叉编译 iOS arm64 的 `libmithril.dylib`。
5. 用 `nm -gU` 校验所有 `egl*` 入口都进入导出表。
6. 上传 `libmithril.dylib` 为 artifact。

每次推送到 `main` 都会触发。

## 致谢

- [MobileGlues](https://github.com/MobileGL-Dev/MobileGlues) —— 目录结构与
  GL 状态管理思路的参考。
- [KhronosGroup/MoltenVK](https://github.com/KhronosGroup/MoltenVK) ——
  Vulkan 1.2 over Metal 2 实现，本项目静态链接其 .xcframework。
- [KhronosGroup/glslang](https://github.com/KhronosGroup/glslang) ——
  GLSL 参考编译器前端，用于 GLSL → SPIR-V 即时翻译。
- [KhronosGroup/Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) ——
  Vulkan 头文件（随 MoltenVK.xcframework 一起分发）。
- [leetal/ios-cmake](https://github.com/leetal/ios-cmake) —— iOS CMake 工具链。

## 开发者

- **EternityQwQ**
- **yitenchen123**
- **Uniaball**

## 许可

详见 [LICENSE](LICENSE)。
