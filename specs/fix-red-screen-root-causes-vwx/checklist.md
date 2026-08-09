# 加载界面红屏根因（V/W/X）修复检查清单

> 配合 [spec.md](./spec.md) 使用。每项必须实际验证（代码核查 / 编译），不可凭推断打勾。

## 一、根因 W：RGB 纹理格式映射到 4 分量

- [x] W1. `MG_Backend/DirectVulkan/FormatMap.cpp` 的 `GL_RGB8` 映射改为 `VK_FORMAT_R8G8B8A8_UNORM`（原 R8G8B8_UNORM）。
- [x] W2. `GL_RGB`（unsized）映射改为 `VK_FORMAT_R8G8B8A8_UNORM`（原 R8G8B8_UNORM）。
- [x] W3. `GL_RGB16F` 映射改为 `VK_FORMAT_R16G16B16A16_SFLOAT`（原 R16G16B16_SFLOAT）。
- [x] W4. `GL_RGB32F` 映射改为 `VK_FORMAT_R32G32B32A32_SFLOAT`（原 R32G32B32_SFLOAT）。
- [x] W5. `Resources.cpp:stage_and_copy_image` 在源 format==GL_RGB（或 GL_BGR）且目标为 4 分量 VkFormat 时，逐像素展开 RGB→RGBA（alpha=0xFF）。
- [x] W6. 展开逻辑正确处理 unpack_alignment（展开后的行 stride 仍按 4 分量 bpp 对齐）。
- [x] W7. 改动处有注释说明 Metal 无 3 分量像素格式，需展开为 RGBA，对标 MobileGL ResolveTextureFormatInfo + ExpandRgbSourceToRgba。
- [x] W8. 确认 RGBA8/RGBA16F/RGBA32F 等 4 分量格式不受影响（不触发展开）。

## 二、根因 V：3 分量顶点属性格式映射

- [x] V1. `Pipeline.cpp:attrib_type_to_vk_format` 对 GL_UNSIGNED_BYTE normalized size==3 返回 `R8G8B8A8_UNORM`（原 R8G8B8_UNORM）。
- [x] V2. GL_BYTE normalized size==3 返回 `R8G8B8A8_SNORM`（原 R8G8B8_SNORM）。
- [x] V3. GL_UNSIGNED_SHORT normalized size==3 返回 `R16G16B16A16_UNORM`（原 R16G16B16_UNORM）。
- [x] V4. GL_SHORT normalized size==3 返回 `R16G16B16A16_SNORM`（原 R16G16B16_SNORM）。
- [x] V5. GL_HALF_FLOAT size==3 返回 `R16G16B16A16_SFLOAT`（原 R16G16B16_SFLOAT）。
- [x] V6. integer 变体（GL_UNSIGNED_BYTE/GL_BYTE/GL_UNSIGNED_SHORT/GL_SHORT integer size==3）返回对应 4 分量格式。
- [x] V7. GL_FLOAT size==3 保持 `R32G32B32_SFLOAT`（Metal 支持 Float3，不转换）。
- [x] V8. GL_INT/GL_UNSIGNED_INT size==3 保持 `R32G32B32_SINT`/`_UINT`（Metal 支持，不转换）。
- [x] V9. 改动处有注释说明 Metal 顶点格式限制（无 UChar3/Char3/UShort3/Short3/Half3）。

## 三、根因 X：Pipeline blend 格式校验

- [x] X1. `Pipeline.cpp:get_or_create_pipeline` 在设置 `cbAttach.blendEnable` 前查询颜色附件格式的 `optimalTilingFeatures`。
- [x] X2. 查询结果被缓存（static map 或成员变量），避免重复调用 `vkGetPhysicalDeviceFormatProperties`。
- [x] X3. 格式不支持 `VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT` 时，`blendEnable` 强制为 `VK_FALSE`。
- [x] X4. 强制禁用 blend 时记录警告日志（限流，避免刷屏）。
- [x] X5. 改动处有注释说明 Vulkan VUID-vkCmdBeginRendering-blendEnable-04727 及 MobileGL VulkanRenderer.cpp:4124-4157 对照。
- [x] X6. 确认 RGBA8 等常见格式不受影响（支持 COLOR_ATTACHMENT_BLEND_BIT）。

## 四、编译验证

- [x] Y1. FormatMap.cpp API 契约验证通过（gl_internal_to_vk 签名不变，仅返回值改变）。完整编译需 Apple 环境。
- [x] Y2. Resources.cpp API 契约验证通过（stage_and_copy_image 签名不变，内部增加展开逻辑）。完整编译需 Apple 环境。
- [x] Y3. Pipeline.cpp API 契约验证通过（attrib_type_to_vk_format / get_or_create_pipeline 签名不变）。完整编译需 Apple 环境。

## 五、回归检查

- [x] Z1. 修复不回滚根因 A-M 的任何改动。
- [x] Z2. `gl_internal_to_vk` 的 API 签名不变。
- [x] Z3. `stage_and_copy_image` 的 API 签名不变。
- [x] Z4. `attrib_type_to_vk_format` 的 API 签名不变。
- [x] Z5. `get_or_create_pipeline` 的 API 签名不变。

## 六、运行验证（需 Apple 目标环境）

> 以下项需在 iOS/macOS + MoltenVK 环境运行验证，linux 沙箱无法执行。

- [ ] AA1. GL_RGB8 纹理创建成功（vkCreateImage 不返回错误）。
- [ ] AA2. GL_RGB8 纹理采样返回正确 RGB 颜色（alpha=1.0）。
- [ ] AA3. GL_RGB8 纹理用作 FBO 颜色附件时 pipeline 创建成功。
- [ ] AA4. size==3 的 GL_UNSIGNED_BYTE normalized 顶点属性 pipeline 创建成功。
- [ ] AA5. 加载界面不再红屏（RGB 纹理 + 3 分量顶点属性 + blend 校验全部修复后）。
