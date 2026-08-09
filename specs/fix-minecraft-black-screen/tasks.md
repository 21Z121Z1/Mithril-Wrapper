# Tasks

- [x] Task 1: Sampler uniform 反射与 samplerUnitMap（State.h + Program.cpp）
  - [x] SubTask 1.1: State.h Program 结构新增 `std::unordered_map<GLuint, GLint> samplerUnitMap`（binding→unit，默认-1）
  - [x] SubTask 1.2: Program.cpp 反射阶段处理 `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`，记入 uniforms（name/binding/location）+ 初始化 samplerUnitMap[binding]=-1
  - [x] SubTask 1.3: Program.cpp `glUniform1i` 对 sampler 类型 uniform 写入 samplerUnitMap[binding]=value

- [x] Task 2: DescriptorSet 从 samplerUnitMap 取纹理单元（DescriptorSet.cpp）
  - [x] SubTask 2.1: 绑定 sampler 时先查 `prog->samplerUnitMap[db.binding]`，取对应 unit 的纹理
  - [x] SubTask 2.2: 兜底：映射不存在或 unit<0 时回退到 `db.binding` 作单元号（兼容单纹理简单场景）

- [x] Task 3: boundTextureForUnit target 优先级（State.cpp）
  - [x] SubTask 3.1: 优先返回 GL_TEXTURE_2D target 绑定（Minecraft 主路径）
  - [x] SubTask 3.2: 2D 为空时再遍历其他 target

- [x] Task 4: glUniformMatrix* transpose 处理（Program.cpp）
  - [x] SubTask 4.1: `transpose=GL_TRUE` 时逐矩阵列→行转置后写 backing store
  - [x] SubTask 4.2: `transpose=GL_FALSE` 保持现有 memcpy 路径

- [x] Task 5: Legacy 纹理格式映射（FormatMap.cpp）
  - [x] SubTask 5.1: gl_internal_to_vk 补 GL_ALPHA→R8/swizzle、GL_LUMINANCE→R8/swizzle、GL_LUMINANCE_ALPHA→R8G8/swizzle 等
  - [ ] SubTask 5.2: 上传路径设置对应 swizzle（Texture.cpp 或 Resources.cpp）— 延后，需改 VkImageView 创建路径

- [x] Task 6: gl_VertexID baseVertex 语义修正（Shader.cpp + Drawing.cpp）
  - [x] SubTask 6.1: 保留源码改名（gl_VertexID→gl_VertexIndex），添加语义差异文档注释
  - [x] SubTask 6.2: 保守方案——仅注释记录完整 push-constant 补偿方案（baseVertex==0 时无语义差异，Minecraft 主路径不受影响）

- [x] Task 7: 编译验证（g++ -std=c++20 -fsyntax-only -Wall -Wextra，用 Vulkan stub）
- [x] Task 8: 回归检查（不回滚根因 Y-AI + GL3.3 完整性修复）
- [ ] Task 9: git commit + push（用 git-commit skill，在 implement-gl33-core-renderer 分支）

# Task Dependencies
- Task 2 依赖 Task 1（samplerUnitMap 字段）
- Task 6 可与 Task 1-5 并行
- Task 7 依赖 Task 1-6
- Task 8 依赖 Task 7
- Task 9 依赖 Task 8
