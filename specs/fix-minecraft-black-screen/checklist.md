# Checklist

- [x] Task 1: State.h 新增 samplerUnitMap 字段；Program.cpp 反射 sampler + glUniform1i 写映射
- [x] Task 2: DescriptorSet.cpp 从 samplerUnitMap 取纹理单元，兜底回退 db.binding
- [x] Task 3: State.cpp boundTextureForUnit 优先返回 2D target
- [x] Task 4: Program.cpp glUniformMatrix*fv 处理 transpose=GL_TRUE
- [x] Task 5: FormatMap.cpp 补 GL_ALPHA/LUMINANCE/LUMINANCE_ALPHA 映射（swizzle 设置延后）
- [x] Task 6: Shader.cpp/Drawing.cpp gl_VertexID baseVertex 语义注释（保守方案）
- [x] Task 7: g++ -std=c++20 -fsyntax-only -Wall -Wextra 通过（Vulkan stub 验证，改动代码 0 错误）
- [x] Task 8: 根因 Y/Z/AA/AE/AF/AG/AH/AI 不回滚；GL3.3 完整性修复（uniform反射/divisor/unpack/动态状态/sampler/clearbuffer）保留
- [ ] Task 9: git commit + push 到 implement-gl33-core-renderer 远程分支
