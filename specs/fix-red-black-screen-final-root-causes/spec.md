# 修复红屏/黑屏最终根因（Y/Z/AA/AE/AF/AG/AH/AI）Spec

## Why

A-X 根因修复后红屏/黑屏仍存在。深度对照 MobileGL 和 MoltenVK 行为后发现，剩余根因集中在 **FBO 布局转换缺失**、**draw call 参数错误传递**、**索引类型支持不全**、**深度/模板 aspect 处理错误** 四大类。这些根因在不同设备上表现不同（有的设备红屏、有的黑屏），需要全部彻底消除。

## What Changes

- **根因 Y（CRITICAL）**：用户 FBO 颜色/深度附件在 `begin_render_pass` / `end_render_pass` 中缺少到 attachment-optimal 的布局转换。`VkRenderingAttachmentInfo.imageLayout` 声明 `COLOR_ATTACHMENT_OPTIMAL`/`DEPTH_STENCIL_ATTACHMENT_OPTIMAL`，但实际布局为 `UNDEFINED` 或 `SHADER_READ_ONLY_OPTIMAL` → dynamic rendering 不做自动转换 → spec 违规 → MoltenVK 丢弃 draw → 黑屏。
- **根因 Z（MEDIUM）**：Viewport/Scissor 的 Y 原点直接传 GL bottom-origin 值给 Vulkan top-origin → scissor 裁剪错误区域 → 局部黑屏。
- **根因 AA（HIGH）**：depth-only 格式（D32_SFLOAT/D16_UNORM）无条件绑定 `pStencilAttachment`，但该 view 无 stencil aspect → VUID-06126 spec 违规 → draw 可能被丢弃 → 黑屏。
- **根因 AE（CRITICAL）**：`GL_UNSIGNED_BYTE` 索引类型不支持，被当作 `UINT16` 处理 → 索引值错乱 → 几何腐败 → 红屏。
- **根因 AF（CRITICAL）**：`primitiveRestartEnable` 硬编码 `VK_FALSE`，忽略 GL `GL_PRIMITIVE_RESTART_FIXED_INDEX` → strip/fan 几何在 restart 索引处不断开 → 几何腐败 → 红屏。
- **根因 AG（CRITICAL）**：`glDrawElementsBaseVertex` / `glDrawElementsInstancedBaseVertex` 的 `baseVertex` / `baseInstance` 被丢弃 → 所有实例读取同一组顶点 → 几何错位 → 红屏/花屏。
- **根因 AH（HIGH）**：descriptor `imageLayout` 硬编码 `SHADER_READ_ONLY_OPTIMAL`，对 depth-stencil 纹理应为 `DEPTH_STENCIL_READ_ONLY_OPTIMAL` → 布局不匹配 → draw 被丢弃 → 黑屏。
- **根因 AI（HIGH）**：`glTexImage2D` 逐级上传 mipmap 时用当前 level 尺寸重建 VkImage → base level 数据丢失 + VkImage extent 错误 → 纹理腐败 → 红屏/花屏。

## Impact

- 受影响 spec：`fix-red-screen-root-causes-vwx`（W/X 互补）、`fix-red-screen-root-causes-klm`（K/L/M 互补）、`fix-red-black-root-causes-hij`（H 互补）
- 受影响代码：
  - `MG_Backend/DirectVulkan/CommandStream.cpp`（根因 Y/Z/AA：FBO 布局转换、viewport Y 翻转、stencil 绑定条件化）
  - `MG_Backend/DirectVulkan/Pipeline.cpp`（根因 AF/AG：primitiveRestart 动态化、baseVertex 纳入签名）
  - `MG_Backend/DirectVulkan/DescriptorSet.cpp`（根因 AH：depth-stencil descriptor layout）
  - `MG_Backend/DirectVulkan/Resources.cpp`（根因 AH/AI：depth-stencil 上传后 layout、mipmap 重建用 base level extent）
  - `MG_Backend/DirectVulkan/Device.cpp`（根因 AE/AF：启用 index_type_uint8 + primitive_topology_list_restart 扩展）
  - `MG_Impl/Drawing.cpp`（根因 AE/AF/AG：uint8 索引大小、primitiveRestart 状态传递、baseVertex/baseInstance 传递）
  - `MG_Impl/Texture.cpp`（根因 AI：mipmap 上传用 base level 尺寸）
  - `MG_Impl/gl.cpp`（根因 AF：primitiveRestart 状态维护）
  - `MG_State/State.h`（根因 AG：可能需新增 baseVertex/baseInstance 字段）

