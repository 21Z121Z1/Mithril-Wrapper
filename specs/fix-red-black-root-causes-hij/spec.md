# 修复红屏/黑屏真实根因（H/I/J）Spec

## Why

已修复根因 A-G（Y翻转/Z重映射/cull/blit/semaphore/纹理上传/alpha）后，加载界面红屏 + 进游戏黑屏问题仍然存在。深度研究 MobileGL + MoltenVK + Mithril 源码审计后，发现 A-G 修复虽然正确，但被三个更底层的根因掩盖。本 spec 修复这三个经代码验证的真实根因。

## What Changes

- **根因 H（CRITICAL）**：顶点属性 offset 被双重应用。GL `glVertexAttribPointer` 的 `pointer` 参数（顶点内成员偏移）被同时传入 `vkCmdBindVertexBuffers.pOffsets`（绑定偏移）和 `VkVertexInputAttributeDescription.offset`（属性偏移），导致有效地址 = buffer + 2*pointer。position(偏移0)正确，但 color/uv/normal 全部错位 → 红屏（UV采样错误区域）+ 黑屏（属性全错位）。
- **根因 I（HIGH）**：用户 FBO 颜色附件纹理创建时缺 `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT`。`Resources.cpp:680` 仅设置 `TRANSFER_DST|SAMPLED`，depth 纹理有 `DEPTH_STENCIL_ATTACHMENT_BIT` 但颜色纹理无对应 bit → 渲染到 FBO 颜色附件违反 Vulkan usage 约束 → MoltenVK 静默丢弃渲染 → 进游戏黑屏。
- **根因 J（HIGH）**：`glClear` 每帧调用 `backend_commit()` 导致帧内提前提交。首次 commit（clear）signal renderFinished 并推进 currentFrame，后续 draw 录到新 slot，eglSwapBuffers 的二次 commit 不再 signal renderFinished → present 只等待 clear 完成而非 draw 完成 → draw 可能在 present 读图前未完成 → 黑屏。

## Impact

- 受影响 spec：`fix-red-black-screen`（A-D）、`fix-other-black-screen-causes`（E-G）— 互补，本 spec 修复被它们掩盖的底层根因
- 受影响代码：
  - `MG_Impl/Drawing.cpp`（根因 H：绑定偏移置 0）
  - `MG_Backend/DirectVulkan/Resources.cpp`（根因 I：颜色纹理加 COLOR_ATTACHMENT_BIT）
  - `MG_Impl/gl.cpp`（根因 J：glClear 移除 backend_commit）

## ADDED Requirements

### Requirement: 顶点属性偏移单次应用

系统 SHALL 确保 GL `glVertexAttribPointer` 的 `pointer` 参数（顶点内成员偏移）仅作为 `VkVertexInputAttributeDescription.offset` 使用，`vkCmdBindVertexBuffers` 的 `pOffsets` 必须为 0。

#### Scenario: 交错顶点格式（position@0, color@12, uv@16）
- **WHEN** 应用使用单一交错 VBO，position 在偏移 0、color 在偏移 12、uv 在偏移 16
- **THEN** 顶点 0 的 color 读取自 buffer+12（而非 buffer+24），uv 读取自 buffer+16（而非 buffer+32），所有属性正确

#### Scenario: 独立 VBO（pointer 全为 0）
- **WHEN** 应用对每个属性使用独立 VBO（pointer=0）
- **THEN** 行为不变（0+0=0，与修复前一致）

### Requirement: 颜色纹理支持 FBO 附件用途

系统 SHALL 在创建颜色纹理时无条件添加 `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT`，使其可作为 FBO 颜色附件渲染目标。

#### Scenario: 纹理用作 FBO 颜色附件
- **WHEN** 应用创建 RGBA 纹理并通过 `glFramebufferTexture2D` 挂为颜色附件
- **THEN** `vkCmdBeginRendering` 将该 image 作为颜色附件时不违反 usage 约束，渲染内容正确写入

#### Scenario: 纹理仅用于采样
- **WHEN** 应用创建纹理仅用于采样（不挂为 FBO 附件）
- **THEN** 额外的 COLOR_ATTACHMENT_BIT 无副作用（Vulkan 允许设置未使用的 usage bit）

### Requirement: glClear 不提前提交帧

系统 SHALL 确保 `glClear` 将清除命令记录到当前 command buffer，不在帧内调用 `backend_commit()`。帧的提交统一由 `eglSwapBuffers` 完成。

#### Scenario: 标准帧序列 glClear → glDraw → eglSwapBuffers
- **WHEN** 应用执行 glClear 后 glDraw 再 eglSwapBuffers
- **THEN** clear 和 draw 在同一 command buffer 中，eglSwapBuffers 统一提交并 signal renderFinished，present 等待完整的 clear+draw 完成

#### Scenario: glReadPixels 需要 immediate 结果
- **WHEN** 应用执行 glClear 后立即 glReadPixels
- **THEN** glReadPixels 自身的 `backend_commit()` 确保 clear 完成（已有逻辑，不受影响）

## MODIFIED Requirements

### Requirement: prepare_draw 顶点缓冲绑定

`Drawing.cpp:283` 的 `backend_set_vertex_buffer` 调用 SHALL 将绑定偏移设为 0：
```cpp
backend_set_vertex_buffer(m.location, buf, 0);  // 偏移由属性描述处理
```
`Pipeline.cpp:302` 的 `ad.offset = (uint32_t)a.offset` 保持不变（属性成员偏移）。

### Requirement: 纹理创建 usage flags

`Resources.cpp:680` 的 `ici.usage` SHALL 对所有颜色格式添加 `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT`：
```cpp
ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
ici.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;  // 颜色纹理可用作 FBO 附件
```

