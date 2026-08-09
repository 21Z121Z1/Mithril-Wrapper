# Tasks

- [x] Task 1: 修复用户 FBO 附件布局转换（根因 Y，CRITICAL）
  - [x] SubTask 1.1: `CommandStream.cpp:begin_render_pass` 对非 swapchain 的用户 FBO 颜色附件从 `tex.currentLayout` barrier 到 `COLOR_ATTACHMENT_OPTIMAL`，深度附件 barrier 到 `DEPTH_STENCIL_ATTACHMENT_OPTIMAL`
  - [x] SubTask 1.2: `CommandStream.cpp:end_render_pass` 将用户 FBO 附件 barrier 回 read-only 布局（用 `sampled_layout_for_format` 保证与 descriptor 一致），更新 `tex.currentLayout`
  - [x] SubTask 1.3: 在 EncoderState 中追踪用户 FBO 附件对应的 tex_id（用于反查 TextureEntry）
  - [x] SubTask 1.4: 添加注释说明 dynamic rendering 不自动转换布局 + MobileGL VkRenderPassManager 对照

- [x] Task 2: 修复 Viewport/Scissor Y 原点转换（根因 Z，MEDIUM）
  - [x] SubTask 2.1: `CommandStream.cpp:backend_set_viewport` 计算 `vk_y = framebufferHeight - gl_y - gl_h`
  - [x] SubTask 2.2: `CommandStream.cpp:backend_set_scissor` 同样转换 Y
  - [x] SubTask 2.3: 添加注释说明 GL bottom-origin vs Vulkan top-origin + MoltenVK 不做 Y 翻转

- [x] Task 3: 修复 Depth-only 格式不绑定 pStencilAttachment（根因 AA，HIGH）
  - [x] SubTask 3.1: `CommandStream.cpp:begin_render_pass` 用 `format_has_stencil(depthFormat)` 判断是否含 stencil aspect，仅对含 stencil 的格式设置 `pStencilAttachment`
  - [x] SubTask 3.2: 添加注释说明 VUID-06126 spec 要求

- [x] Task 4: 修复 GL_UNSIGNED_BYTE 索引类型支持（根因 AE，CRITICAL）
  - [x] SubTask 4.1: `Device.cpp` 启用 `VK_EXT_index_type_uint8` 扩展 + `VkPhysicalDeviceIndexTypeUint8FeaturesEXT` feature chain
  - [x] SubTask 4.2: `Drawing.cpp:index_type_to_int` 增加 `GL_UNSIGNED_BYTE → 2`
  - [x] SubTask 4.3: `CommandStream.cpp:backend_draw_indexed` / `backend_draw_indexed_instanced` 增加 `case 2: VK_INDEX_TYPE_UINT8_EXT`
  - [x] SubTask 4.4: `Drawing.cpp` client-memory 索引 staging 路径修正 elem 大小
  - [x] SubTask 4.5: 添加注释说明 MobileGL 对照 + MoltenVK 支持

- [x] Task 5: 修复 Primitive Restart 支持（根因 AF，CRITICAL）
  - [x] SubTask 5.1: `Device.cpp` 启用 `VK_EXT_primitive_topology_list_restart` 扩展 + feature chain
  - [x] SubTask 5.2: `gl.cpp` 维护 `g_state->primitiveRestartFixedIndex` 状态（glEnable/glDisable/glIsEnabled）
  - [x] SubTask 5.3: `Pipeline.cpp:get_or_create_pipeline` 将 `ia.primitiveRestartEnable` 改为从 GL 状态读取
  - [x] SubTask 5.4: `Pipeline.cpp:hash_signature` 纳入 primitiveRestart 状态
  - [x] SubTask 5.5: 添加注释说明 MobileGL VulkanRenderer.cpp:3861-3877 对照

- [x] Task 6: 修复 BaseVertex / BaseInstance 支持（根因 AG，CRITICAL）
  - [x] SubTask 6.1: `State.h` 新增 `currentBaseVertex` / `currentBaseInstance` 字段
  - [x] SubTask 6.2: `Drawing.cpp` 各 BaseVertex/BaseInstance draw 变体设置 g_state 字段后调用 draw
  - [x] SubTask 6.3: `CommandStream.cpp:backend_draw_indexed` 等从 g_state 读取 baseVertex/baseInstance 传给 vkCmdDrawIndexed
  - [x] SubTask 6.4: draw 后重置 g_state 字段为 0（避免泄漏）
  - [x] SubTask 6.5: 添加注释说明 MobileGL drawParams 对照

- [x] Task 7: 修复 Depth-Stencil descriptor layout（根因 AH，HIGH）
  - [x] SubTask 7.1: 新增辅助函数 `sampled_layout_for_format(VkFormat)`（FormatMap.h/.cpp）
  - [x] SubTask 7.2: `DescriptorSet.cpp` descriptor imageInfo 的 `imageLayout` 用 `sampled_layout_for_format(tex.format)`
  - [x] SubTask 7.3: `Resources.cpp:stage_and_copy_image` 上传后 layout transition 用 `sampled_layout_for_format(tex.format)`
  - [x] SubTask 7.4: 添加注释说明 MobileGL ResolveSampledReadOnlyLayout 对照
  - [x] SubTask 7.5: `CommandStream.cpp:end_render_pass` 用户 FBO 附件回退也用 `sampled_layout_for_format`（保证与 descriptor 一致）

- [x] Task 8: 修复 glTexImage2D mipmap 用 base level 尺寸（根因 AI，HIGH）
  - [x] SubTask 8.1: `Texture.cpp:glTexImage2D` 用 `t->width` / `t->height`（base level 尺寸）调用 `backend_get_or_create_texture`
  - [x] SubTask 8.2: `Resources.cpp:backend_get_or_create_texture` 复用条件额外比较 `levels`
  - [x] SubTask 8.3: 添加注释说明 MobileGL CheckMipmapCompleteness 对照

- [x] Task 9: 编译验证
  - [x] SubTask 9.1: API 契约核查（沙箱无 Vulkan SDK），确认无语法错误，所有签名不变

- [x] Task 10: 代码核查与 checklist 验证 + git commit
  - [x] SubTask 10.1: 逐项验证 checklist.md 中的检查点

- [x] Task 11: 补全 backend_set_fbo_attachment_tex_ids 的外部声明与调用（根因 Y 闭环）
  - [x] SubTask 11.1: `Backend.h` 声明 `backend_set_fbo_attachment_tex_ids`（C API 原型）
  - [x] SubTask 11.2: `Drawing.cpp:prepare_draw` 在 begin_render_pass 前对用户 FBO 调用 `backend_set_fbo_attachment_tex_ids` 注册颜色/深度 tex_id；对 FBO 0 传 null/0 清除

# Task Dependencies
- Task 1, 2, 3 修改 CommandStream.cpp，串行实现避免冲突
- Task 4, 5, 6 修改 Drawing.cpp + Device.cpp + Pipeline.cpp，部分串行
- Task 7 修改 DescriptorSet.cpp + Resources.cpp，独立
- Task 8 修改 Texture.cpp + Resources.cpp，独立但与 Task 7 在 Resources.cpp 上串行
- Task 9 依赖 Task 1-8 完成
- Task 10 依赖 Task 9 完成
- Task 11 依赖 Task 1，跨 Backend.h + Drawing.cpp