## ADDED Requirements

### Requirement: 用户 FBO 附件布局转换

系统 SHALL 在 `begin_render_pass` 时将所有用户 FBO 颜色附件从 `currentLayout` 转换到 `COLOR_ATTACHMENT_OPTIMAL`，深度附件转换到 `DEPTH_STENCIL_ATTACHMENT_OPTIMAL`；在 `end_render_pass` 时将颜色附件转回 `SHADER_READ_ONLY_OPTIMAL`，深度附件转回 `DEPTH_STENCIL_READ_ONLY_OPTIMAL`，并更新 `TextureEntry::currentLayout`。

#### Scenario: 用户 FBO 渲染后采样
- **WHEN** 应用创建 FBO、绑定颜色+深度纹理、渲染、然后采样该纹理
- **THEN** begin_render_pass 将纹理转到 attachment-optimal，end_render_pass 转回 read-only，采样读取到正确内容

### Requirement: GL_UNSIGNED_BYTE 索引类型支持

系统 SHALL 启用 `VK_EXT_index_type_uint8` 扩展，将 `GL_UNSIGNED_BYTE` 索引映射到 `VK_INDEX_TYPE_UINT8`，并在 client-memory 索引 staging 时按 1 字节/索引计算大小。

#### Scenario: glDrawElements 使用 GL_UNSIGNED_BYTE 索引
- **WHEN** 应用调用 `glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_BYTE, indices)`
- **THEN** Vulkan 使用 `VK_INDEX_TYPE_UINT8`，索引值正确解释，几何正确渲染

### Requirement: Primitive Restart 支持

系统 SHALL 根据 GL 状态（`GL_PRIMITIVE_RESTART` / `GL_PRIMITIVE_RESTART_FIXED_INDEX`）设置 `primitiveRestartEnable`，启用 `VK_EXT_primitive_topology_list_restart` 扩展以支持 list topology 上的 restart，并将 restart 状态纳入 pipeline 签名哈希。

#### Scenario: strip 几何使用 primitive restart
- **WHEN** 应用启用 `GL_PRIMITIVE_RESTART_FIXED_INDEX` 并绘制 strip 几何
- **THEN** pipeline 的 `primitiveRestartEnable = VK_TRUE`，strip 在 restart 索引处正确断开

### Requirement: BaseVertex / BaseInstance 支持

系统 SHALL 将 `glDrawElementsBaseVertex` 的 `baseVertex` 和 `glDrawElementsInstancedBaseInstance` 的 `baseInstance` 传递到 `vkCmdDrawIndexed` 的 `vertexOffset` 和 `firstInstance` 参数。

#### Scenario: instanced 渲染使用 baseVertex
- **WHEN** 应用调用 `glDrawElementsInstancedBaseVertex(mode, count, type, indices, instanceCount, baseVertex)`
- **THEN** `vkCmdDrawIndexed` 的 `vertexOffset = baseVertex`，每个实例的顶点正确偏移

### Requirement: Depth-Stencil 纹理采样布局

系统 SHALL 对 depth-stencil 格式的纹理在 descriptor 写入和上传后布局转换中使用 `VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL`，对 color 格式使用 `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`。

#### Scenario: 采样 depth 纹理（shadow map）
- **WHEN** shader 采样 depth-stencil 纹理
- **THEN** descriptor `imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL`，与 image 实际布局匹配

### Requirement: glTexImage2D mipmap 用 base level 尺寸

系统 SHALL 在 `glTexImage2D` 逐级上传 mipmap 时，始终用 base level（level 0）的尺寸创建 VkImage，而非当前 level 的尺寸。

#### Scenario: 逐级上传 mipmap
- **WHEN** 应用调用 `glTexImage2D(GL_TEXTURE_2D, 1, w/2, h/2, ...)` 上传 level 1
- **THEN** VkImage extent 保持 base level 尺寸 (w, h)，level 1 数据写入正确的 mip level

