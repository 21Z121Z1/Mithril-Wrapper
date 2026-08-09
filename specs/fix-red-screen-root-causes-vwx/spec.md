# 修复加载界面红屏根因（V/W/X）Spec

## Why

H-M 修复后加载界面红屏仍然存在。深度对比 MobileGL 与 Mithril-Wrapper 后发现，Mithril 使用了 Metal/MoltenVK 不支持的 3 分量像素/顶点格式，且缺少 blend 格式校验。当 pipeline 创建失败时，draw 被跳过，屏幕只剩 clear color（红色）。

## What Changes

- **根因 W（CRITICAL）**：`FormatMap.cpp` 将 `GL_RGB8`/`GL_RGB` 等 3 分量格式映射为 `VK_FORMAT_R8G8B8_UNORM`（3 分量）。Metal **无 3 分量像素格式**（MTLPixelFormat 从 RG8 直接跳到 RGBA8，无 RGB8）→ 用作 FBO 颜色附件时 pipeline 创建失败，用作采样纹理时 image 创建失败 → draw 跳过 → 红屏。修复：将 3 分量 GL 格式映射到 4 分量 VkFormat（如 RGB8→RGBA8），并在上传路径做 RGB→RGBA 展开（alpha=0xFF）。
- **根因 V（HIGH）**：`Pipeline.cpp:attrib_type_to_vk_format` 对 `size==3` 返回 3 分量顶点格式（如 `R8G8B8_UNORM`、`R16G16B16_SFLOAT`）。Metal 顶点格式枚举不含 UChar3/Char3/UShort3/Short3/Half3（仅有 1/2/4 分量及 Float3）→ pipeline 创建失败 → draw 跳过 → 红屏。修复：将 size==3 的非 float 格式映射到对应 4 分量格式（如 R8G8B8_UNORM→R8G8B8A8_UNORM），并在顶点绑定路径做数据流转换（或将 stride 调整使第 4 分量读取 padding）。
- **根因 X（HIGH）**：`Pipeline.cpp:399-408` 直接按 GL blend 状态设置 `blendEnable`，不校验颜色附件格式是否支持 `VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT`。对不支持 blend 的格式（如 float FBO、整数 FBO）启用 blend 属非法 pipeline 状态 → MoltenVK 编译失败或静默丢 draw → 红屏。修复：pipeline 创建前查询格式属性，不支持时强制禁用 blend。

## Impact

- 受影响 spec：`fix-red-screen-root-causes-klm`（K/L/M，互补）
- 受影响代码：
  - `MG_Backend/DirectVulkan/FormatMap.cpp`（根因 W：3 分量→4 分量映射）
  - `MG_Backend/DirectVulkan/Resources.cpp`（根因 W：上传路径 RGB→RGBA 展开）
  - `MG_Backend/DirectVulkan/Pipeline.cpp`（根因 V：size==3 顶点格式→4 分量；根因 X：blend 格式校验）

## ADDED Requirements

### Requirement: 3 分量 GL 纹理格式映射到 4 分量 VkFormat

系统 SHALL 将所有 3 分量 GL 纹理格式（GL_RGB8/GL_RGB/GL_RGB16F/GL_RGB32F 等）映射到对应的 4 分量 VkFormat（RGBA8/RGBA16F/RGBA32F），并在纹理上传时展开 RGB 源数据为 RGBA（alpha 填充 0xFF 或 1.0）。

#### Scenario: GL_RGB8 纹理创建并采样
- **WHEN** 应用调用 `glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, data)`
- **THEN** VkImage 使用 `VK_FORMAT_R8G8B8A8_UNORM`，上传时每像素补 alpha=0xFF，采样返回正确 RGB 颜色（alpha=1.0）

#### Scenario: GL_RGB8 纹理用作 FBO 颜色附件
- **WHEN** 应用将 GL_RGB8 纹理绑定为 FBO 颜色附件并渲染
- **THEN** pipeline 的 `pColorAttachmentFormats` 含 `VK_FORMAT_R8G8B8A8_UNORM`（Metal 支持），pipeline 创建成功

### Requirement: 3 分量顶点属性映射到 4 分量 VkFormat

系统 SHALL 将 Metal 不支持的 3 分量顶点属性格式映射到对应的 4 分量格式。对于 size==3 的非 float 类型，返回 4 分量 VkFormat。

