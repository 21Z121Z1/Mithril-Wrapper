# 修复加载界面红屏根因（K/L/M）Spec

## Why

H/I/J 修复（顶点偏移双重应用 / 颜色纹理缺 COLOR_ATTACHMENT_BIT / glClear 提前提交）已推送后，加载界面红屏仍然存在。深度对比 MobileGL + 审计 Mithril 源码后，发现三个被 H/I/J 掩盖或正交的新根因。本 spec 修复这三个经代码验证的真实根因。

## What Changes

- **根因 K（CRITICAL）**：Y 翻转面剔除双重补偿。`Drawing.cpp:238-255` 在渲染默认帧缓冲（FBO 0，即加载界面上屏目标）时，**同时**交换了 cull mode（GL_BACK→VK_FRONT）**和**将 frontFace 硬编码为 CLOCKWISE。这是双重补偿：frontFace=CW 已将 GL-CCW（Y翻转后变 Vulkan-CW）标记为正面，此时再交换 cull mode 为 VK_FRONT 会剔除正面 → 所有正面几何消失，只剩 clear color（红色）。
- **根因 L（HIGH）**：Pipeline 缓存键遗漏顶点属性 `offset` 字段。`Pipeline.cpp:140-147` 的 `hash_signature` 混入了 location/size/type/normalized/integer/stride，但 **未混入 offset**。而 offset 被烘焙进管线（Pipeline.cpp:302 `ad.offset = (uint32_t)a.offset`）。不同 offset 的 VAO 复用同一管线 → 顶点属性从错误字节偏移读取 → 顶点错位/花屏。
- **根因 M（HIGH）**：采样器参数被硬编码为 `GL_LINEAR / GL_REPEAT`，完全忽略纹理真实的 minFilter/magFilter/wrapS/wrapT/wrapR。`DescriptorSet.cpp:455-458` 传给 `backend_get_or_create_sampler` 的是常量，且采样器按纹理名缓存永不重建。Minecraft 像素风纹理（GL_NEAREST）被双线性插值，图集纹理（CLAMP_TO_EDGE）被 REPEAT → 采样到错误纹素 → 颜色偏红/花屏。

## Impact

- 受影响 spec：`fix-red-black-root-causes-hij`（H/I/J，互补）、`fix-red-black-screen`（A-D，Y翻转相关但未涉及 cull 交互）
- 受影响代码：
  - `MG_Impl/Drawing.cpp`（根因 K：移除 cull mode 交换，仅保留 frontFace=CW）
  - `MG_Backend/DirectVulkan/Pipeline.cpp`（根因 L：hash_signature 混入 offset）
  - `MG_Backend/DirectVulkan/DescriptorSet.cpp`（根因 M：从 Texture 结构体读取真实 sampler 参数）

## ADDED Requirements

### Requirement: Y 翻转面剔除单次补偿

系统 SHALL 在 Y 翻转渲染（默认帧缓冲）时仅执行一种面缠绕补偿：设置 `frontFace = VK_FRONT_FACE_CLOCKWISE`，但 **不交换** cull mode（GL_BACK→VK_BACK，GL_FRONT→VK_FRONT）。

#### Scenario: 默认帧缓冲 + 背面剔除
- **WHEN** 应用渲染到 FBO 0，启用 `GL_CULL_FACE`，`cullMode=GL_BACK`，`frontFace=GL_CCW`
- **THEN** Y 翻转使 GL-CCW 三角形在 Vulkan 屏幕空间为 CW 缠绕；frontFace=CW 将其标记为正面；cullMode=VK_BACK 剔除背面（GL-CW）→ GL 正面三角形可见，背面被剔除（与 GL 语义一致）

#### Scenario: 用户 FBO + 背面剔除
- **WHEN** 应用渲染到用户 FBO（无 Y 翻转），启用 `GL_CULL_FACE`，`cullMode=GL_BACK`，`frontFace=GL_CCW`
- **THEN** 行为不变（无 Y 翻转 → 不改 frontFace，不交换 cull mode）

### Requirement: Pipeline 缓存键包含顶点属性 offset

系统 SHALL 将 `MGVertexAttrib.offset` 混入 pipeline 缓存键哈希，确保不同顶点偏移的 VAO 不会复用错误管线。

#### Scenario: 同一 program 的两个 VAO，offset 不同
- **WHEN** 两个 VAO 的 location/size/type/normalized/integer/stride 相同但 offset 不同
- **THEN** 生成两个不同的 pipeline（不同哈希），各自的 `VkVertexInputAttributeDescription.offset` 正确

### Requirement: 采样器参数从纹理状态读取

系统 SHALL 在绑定 combined-image-sampler 时，从 `Texture` 结构体读取真实的 `minFilter/magFilter/wrapS/wrapT/wrapR`，而非硬编码常量。