## MODIFIED Requirements

### Requirement: Viewport/Scissor Y 原点转换

`CommandStream.cpp:backend_set_viewport` 和 `backend_set_scissor` SHALL 将 GL bottom-origin Y 转换为 Vulkan top-origin Y：`vk_y = framebufferHeight - gl_y - gl_h`。

### Requirement: Depth-only 格式不绑定 pStencilAttachment

`CommandStream.cpp:begin_render_pass` SHALL 仅对含 stencil aspect 的格式（D24_UNORM_S8_UINT / D32_SFLOAT_S8_UINT / S8_UINT）设置 `pStencilAttachment`，对 depth-only 格式（D32_SFLOAT / D16_UNORM）设为 `nullptr`。

### Requirement: Pipeline primitiveRestartEnable 从 GL 状态读取

`Pipeline.cpp:get_or_create_pipeline` SHALL 将 `ia.primitiveRestartEnable` 改为从 GL 状态读取（`g_state->primitiveRestart || g_state->primitiveRestartFixedIndex`），并将其纳入 `hash_signature`。

### Requirement: backend_draw_indexed 支持 baseVertex/baseInstance

`CommandStream.cpp:backend_draw_indexed` SHALL 从 `g_state` 读取 `currentBaseVertex` 和 `currentBaseInstance`，传递给 `vkCmdDrawIndexed`。

## REMOVED Requirements

无。

## 详细设计

### 根因 Y：用户 FBO 附件布局转换

**底层机制**：`VK_KHR_dynamic_rendering` 的 `vkCmdBeginRendering` **不自动转换附件布局**——它只验证 image 在 pass 期间处于 `VkRenderingAttachmentInfo.imageLayout` 声明的布局。用户 FBO 的颜色/深度纹理创建时 `currentLayout = UNDEFINED`，上传后变为 `SHADER_READ_ONLY_OPTIMAL`。但 `begin_render_pass` 硬编码 `imageLayout = COLOR_ATTACHMENT_OPTIMAL`（CommandStream.cpp:454），且**仅对 swapchain 附件做 barrier**（CommandStream.cpp:404-437），用户 FBO 附件完全不做 barrier → 实际布局与声明布局不匹配 → spec 违规 → MoltenVK 丢弃 draw → 黑屏。

MobileGL 的 VkRenderPassManager（VkRenderPassManager.cpp:711-784）在 render pass begin 前对**所有**附件做 layout barrier。

**最小影响域修复**：
1. `begin_render_pass`：在 swapchain barrier 块之后，对非 swapchain 的用户 FBO 颜色附件，从 `tex.currentLayout` barrier 到 `COLOR_ATTACHMENT_OPTIMAL`；对深度附件 barrier 到 `DEPTH_STENCIL_ATTACHMENT_OPTIMAL`。
2. `end_render_pass`：将用户 FBO 颜色附件 barrier 回 `SHADER_READ_ONLY_OPTIMAL`，深度附件 barrier 回 `DEPTH_STENCIL_READ_ONLY_OPTIMAL`，更新 `tex.currentLayout`。
3. 需要从 `texture_table()` 查找 `TextureEntry`（通过 VkImageView 反查或存储 tex_id 到 EncoderState）。

### 根因 Z：Viewport/Scissor Y 原点转换

**底层机制**：GL viewport/scissor 的 Y 从底部测量（bottom-left origin），Vulkan 从顶部测量（top-left origin）。代码直接传 GL Y 给 Vulkan Y。全屏 viewport（y=0）不触发，但非零 Y 的 scissor（如 GUI 裁剪）会裁剪错误区域 → 局部黑屏。MoltenVK 不做 Y 翻转（Y 翻转在 vertex shader 中完成）。

MobileGL 的 VulkanRenderer 执行 `viewport.y = extent.height - glViewport.y - glViewport.height`。

**最小影响域修复**：`backend_set_viewport` / `backend_set_scissor` 中计算 `vk_y = framebufferHeight - gl_y - gl_h`。framebufferHeight 从 `g_state->viewportH` 或当前 FBO 附件尺寸获取。

