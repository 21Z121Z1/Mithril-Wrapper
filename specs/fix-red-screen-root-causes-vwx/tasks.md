# Tasks

- [x] Task 1: 修复 RGB 纹理格式映射到 4 分量（根因 W，CRITICAL）
  - [x] SubTask 1.1: 修改 `MG_Backend/DirectVulkan/FormatMap.cpp:20,25,27,51`，将 GL_RGB8/GL_RGB16F/GL_RGB32F/GL_RGB 映射到对应 4 分量 VkFormat
  - [x] SubTask 1.2: 修改 `MG_Backend/DirectVulkan/Resources.cpp:stage_and_copy_image`，检测源 format==GL_RGB（或 GL_BGR）时逐像素展开为 RGBA（alpha=0xFF）
  - [x] SubTask 1.3: 添加注释说明 Metal 无 3 分量像素格式，需展开为 RGBA

- [x] Task 2: 修复 3 分量顶点属性格式映射（根因 V，HIGH）
  - [x] SubTask 2.1: 修改 `MG_Backend/DirectVulkan/Pipeline.cpp:attrib_type_to_vk_format`，对 size==3 的非 float 格式返回 4 分量格式
  - [x] SubTask 2.2: 保留 GL_FLOAT/GL_INT/GL_UNSIGNED_INT size==3 的 3 分量格式（Metal 支持）
  - [x] SubTask 2.3: 添加注释说明 Metal 顶点格式限制

- [x] Task 3: 修复 Pipeline blend 格式校验（根因 X，HIGH）
  - [x] SubTask 3.1: 修改 `MG_Backend/DirectVulkan/Pipeline.cpp:get_or_create_pipeline`，在设置 cbAttach.blendEnable 前查询颜色附件格式属性
  - [x] SubTask 3.2: 缓存格式查询结果（避免每次 pipeline 创建都查询）
  - [x] SubTask 3.3: 不支持 COLOR_ATTACHMENT_BLEND_BIT 时强制 blendEnable=VK_FALSE 并记录警告日志
  - [x] SubTask 3.4: 添加注释说明 Vulkan 规范要求及 MobileGL 对照

- [x] Task 4: 编译验证
  - [x] SubTask 4.1: API 契约核查（沙箱无 Vulkan SDK），确认无语法错误

- [x] Task 5: 代码核查与 checklist 验证
  - [x] SubTask 5.1: 逐项验证 checklist.md 中的检查点

# Task Dependencies
- Task 1, 2, 3 互相独立，可并行实现
- Task 4 依赖 Task 1, 2, 3 完成
- Task 5 依赖 Task 4 完成