#### Scenario: GL_NEAREST 像素风纹理
- **WHEN** 应用通过 `glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST)` 设置最近邻过滤
- **THEN** 创建的 VkSampler 使用 `VK_FILTER_NEAREST`，纹素精确采样无插值

#### Scenario: GL_CLAMP_TO_EDGE 图集纹理
- **WHEN** 应用通过 `glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE)` 设置边缘钳位
- **THEN** 创建的 VkSampler 使用 `VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE`，图集边缘不 wrap

## MODIFIED Requirements

### Requirement: prepare_draw 面剔除状态设置

`Drawing.cpp:238-255` 的面剔除逻辑 SHALL 修改为：
```cpp
if (g_state->cullFace) {
    // Y 翻转补偿：仅改 frontFace，不交换 cull mode
    int vk_cull = 0;
    if (g_state->cullMode == GL_FRONT) vk_cull = 1;       // VK_CULL_MODE_FRONT_BIT
    else if (g_state->cullMode == GL_BACK) vk_cull = 2;    // VK_CULL_MODE_BACK_BIT
    else vk_cull = 3;                                       // VK_CULL_MODE_FRONT_AND_BACK
    backend_set_cull_mode(vk_cull);
    // Y 翻转使缠绕反转：GL-CCW → Vulkan-CW。设 frontFace=CW 补偿。
    backend_set_front_face(is_default_fbo ? 0 /*CW*/ :
                           (g_state->frontFace == GL_CCW ? 1 : 0));
}
```

### Requirement: pipeline 缓存键哈希

`Pipeline.cpp:140-147` 的 `hash_signature` SHALL 在循环内添加：
```cpp
mix(&attribs[i].offset, sizeof(attribs[i].offset));
```

### Requirement: 采样器参数绑定

`DescriptorSet.cpp:455-458` SHALL 修改为从纹理状态读取参数：
```cpp
mithril::Texture* tex = mithril::state_get_texture(tex_id);
GLenum minF = tex ? tex->minFilter : GL_NEAREST_MIPMAP_LINEAR;
GLenum magF = tex ? tex->magFilter : GL_LINEAR;
GLenum wrapS = tex ? tex->wrapS : GL_REPEAT;
GLenum wrapT = tex ? tex->wrapT : GL_REPEAT;
GLenum wrapR = tex ? tex->wrapR : GL_REPEAT;
samp = backend_get_or_create_sampler(tex_id, minF, magF, wrapS, wrapT, wrapR, nullptr);
```

## REMOVED Requirements

无。

## 详细设计

### 根因 K：Y 翻转面剔除双重补偿

**底层机制**：Vulkan 的面剔除由两个独立状态控制：
1. `frontFace`：定义哪种缠绕方向是"正面"（CCW 或 CW）
2. `cullMode`：定义剔除哪一面（FRONT、BACK、FRONT_AND_BACK、NONE）

Y 翻转（`gl_Position.y = -gl_Position.y`）会反转三角形缠绕方向：GL-CCW → Vulkan-CW，GL-CW → Vulkan-CCW。

**正确补偿（二选一）**：
- 方案 A：设 `frontFace=CW`（让 GL-CCW→Vulkan-CW 被判定为正面），**不交换** cull mode（GL_BACK→VK_BACK 剔除 Vulkan-背面=GL-背面）
- 方案 B：保持 `frontFace=CCW`（GL-CCW→Vulkan-CW 被判定为背面），**交换** cull mode（GL_BACK→VK_FRONT 剔除 Vulkan-正面=GL-背面）

**当前代码的 bug**：同时执行方案 A 和 B → 双重补偿：
- `frontFace=CW`：GL-CCW→Vulkan-CW="正面"
- `cullMode=GL_BACK→VK_FRONT`：剔除"正面"=GL-CCW=GL-正面
- 结果：GL 正面被剔除，GL 背面可见 → **完全反了**

**为何导致红屏**：Minecraft 加载界面渲染到默认帧缓冲（FBO 0），若启用背面剔除（3D 渲染常见状态），所有正面三角形被剔除，只剩 clear color。Minecraft 加载界面的 clear color 是红色 → 红屏。

**最小影响域修复**：移除 cull mode 交换逻辑，仅保留 frontFace=CW 补偿。一行核心改动（删除 `invert_clockwise` 三元运算），API 契约不变。

### 根因 L：Pipeline 缓存键遗漏 offset

**底层机制**：Pipeline 缓存键是 `uint64_t` 哈希，用于避免重复创建 `VkGraphicsPipeline`。顶点属性状态（location/size/type/normalized/integer/stride）被混入哈希，但 `offset` 被遗漏。而 `offset` 是 `VkVertexInputAttributeDescription` 的成员（Pipeline.cpp:302），被烘焙进管线。