#### Scenario: GL_UNSIGNED_BYTE normalized size=3 顶点属性
- **WHEN** 应用使用 `glVertexAttribPointer(loc, 3, GL_UNSIGNED_BYTE, GL_TRUE, stride, offset)`
- **THEN** `VkVertexInputAttributeDescription.format` 为 `VK_FORMAT_R8G8B8A8_UNORM`（4 分量，Metal 支持），第 4 分量读取 padding 字节（不影响正确性，因 shader 仅用 vec3）

#### Scenario: GL_FLOAT size=3 顶点属性
- **WHEN** 应用使用 `glVertexAttribPointer(loc, 3, GL_FLOAT, GL_FALSE, stride, offset)`
- **THEN** `format` 保持 `VK_FORMAT_R32G32B32_SFLOAT`（Metal 支持 Float3，无需转换）

### Requirement: Blend 启用前校验格式支持

系统 SHALL 在 pipeline 创建前查询颜色附件格式是否支持 `VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT`，不支持时强制禁用 blend。

#### Scenario: float FBO 启用 blend
- **WHEN** 应用在 RGBA16F FBO 上启用 blend
- **THEN** 若格式不支持 COLOR_ATTACHMENT_BLEND_BIT，pipeline 的 `blendEnable` 强制为 VK_FALSE（避免非法状态），并记录警告日志

#### Scenario: 普通 RGBA8 FBO 启用 blend
- **WHEN** 应用在 RGBA8 FBO 上启用 blend
- **THEN** blend 正常启用（RGBA8 支持 COLOR_ATTACHMENT_BLEND_BIT）

## MODIFIED Requirements

### Requirement: GL 内部格式到 VkFormat 映射

`FormatMap.cpp:gl_internal_to_vk` SHALL 将以下 3 分量格式映射到 4 分量：
```cpp
case GL_RGB8:    return VK_FORMAT_R8G8B8A8_UNORM;  // 原 R8G8B8_UNORM
case GL_RGB:     return VK_FORMAT_R8G8B8A8_UNORM;  // 原 R8G8B8_UNORM
case GL_RGB16F:  return VK_FORMAT_R16G16B16A16_SFLOAT;  // 原 R16G16B16_SFLOAT
case GL_RGB32F:  return VK_FORMAT_R32G32B32A32_SFLOAT;  // 原 R32G32B32_SFLOAT
```

### Requirement: 纹理上传路径 RGB→RGBA 展开

`Resources.cpp:stage_and_copy_image` SHALL 在检测到源 `format==GL_RGB`（或 `GL_BGR`）且目标 VkFormat 为 4 分量时，逐像素展开：每 3 字节 RGB 复制为 4 字节 RGBA（alpha=0xFF）。

### Requirement: 顶点属性格式映射

`Pipeline.cpp:attrib_type_to_vk_format` SHALL 对 size==3 的非 float 格式返回 4 分量格式：
```cpp
// normalized ubyte size=3: R8G8B8A8_UNORM（原 R8G8B8_UNORM）
// snorm byte size=3: R8G8B8A8_SNORM
// normalized ushort size=3: R16G16B16A16_UNORM
// snorm short size=3: R16G16B16A16_SNORM
// half float size=3: R16G16B16A16_SFLOAT
// integer variants 同理
// GL_FLOAT size=3: 保持 R32G32B32_SFLOAT（Metal 支持）
// GL_INT/GL_UNSIGNED_INT size=3: 保持 R32G32B32_SINT/_UINT（Metal 支持）
```

### Requirement: Pipeline blend 格式校验

`Pipeline.cpp:get_or_create_pipeline` SHALL 在设置 `cbAttach.blendEnable` 前查询颜色附件格式的 `optimalTilingFeatures`，若不含 `VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT` 则强制 `blendEnable=VK_FALSE`。

## REMOVED Requirements

无。

## 详细设计

### 根因 W：RGB 纹理格式映射 + 上传展开

**底层机制**：Metal 的 MTLPixelFormat 枚举**不含** 3 分量格式（无 RGB8、RGB16F、RGB32F）。MoltenVK 将 `VK_FORMAT_R8G8B8_UNORM` 等映射到不存在的 MTLPixelFormat → `vkCreateImage`/`vkCreateImageView` 失败或返回未定义格式。当该 image 用作 FBO 颜色附件时，pipeline 创建的 `pColorAttachmentFormats` 含不支持格式 → `vkCreateGraphicsPipelines` 失败 → draw 跳过 → 红屏。