### 根因 AA：Depth-only 格式不绑定 pStencilAttachment

**底层机制**：`begin_render_pass` 中 `pStencilAttachment = e.depthView ? &depthAttach : nullptr`（CommandStream.cpp:521-522）无条件绑定。对 depth-only 格式（D32_SFLOAT/D16_UNORM），ImageView aspect 仅为 `DEPTH_BIT`，绑定到 stencil 附件违反 VUID-06126（stencil 附件 view 必须含 stencil aspect）→ draw 可能被丢弃 → 黑屏。

**最小影响域修复**：用 `aspect_for_format(depthFormat)` 判断是否含 stencil aspect，仅对含 stencil 的格式设置 `pStencilAttachment`。

### 根因 AE：GL_UNSIGNED_BYTE 索引类型

**底层机制**：`index_type_to_int`（Drawing.cpp:332-334）仅返回 0（UINT16）或 1（UINT32），`GL_UNSIGNED_BYTE` 被当作 UINT16 → 1 字节/索引的数据按 2 字节解释 → 索引值错乱 → 几何腐败。client-memory 路径的 staging 大小也错误（Drawing.cpp:398,429 按 2 字节计算）。

MobileGL 启用 `VK_EXT_index_type_uint8` 并映射到 `VK_INDEX_TYPE_UINT8`（VulkanRenderer.cpp:3093-3109）。

**最小影响域修复**：
1. `Device.cpp`：启用 `VK_EXT_index_type_uint8` 扩展 + `VkPhysicalDeviceIndexTypeUint8FeaturesEXT`。
2. `Drawing.cpp:index_type_to_int`：增加 `GL_UNSIGNED_BYTE → 2`。
3. `CommandStream.cpp:backend_draw_indexed`：增加 `case 2: VK_INDEX_TYPE_UINT8`。
4. `Drawing.cpp` client-memory 路径：`elem = (type==GL_UNSIGNED_INT)?4:(type==GL_UNSIGNED_BYTE)?1:2`。

### 根因 AF：Primitive Restart

**底层机制**：`ia.primitiveRestartEnable = VK_FALSE` 硬编码（Pipeline.cpp:422），忽略 GL `GL_PRIMITIVE_RESTART_FIXED_INDEX`。Iris/Oculus shader pack 的 strip/fan 几何启用 restart 后，strip 在 restart 索引处不断开 → 连接到无效顶点 → 几何腐败。

MobileGL 启用 `VK_EXT_primitive_topology_list_restart` 并根据 GL 状态设置 `primitiveRestartEnable`（VulkanRenderer.cpp:3861-3877）。

**最小影响域修复**：
1. `Device.cpp`：启用 `VK_EXT_primitive_topology_list_restart` 扩展 + `VkPhysicalDevicePrimitiveTopologyListRestartFeaturesEXT`。
2. `gl.cpp`：维护 `g_state->primitiveRestart` / `g_state->primitiveRestartFixedIndex` 状态（可能已存在，需确认）。
3. `Pipeline.cpp`：`ia.primitiveRestartEnable` 改为读 `g_state`；`hash_signature` 纳入 restart 状态。

### 根因 AG：BaseVertex / BaseInstance

**底层机制**：`glDrawElementsBaseVertex` 的 `baseVertex` 被 `(void)basevertex` 丢弃（Drawing.cpp:379,413），`backend_draw_indexed` 的 `vkCmdDrawIndexed` 写死 `vertexOffset=0`（CommandStream.cpp:1316）。Sodium/Iris 的 instanced 渲染用 baseVertex 在同一 VBO 内偏移 → 丢弃后所有实例读同一组顶点 → 几何错位。

MobileGL 完整传递 `drawParams.baseVertex` / `drawParams.baseInstance` 到 `vkCmdDrawIndexed`。

