# Mithril-Wrapper

[English](README.md)

Mithril-Wrapper 是面向 Apple 平台上 Minecraft Java 和 LWJGL 的 EGL 与
OpenGL 兼容层。它把可观察到的 API 行为解析成明确的后端无关绘制与资源
意图，再由原生后端执行这些意图。

Apple 平台的交付目标使用 **DirectMetal**。Vulkan 目标是独立的参考后端。
Linux CI 使用 Vulkan 目标进行跨后端回归。Apple 构建可以将它用于明确的
研究任务，但它不属于 DirectMetal 交付产物。

Mithril-Wrapper 不声称具备通用 OpenGL 一致性。导出符号、可接受的调用、
聚焦测试、Minecraft 验收和一致性认证是不同层级的证据。

## 架构

```text
Minecraft / LWJGL 可观察行为
                 |
                 v
          EGL 与宿主生命周期
                 |
                 v
   OpenGL 状态、对象、错误和着色器
                 |
                 v
       已解析的后端无关意图
                 |
          +------+------+
          |             |
          v             v
     DirectMetal      Vulkan
        交付           参考
          |             |
          +------+------+
                 v
            平台呈现
```

语义所有者如下：

- `src/egl`：EGL 对象、生命周期和宿主 surface 边界。
- `src/gl` 与 `src/state`：OpenGL 可见的状态和对象行为。
- `src/shader`：着色器翻译、反射和链接接口。
- `src/backend`：供原生执行使用的不可变意图和资源身份。
- `src/metal`：DirectMetal 执行。
- `src/vk`：Vulkan 执行。

通用的 EGL、OpenGL 或着色器规则不得分别由 Metal 和 Vulkan 解释。参见
[架构](docs/architecture.zh-CN.md)。

## 构建与测试

首次构建前先克隆子模块。

### Linux Vulkan 参考后端

```bash
git submodule update --init --recursive
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DMITHRIL_BUILD_LEGACY=ON -DMITHRIL_BUILD_DIRECT=OFF
cmake --build build --parallel
ctest --test-dir build -L vulkan --output-on-failure
```

### macOS DirectMetal

```bash
git submodule update --init \
  third_party/SPIRV-Cross third_party/SPIRV-Headers third_party/glslang
cmake -S . -B build-direct -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DMITHRIL_BUILD_LEGACY=OFF -DMITHRIL_BUILD_DIRECT=ON \
  -DMITHRIL_OUTPUT_DIRECTORY="$PWD/build-direct/artifacts"
cmake --build build-direct --parallel
ctest --test-dir build-direct -L directmetal --output-on-failure
scripts/verify_directmetal_artifact.sh \
  build-direct/artifacts/libmithril.dylib \
  build-direct/mithril_direct.boundary.json
```

需要把库放到默认 `output` 目录之外时，设置
`MITHRIL_OUTPUT_DIRECTORY`。

### iPhoneOS DirectMetal 包

打包脚本需要 Xcode 和一个 iPhoneOS CMake toolchain 文件。GitHub Actions
会在运行脚本前获取固定版本的 `ios-cmake` toolchain。

```bash
MITHRIL_IOS_TOOLCHAIN_FILE=/path/to/ios.toolchain.cmake \
  scripts/build_iphoneos.sh
```

## 证据

常规 `build-mithril` Workflow 为 `main` 及面向 `main` 的 Pull Request
提供三个互相独立的门禁：

1. DirectMetal macOS 语义和无 Vulkan 的产物边界。
2. DirectMetal iPhoneOS arm64 打包与 ABI 检查。
3. Linux 上的 Vulkan 参考后端回归。

手动 `platform-runtime-validation` Workflow 提供 托管 Apple Silicon Metal 和
iOS Simulator 运行时证据。只有当声明需要该平台运行环境时才运行它。

聚焦的 CTest 程序证明小范围语义不变量。它们本身不能证明真实的
Minecraft 画面。真实 Minecraft 证据也不能替代底层规则的聚焦回归。参见
[验证](docs/validation.zh-CN.md)。

## Minecraft 参考源码

该辅助脚本下载 Mojang 客户端 JAR，验证发布的哈希，并为调查创建本地反编译
源码树：

```bash
SRC="$(bash scripts/minecraft-reference.sh --print-path)"
```

生成的 `.minecraft-reference` 目录已被 Git 忽略。不要提交或上传其中内容。

## 开发

进行仓库级修改前先读 [AGENTS.md](AGENTS.md)。产品工作使用 `main`。
历史 `archive/*` 引用只用于保存来源。应从 GitHub 查询实时分支、PR 和
Actions 状态，不要从旧文档推断当前状态。