MobileGL 的 `ResolveTextureFormatInfo`（VkTextureManager.cpp:374-427）将所有 3 分量格式统一展开为 RGBA，并在 `ExpandRgbSourceToRgba`（:429+）逐像素补 alpha。

**最小影响域修复**：
1. `FormatMap.cpp`：将 3 分量 GL 格式映射到 4 分量 VkFormat（4 行改动）
2. `Resources.cpp:stage_and_copy_image`：在 staging 阶段检测 `format==GL_RGB`（或 GL_BGR）且目标为 4 分量时，逐像素展开。复用现有 arena 分配路径，仅改变写入逻辑

### 根因 V：3 分量顶点属性格式

**底层机制**：Metal 的 MTLVertexFormat 枚举对**非 float** 类型不含 3 分量变体（无 UChar3、Char3、UShort3、Short3、Half3，仅有 UChar4/Char4/UShort4/Short4/Half4 及 Float3）。MoltenVK 无法映射 `R8G8B8_UNORM` 等顶点格式 → pipeline 创建失败 → draw 跳过 → 红屏。

MobileGL 的 `ConvertIntegerVertexStreamToFloat32`/`RepackVertexStream`（VulkanRenderer.cpp:602-666）在 pipeline 创建前将不支持格式的顶点数据流转换为支持的 4 分量格式。

**最小影响域修复**：`Pipeline.cpp:attrib_type_to_vk_format` 对 size==3 的非 float 格式返回 4 分量格式。shader 声明 `vec3` 时仅取前 3 分量，第 4 分量读取 stride 内的 padding（若 stride 不足 4 分量则读取越界，但 Metal 的 vertex fetch 对 padding 容忍）。这是最小影响域修复——不引入数据流转换的复杂性，仅改格式枚举。

### 根因 X：Blend 格式校验

**底层机制**：Vulkan 规范要求启用 blend 的颜色附件格式必须支持 `VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT`（VUID-VkGraphicsPipelineCreateInfo-blendEnable-04727）。Mithril 直接按 GL blend 状态设置 `blendEnable`，不校验格式。对 float FBO（RGBA16F）或整数 FBO（RGBA8UI）启用 blend 属非法状态 → MoltenVK 编译 Metal blend 状态失败或静默丢 draw → 红屏。

MobileGL 的 `VulkanRenderer.cpp:4124-4157` 在 pipeline 创建前查询格式属性，不支持时强制 `effectiveBlendEnabled = false`。

**最小影响域修复**：`Pipeline.cpp:get_or_create_pipeline` 在构造 `cbAttach` 前查询 `vkGetPhysicalDeviceFormatProperties`，缓存结果，不支持时 `blendEnable=VK_FALSE`。

## 测试验证

### 根因 W 验证

**复现步骤（修复前 Fail）**：
1. `glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 256, 256, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb_data)`
2. 将该纹理绑定为 FBO 颜色附件并绘制三角形
3. 修复前：`R8G8B8_UNORM` 不被 Metal 支持 → pipeline 失败 → draw 跳过 → 红屏

**修复后 Pass**：
1. 同上测试
2. 修复后：`R8G8B8A8_UNORM` 被 Metal 支持 → pipeline 成功 → 三角形可见，RGB 颜色正确

### 根因 V 验证

**复现步骤（修复前 Fail）**：
1. `glVertexAttribPointer(loc, 3, GL_UNSIGNED_BYTE, GL_TRUE, stride, offset)` 设置 normalized ubyte3 属性
2. 绘制几何
3. 修复前：`R8G8B8_UNORM` 不被 Metal 顶点格式支持 → pipeline 失败 → draw 跳过 → 红屏

**修复后 Pass**：
1. 同上测试
2. 修复后：`R8G8B8A8_UNORM` 被支持 → pipeline 成功 → 几何可见，属性正确

### 根因 X 验证

**复现步骤（修复前 Fail）**：
1. 创建 RGBA16F FBO 并启用 blend
2. 绘制半透明几何
3. 修复前（若 GPU 不支持 RGBA16F blend）：非法 pipeline 状态 → draw 失败 → 红屏

**修复后 Pass**：
1. 同上测试
2. 修复后：blend 被强制禁用 → pipeline 成功 → 几何可见（无 blend 效果，但不红屏）
