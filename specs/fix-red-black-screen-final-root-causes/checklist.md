# 红屏/黑屏最终根因（Y/Z/AA/AE/AF/AG/AH/AI）修复检查清单

> 配合 [spec.md](./spec.md) 使用。每项必须实际验证（代码核查 / 编译），不可凭推断打勾。

## 一、根因 Y：用户 FBO 附件布局转换

- [x] Y1. `CommandStream.cpp:begin_render_pass` 对非 swapchain 的用户 FBO 颜色附件从 `tex.currentLayout` barrier 到 `COLOR_ATTACHMENT_OPTIMAL`。
- [x] Y2. `begin_render_pass` 对用户 FBO 深度附件 barrier 到 `DEPTH_STENCIL_ATTACHMENT_OPTIMAL`。
- [x] Y3. `CommandStream.cpp:end_render_pass` 将用户 FBO 附件 barrier 回 read-only 布局（`sampled_layout_for_format`）。
- [x] Y4. `end_render_pass` 深度附件回退用 `sampled_layout_for_format(tex.format)`（depth-stencil → DEPTH_STENCIL_READ_ONLY_OPTIMAL，depth-only → DEPTH_READ_ONLY_OPTIMAL）。
- [x] Y5. `end_render_pass` 更新 `TextureEntry::currentLayout` 为正确的 read-only 布局。
- [x] Y6. EncoderState 中追踪用户 FBO 附件对应的 tex_id（用于反查 TextureEntry）。
- [x] Y7. 改动处有注释说明 dynamic rendering 不自动转换布局 + MobileGL VkRenderPassManager 对照。
- [x] Y8. 确认 swapchain 附件路径不受影响（保持原有 barrier 逻辑）。
- [x] Y9. `Backend.h` 声明 `backend_set_fbo_attachment_tex_ids`（Task 11 闭环）。
- [x] Y10. `Drawing.cpp:prepare_draw` 在 begin_render_pass 前调用 `backend_set_fbo_attachment_tex_ids` 注册用户 FBO tex_id；FBO 0 传 null/0 清除。

## 二、根因 Z：Viewport/Scissor Y 原点转换

- [x] Z1. `CommandStream.cpp:backend_set_viewport` 计算 `vk_y = framebufferHeight - gl_y - gl_h`。
- [x] Z2. `CommandStream.cpp:backend_set_scissor` 同样转换 Y。
- [x] Z3. framebufferHeight 从 `encoder_height_for_yflip()`（返回 `e.height`）获取，fallback `g_state->viewportH`。
- [x] Z4. 改动处有注释说明 GL bottom-origin vs Vulkan top-origin + MoltenVK 不做 Y 翻转。
- [x] Z5. 确认全屏 viewport（y=0）行为不变（vk_y = H - 0 - H = 0）。

## 三、根因 AA：Depth-only 格式不绑定 pStencilAttachment

- [x] AA1. `CommandStream.cpp:begin_render_pass` 用 `format_has_stencil(e.depthFormat)` 判断是否含 stencil aspect。
- [x] AA2. 仅对含 stencil 的格式（D24_UNORM_S8_UINT / D32_SFLOAT_S8_UINT / S8_UINT）设置 `pStencilAttachment`。
- [x] AA3. 对 depth-only 格式（D32_SFLOAT / D16_UNORM）设为 `nullptr`。
- [x] AA4. 改动处有注释说明 VUID-VkRenderingInfo-pStencilAttachment-06126。
- [x] AA5. 确认 swapchain 深度（D32_SFLOAT_S8_UINT，含 stencil）不受影响。

## 四、根因 AE：GL_UNSIGNED_BYTE 索引类型支持

