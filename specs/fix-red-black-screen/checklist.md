# 红屏/黑屏修复检查清单

> 配合 [spec.md](./spec.md) 使用。每项必须实际验证（代码核查 / 编译 / 运行截图），不可凭推断打勾。

## 一、根因修复验证（对应 spec 第二章三个根因）

### 根因 A：Y 翻转按 framebuffer 区分

- [x] A1. `MG_State/State.h` 的 `Program` 结构已新增 `std::vector<uint32_t> vertexSpirvYFlipped` 字段，且字段注释说明用途（default framebuffer 用，Z 重映射 + Y 翻转）。
- [x] A2. `MG_Impl/Shader.cpp` 的 `shader_translate` / `glsl_to_spirv` 函数签名已新增 `bool flip_y = false` 参数。
- [x] A3. `flip_y=true` 时实际触发了 Y 翻转。glslang 无 `setInvertY` API，故采用等价的 GLSL 源码注入：在 `inject_position_fixup` 中追加 `gl_Position.y = -gl_Position.y;`（Shader.cpp 注入逻辑，两份 SPIR-V 差异即此处）。
- [x] A4. `MG_Impl/Program.cpp` 的 `glLinkProgram` 对同一份 vertex shader 源码编译了**两次**，分别产出 `vertexSpirv`（flip_y=false）和 `vertexSpirvYFlipped`（flip_y=true）。
- [x] A5. fragment shader 只编译一次，未受 flip_y 影响（`shader_translate` 对 fragment 不注入 Y 翻转）。
- [x] A6. shader 缓存键包含 flip_y 标志位：`if (flip_y) key ^= 0xABCD1234567890ABULL;`（Shader.cpp），翻转/不翻转版本不会缓存冲突。
- [x] A7. `MG_Backend/DirectVulkan/Device.cpp` 已将 `MVK_CONFIG_SHADER_CONVERSION_FLIP_VERTEX_Y` 设置为 `"0"`，全局 Y 翻转已关闭。
- [x] A8. grep 确认全工程无其他地方重新开启 `MVK_CONFIG_SHADER_CONVERSION_FLIP_VERTEX_Y=1`（仅 Device.cpp:441 一处，值为 "0"；libMoltenVK.dylib 二进制不计）。

### 根因 B：frontFace / cullMode 根据 Y 翻转调整

- [x] B1. `MG_Impl/Drawing.cpp` 的 `prepare_draw` 计算 `bool is_default_fbo = (g_state->currentDrawFBO == 0)` 和 `bool invert_clockwise = is_default_fbo`。
- [x] B2. `prepare_draw` 根据 `is_default_fbo` 选择 SPIR-V 版本：default FBO 用 `vertexSpirvYFlipped`，用户 FBO 用 `vertexSpirv`。
- [x] B3. cullMode 调整逻辑对标 MobileGL `ConvertCullFaceModeToVkEnum`：
  - `GL_FRONT` + invert → `VK_CULL_MODE_BACK_BIT` (vk_cull=2)
  - `GL_BACK` + invert → `VK_CULL_MODE_FRONT_BIT` (vk_cull=1)
  - `GL_FRONT` + no invert → `VK_CULL_MODE_FRONT_BIT` (vk_cull=1)
  - `GL_BACK` + no invert → `VK_CULL_MODE_BACK_BIT` (vk_cull=2)
  - `GL_FRONT_AND_BACK` → `VK_CULL_MODE_FRONT_AND_BACK` (vk_cull=3，不随 invert 变化)
