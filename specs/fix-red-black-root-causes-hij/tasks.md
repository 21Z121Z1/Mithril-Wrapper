# Tasks

- [x] Task 1: 修复顶点属性偏移双重应用（根因 H，CRITICAL）
  - [x] SubTask 1.1: 修改 `MG_Impl/Drawing.cpp:283`（现 290），将 `backend_set_vertex_buffer(m.location, buf, (VkDeviceSize)m.offset)` 改为 `backend_set_vertex_buffer(m.location, buf, 0)`，绑定偏移置 0，偏移由属性描述处理
  - [x] SubTask 1.2: 确认 `MG_Backend/DirectVulkan/Pipeline.cpp:302` 的 `ad.offset = (uint32_t)a.offset` 保持不变（属性成员偏移）
  - [x] SubTask 1.3: 在 Drawing.cpp:283 改动处添加注释说明偏移由属性描述处理

- [x] Task 2: 修复颜色纹理缺 COLOR_ATTACHMENT_BIT（根因 I，HIGH）
  - [x] SubTask 2.1: 修改 `MG_Backend/DirectVulkan/Resources.cpp:680`（现 688-689），在 `ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;` 后添加 `| VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT`
  - [x] SubTask 2.2: 在改动处添加注释说明颜色纹理需支持 FBO 附件用途

- [x] Task 3: 修复 glClear 帧内提前提交（根因 J，HIGH）
  - [x] SubTask 3.1: 修改 `MG_Impl/gl.cpp:59`（现 66），移除 `backend_commit()` 调用
  - [x] SubTask 3.2: 更新 glClear 的注释，说明 clear 命令保留在当前 command buffer 中，由 eglSwapBuffers 统一提交

- [x] Task 4: 编译验证
  - [x] SubTask 4.1: 沙箱无 Vulkan SDK，改用 API 契约核查：确认 `backend_set_vertex_buffer` 签名接受 0、`VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT` 是标准枚举（已在 SwapchainCommon.cpp 使用）、移除 `backend_commit()` 不破坏语法。完整编译需 Apple 环境。

- [x] Task 5: 代码核查与 checklist 验证
  - [x] SubTask 5.1: 逐项验证 checklist.md 中的检查点（H1-H5、I1-I5、J1-J5、K1-K3、L1-L4 均已通过；M1-M5 需 Apple 环境运行验证）

# Task Dependencies
- Task 1, 2, 3 互相独立，可并行实现
- Task 4 依赖 Task 1, 2, 3 完成
- Task 5 依赖 Task 4 完成