- [x] AE1. `Device.cpp` 启用 `VK_EXT_index_type_uint8` 扩展。
- [x] AE2. `Device.cpp` 链入 `VkPhysicalDeviceIndexTypeUint8FeaturesEXT` feature（indexTypeUint8 = VK_TRUE）。
- [x] AE3. `Drawing.cpp:index_type_to_int` 增加 `GL_UNSIGNED_BYTE → 2`。
- [x] AE4. `CommandStream.cpp:backend_draw_indexed` / `backend_draw_indexed_instanced` 增加 `case 2: VK_INDEX_TYPE_UINT8_EXT`。
- [x] AE5. `Drawing.cpp` client-memory 索引 staging 路径 `elem = (type==GL_UNSIGNED_INT)?4:(type==GL_UNSIGNED_BYTE)?1:2`。
- [x] AE6. 改动处有注释说明 MobileGL VulkanRenderer.cpp:3093-3109 对照。
- [x] AE7. 确认 GL_UNSIGNED_SHORT（→0/UINT16）和 GL_UNSIGNED_INT（→1/UINT32）路径不受影响。

## 五、根因 AF：Primitive Restart 支持

- [x] AF1. `Device.cpp` 启用 `VK_EXT_primitive_topology_list_restart` 扩展。
- [x] AF2. `Device.cpp` 链入 `VkPhysicalDevicePrimitiveTopologyListRestartFeaturesEXT`（primitiveTopologyListRestart = VK_TRUE）。
- [x] AF3. `gl.cpp` 维护 `g_state->primitiveRestartFixedIndex` 状态（glEnable/glDisable/glIsEnabled）。
- [x] AF4. `Pipeline.cpp:get_or_create_pipeline` 的 `ia.primitiveRestartEnable` 从 GL 状态读取。
- [x] AF5. `Pipeline.cpp:hash_signature` 纳入 primitiveRestart 状态。
- [x] AF6. 改动处有注释说明 MobileGL VulkanRenderer.cpp:3861-3877 对照。
- [x] AF7. 确认未启用 restart 时 `primitiveRestartEnable = VK_FALSE`（行为不变）。

## 六、根因 AG：BaseVertex / BaseInstance 支持

- [x] AG1. `State.h` 新增 `currentBaseVertex`（int32_t）/ `currentBaseInstance`（uint32_t）字段，默认 0。
- [x] AG2. `Drawing.cpp:glDrawElementsBaseVertex` 设置 `g_state->currentBaseVertex = basevertex` 后调用 draw。
- [x] AG3. `Drawing.cpp:glDrawElementsInstancedBaseVertex` 设置 `g_state->currentBaseVertex`。
- [x] AG4. `Drawing.cpp:glDrawElementsInstancedBaseInstance` 设置 `g_state->currentBaseInstance`。
- [x] AG5. `Drawing.cpp:glDrawArraysInstancedBaseInstance` 设置 `g_state->currentBaseInstance`。
- [x] AG6. `CommandStream.cpp:backend_draw_indexed` 从 g_state 读取 currentBaseVertex 传给 `vkCmdDrawIndexed` 的 `vertexOffset`。
- [x] AG7. `CommandStream.cpp:backend_draw_indexed_instanced` 从 g_state 读取 baseVertex/baseInstance 传给 `vkCmdDrawIndexed`。
- [x] AG8. `CommandStream.cpp:backend_draw_arrays` / `backend_draw_arrays_instanced` 从 g_state 读取 currentBaseInstance 传给 `vkCmdDraw` 的 `firstInstance`。
- [x] AG9. draw 后重置 `g_state->currentBaseVertex = 0` / `currentBaseInstance = 0`（避免泄漏）。
- [x] AG10. 改动处有注释说明 MobileGL drawParams 对照。

## 七、根因 AH：Depth-Stencil descriptor layout

