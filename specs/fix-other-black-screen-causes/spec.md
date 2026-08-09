# 修复其他导致黑屏有声音的根因 Spec

## Why

深度研究 MoltenVK（`https://github.com/KhronosGroup/MoltenVK`）和 MobileGL（`https://github.com/MobileGL-Dev/MobileGL`）后，发现 Mithril 在已修复的根因 A-D（Y翻转/frontFace/Z重映射/blit Y翻转）之外，仍存在若干可能导致"黑屏有声音"或"纹理损坏黑色斑块"的隐患。本 spec 修复其中经过代码审计确认真实存在的 3 个问题，进一步提升渲染鲁棒性。

## What Changes

- **根因 E（最高）**：显式设置 `MVK_CONFIG_VK_SEMAPHORE_SUPPORT_STYLE=1`（Metal 信号量，真实 GPU 同步）。Mithril 的同步设计完全依赖 Vulkan semaphore（imageAvailable + renderFinishedPerImage），但未设置此项。MoltenVK 默认在 Rosetta2/NVIDIA/旧版本上可能退化为 callback 模式（无真实 GPU 同步）→ 黑屏。
- **根因 F（中）**：`glTexSubImage2D` 部分上传时保留纹理既有内容。当前 `stage_and_copy_image` 无条件用 `oldLayout = UNDEFINED`，会丢弃整个 image 既有内容 → 部分上传后其余区域变 undefined → 纹理损坏 → 物体黑色斑块。
- **根因 G（中）**：RGB swapchain 格式的 clear alpha 强制 1.0 防御性逻辑。当前 swapchain 恒 BGRA8+OPAQUE 未触发，但无防护；对标 MobileGL `ResolveColorClearAlpha`。当前为防御性加固，不立即触发黑屏但消除隐患。

## Impact

- 受影响 spec：`fix-red-black-screen`（互补，本 spec 处理 A-D 之外的根因）
- 受影响代码：
  - `MG_Backend/DirectVulkan/Device.cpp`（根因 E：setenv 增加 SEMAPHORE_SUPPORT_STYLE）
  - `MG_Backend/DirectVulkan/Resources.cpp`（根因 F：stage_and_copy_image 区分 full/sub 上传）
  - `MG_Impl/Texture.cpp` 或 `MG_Backend/Backend.h`（根因 F：传递 is_full_upload 标志）
  - `MG_Backend/DirectVulkan/CommandStream.cpp`（根因 G：clear alpha 强制逻辑）
  - `MG_Impl/gl.cpp`（根因 G：glClearColor 配合）

## ADDED Requirements

### Requirement: 显式设置 MoltenVK 信号量同步样式

系统 SHALL 在初始化 MoltenVK 前（`Device.cpp` 的 `setenv` 块）显式设置 `MVK_CONFIG_VK_SEMAPHORE_SUPPORT_STYLE=1`（`VK_SEMAPHORE_SUPPORT_STYLE_METAL_SEMAPHORE`），强制使用 Metal 信号量实现真实 GPU 侧同步。

#### Scenario: Rosetta2 / NVIDIA / 旧版 MoltenVK 环境
- **WHEN** 应用在 Rosetta2（x86_64 on Apple Silicon）或 NVIDIA GPU 或旧版 MoltenVK 上运行
- **THEN** 信号量 wait/signal 使用真实 Metal 信号量，GPU 侧有正确同步，渲染内容正确显示（不黑屏）

#### Scenario: 现代 Apple Silicon 原生环境
- **WHEN** 应用在 Apple Silicon 原生（arm64）+ 现代 MoltenVK 上运行
- **THEN** 显式设置与默认值一致，行为不变，无副作用

### Requirement: 部分纹理上传保留既有内容

系统 SHALL 在 `glTexSubImage2D`/`glTexSubImage3D` 部分上传时，使用 `oldLayout = tex.currentLayout`（而非无条件 `UNDEFINED`），保留纹理未更新区域的既有内容。

#### Scenario: 部分纹理更新
- **WHEN** 应用对已存在纹理调用 `glTexSubImage2D` 更新某个子区域
- **THEN** 仅更新指定子区域，其余区域保持原内容不变（纹理不损坏，无黑色斑块）

#### Scenario: 完整纹理上传
- **WHEN** 应用对纹理调用 `glTexImage2D`（完整重定义）或首次上传
- **THEN** 使用 `oldLayout = UNDEFINED`（丢弃旧内容，符合语义）

### Requirement: RGB swapchain 格式 clear alpha 强制 1.0

系统 SHALL 在 clear swapchain 颜色 attachment 时，若 swapchain 格式无 alpha 通道（如 `VK_FORMAT_R8G8B8_UNORM`/`B8G8R8_UNORM`），强制 clear color 的 alpha 分量为 1.0，防止合成器将窗口视为透明。

#### Scenario: RGB swapchain 格式
- **WHEN** swapchain 格式为 RGB（无 alpha）且应用调用 `glClearColor(r,g,b,0)`
- **THEN** 实际 clear alpha 为 1.0，窗口不透明，不显示黑屏

#### Scenario: RGBA swapchain 格式
- **WHEN** swapchain 格式为 RGBA（有 alpha，当前默认 BGRA8）
- **THEN** clear alpha 原样使用，行为不变