- [x] B4. 启用 cullFace 且 Y 翻转时 `frontFace` 硬编码为 `VK_FRONT_FACE_CLOCKWISE`（`backend_set_front_face(0)`）；用户 FBO（不翻转）保留 GL 的 CCW/CW 设置。
- [x] B5. 未启用 cullFace 时 cullMode 设为 `VK_CULL_MODE_NONE`（`backend_set_cull_mode(0)`）。
- [x] B6. `backend_set_front_face(0)` 的 0 在 CommandStream.cpp:1227 映射为 `VK_FRONT_FACE_CLOCKWISE`（`ccw ? CCW : CW`，0→CW, 1→CCW）。
- [x] B7. `MG_Backend/DirectVulkan/Pipeline.cpp` 的 pipeline 签名哈希已纳入 `is_default_fbo`（`hash_signature` 末尾 `mix(&is_default_fbo, ...)`），同一 program 在不同 FBO 下产生不同 pipeline。

### 根因 C：Z 重映射

- [x] C1. `MG_Impl/Shader.cpp` 新增 `inject_position_fixup(std::string& src, GLenum gl_stage, bool flip_y)` 函数（合并 Z 重映射 + Y 翻转，等价于 checklist 中的 `inject_z_remap`，命名更准确因其同时处理两种 position 变换）。
- [x] C2. `inject_position_fixup` 仅对 `GL_VERTEX_SHADER` 生效（`if (gl_stage != GL_VERTEX_SHADER) return;`），对 fragment/其他 stage 直接返回。
- [x] C3. 注入逻辑正确：将 `void main(` 重命名为 `void _mithril_original_main(`，末尾追加包装 main，内含 `gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;`。
- [x] C4. 正则匹配 `\bvoid\s+main\s*\(`，能处理 `void main ()` / `void  main(` 等空白变体。
- [x] C5. 若源码无 `void main(` 匹配，函数安全返回不注入（`if (!std::regex_search(src, main_re)) return;`，不崩溃、不破坏原 shader）。
- [x] C6. `inject_position_fixup` 在 `glsl_to_spirv` 中的调用时机正确：在 `ensure_glsl_version` / `rewrite_desktop_builtins` / `apply_attrib_bindings` 之后、`wrap_loose_uniforms` 之前（Shader.cpp 注入点，glslang 编译之前）。
- [x] C7. Z 重映射对**两个** vertex SPIR-V 版本都注入（`inject_position_fixup` 在 flip_y 分支之前无条件执行，flip_y 仅控制是否追加 Y 翻转行；两份都经过 Z 重映射注入）。
- [x] C8. Shader.cpp 通过 g++ -fsyntax-only 编译验证（-std=c++20 -Wall -Wextra 无 warning），注入逻辑语法正确。

## 二、不改动项确认（对应 spec 4.6 / 4.7）

- [x] D1. `MG_Backend/DirectVulkan/CommandStream.cpp` 的 `backend_set_viewport` 未翻转 Y（直接透传 GL viewport 值；Y 翻转已从 MoltenVK 全局移至着色器层，viewport 行为等价不变）。
- [x] D2. `MG_Backend/DirectVulkan/CommandStream.cpp` 的 `backend_set_scissor` 未翻转 Y（直接透传 GL scissor 值；对标 MobileGL `MakeClampedScissorRect`，scissor Y 不翻转）。

### 根因 D：blit 到 default framebuffer 的目标 Y 翻转

