# 架构

[English](architecture.md)

## 系统边界

Mithril-Wrapper 实现客户端可以观察到的 EGL 与 OpenGL 行为。主要客户端是
Minecraft Java 和 LWJGL。该项目是兼容系统，不是把每个调用一一翻译到另一
套 API 的转换器。

前端在提交原生工作前解析可变 API 状态。后端契约携带明确的绘制状态、资源
身份、内容版本和生命周期信息。原生后端不得通过隐藏的前端状态重新构造
OpenGL 状态机。

## 所有权层级

### 宿主与 EGL

`src/egl` 负责 display、context、surface、current binding、swap 和宿主窗口
生命周期行为。平台对象可以通过这一层进入系统，但通用 OpenGL 策略不得进入。

部分 Amethyst 宿主桥接会先用 `EGL_OPENGL_ES3_BIT` 和
`EGL_OPENGL_ES_API` 协商，再由 LWJGL 加载 desktop OpenGL 符号。Mithril 把
该请求作为宿主兼容别名处理。它不得改变前端暴露的 GL profile 或语义。

### OpenGL 状态与对象

`src/gl` 和 `src/state` 负责 OpenGL 可见行为，包括对象名称、绑定、错误产生、
framebuffer 完整性、draw/read 选择、pixel store、buffer 与 texture 状态、
query、同步和删除语义。

必须在原生执行前失败的命令应在这一层失败。例如：不完整 framebuffer、无效
对象关系或不受支持的可观察状态组合。

### 着色器语义

`src/shader` 负责 GLSL 重写、SPIR-V 生成、反射、uniform 与资源映射，以及已
链接的 vertex-to-fragment 接口行为。后端使用链接结果，不得另行发明不同的
着色器接口规则。

### 后端无关意图

`src/backend` 定义原生执行契约，包括已解析的 pipeline state、dynamic state、
vertex 与 index source、uniform source、采样资源、render-target 身份，以及
生命周期和版本数据。

该契约不包含 Metal 或 Vulkan handle。借用的前端数据只在同步后端调用期间
有效。延迟执行的后端必须在调用返回前保留或复制所需原生资源。

### 原生执行

`src/metal` 负责 DirectMetal 资源创建、pipeline 创建、命令编码、Metal 同步，
以及通过 Metal 对象进行呈现。它是 Apple 平台交付后端。

`src/vk` 负责 Vulkan 资源创建、pipeline 创建、命令编码和 Vulkan 同步。它是
参考后端，也是 Linux 上的跨后端回归目标。

原生后端可以使用不同的执行策略，但不能对源自 EGL、OpenGL 或已链接着色器
程序的规则作出不同解释。

## 构建边界

`mithril_direct` 目标包含共享前端和 DirectMetal。它不得编译或链接 Vulkan
源码、Vulkan-Headers、MoltenVK 或 Vulkan loader。CMake 和
`scripts/verify_directmetal_artifact.sh` 强制执行该边界。

`mithril_legacy` 目标包含共享前端和 Vulkan 后端。它是独立产物，在 Apple
平台使用不同的输出名称。

## 呈现边界

语义测试可以使用 offscreen 或 default framebuffer 隔离规则。最终宿主行为还
依赖真实 surface 和呈现路径。`tests/amethyst_egl_smoke.mm` 检查 CAMetalLayer
边界。超出该边界的声明需要真实 Minecraft 或物理设备证据。

## 放置规则

当一项修改可能属于多个目录时，使用以下规则：

- EGL 生命周期与 surface 规则放入 `src/egl`。
- API 验证、状态解析、对象身份和错误行为放入 `src/gl` 或 `src/state`。
- 着色器语言与链接接口规则放入 `src/shader`。
- 可直接供原生执行使用的数据形态放入 `src/backend`。
- API 特定的资源创建和命令编码放入 `src/metal` 或 `src/vk`。
- 两个原生后端都需要同一个通用条件时，应把条件上移，而不是复制。

不要只为使目录结构看起来对称而增加抽象。只有当抽象能删除重复含义、明确
所有权或让不变量可执行时才增加它。

## 历史代码

`main` 是产品线。历史 `archive/*` 引用保存断开的研究和旧执行树。它们不是
产品分支，也不是可整体合并的来源。

复用历史工作时：

1. 明确可观察不变量。
2. 找到或创建当前的聚焦测试。
3. 在当前所有者中实现该不变量。
4. 把历史 commit 或 PR 记录为来源。
5. 验证当前代码树。