## MODIFIED Requirements

### Requirement: stage_and_copy_image 纹理上传

`stage_and_copy_image`（Resources.cpp:228）的初始 layout barrier SHALL 根据 `is_full_upload` 参数选择 `oldLayout`：
- `is_full_upload = true`（glTexImage2D 首次/重定义）：`oldLayout = VK_IMAGE_LAYOUT_UNDEFINED`
- `is_full_upload = false`（glTexSubImage 部分更新）：`oldLayout = tex.currentLayout`（保留既有内容）

`backend_texture_upload`（Backend.h 声明）SHALL 增加 `int is_full_upload` 参数，由 `glTexImage2D` 传 1、`glTexSubImage2D`/`glTexSubImage3D` 传 0。

## REMOVED Requirements

无。

## 详细设计

### 根因 E：MVK_CONFIG_VK_SEMAPHORE_SUPPORT_STYLE

**位置**：`MG_Backend/DirectVulkan/Device.cpp:442` 后

**参考**：
- MoltenVK `MVKDevice.mm:3621-3627`：默认 `WHERE_SAFE`（值 1），但 NVIDIA/Rosetta2 退化为 `SingleQueue`（no-op）。
- MoltenVK `Docs/MoltenVK_Configuration_Parameters.md:633-650`：值 `1`=METAL_SEMAPHORE（真实 GPU 同步），`2`=METAL_EVENTS_WHERE_SAFE，`3`=CALLBACK。
- 注：MoltenVK 版本不同，枚举值定义可能略有差异。值 `1` 在主流版本对应 Metal Semaphore（真实同步），是最安全的强制选择。

**实现**：
```cpp
setenv("MVK_CONFIG_VK_SEMAPHORE_SUPPORT_STYLE", "1", 1);  // force Metal semaphore (real GPU sync)
```

**风险**：极低。值 `1` 是强制使用 Metal 信号量，所有 MoltenVK 版本均支持。最坏情况下与默认行为一致。

### 根因 F：部分纹理上传保留既有内容

**位置**：`MG_Backend/DirectVulkan/Resources.cpp:361-367`、`MG_Impl/Texture.cpp`

**问题代码**（Resources.cpp:366）：
```cpp
barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;  // 无条件丢弃
```

**修复方案**：
1. `stage_and_copy_image` 签名增加 `bool is_full_upload` 参数。
2. `barrier.oldLayout = is_full_upload ? VK_IMAGE_LAYOUT_UNDEFINED : tex.currentLayout;`
3. `backend_texture_upload` 增加 `int is_full_upload` 参数并透传。
4. `glTexImage2D` 调用传 `is_full_upload=1`，`glTexSubImage2D`/`glTexSubImage3D` 传 `is_full_upload=0`。

**注意**：`tex.currentLayout` 在 `stage_and_copy_image` 调用时可能为 `SHADER_READ_ONLY_OPTIMAL`（上次上传后设置的）。部分上传用此作为 oldLayout 是合法的 layout 转换。

**对标**：MoltenVK `flushToDevice`（MVKImage.mm:572-590）仅在 UNDEFINED/PREINITIALIZED/GENERAL 下刷新主机数据。但 Mithril 用 staging buffer（非主机可见 image），不经过 `flushToDevice`，所以这里的 oldLayout 选择才是关键。

### 根因 G：RGB swapchain clear alpha 强制 1.0

**位置**：`MG_Backend/DirectVulkan/CommandStream.cpp` clear 路径

**对标**：MobileGL `VulkanRenderer.cpp:193-198` `ResolveColorClearAlpha`。

**实现**：在 `commit_frame` 或 `clear_attachments` 设置 clearValue.color.float32[3] 时，检查 swapchain 格式是否有 alpha 通道。若无（如 B8G8R8/R8G8B8），强制 alpha=1.0。

**当前状态**：swapchain 恒 `VK_FORMAT_B8G8R8A8_UNORM`（SwapchainCommon.cpp:47）+ `VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR`（SwapchainCommon.cpp:85-87），所以当前不触发。但这是防御性加固，对标 MobileGL。

**简化实现**：由于当前 swapchain 格式固定且 compositeAlpha=OPAQUE，可在 clear 路径增加一个格式检查 helper，仅在格式无 alpha 时强制 1.0。

## 不改动项确认

以下 MobileGL/MoltenVK 机制经审计确认 Mithril 已正确实现，不在本 spec 范围：
- FBO-only 帧 PRESENT_SRC_KHR barrier（CommandStream.cpp:685-698, 791-798 已实现）
- imageAvailableSemaphore consumed 标志（Swapchain.h:99, CommandStream.cpp:846 已实现）
- 零面积窗口 present 挂起（egl.cpp:929-932 已实现）
- frame fence 创建即 signaled（Device.cpp:727-732 已实现）
- acquire/present SUBOPTIMAL 当成功码（SwapchainCommon.cpp:344-346, 492 已实现）
- default FBO finalLayout=PRESENT_SRC_KHR（CommandStream.cpp:788-798 已实现，含 BOTTOM_OF_PIPE 修正）
- glClear 用 vkCmdClearAttachments（gl.cpp:41-60 已实现）
- 内容有效性→DONT_CARE（color 路径已实现）
