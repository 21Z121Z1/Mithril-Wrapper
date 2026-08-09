# Tasks

- [x] Task 1: 修复 Y 翻转面剔除双重补偿（根因 K，CRITICAL）
  - [x] SubTask 1.1: 修改 `MG_Impl/Drawing.cpp:238-265`，移除 cull mode 交换逻辑（`invert_clockwise` 变量及三元运算），cull mode 直接按 GL 值映射（GL_FRONT→VK_FRONT, GL_BACK→VK_BACK），保留 frontFace=CW 补偿
  - [x] SubTask 1.2: 更新注释说明：仅 frontFace 补偿，不交换 cull mode（避免双重补偿）
  - [x] SubTask 1.3: 确认用户 FBO 路径（is_default_fbo=false）行为不变

- [x] Task 2: 修复 Pipeline 缓存键遗漏 offset（根因 L，HIGH）
  - [x] SubTask 2.1: 修改 `MG_Backend/DirectVulkan/Pipeline.cpp:140-152`，在循环内添加 `mix(&attribs[i].offset, sizeof(attribs[i].offset));`
  - [x] SubTask 2.2: 添加注释说明 offset 是 VkVertexInputAttributeDescription 的成员，必须参与缓存键

- [x] Task 3: 修复采样器参数硬编码（根因 M，HIGH）
  - [x] SubTask 3.1: 修改 `MG_Backend/DirectVulkan/DescriptorSet.cpp:454-472`，从 `mithril::state_get_texture(tex_id)` 读取真实的 minFilter/magFilter/wrapS/wrapT/wrapR
  - [x] SubTask 3.2: 添加注释说明采样器参数来源及缓存策略

- [x] Task 4: 编译验证
  - [x] SubTask 4.1: API 契约核查通过（沙箱无 Vulkan SDK）。Drawing.cpp: backend_set_cull_mode/front_face 签名不变；Pipeline.cpp: hash_signature 签名不变；DescriptorSet.cpp: state_get_texture 经 State.h 直接 include 可访问，backend_get_or_create_sampler 签名不变。

- [x] Task 5: 代码核查与 checklist 验证
  - [x] SubTask 5.1: 逐项验证 checklist.md 中的检查点（K1-K6、L1-L5、M1-M6、N1-N3、O1-O5 均已通过；P1-P5 需 Apple 环境运行验证）

# Task Dependencies
- Task 1, 2, 3 互相独立，可并行实现
- Task 4 依赖 Task 1, 2, 3 完成
- Task 5 依赖 Task 4 完成