**最小影响域修复**（保持 API 契约不变）：
1. `State.h`：新增 `g_state->currentBaseVertex` / `g_state->currentBaseInstance`（int32_t，默认 0）。
2. `Drawing.cpp`：`glDrawElementsBaseVertex` 等函数设置 `g_state->currentBaseVertex = basevertex` 后调用 `glDrawElements`；`glDrawElementsInstancedBaseInstance` 设置 `g_state->currentBaseInstance = baseinstance`。
3. `CommandStream.cpp:backend_draw_indexed` / `backend_draw_indexed_instanced`：从 `g_state` 读取 baseVertex/baseInstance 传给 `vkCmdDrawIndexed`。
4. draw 后重置 `g_state->currentBaseVertex = 0` / `currentBaseInstance = 0`（避免泄漏到下一个 draw）。

### 根因 AH：Depth-Stencil descriptor layout

**底层机制**：`DescriptorSet.cpp:490` 硬编码 `imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`。对 depth-stencil 纹理，image 实际布局（经根因 Y 修复后为 `DEPTH_STENCIL_READ_ONLY_OPTIMAL`）与 descriptor 声明不匹配 → MoltenVK 验证错误或静默丢 draw → 黑屏。同时 `Resources.cpp:486` 的上传后 layout transition 也对 depth-stencil 纹理错误转到 `SHADER_READ_ONLY_OPTIMAL`。

MobileGL 的 `ResolveSampledReadOnlyLayout`（VkTextureManager.cpp:177）对 depth-stencil aspect 返回 `DEPTH_STENCIL_READ_ONLY_OPTIMAL`。

**最小影响域修复**：
1. 新增辅助函数 `sampled_layout_for_format(VkFormat)`：depth-stencil 格式返回 `DEPTH_STENCIL_READ_ONLY_OPTIMAL`，color 格式返回 `SHADER_READ_ONLY_OPTIMAL`。
2. `DescriptorSet.cpp:490`：用 `sampled_layout_for_format(tex.format)` 替换硬编码。
3. `Resources.cpp:486`：上传后 layout transition 用 `sampled_layout_for_format(tex.format)`。

### 根因 AI：glTexImage2D mipmap 用 base level 尺寸

**底层机制**：`Texture.cpp:139-153` 用当前 level 的 width/height 调用 `backend_get_or_create_texture`。上传 level 1 时 width/height = base/2，不匹配已存的 base level 尺寸 → `Resources.cpp:749-753` 的复用条件不满足 → 重建 VkImage with extent=(base/2, base/2) → base level 数据丢失 + VkImage extent 错误 → 纹理腐败。

MobileGL 始终用 base level 尺寸作为 VkImage extent（VkTextureManager.cpp:1918-1957）。

**最小影响域修复**：
1. `Texture.cpp:glTexImage2D`：`backend_get_or_create_texture` 调用改用 `t->width` / `t->height`（base level 尺寸，在 level==0 时更新），而非当前 level 的 width/height。
2. `Resources.cpp:749-753`：复用条件额外比较 `levels`，不匹配则重建（用传入的 base level extent）。

## 测试验证

### 根因 Y 验证
**修复前 Fail**：创建用户 FBO + 颜色纹理 + 深度纹理 → 渲染到 FBO → 采样纹理绘制到屏幕 → 黑屏（布局不匹配，draw 被丢弃）。
**修复后 Pass**：同上 → 屏幕显示 FBO 渲染内容。

### 根因 AE 验证
**修复前 Fail**：`glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, indices)` → 索引按 UINT16 解释 → 几何乱飞 → 红屏。
**修复后 Pass**：索引按 UINT8 解释 → 几何正确。

### 根因 AF 验证
**修复前 Fail**：启用 `GL_PRIMITIVE_RESTART_FIXED_INDEX` + strip 几何 → strip 不断开 → 几何腐败 → 红屏。
**修复后 Pass**：strip 在 restart 索引处断开 → 几何正确。

### 根因 AG 验证
**修复前 Fail**：`glDrawElementsInstancedBaseVertex(mode, count, type, indices, instanceCount, baseVertex=100)` → 所有实例读 vertex 0-99 → 几何叠加 → 红屏。
**修复后 Pass**：实例 i 读 vertex (i*stride + 100) → 几何正确分布。

### 根因 AH 验证
**修复前 Fail**：shader 采样 depth 纹理 → descriptor layout 不匹配 → draw 被丢弃 → 黑屏。
**修复后 Pass**：descriptor layout 匹配 → 采样正确。
