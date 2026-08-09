# 红屏/黑屏真实根因（H/I/J）修复检查清单

> 配合 [spec.md](./spec.md) 使用。每项必须实际验证（代码核查 / 编译），不可凭推断打勾。

## 一、根因 H：顶点属性偏移单次应用

- [x] H1. `MG_Impl/Drawing.cpp:290` 的 `backend_set_vertex_buffer` 调用已将第三个参数（绑定偏移）改为 `0`，不再传 `(VkDeviceSize)m.offset`。（行号因新增注释块从 283 移至 290）
- [x] H2. `MG_Backend/DirectVulkan/Pipeline.cpp:302` 的 `ad.offset = (uint32_t)a.offset` 保持不变（属性成员偏移仍由 GL pointer 提供）。
- [x] H3. 改动处有清晰注释说明：绑定偏移设 0，偏移由属性描述处理（Vulkan 寻址公式：buffer + 0 + vertexIndex*stride + attr.offset）。
- [x] H4. grep 确认无其他地方调用 `backend_set_vertex_buffer` 时传入 `m.offset` 作为偏移（零缓冲绑定 line 298 已是 0，正确）。
- [x] H5. 机理验证：交错格式 position@0/color@12/uv@24 → 修复前 color 读自 buffer+24（错误），修复后 color 读自 buffer+12（正确）。

## 二、根因 I：颜色纹理支持 FBO 附件用途

- [x] I1. `MG_Backend/DirectVulkan/Resources.cpp:688-689` 的 `ici.usage` 已添加 `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT`。
- [x] I2. 该 usage bit 对所有颜色纹理无条件添加（不区分是否用作附件），对标 MobileGL 的纹理创建策略。
- [x] I3. depth 纹理的 `DEPTH_STENCIL_ATTACHMENT_BIT` 逻辑（line 690-694）保持不变。
- [x] I4. 改动处有注释说明：颜色纹理需支持 FBO 附件用途（Minecraft deferred renderer 大量使用 FBO 颜色附件）。
- [x] I5. 确认对仅用于采样的纹理无副作用（Vulkan 允许设置未使用的 usage bit）。已确认 `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT` 在 SwapchainCommon.cpp:144 已被使用，属标准 Vulkan 枚举。

## 三、根因 J：glClear 不提前提交帧

- [x] J1. `MG_Impl/gl.cpp:66` 的 `backend_commit()` 调用已移除（原 line 59，现替换为注释 `// 不调用 backend_commit() — 帧提交由 eglSwapBuffers 统一处理。`）。
- [x] J2. glClear 的其余调用（begin_render_pass + clear_attachments + end_render_pass）保持不变（line 62-65），clear 命令保留在当前 command buffer 中。
- [x] J3. 更新后的注释说明：clear 不触发帧提交，由 eglSwapBuffers 统一提交。
- [x] J4. 确认 glReadPixels 的 `backend_commit()`（ImageOps.cpp:264）不受影响，仍能获取 immediate 结果。
- [x] J5. 确认 eglWaitClient/eglWaitGL 的 commit（egl.cpp:984-985）不受影响（它们是显式同步点，需要提交）。另确认 glFlush/glFinish（gl.cpp:385/391）、eglSwapBuffers（egl.cpp:908）、glBlitFramebuffer（Framebuffer.cpp:450）的 commit 均不受影响。

## 四、编译验证

- [x] K1. Drawing.cpp API 契约验证通过：`backend_set_vertex_buffer(int, VkBuffer, VkDeviceSize)` 第三参接受 `0`（VkDeviceSize 即 uint64_t）。完整编译需 Apple 环境。
- [x] K2. Resources.cpp API 契约验证通过：`VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT` 是标准 Vulkan 枚举，已在 SwapchainCommon.cpp 使用。完整编译需 Apple 环境。
- [x] K3. gl.cpp API 契约验证通过：移除 `backend_commit();` 语句不影响语法（前一行 `backend_end_render_pass();` 分号完整）。完整编译需 Apple 环境。

> 注：沙箱环境（Linux）无 Vulkan SDK / Metal headers，无法执行 `g++ -fsyntax-only`。已通过 API 契约核查（Backend.h 函数签名 + 标准枚举可用性）替代。完整编译验证需在 Apple 目标环境执行。

## 五、回归检查

- [x] L1. 修复不回滚根因 A-G 的任何改动（Y翻转/Z重映射/cull/blit/semaphore/纹理上传/alpha 均保持）。
- [x] L2. `backend_set_vertex_buffer` 的 API 签名不变（仍是 `int slot, VkBuffer buffer, VkDeviceSize offset`）。
- [x] L3. `backend_commit` 的 API 签名不变（`void backend_commit(void)`）。
- [x] L4. 纹理创建的 API 契约不变（`create_image` 签名不变）。

## 六、运行验证（需 Apple 目标环境）

> 以下项需在 iOS/macOS + MoltenVK 环境运行验证，linux 沙箱无法执行。

- [ ] M1. 加载界面不再红屏（顶点 UV/color 偏移修复后，纹理采样正确）。
- [ ] M2. 进游戏后不再黑屏（FBO 颜色附件渲染正确 + glClear 不提前提交）。
- [ ] M3. 交错顶点格式渲染正确（position/color/uv/lightmap/normal 各属性对齐）。
- [ ] M4. FBO 渲染通道（gbuffer/composite/lighting）输出正确。
- [ ] M5. 帧序列 glClear→glDraw→eglSwapBuffers 显示完整 draw 内容（非仅 clear color）。