- [x] D3. `MG_Backend/Backend.h` 的 `backend_blit_images` 声明已增加 `int is_dst_default_fbo` 和 `int dst_height` 参数（Backend.h:284-289）。
- [x] D4. `MG_Backend/DirectVulkan/ImageOps.cpp` 的 `blit_images_impl` 签名已增加 `bool is_dst_default_fbo, int dst_height` 参数（ImageOps.cpp:487-494）。
- [x] D5. `blit_images_impl` 在构造 `VkImageBlit` 前正确翻转目标 Y：`if (is_dst_default_fbo && dst_height > 0) { dstY0 = dst_height - dstY0; dstY1 = dst_height - dstY1; }`（ImageOps.cpp:503-525，对标 MobileGL `ApplyNativeBlitDefaultFramebufferTransform` identity 分支）。
- [x] D6. `backend_blit_images` 签名同步增加参数并透传到 `blit_images_impl`（ImageOps.cpp:658-684，`is_dst_default_fbo != 0` 转为 bool）。
- [x] D7. `MG_Impl/Framebuffer.cpp` 的 `glBlitFramebuffer` 计算 `is_dst_default_fbo = (currentDrawFBO == 0)` 并获取 `dst_height`（default FBO 从 `g_state->eglDefaultHeight` 获取；用户 FBO 从 `t->height` 获取）（Framebuffer.cpp:488-507）。
- [x] D8. `glBlitFramebuffer` 调用 `backend_blit_images` 时传入 `is_dst_default_fbo ? 1 : 0` 和 `dst_height`（Framebuffer.cpp:522-527）。
- [x] D9. 源 Y 坐标不翻转（对标 MobileGL，源内容方向由 draw 路径决定；blit_images_impl 仅翻转 dst Y）。
- [x] D10. 用户 FBO 目标 Y 不翻转（`is_dst_default_fbo=false` 时 if 条件不满足，直接透传 GL 坐标）。
- [x] D11. grep 确认全工程无其他 `backend_blit_images` 调用点遗漏新参数（仅 Framebuffer.cpp:522 一处调用，Backend.h:284 声明，ImageOps.cpp:658 定义，三处已同步更新）。

## 三、编译验证

- [x] E1. 修改后的 .cpp 文件（ImageOps.cpp、Framebuffer.cpp、Drawing.cpp）通过 g++ -fsyntax-only（-std=c++20 -Wall -Wextra -Wno-unused-parameter，使用 Khronos Vulkan-Headers），无新增 warning/error。（注：linux 沙箱无 MoltenVK/ObjC++，仅做跨平台 C++ TU 语法检查；完整链接构建需在 Apple 目标环境进行）
- [x] E2. 无未使用变量 / 未包含头文件问题。
- [x] E3. `inject_position_fixup` 使用 `std::regex`，Shader.cpp:43 已 `#include <regex>`。

## 四、运行验证（spec 第七章验证标准）

> 以下项需在 Apple 目标环境（iOS/macOS + 目标 App + MoltenVK）运行验证，linux 沙箱无法执行。

- [ ] F1. 主界面正常显示（无红屏）。
- [ ] F2. 进入游戏后画面正常（无黑屏，有画面有声音）。
- [ ] F3. GUI 渲染正确（按钮、文字、物品栏位置正确）。
- [ ] F4. 光效渲染正确（动态光影、水面反射）。
- [ ] F5. 深度测试正确（物体遮挡关系正确，无 z-fighting）。
- [ ] F6. 启用 `GL_CULL_FACE` 时几何体不被错误剔除。
- [ ] F7. 用户 FBO 纹理采样正确（无上下颠倒）。
- [ ] F8. default framebuffer 图像方向正确（无上下颠倒）。
- [ ] F9. 长时间运行（>30 分钟）无回归。
- [ ] F10. 无新增崩溃（SIGBUS/SIGSEGV/deviceLost）。

## 五、回归与回退

- [x] G1. 修复基于 main 分支（git branch 确认 main，origin/main 同步）。
- [ ] G2. 修复后再次构建运行，确认 F1-F10 全部通过。【待目标环境运行验证】
- [x] G3. 回退方案已在 spec 6.2 记录：恢复 `MVK_CONFIG_SHADER_CONVERSION_FLIP_VERTEX_Y=1` + 恢复 frontFace 透传 + 移除 Z 重映射 + 移除双 SPIR-V。
- [x] G4. 未经用户允许，未创建任何 PR / 未推送至远程（当前仅本地工作区修改，尚未 commit）。

## 六、约束遵守

- [x] H1. 全程在 main 分支工作。
- [x] H2. 推送令牌 `ghp_***` 仅用于推送（如用户要求推送时），本次未使用、未推送。
- [x] H3. 未经用户允许未提交 PR。
- [x] H4. 所有回答使用中文。