**隐式契约失效**：缓存键隐含契约"哈希相同 → 管线状态相同"。遗漏 offset 违反此契约：两个 offset 不同但其余字段相同的 VAO 共享同一管线，但管线内的 `ad.offset` 是第一个 VAO 的值 → 第二个 VAO 的属性从错误偏移读取。

**为何导致红屏**：Minecraft 使用多个 VAO，不同属性布局的 offset 不同。若两个 VAO 的某属性 offset 分别为 0 和 12，但 location/size/type 相同，第二个 VAO 复用第一个的管线 → 从 offset=0 读取（应为 12）→ 顶点数据错位 → 红屏/花屏。这与根因 H（偏移双重应用）不同——H 修复了 binding offset，但 attribute offset 的缓存键遗漏是独立 bug。

**最小影响域修复**：在 `hash_signature` 循环内添加一行 `mix(&attribs[i].offset, sizeof(attribs[i].offset));`。

### 根因 M：采样器参数硬编码

**底层机制**：GL 纹理对象包含 sampler 状态（minFilter/magFilter/wrapS/wrapT/wrapR），通过 `glTexParameteri` 设置。这些状态应映射到 `VkSamplerCreateInfo` 的对应字段。当前代码在 `DescriptorSet.cpp:455-458` 硬编码为 `GL_LINEAR / GL_REPEAT`，且 `backend_get_or_create_sampler`（Resources.cpp:793-830）按纹理名缓存，首次创建后永不重建。

**状态不一致**：`Texture` 结构体（State.h:227-231）记录了真实的 sampler 参数，但绑定路径从不读取它们。`backend_texture_set_params`（Resources.cpp:760-761）是空操作。应用通过 `glTexParameteri` 设置的参数被完全忽略。

**为何导致红屏**：Minecraft 方块纹理和图集使用 `GL_NEAREST`（像素风）和 `GL_CLAMP_TO_EDGE`（图集边缘）。硬编码 `GL_LINEAR` 导致双线性插值采样到相邻图素的边界纹素；硬编码 `GL_REPEAT` 导致图集边缘 wrap 到对侧纹素。加载界面的 logo/进度条纹理采样到错误颜色 → 偏红/花屏。

**最小影响域修复**：`DescriptorSet.cpp:455-458` 从 `Texture` 结构体读取参数。需注意采样器缓存按纹理名，若 `glTexParameteri` 修改参数后需失效缓存——但当前 `backend_texture_set_params` 是空操作，且 Minecraft 通常在纹理创建后立即设置参数并不再修改，因此首次绑定读取的参数即为最终参数，无需额外缓存失效逻辑。

## 测试验证

### 根因 K 验证

**复现步骤（修复前 Fail）**：
1. 创建 CCW 缠绕的全屏 quad（两个三角形），渲染到默认帧缓冲
2. 启用 `GL_CULL_FACE`，`cullMode=GL_BACK`，`frontFace=GL_CCW`
3. `glClearColor(1, 0, 0, 1)` + `glClear` + 绘制 quad
4. 修复前：quad 是正面（CCW），被双重补偿剔除 → 屏幕全红（仅 clear color）

**修复后 Pass**：
1. 同上测试
2. 修复后：quad 正面可见 → 屏幕显示 quad 颜色（非红色）

### 根因 L 验证

**复现步骤（修复前 Fail）**：
1. 创建 VAO-A：position(float3, offset=0) + color(float4, offset=12)
2. 创建 VAO-B：position(float3, offset=0) + color(float4, offset=28)（不同布局，相同 location/size/type）
3. 先用 VAO-A 绘制，再用 VAO-B 绘制
4. 修复前：VAO-B 复用 VAO-A 的管线，color 从 offset=12 读取（应为 28）→ 颜色错乱

**修复后 Pass**：
1. 同上测试
2. 修复后：VAO-B 使用独立管线，color 从 offset=28 读取 → 颜色正确

### 根因 M 验证

**复现步骤（修复前 Fail）**：
1. 创建 2x2 纹理：左上红色(255,0,0)、右上绿色(0,255,0)、左下蓝色(0,0,255)、右下白色(255,255,255)
2. `glTexParameteri(GL_TEXTURE_MIN_FILTER, GL_NEAREST)` + `glTexParameteri(GL_TEXTURE_MAG_FILTER, GL_NEAREST)`
3. 在 UV=(0.25, 0.25) 处采样（应命中左上红色纹素中心）
4. 修复前：GL_LINEAR 插值 → 采样到四纹素混合色（非纯红）

**修复后 Pass**：
1. 同上测试
2. 修复后：GL_NEAREST 精确采样 → 纯红色
