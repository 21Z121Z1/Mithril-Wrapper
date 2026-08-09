# 加载界面红屏根因（K/L/M）修复检查清单

> 配合 [spec.md](./spec.md) 使用。每项必须实际验证（代码核查 / 编译），不可凭推断打勾。

## 一、根因 K：Y 翻转面剔除单次补偿

- [x] K1. `MG_Impl/Drawing.cpp:238-265` 的 cull mode 交换逻辑已移除（`invert_clockwise` 变量已删除，不再有三元运算 `invert_clockwise ? 2 : 1` 或 `invert_clockwise ? 1 : 2`）。
- [x] K2. cull mode 直接按 GL 值映射：GL_FRONT→VK_CULL_MODE_FRONT_BIT(1)，GL_BACK→VK_CULL_MODE_BACK_BIT(2)，GL_FRONT_AND_BACK→VK_CULL_MODE_FRONT_AND_BACK(3)。
- [x] K3. frontFace 补偿保留：`is_default_fbo` 时 frontFace=CW(0)，否则按 GL frontFace 映射（CCW→1, CW→0）。
- [x] K4. 注释已更新说明：仅 frontFace 补偿，不交换 cull mode（避免双重补偿），含方案A/B对比及机理说明。
- [x] K5. 用户 FBO 路径（is_default_fbo=false）行为不变（无 Y 翻转 → 不改 frontFace，不交换 cull mode）。
- [x] K6. 机理验证：GL cullFace=ON, cullMode=GL_BACK, frontFace=GL_CCW, is_default_fbo=true → frontFace=CW 使 GL-CCW→Vulkan-CW="正面"，cullMode=VK_BACK 剔除 Vulkan-背面=GL-背面 → GL 正面可见（正确）。

## 二、根因 L：Pipeline 缓存键包含 offset

- [x] L1. `MG_Backend/DirectVulkan/Pipeline.cpp:151` 的 `hash_signature` 循环内已添加 `mix(&attribs[i].offset, sizeof(attribs[i].offset));`。
- [x] L2. 添加位置在 stride 之后（line 146 stride → line 147-150 注释 → line 151 offset），不影响现有字段顺序。
- [x] L3. 注释说明 offset 是 VkVertexInputAttributeDescription 的成员（Pipeline.cpp:307 ad.offset），必须参与缓存键。
- [x] L4. 确认 `MGVertexAttrib.offset` 字段存在且被填充（State.h:315 定义，Drawing.cpp 的 `m.offset = (int)(intptr_t)a.pointer` 填充）。
- [x] L5. 机理验证：两个 VAO offset 不同但其余字段相同 → 哈希不同 → 生成两个独立管线。

## 三、根因 M：采样器参数从纹理状态读取

- [x] M1. `MG_Backend/DirectVulkan/DescriptorSet.cpp:464-471` 已从 `mithril::state_get_texture(tex_id)` 读取 minFilter/magFilter/wrapS/wrapT/wrapR。
- [x] M2. 纹理不存在时使用合理默认值（GL_NEAREST_MIPMAP_LINEAR / GL_LINEAR / GL_REPEAT），与 Texture 结构体默认值一致。
- [x] M3. 注释说明采样器参数来源（Texture 结构体）及缓存策略（按纹理名，首次创建后复用）。
- [x] M4. 确认 `Texture` 结构体（State.h:227-231）包含 minFilter/magFilter/wrapS/wrapT/wrapR 字段。
- [x] M5. 确认 `glTexParameteri`（Texture.cpp:282-286）正确写入 Texture 结构体的对应字段。
- [x] M6. 机理验证：GL_NEAREST 纹理 → VkSampler 使用 VK_FILTER_NEAREST；GL_CLAMP_TO_EDGE → VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE。

## 四、编译验证

- [x] N1. Drawing.cpp API 契约验证通过：`backend_set_cull_mode(int)` / `backend_set_front_face(int)` 签名不变，传入值类型兼容。完整编译需 Apple 环境。
- [x] N2. Pipeline.cpp API 契约验证通过：`hash_signature` 内部添加一行 mix 调用，签名和返回类型不变。完整编译需 Apple 环境。
- [x] N3. DescriptorSet.cpp API 契约验证通过：State.h 已在 line 25 直接 include，`mithril::state_get_texture` / `mithril::Texture` 可访问；`backend_get_or_create_sampler` 签名不变（7 参数）。完整编译需 Apple 环境。

> 注：沙箱环境（Linux）无 Vulkan SDK / Metal headers，无法执行 `g++ -fsyntax-only`。已通过 API 契约核查（Backend.h 函数签名 + State.h 字段确认 + include 链验证）替代。完整编译验证需在 Apple 目标环境执行。

## 五、回归检查

- [x] O1. 修复不回滚根因 A-J 的任何改动（Y翻转/Z重映射/blit/semaphore/纹理上传/alpha/顶点偏移/COLOR_ATTACHMENT_BIT/glClear 均保持）。cull 相关仅移除交换逻辑，frontFace 补偿保留。
- [x] O2. `backend_set_cull_mode` 的 API 签名不变（`void backend_set_cull_mode(int)`）。
- [x] O3. `backend_set_front_face` 的 API 签名不变（`void backend_set_front_face(int)`）。
- [x] O4. `backend_get_or_create_sampler` 的 API 签名不变（7 参数：GLuint, GLenum×5, void*）。
- [x] O5. `hash_signature` 的 API 签名不变（参数列表和返回类型 `uint64_t` 均不变）。

## 六、运行验证（需 Apple 目标环境）

> 以下项需在 iOS/macOS + MoltenVK 环境运行验证，linux 沙箱无法执行。

- [ ] P1. 加载界面不再红屏（正面几何不再被双重补偿剔除）。
- [ ] P2. 启用背面剔除的 3D 渲染几何正确（正面可见，背面剔除）。
- [ ] P3. 不同 offset 的 VAO 渲染正确（无顶点错位）。
- [ ] P4. GL_NEAREST 像素风纹理精确采样（无双线性插值模糊）。
- [ ] P5. GL_CLAMP_TO_EDGE 图集纹理边缘不 wrap。