### Requirement: glClear 实现路径

`gl.cpp:41-60` 的 `glClear` SHALL 移除 `backend_commit()` 调用，清除命令保留在当前 command buffer 中，由 eglSwapBuffers 统一提交。

## REMOVED Requirements

无。

## 详细设计

### 根因 H：顶点属性偏移双重应用

**底层机制**：Vulkan 顶点属性寻址公式为：
```
有效地址 = buffer + pOffsets[binding] + vertexIndex * stride + attr.offset
```
GL 的 `glVertexAttribPointer(loc, size, type, norm, stride, pointer)` 中，`pointer` 是顶点内成员偏移（对交错格式如 Minecraft 的 POSITION_COLOR_TEX_LIGHTMAP_NORMAL 为 0/12/16/24/28）。

Mithril 当前将 `pointer` 同时设为 `pOffsets`（Drawing.cpp:283）和 `attr.offset`（Pipeline.cpp:302），导致：
```
有效地址 = buffer + pointer + 0*stride + pointer = buffer + 2*pointer
```
- position(pointer=0)：buffer+0 ✓
- color(pointer=12)：buffer+24 ✗（读到 lightmap/normal 数据）
- uv(pointer=16)：buffer+32 ✗（读到 normal 数据当 UV）

**为何 A-G 修复无效**：Y翻转/Z重映射/cull/blit 都正确，但被 offset 错位掩盖——几何位置正确（position 偏移 0），但颜色和 UV 全错位，导致红屏（UV 采样错误区域）和黑屏（属性乱导致渲染无效）。

**最小影响域修复**：`Drawing.cpp:283` 改为 `backend_set_vertex_buffer(m.location, buf, 0)`。一行改动，保留 Pipeline.cpp 的属性偏移。API 契约不变（`backend_set_vertex_buffer` 签名不变）。

### 根因 I：颜色纹理缺 COLOR_ATTACHMENT_BIT

**底层机制**：Vulkan 要求用作 render pass 颜色附件的 image 必须在创建时设置 `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT`（VUID-vkCmdBeginRendering-image-06124）。违反此约束在 validation layer 报错，在 MoltenVK 上可能静默丢弃渲染。

Mithril 的 `Resources.cpp:680` 对所有纹理设置 `TRANSFER_DST|SAMPLED`，depth 纹理额外加 `DEPTH_STENCIL_ATTACHMENT_BIT`（line 682-684），但颜色纹理无对应 bit。Minecraft 的 deferred renderer 大量使用 FBO 颜色附件（gbuffer/lighting/composite 通道），全部受影响。

**最小影响域修复**：`Resources.cpp:680` 后添加 `ici.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;`。对仅用于采样的纹理无副作用（Vulkan 允许设置未使用的 usage bit）。

### 根因 J：glClear 帧内提前提交

**底层机制**：`commit_frame`（CommandStream.cpp:902-910）的 renderFinished 信号逻辑：仅当 `!renderFinishedSignaledPerImage[currentImage]` 时才 signal。glClear 的 `backend_commit()` 是帧内首次 commit，它会：
1. signal `renderFinishedPerImage[currentImage]` 并置 `signaled=true`
2. 推进 `currentFrame`（下一 slot）
3. 提交仅含 clear 的 command buffer

后续 glDraw 录到新 slot 的 command buffer，eglSwapBuffers 的二次 commit 因 `signaled=true` 不再 signal renderFinished。present 只等待 clear 的 renderFinished，draw 的 command buffer 可能在 present 读图前未完成。

**隐式契约失效**：GL 语义中 glClear 不触发帧提交（它只是状态变更）。eglSwapBuffers 才是帧边界。Mithril 的 glClear 调用 backend_commit 违反了此契约。

**最小影响域修复**：`gl.cpp:59` 移除 `backend_commit()` 调用。clear 命令（begin_pass + clear_attachments + end_pass）保留在当前 command buffer 中，由 eglSwapBuffers 统一提交。glReadPixels 等需要 immediate 结果的路径已有自己的 `backend_commit()`（ImageOps.cpp），不受影响。

## 测试验证

### 根因 H 验证

**复现步骤（修复前 Fail）**：
1. 创建交错 VBO：position(float3, offset=0) + color(float4, offset=12) + uv(float2, offset=28)
2. 渲染一个三角形，color=(0,1,0,1)（绿色），uv 采样已知纹理
3. 修复前：color 读取自 offset=24（非 color 数据），uv 读取自 offset=56（非 uv 数据）→ 颜色错误/纹理错误

**修复后 Pass**：
1. 同上测试
2. 修复后：color 读取自 offset=12（正确），uv 读取自 offset=28（正确）→ 绿色三角形 + 正确纹理

### 根因 I 验证

**复现步骤（修复前 Fail）**：
1. `glGenTextures` + `glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 256, ...)` 创建纹理
2. `glGenFramebuffers` + `glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0)`
3. `glBindFramebuffer` 并渲染到该 FBO
4. blit 到 default framebuffer
5. 修复前：FBO 渲染被 MoltenVK 静默丢弃 → 黑屏

**修复后 Pass**：
1. 同上测试
2. 修复后：FBO 渲染内容正确显示

### 根因 J 验证

**复现步骤（修复前 Fail）**：
1. 帧序列：`glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT)` → `glDrawArrays(...)` → `eglSwapBuffers`
2. 修复前：glClear 提前 commit，draw 可能未在 present 前完成 → 间歇黑屏（仅 clear color 可见）

**修复后 Pass**：
1. 同上测试
2. 修复后：clear + draw 在同一 command buffer，eglSwapBuffers 统一提交 → draw 内容正确显示
