# 修复 Minecraft 黑屏：Sampler 绑定语义与着色器内置变量 Spec

## Why

当前渲染器能渲染简单单纹理图形，但渲染 Minecraft（多纹理 + 多 uniform）时黑屏。根因是 sampler 描述符绑定号被假设等于 GL 纹理单元号，但合成 UBO `mithril_GlobalBlock` 占用了 binding 0，把所有 sampler 推到错误 binding，导致多纹理 shader 采到空纹理单元。同时 sampler uniform 未被反射进 `p->uniforms`，`glGetUniformLocation("Sampler0")` 返回 -1，`glUniform1i` 变成 no-op，sampler→单元映射完全丢失。对照 MobileGL `VulkanRenderer.cpp:3201-3213` 的 `samplerNameByBinding` 按名解析机制。

## What Changes

- **Sampler uniform 反射**：`Program.cpp` 反射阶段不仅处理 UBO，还处理 `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`，把每个 sampler 记入 `p->uniforms`（含 name + binding + location）。
- **sampler→纹理单元映射**：`glUniform1i(samplerLoc, unit)` 对 sampler uniform 记录其目标纹理单元号到 `Program::samplerUnitMap[binding]`；`DescriptorSet.cpp` 从该映射取纹理单元，而非用 `db.binding` 直接当单元号。
- **gl_VertexID/gl_InstanceID 语义修正**：源码改名后，对索引绘制（`glDrawElementsBaseVertex` 等）在 SPIR-V 层或 draw 调用补 `baseVertex` 偏移，避免 Minecraft `rendertype_lines` 等着色器取错顶点。
- **boundTextureForUnit target 特异性**：按 shader 期望的 target 取绑定，避免同单元多 target 纹理错配。
- **glUniformMatrix* transpose 处理**：`transpose=GL_TRUE` 时逐矩阵列→行转置后写 UBO。
- **Legacy 纹理格式映射**：`FormatMap.cpp` 补 `GL_ALPHA`/`GL_LUMINANCE`/`GL_LUMINANCE_ALPHA` → 对应 RGBA + swizzle。

## Impact

- Affected specs: `implement-gl33-core-renderer`（补充而非替代）
- Affected code:
  - `MG_Impl/Program.cpp`（sampler 反射 + transpose + samplerUnitMap）
  - `MG_Impl/Shader.cpp`（gl_VertexID 语义）
  - `MG_Backend/DirectVulkan/DescriptorSet.cpp`（sampler 绑定取单元）
  - `MG_State/State.h`（Program 新增 samplerUnitMap 字段）
  - `MG_State/State.cpp`（boundTextureForUnit target 优先级）
  - `MG_Impl/Drawing.cpp`（baseVertex 传递给 shader）
  - `MG_Backend/DirectVulkan/FormatMap.cpp`（legacy 格式）

## ADDED Requirements

### Requirement: Sampler Uniform 反射与单元映射
系统 SHALL 在 `glLinkProgram` 反射阶段把 SPIR-V 中的 `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER` 也记入 `Program::uniforms`（含 name/binding/location），并维护 `samplerUnitMap[binding]→unit` 映射。`glUniform1i(loc, unit)` 对 sampler 类型 uniform 写入该映射。`DescriptorSet` 绑定 sampler 时 SHALL 从 `samplerUnitMap` 取目标纹理单元，而非用 descriptor binding 号直接当单元号。

#### Scenario: Minecraft 多纹理 shader 正确采样
- **WHEN** Minecraft 调用 `glUniform1i(glGetUniformLocation("Sampler0"), 0)` 和 `glUniform1i(glGetUniformLocation("Sampler1"), 1)`
- **THEN** `Sampler0` 采样纹理单元 0 的纹理，`Sampler1` 采样纹理单元 1 的纹理，而非全部回退到单元 0

### Requirement: gl_VertexID 语义保留
系统 SHALL 保留 GL `gl_VertexID` 在索引绘制中的 baseVertex 语义（含 baseVertex 偏移），使 `glDrawElementsBaseVertex` 下着色器计算的顶点 ID 与 GL 一致。

#### Scenario: baseVertex 绘制顶点 ID 正确
- **WHEN** 应用调用 `glDrawElementsBaseVertex` 且 shader 使用 `gl_VertexID`
- **THEN** shader 内 `gl_VertexID` 值 = index + baseVertex，与 GL 行为一致

## MODIFIED Requirements

### Requirement: 矩阵 uniform 转置
`glUniformMatrix*fv` SHALL 在 `transpose=GL_TRUE` 时逐矩阵做列→行转置后再写入 UBO backing store（Vulkan/SPIR-V 一律列主序）。

### Requirement: Legacy 纹理格式
`gl_internal_to_vk` SHALL 支持 `GL_ALPHA`/`GL_LUMINANCE`/`GL_LUMINANCE_ALPHA` 及其 8-bit 变体，映射到对应 RGBA 格式 + texture swizzle。
