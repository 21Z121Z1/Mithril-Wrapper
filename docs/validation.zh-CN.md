# 验证

[English](validation.md)

## 证据必须对应声明

使用能够证伪声明的最低成本测试。不得用较弱测试替代所需的更强环境。

常规顺序如下：

1. 静态检查与源码不变量。
2. 聚焦语义回归。
3. 受影响的 CTest 后端标签。
4. 构建、ABI 与打包检查。
5. 托管 Apple 运行时证据。
6. 真实 Minecraft 验收。
7. 物理设备呈现或匹配条件下的性能测量。

较低层级失败时，在解释该失败前不得作出更强声明。

## 聚焦语义测试

`tests` 下的测试是小型原生程序。CMake 在
`cmake/MithrilSmokeTests.cmake` 中注册它们。

使用聚焦测试证明 EGL、OpenGL、着色器、资源生命周期或原生执行不变量。
有效回归应在旧行为上失败，在修正行为上通过。它应检查可观察输出或明确的
结构计数器，而不只是“不崩溃”。

共享语义修改在环境支持时必须运行两个标签：

```bash
ctest --test-dir build-direct -L directmetal --output-on-failure
ctest --test-dir build -L vulkan --output-on-failure
```

后端专属修改不需要运行无关后端测试，但不得悄然改变共享契约。

## 打包与边界证据

DirectMetal 交付产物具有窄 C ABI 和无 Vulkan 的构建边界。macOS 门禁检查
动态库和生成的边界清单。iPhoneOS 门禁检查架构、平台、deployment target、
install name、导出符号和签名可行性。

macOS 测试结果不能证明 iPhoneOS 包。iPhoneOS 交叉构建也不能证明物理设备
画面。

## 平台运行时证据

手动 `platform-runtime-validation` Workflow 在 macOS 26 Apple Silicon 上
运行。一个 job 在真实托管 Metal GPU 上执行已注册 DirectMetal 测试；另一个
job 为 arm64 iOS Simulator 构建，并通过应用包运行选定 smoke test。

当修改影响 Metal 运行行为、CAMetalLayer 边界、Simulator 行为，或常规
macOS 15 矩阵无法证明的声明时，运行该 Workflow。

## Minecraft 证据

语义 oracle 和真实 Minecraft 运行回答不同问题。

- 聚焦 oracle 识别规则并防止其回归。
- 真实 Minecraft 运行证明当前客户端、启动器、宿主桥接、库身份和呈现路径
  能共同工作。

真实渲染声明需要可识别的客户端输出。仅有变化的哈希、非黑图像或合成的用户
framebuffer 图像并不足够。应在受控场景中检查地形、纹理、GUI 或 HUD 及其
空间关系。

不要提交反编译 Minecraft 源码或私有 fixture。这些输入应留在 Git 与 CI
artifact 之外。

## 性能证据

先使正确性通过。只有在条件匹配时测量性能：相同世界、相机、render distance、
客户端配置、renderer path、build identity 和测量窗口。

结构计数器可以证明一次 allocation、upload、compilation 或 state resolution
事件已被删除，但不能证明帧时间改善。没有成对测量时，不要发布百分比。

## 能力表述

使用精确术语：

- **已导出**：ABI 符号存在。
- **已接受**：调用被解析，且输入未被立即拒绝。
- **部分实现**：部分可观察情形已经实现。
- **聚焦测试覆盖**：已注册回归证明所述情形。
- **客户端验证**：具名真实客户端完成所述场景。
- **不支持**：实现拒绝该情形或尚未实现。

除非适当的一致性测试套件及其精确范围支持该表述，否则不要使用“一致性通过”。

## 精确验证对象

记录每个结果实际测试的 commit SHA 和 tree。对于 Pull Request，候选 head 与
GitHub 合成 merge result 可能是不同验证对象，应明确实际运行的是哪一个。

job 失败时，检查其 step 和日志。把失败归类为产品代码、语义回归、测试缺陷、
依赖、环境、权限、基础设施或已知既有失败。不得用 rerun 或放宽断言掩盖确定性
失败。
