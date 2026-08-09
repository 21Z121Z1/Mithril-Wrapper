# 红屏/黑屏修复规范（深度参考 MobileGL）

## 一、问题现象

- **主界面红屏**：游戏启动后主界面显示为红色（clear color），无任何几何体渲染。
- **进游戏黑屏**：进入游戏后屏幕全黑，有声音无画面。

## 二、根因定位（深度对比 MobileGL）

通过完整阅读 Mithril 的渲染正确性核心源码（Pipeline.cpp / Shader.cpp / Drawing.cpp / CommandStream.cpp / Device.cpp / Framebuffer.cpp / SwapchainCommon.cpp / egl.cpp / SurfaceMetal.mm / State.h），并深度对比 MobileGL 的 VulkanRenderer.cpp / ProgramFactory.cpp / RenderStateEnumConverter.cpp / ShaderCompiler.cpp，定位到 **3 个精确根因**：

### 根因 A：Y 翻转全局应用（红屏/黑屏主因）

**Mithril 的实现**：
- [Device.cpp:435](file:///workspace/Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/Device.cpp#L435) 设置 `MVK_CONFIG_SHADER_CONVERSION_FLIP_VERTEX_Y=1`
- MoltenVK 在 SPIR-V→MSL 翻译时，对**所有** vertex shader 翻转顶点 Y（`y' = -y`）
- 这是**全局**的，不区分 default framebuffer（FBO 0）和用户创建的 FBO

**MobileGL 的实现**：
- [VulkanRenderer.cpp:2386-2406](file:///workspace/.mobilegl_analysis/VulkanRenderer.cpp#L2386-L2406) `GetShaderTransformFlags()`：
  ```cpp
  ProgramFactory::CompileOptionFlags flags = ProgramFactory::CompileOptionBit::PositionZRemap; // 总是 Z 重映射
  if (currentDrawFBO != nullptr && currentDrawFBO->IsDefaultFramebuffer()) {
      flags |= ProgramFactory::CompileOptionBit::PositionYFlip; // 仅 default framebuffer Y 翻转
  }
  ```
- [ProgramFactory.cpp:789-857](file:///workspace/MobileGL/MobileGL/MG_Backend/DirectVulkan/Renderer/ProgramFactory.cpp#L789-L857) `InsertPositionFixup()`：在 SPIR-V 层面按 transformFlags 做位置变换
- **Y 翻转仅在 default framebuffer（FBO 0）时应用**

**为什么用户 FBO 不能 Y 翻转**：
- GL 的纹理坐标系：原点在左下角，Y 朝上
- Vulkan/Metal 的纹理坐标系：原点在左上角，Y 朝下
- 用户 FBO 渲染的纹理后续会被 GL 着色器采样，GL 着色器用的是 GL 纹理坐标（Y 朝上）
- 如果用户 FBO 渲染时翻转 Y，纹理内容变成 Vulkan 风格（Y 朝下），GL 着色器采样时坐标系不匹配 → 采样到错误的像素 → 红屏/黑屏
- default framebuffer 的图像直接呈现到屏幕，屏幕是 Vulkan 风格（Y 朝下），所以需要 Y 翻转

**Mithril 的后果**：
- MoltenVK 全局翻转 Y，用户 FBO 的纹理内容上下颠倒
- Minecraft Java 大量使用 FBO（光效、水面、后处理、GUI 等），这些 FBO 纹理被采样时全部错位
- 主界面红屏（GUI FBO 错误）、进游戏黑屏（场景 FBO 错误）

### 根因 B：Y 翻转后 frontFace 未调整（黑屏）

**Mithril 的实现**：
- [Drawing.cpp:182-190](file:///workspace/Mithril-Wrapper-cpp/MG_Impl/Drawing.cpp#L182-L190)：
  ```cpp
  if (g_state->cullFace) {
      ...
      backend_set_front_face(g_state->frontFace == GL_CCW ? 1 : 0);
  }
  ```
- 直接使用 GL 的 frontFace（CCW=1, CW=0），**不根据 Y 翻转调整**
- [CommandStream.cpp:1223-1227](file:///workspace/Mithril-Wrapper-cpp/MG_Backend/DirectVulkan/CommandStream.cpp#L1223-L1227)：
  ```cpp
  void backend_set_front_face(int ccw) {
      vkCmdSetFrontFace(b->commandBuffer, ccw ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE);
  }
  ```

**MobileGL 的实现**：
- [VulkanRenderer.cpp:3707](file:///workspace/.mobilegl_analysis/VulkanRenderer.cpp#L3707)：
  ```cpp
  Bool invertClockwise = transformFlags & ProgramFactory::CompileOptionBit::PositionYFlip;
  ```
- [VulkanRenderer.cpp:3889-3893](file:///workspace/.mobilegl_analysis/VulkanRenderer.cpp#L3889-L3893)：
  ```cpp
  .cullMode = cullFaceEnabled
      ? MG_Util::ConvertCullFaceModeToVkEnum(MG_State::pGLContext->GetCullFaceMode(), invertClockwise)
      : VK_CULL_MODE_NONE,
  .frontFace = VK_FRONT_FACE_CLOCKWISE,  // 硬编码 CLOCKWISE
  ```
- [RenderStateEnumConverter.cpp:48-62](file:///workspace/MobileGL/MobileGL/MG_Util/Converters/MGToVk/RenderStateEnumConverter.cpp#L48-L62)：
  ```cpp
  VkCullModeFlags ConvertCullFaceModeToVkEnum(CullFaceMode v, Bool invertClockwise) {
      switch (v) {
      case CullFaceMode::Front:
          return invertClockwise ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_FRONT_BIT;
      case CullFaceMode::Back:
          return invertClockwise ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT;
      ...
      }
  }
  ```

**为什么需要调整 frontFace**：
- Y 翻转（`y' = -y`）会让顶点的 Y 坐标取反，三角形绕序反转（CCW → CW，CW → CCW）
- GL 的 frontFace 默认是 CCW（逆时针为正面）
- Y 翻转后，GL 的 CCW 三角形变成 Vulkan 的 CW 三角形
- 如果 Vulkan frontFace 仍设为 CCW，GL 的 CCW 三角形（现在是 CW）会被当作背面
- 启用 `GL_CULL_FACE` + `GL_BACK` 时，CCW 三角形被错误剔除 → 黑屏

**Mithril 的后果**：
- Minecraft Java 的渲染管线启用 `GL_CULL_FACE`（剔除背面）
- Y 翻转后绕序反转，但 frontFace 仍为 CCW
- 所有 CCW 正面三角形被当作背面剔除 → 几何体消失 → 黑屏

### 根因 C：Z 重映射缺失（深度测试错误 / 近平面裁剪错误）

**Mithril 的实现**：
- 完全没有 Z 重映射（grep `ZRemap|z_remap|0.5f.*z|PositionZ` 无任何匹配）
- [Shader.cpp](file:///workspace/Mithril-Wrapper-cpp/MG_Impl/Shader.cpp) 的 GLSL→SPIR-V 翻译不做任何位置变换
- [Drawing.cpp:163-165](file:///workspace/Mithril-Wrapper-cpp/MG_Impl/Drawing.cpp#L163-L165) 视口直接使用 GL 的 depthNear/depthFar（默认 0.0/1.0）

**MobileGL 的实现**：
- [VulkanRenderer.cpp:2387](file:///workspace/.mobilegl_analysis/VulkanRenderer.cpp#L2387)：
  ```cpp
  ProgramFactory::CompileOptionFlags flags = ProgramFactory::CompileOptionBit::PositionZRemap; // 总是 Z 重映射
  ```
- [ProgramFactory.cpp:844-851](file:///workspace/MobileGL/MobileGL/MG_Backend/DirectVulkan/Renderer/ProgramFactory.cpp#L844-L851)：
  ```cpp
  if (doZRemap) {
      auto* zPlusW = builder.AddBinaryOp(target.floatTypeId, spv::Op::OpFAdd, z->result_id(), w->result_id());
      auto* mappedZ = builder.AddBinaryOp(target.floatTypeId, spv::Op::OpFMul, zPlusW->result_id(), halfConstId);
      zValueId = mappedZ->result_id();
  }
  ```
- Z 重映射公式：`z' = (z + w) * 0.5`
- **总是应用**（无论 default framebuffer 还是用户 FBO）

**为什么需要 Z 重映射**：
- GL NDC Z 范围：[-1, 1]（-1 是近平面，1 是远平面）
- Vulkan NDC Z 范围：[0, 1]（0 是近平面，1 是远平面）
- GLSL 着色器输出的 `gl_Position.z` 是 GL NDC 风格（[-1, 1]）
- Vulkan 的裁剪阶段要求 NDC Z ∈ [0, w]，即 z/w ∈ [0, 1]
- 如果 GL 着色器输出 z/w = -1（GL 近平面），Vulkan 认为超出近平面（< 0）→ **被裁剪掉**
- 即使没被裁剪，深度测试也比较错误的 Z 值

**为什么不能用视口 minDepth/maxDepth 替代**：
- Vulkan 视口变换：`depthBufferZ = ndcZ * (maxDepth - minDepth) + minDepth`
- 但 Vulkan 的**裁剪阶段**在视口变换之前，要求 NDC Z ∈ [0, 1]
- GL 着色器输出的 NDC Z = -1 在裁剪阶段就被剔除，根本到不了视口变换
- 所以 Z 重映射必须在**着色器层面**（裁剪之前）做，不能用视口参数替代

**Mithril 的后果**：
- GL 近平面附近的几何体（z/w 接近 -1）被 Vulkan 裁剪掉 → 几何体缺失
- 深度测试比较错误的 Z 值 → 渲染顺序错误、z-fighting
- 配合 Y 翻转错误，加剧黑屏

## 三、坐标系差异全景（GL → Vulkan → Metal）

```
坐标系            GL              Vulkan          Metal
─────────────────────────────────────────────────────────
NDC X             [-1, 1] 右      [-1, 1] 右      [-1, 1] 右
NDC Y             [-1, 1] 上      [-1, 1] 上      [-1, 1] 上
NDC Z             [-1, 1]         [0, 1]          [0, 1]
Framebuffer 原点   左下            左上            左上
Framebuffer Y     朝上            朝下            朝下
纹理原点          左下            左上            左上
纹理 Y            朝上            朝下            朝下
```

**需要的转换**（GL → Vulkan）：
1. **NDC Z 重映射**：[-1, 1] → [0, 1]，即 `z' = (z + w) * 0.5`（在着色器输出前）
2. **Framebuffer Y 翻转**：GL Y 朝上 → Vulkan Y 朝下，翻转顶点 Y `y' = -y`（仅 default framebuffer，因为用户 FBO 纹理保持 GL 风格供后续采样）
3. **frontFace 调整**：Y 翻转导致绕序反转，frontFace 需相应反转

**MoltenVK 的角色**：
- MoltenVK 翻译 Vulkan → Metal，Vulkan 和 Metal 的坐标系一致（都 Y 朝下），不需要额外翻转
- `MVK_CONFIG_SHADER_CONVERSION_FLIP_VERTEX_Y` 是 MoltenVK 提供的便利选项，在 SPIR-V→MSL 时翻转 Y，但它是**全局**的，无法按 framebuffer 区分

## 四、修复方案（完整参考 MobileGL）

### 4.1 总体策略

放弃 MoltenVK 全局 Y 翻转，改为在**着色器编译层面**按需做位置变换（Y 翻转 + Z 重映射），完全对标 MobileGL 的 `GetShaderTransformFlags` + `InsertPositionFixup` 机制。

### 4.2 根因 A 修复：按 framebuffer 区分 Y 翻转

**文件**：`MG_Impl/Shader.cpp`、`MG_State/State.h`、`MG_Impl/Program.cpp`

**方案**：为每个 vertex shader 程序编译**两个 SPIR-V 版本**：
- `vertexSpirv`：Z 重映射，**不翻转 Y**（用于用户 FBO）
- `vertexSpirvYFlipped`：Z 重映射 **+ 翻转 Y**（用于 default framebuffer）

**Program 结构扩展**（State.h）：
```cpp
struct Program {
    ...
    std::vector<uint32_t> vertexSpirv;           // 用户 FBO 用（Z 重映射，不翻转 Y）
    std::vector<uint32_t> vertexSpirvYFlipped;   // default framebuffer 用（Z 重映射 + Y 翻转）
    std::vector<uint32_t> fragmentSpirv;
};
```

**Shader.cpp 修改**：
- `shader_translate` / `glsl_to_spirv` 增加 `bool flip_y` 参数
- `flip_y=true` 时调用 `shader.setInvertY(true)`（glslang 内置功能，在 SPIR-V 中插入 OpFNegate 翻转 Position Y）
- Z 重映射通过 GLSL 源码注入（见 4.4）

**Program.cpp 修改**（glLinkProgram）：
- 链接时编译 vertex shader 两次：
  - `shader_translate(GL_VERTEX_SHADER, src, vertexSpirv, ..., /*flip_y=*/false)`
  - `shader_translate(GL_VERTEX_SHADER, src, vertexSpirvYFlipped, ..., /*flip_y=*/true)`
- fragment shader 只编译一次（不涉及位置变换）

### 4.3 根因 B 修复：frontFace / cullMode 根据 Y 翻转调整

**文件**：`MG_Impl/Drawing.cpp`、`MG_Backend/DirectVulkan/Pipeline.cpp`、`MG_Backend/DirectVulkan/CommandStream.cpp`

**方案**：完全对标 MobileGL 的 `invertClockwise` 机制。

**Drawing.cpp 修改**（prepare_draw）：
```cpp
// 判断是否为 default framebuffer（需要 Y 翻转）
bool is_default_fbo = (g_state->currentDrawFBO == 0);
bool invert_clockwise = is_default_fbo;  // Y 翻转时反转绕序

// 选择对应版本的 SPIR-V
const uint32_t* vs_spirv = is_default_fbo
    ? prog->vertexSpirvYFlipped.data()
    : prog->vertexSpirv.data();
int vs_word_count = is_default_fbo
    ? (int)prog->vertexSpirvYFlipped.size()
    : (int)prog->vertexSpirv.size();

// pipeline 创建传入 is_default_fbo 作为签名的一部分
VkPipeline pipeline = backend_get_or_create_pipeline(
    prog->id,
    vs_spirv, vs_word_count,
    ...);

// frontFace / cullMode 调整（对标 MobileGL ConvertCullFaceModeToVkEnum）
if (g_state->cullFace) {
    // Y 翻转时，cullMode 反转：GL_BACK → VK_FRONT, GL_FRONT → VK_BACK
    int vk_cull = 0;
    if (g_state->cullMode == GL_FRONT) {
        vk_cull = invert_clockwise ? 2 /*VK_BACK*/ : 1 /*VK_FRONT*/;
    } else if (g_state->cullMode == GL_BACK) {
        vk_cull = invert_clockwise ? 1 /*VK_FRONT*/ : 2 /*VK_BACK*/;
    } else { // GL_FRONT_AND_BACK
        vk_cull = 3;
    }
    backend_set_cull_mode(vk_cull);
    // frontFace 硬编码 CLOCKWISE（对标 MobileGL .frontFace = VK_FRONT_FACE_CLOCKWISE）
    backend_set_front_face(0);  // 0 = CLOCKWISE
} else {
    backend_set_cull_mode(0);  // VK_CULL_MODE_NONE
}
```

**Pipeline.cpp 修改**：
- `get_or_create_pipeline` 的签名哈希增加 `is_default_fbo` 字段（因为不同 framebuffer 用不同 SPIR-V，pipeline 不同）
- 实际上 SPIR-V 内容不同已经会让 program 不同（但这里 program 相同，只是 SPIR-V 版本不同），所以需要额外字段区分

### 4.4 根因 C 修复：Z 重映射（GLSL 源码注入）

**文件**：`MG_Impl/Shader.cpp`

**方案**：由于 Mithril 没有依赖 SPIRV-Tools（无法像 MobileGL 那样做 SPIR-V 后处理 pass），采用 **GLSL 源码注入**方式实现 Z 重映射。

**注入策略**：对 vertex shader 源码，将 `void main()` 重命名为 `void _mithril_original_main()`，然后在文件末尾追加包装的 main：
```glsl
void main() {
    _mithril_original_main();
    gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;
}
```

**Shader.cpp 实现**（新增函数 `inject_z_remap`）：
```cpp
// 仅对 vertex shader 注入 Z 重映射
// 将 "void main(" 重命名为 "void _mithril_original_main("
// 在源码末尾追加包装 main
void inject_z_remap(std::string& src, GLenum gl_stage) {
    if (gl_stage != GL_VERTEX_SHADER) return;
    // 跳过注释中的 "void main"
    // 用正则匹配 "void main(" (允许任意空白)
    static const std::regex main_re(R"(\bvoid\s+main\s*\()");
    if (!std::regex_search(src, main_re)) return;
    src = std::regex_replace(src, main_re, "void _mithril_original_main(");
    src += "\nvoid main() {\n    _mithril_original_main();\n"
           "    gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;\n}\n";
}
```

**调用时机**：在 `glsl_to_spirv` 中，`ensure_glsl_version` 之后、`wrap_loose_uniforms` 之前调用 `inject_z_remap`。

**注意事项**：
- Minecraft Java 的 vertex shader 都有标准的 `void main()`，注入安全
- 如果 shader 没有 `gl_Position` 赋值（极少见），注入的代码会编译失败 → glslang 报错 → 走 fallback 路径
- Z 重映射对 geometry/tessellation shader 不注入（MobileGL 在 SPIR-V 后处理中对这些 stage 也做，但 MC Java 不用这些 stage）

### 4.5 移除 MoltenVK 全局 Y 翻转

**文件**：`MG_Backend/DirectVulkan/Device.cpp`

**修改**：
```cpp
// 移除这行（或改为 0）：
// setenv("MVK_CONFIG_SHADER_CONVERSION_FLIP_VERTEX_Y", "1", 1);
setenv("MVK_CONFIG_SHADER_CONVERSION_FLIP_VERTEX_Y", "0", 1);
```

**注释更新**：说明 Y 翻转已改为在着色器编译层面按需处理（对标 MobileGL），不再依赖 MoltenVK 全局翻转。

### 4.6 视口 Y 处理（保持不变）

**文件**：`MG_Backend/DirectVulkan/CommandStream.cpp`

**保持现状**：`backend_set_viewport` 直接使用 GL 视口坐标，不翻转 Y。

**理由**（对标 MobileGL `ApplyGLViewportState` VulkanRenderer.cpp:220-249）：
- MobileGL 的视口也是直接用 GL 坐标，不翻转 Y
- Y 翻转通过着色器层面（PositionYFlip）处理，视口不翻转
- Mithril 修复后也是着色器层面翻转，视口保持不变

### 4.7 glBlitFramebuffer Y 处理（需修复：blit 到 default framebuffer 时翻转目标 Y）

**文件**：`MG_Impl/Framebuffer.cpp`、`MG_Backend/DirectVulkan/ImageOps.cpp`、`MG_Backend/Backend.h`

**问题**：draw 路径的 Y 翻转已从 MoltenVK 全局开关（`MVK_CONFIG_SHADER_CONVERSION_FLIP_VERTEX_Y`）移至着色器层面（仅 default FBO 翻转）。但 blit 操作（`vkCmdBlitImage`）不经过 vertex shader，MoltenVK 对 blit 不做 Y 翻转。因此 blit 到 default framebuffer 时，GL 底左坐标系的目标 Y 坐标直接传给 Vulkan 顶左坐标系，导致画面上下颠倒 → 黑屏/错位。

**MobileGL 的实现**（深度参考）：
- [VulkanRenderer.cpp:5828-5830](file:///workspace/.mobilegl_analysis/VulkanRenderer.cpp#L5828-L5830)：blit 到 default framebuffer 时调用 `ApplyNativeBlitDefaultFramebufferTransform`
- [VulkanRenderer.cpp:1650-1665](file:///workspace/.mobilegl_analysis/VulkanRenderer.cpp#L1650-L1665)：identity 变换下翻转目标 Y：
  ```cpp
  case VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR:
      blitRegion.dstOffsets[0].y = dstBinding.extent.y() - blitRegion.dstOffsets[0].y;
      blitRegion.dstOffsets[1].y = dstBinding.extent.y() - blitRegion.dstOffsets[1].y;
      break;
  ```
- 源 Y 永不翻转（源内容的方向已由 draw 路径决定，GL 坐标直接读即可）
- 用户 FBO 目标永不翻转（用户 FBO 内容保持 GL 方向供后续采样）

**坐标系分析**（为什么需要翻转目标 Y）：
- default framebuffer 内容方向（draw 路径已翻转 Y）：Vulkan Y=0（顶部）= GL 顶部内容，Vulkan Y=H（底部）= GL 底部内容
- GL blit 目标坐标：底左原点，GL dstY=0 表示底部
- 要将 GL 底部内容写入 default framebuffer 的 Vulkan 底部（Y=H），需要：`vulkanDstY = H - glDstY`
- 用户 FBO 内容方向（draw 路径未翻转）：Vulkan Y=0（顶部）= GL 底部内容，Vulkan Y=H（底部）= GL 顶部内容
- GL blit 目标坐标直接对应 Vulkan 坐标，无需翻转

**修复方案**：
1. `glBlitFramebuffer`（Framebuffer.cpp）计算 `is_dst_default_fbo = (currentDrawFBO == 0)` 和目标帧缓冲高度 `dst_height`
2. 将 `is_dst_default_fbo` 和 `dst_height` 传递给 `backend_blit_images`
3. `blit_images_impl`（ImageOps.cpp）在 `is_dst_default_fbo=true` 时翻转目标 Y：
   ```cpp
   if (is_dst_default_fbo && dst_height > 0) {
       dstY0 = dst_height - dstY0;
       dstY1 = dst_height - dstY1;
   }
   ```
4. 源 Y 不翻转（对标 MobileGL）

**不改动项**：
- 源 Y 坐标不翻转（MobileGL 也不翻转源 Y；Minecraft Java 的典型 blit 路径是用户 FBO → default FBO，源是用户 FBO 无需翻转）
- 视口 Y、scissor Y 不翻转（对标 MobileGL，4.6 节已说明）

## 五、实现细节

### 5.1 修改文件清单

| 文件 | 修改内容 |
|------|----------|
| `MG_State/State.h` | Program 结构增加 `vertexSpirvYFlipped` 字段 |
| `MG_Impl/Shader.cpp` | 增加 `flip_y` 参数；增加 `inject_z_remap` 函数 |
| `MG_Impl/Program.cpp` | glLinkProgram 编译两个 vertex SPIR-V 版本 |
| `MG_Impl/Drawing.cpp` | prepare_draw 判断 default FBO，选择 SPIR-V 版本，调整 frontFace/cullMode |
| `MG_Backend/DirectVulkan/Pipeline.cpp` | pipeline 签名增加 is_default_fbo；调整 frontFace 默认值 |
| `MG_Backend/DirectVulkan/Device.cpp` | 移除/关闭 MVK_CONFIG_SHADER_CONVERSION_FLIP_VERTEX_Y |
| `MG_Impl/Framebuffer.cpp` | glBlitFramebuffer 计算 is_dst_default_fbo + dst_height，传给 backend_blit_images |
| `MG_Backend/DirectVulkan/ImageOps.cpp` | blit_images_impl / backend_blit_images 增加 is_dst_default_fbo + dst_height 参数，翻转目标 Y |
| `MG_Backend/Backend.h` | backend_blit_images 声明增加 is_dst_default_fbo + dst_height 参数 |

### 5.2 数据流

```
glLinkProgram
  ├─ shader_translate(vertex, flip_y=false) → vertexSpirv        (Z 重映射，用户 FBO)
  ├─ shader_translate(vertex, flip_y=true)  → vertexSpirvYFlipped (Z 重映射 + Y 翻转，default FBO)
  └─ shader_translate(fragment)             → fragmentSpirv

glDrawArrays / glDrawElements
  ├─ prepare_draw
  │   ├─ is_default_fbo = (currentDrawFBO == 0)
  │   ├─ 选择 vs_spirv = is_default_fbo ? vertexSpirvYFlipped : vertexSpirv
  │   ├─ get_or_create_pipeline(..., is_default_fbo, ...)
  │   ├─ invert_clockwise = is_default_fbo
  │   ├─ cullMode: invert_clockwise ? 反转 : 不反转
  │   └─ frontFace: CLOCKWISE (硬编码，对标 MobileGL)
  └─ backend_draw_*
```

### 5.3 Shader 缓存键

`shader_translate` 的缓存键需要包含 `flip_y`，避免翻转/不翻转版本缓存冲突：
```cpp
uint64_t key = fnv1a(glsl_source) ^ (uint64_t)gl_stage * 0x9E3779B97F4A7C15ULL;
if (flip_y) key ^= 0xABCD1234567890ABULL;  // 翻转版本用不同 key
```

### 5.4 Pipeline 签名

`hash_signature` 需要增加 `is_default_fbo` 字段，因为同一个 program 在 default FBO 和用户 FBO 下用不同 SPIR-V，pipeline 不同：
```cpp
mix(&is_default_fbo, sizeof(is_default_fbo));
```

## 六、风险与回退

### 6.1 风险

1. **GLSL Z 重映射注入可能失败**：如果 vertex shader 没有标准 `void main()`（极少见），注入会失败。缓解：注入失败时不注入，走原路径（深度测试可能错误，但不会崩溃）。

2. **双 SPIR-V 版本增加内存**：每个 program 多存一份 vertex SPIR-V（通常几 KB）。MC Java 有 ~50 个 program，增加 ~几百 KB，可忽略。

3. **frontFace 硬编码 CLOCKWISE 的兼容性**：如果某些几何体的绕序与 MC 标准不同，可能剔除错误。缓解：完全对标 MobileGL，MobileGL 在 MC 上验证过。

4. **glBlitFramebuffer Y 方向**：本次修复增加了 blit 到 default framebuffer 时的目标 Y 翻转（对标 MobileGL `ApplyNativeBlitDefaultFramebufferTransform`）。源 Y 不翻转（对标 MobileGL）。如果 default FBO → 用户 FBO 的 blit 出现上下颠倒，可能需要额外翻转源 Y（MobileGL 也未处理此罕见路径）。

### 6.2 回退方案

如果修复后问题恶化，可回退到 MoltenVK 全局 Y 翻转：
1. 恢复 `MVK_CONFIG_SHADER_CONVERSION_FLIP_VERTEX_Y=1`
2. 恢复 frontFace 直接使用 GL 值
3. 移除 Z 重映射注入
4. 移除双 SPIR-V 版本

回退后状态与修复前一致（红屏/黑屏仍存在，但不会更糟）。

## 七、验证标准

- [ ] 主界面正常显示（无红屏）
- [ ] 进入游戏后画面正常（无黑屏，有画面有声音）
- [ ] GUI 渲染正确（按钮、文字、物品栏位置正确）
- [ ] 光效渲染正确（动态光影、水面反射）
- [ ] 深度测试正确（物体遮挡关系正确，无 z-fighting）
- [ ] 启用 GL_CULL_FACE 时几何体不被错误剔除
- [ ] 用户 FBO 纹理采样正确（无上下颠倒）
- [ ] default framebuffer 图像方向正确（无上下颠倒）
- [ ] 长时间运行（>30分钟）无回归
- [ ] 无新增崩溃（SIGBUS/SIGSEGV）