- [x] AH1. 新增辅助函数 `sampled_layout_for_format(VkFormat)`（FormatMap.h/.cpp）。
- [x] AH2. `DescriptorSet.cpp` descriptor imageInfo 的 `imageLayout` 用 `sampled_layout_for_format(tex.format)`。
- [x] AH3. `Resources.cpp:stage_and_copy_image` 上传后 layout transition 用 `sampled_layout_for_format(tex.format)`。
- [x] AH4. 改动处有注释说明 MobileGL ResolveSampledReadOnlyLayout 对照。
- [x] AH5. 确认 color 格式仍使用 `SHADER_READ_ONLY_OPTIMAL`（不受影响）。
- [x] AH6. `CommandStream.cpp:end_render_pass` 用户 FBO 附件回退也用 `sampled_layout_for_format`（保证与 descriptor + Resources 一致，消除 depth-only SHADER_READ_ONLY vs DEPTH_READ_ONLY 不匹配）。

## 八、根因 AI：glTexImage2D mipmap 用 base level 尺寸

- [x] AI1. `Texture.cpp:glTexImage2D` 在 level==0 时更新 `t->width` / `t->height`。
- [x] AI2. `Texture.cpp:glTexImage2D` 调用 `backend_get_or_create_texture` 时用 `t->width` / `t->height`（base level 尺寸）。
- [x] AI3. `Resources.cpp:backend_get_or_create_texture` 复用条件额外比较 `levels`。
- [x] AI4. 改动处有注释说明 MobileGL CheckMipmapCompleteness 对照。
- [x] AI5. 确认 level==0 上传行为不变（width/height == t->width/t->height）。

## 九、编译验证

- [x] BB1. CommandStream.cpp API 契约验证通过（所有 backend_* 签名不变）。完整编译需 Apple 环境。
- [x] BB2. Pipeline.cpp API 契约验证通过（get_or_create_pipeline / hash_signature 签名不变）。完整编译需 Apple 环境。
- [x] BB3. DescriptorSet.cpp API 契约验证通过。完整编译需 Apple 环境。
- [x] BB4. Resources.cpp API 契约验证通过。完整编译需 Apple 环境。
- [x] BB5. Device.cpp API 契约验证通过。完整编译需 Apple 环境。
- [x] BB6. Drawing.cpp API 契约验证通过。完整编译需 Apple 环境。
- [x] BB7. Texture.cpp API 契约验证通过。完整编译需 Apple 环境。
- [x] BB8. State.h 新增字段不破坏现有结构布局。

## 十、回归检查

- [x] CC1. 修复不回滚根因 A-X 的任何改动。
- [x] CC2. `begin_render_pass` / `end_render_pass` 的 API 签名不变。
- [x] CC3. `backend_set_viewport` / `backend_set_scissor` 的 API 签名不变。
- [x] CC4. `get_or_create_pipeline` / `hash_signature` 的 API 签名不变。
- [x] CC5. `backend_draw_indexed` / `backend_draw_indexed_instanced` 的 API 签名不变。
- [x] CC6. `backend_get_or_create_texture` 的 API 签名不变。
- [x] CC7. `glTexImage2D` 的 API 签名不变。

## 十一、运行验证（需 Apple 目标环境）

> 以下项需在 iOS/macOS + MoltenVK 环境运行验证，linux 沙箱无法执行。

- [ ] DD1. 用户 FBO 渲染后采样纹理 → 屏幕显示正确内容（根因 Y）。
- [ ] DD2. GUI scissor 裁剪正确区域（根因 Z）。
- [ ] DD3. depth-only FBO draw 不被丢弃（根因 AA）。
- [ ] DD4. GL_UNSIGNED_BYTE 索引几何正确渲染（根因 AE）。
- [ ] DD5. primitive restart strip 几何正确断开（根因 AF）。
- [ ] DD6. baseVertex instanced 渲染几何正确分布（根因 AG）。
- [ ] DD7. depth 纹理采样返回正确值（根因 AH）。
- [ ] DD8. glTexImage2D 逐级 mipmap 上传后纹理完整（根因 AI）。
- [ ] DD9. 加载界面不再红屏/黑屏。
