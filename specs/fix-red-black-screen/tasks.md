# 红屏/黑屏修复任务列表

> 基于 [spec.md](./spec.md) 的修复方案拆解。所有任务在 `main` 分支上进行，未经用户允许不提交 PR、不推送远程。每项任务完成后对照 [checklist.md](./checklist.md) 勾选对应项。

## 阶段 0：基线确认

- [ ] T0.1 确认当前处于 `main` 分支（`git status` / `git branch --show-current`）。
- [ ] T0.2 确认基线代码可复现「主界面红屏 / 进游戏黑屏」现象（如有运行环境）。
- [ ] T0.3 重新核查 spec 中引用的代码行号与实际源码一致（spec 第二章 A/B/C 三处引用）。

## 阶段 1：根因 C —— Z 重映射（最先做，因为两份 vertex SPIR-V 都依赖它）

- [ ] T1.1 在 `MG_Impl/Shader.cpp` 实现 `inject_z_remap(std::string& src, GLenum gl_stage)`：
  - 仅 `GL_VERTEX_SHADER` 生效。
  - 正则 `\bvoid\s+main\s*\(` → `void _mithril_original_main(`。
  - 末尾追加包装 main：`gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5;`。
  - 无匹配时安全返回。
- [ ] T1.2 在 `glsl_to_spirv` 中调用 `inject_z_remap`，时机：`ensure_glsl_version` 之后、`wrap_loose_uniforms` 之前。
- [ ] T1.3 确认 `Shader.cpp` 已 `#include <regex>`。
- [ ] T1.4 编写最小测试 vertex shader（含 `void main(){ gl_Position = vec4(0); }`），验证注入后能成功编译为 SPIR-V（可通过临时日志或单元测试）。

## 阶段 2：根因 A —— 按 framebuffer 区分 Y 翻转

- [ ] T2.1 `MG_State/State.h`：`Program` 结构新增 `std::vector<uint32_t> vertexSpirvYFlipped;`，加注释。
- [ ] T2.2 `MG_Impl/Shader.cpp`：`shader_translate` / `glsl_to_spirv` 增加 `bool flip_y = false` 参数；`flip_y=true` 时调用 glslang `setInvertY(true)`（或等价机制）。
- [ ] T2.3 `MG_Impl/Shader.cpp`：shader 缓存键纳入 flip_y（异或不同常量），避免缓存冲突。
- [ ] T2.4 `MG_Impl/Program.cpp`：`glLinkProgram` 中对 vertex shader 编译两次：
  - `shader_translate(GL_VERTEX_SHADER, src, prog->vertexSpirv, info_log, &attrib_bindings, /*flip_y=*/false)`
  - `shader_translate(GL_VERTEX_SHADER, src, prog->vertexSpirvYFlipped, info_log, &attrib_bindings, /*flip_y=*/true)`
- [ ] T2.5 `MG_Impl/Program.cpp`：fragment shader 仍只编译一次。
- [ ] T2.6 `MG_Backend/DirectVulkan/Device.cpp`：`MVK_CONFIG_SHADER_CONVERSION_FLIP_VERTEX_Y` 改为 `"0"`，更新注释说明 Y 翻转已改为着色器层面按需处理。
- [ ] T2.7 grep 全工程确认无其他位置重新开启 `MVK_CONFIG_SHADER_CONVERSION_FLIP_VERTEX_Y=1`。

## 阶段 3：根因 B —— frontFace / cullMode 调整 + pipeline 签名

- [ ] T3.1 `MG_Impl/Drawing.cpp` `prepare_draw`：计算 `is_default_fbo = (g_state->currentDrawFBO == 0)`、`invert_clockwise = is_default_fbo`。
- [ ] T3.2 `prepare_draw`：按 `is_default_fbo` 选择 `vertexSpirvYFlipped` 或 `vertexSpirv`。
- [ ] T3.3 `prepare_draw`：实现 cullMode 调整（对标 `ConvertCullFaceModeToVkEnum`）：
  - `GL_FRONT` + invert → VK_BACK
  - `GL_BACK` + invert → VK_FRONT
  - `GL_FRONT_AND_BACK` → VK_FRONT_AND_BACK
- [ ] T3.4 `prepare_draw`：启用 cullFace 时 `backend_set_front_face(0)`（CLOCKWISE）；未启用时 `backend_set_cull_mode(0)`。
- [ ] T3.5 `MG_Backend/DirectVulkan/CommandStream.cpp`：核查 `backend_set_front_face` 的 0/1 映射（0=CW, 1=CCW），确保与 T3.4 一致。
- [ ] T3.6 `MG_Backend/DirectVulkan/Pipeline.cpp`：pipeline 签名哈希纳入 `is_default_fbo`（`mix(&is_default_fbo, sizeof(is_default_fbo))`），确保同一 program 在不同 FBO 下产生不同 pipeline。
- [ ] T3.7 `Pipeline.cpp`：若 pipeline 创建时设置静态 frontFace，确认默认值/传参与新逻辑一致（动态状态优先）。

## 阶段 4：编译验证

- [ ] T4.1 完整构建工程，确认无新增 warning/error。
- [ ] T4.2 修复编译过程中发现的问题（头文件缺失、未使用变量等）。

## 阶段 4.5：根因 D —— blit 到 default framebuffer 的目标 Y 翻转

- [x] T4.5.1 `MG_Backend/Backend.h`：`backend_blit_images` 声明增加 `int is_dst_default_fbo` 和 `int dst_height` 参数。
- [x] T4.5.2 `MG_Backend/DirectVulkan/ImageOps.cpp`：
  - `blit_images_impl` 签名增加 `bool is_dst_default_fbo, int dst_height` 参数。
  - 在构造 `VkImageBlit` 前翻转目标 Y：`if (is_dst_default_fbo && dst_height > 0) { dstY0 = dst_height - dstY0; dstY1 = dst_height - dstY1; }`。
  - `backend_blit_images` 签名同步增加参数，透传到 `blit_images_impl`。
- [x] T4.5.3 `MG_Impl/Framebuffer.cpp` `glBlitFramebuffer`：
  - 计算 `bool is_dst_default_fbo = (g_state->currentDrawFBO == 0)`。
  - 计算目标帧缓冲高度 `dst_height`：default FBO 从 `g_state->eglDefaultHeight` 获取；用户 FBO 从纹理 `t->height` 获取。
  - 调用 `backend_blit_images(..., is_dst_default_fbo ? 1 : 0, dst_height)`。
- [x] T4.5.4 确认源 Y 不翻转（对标 MobileGL）。
- [x] T4.5.5 grep 确认无其他 `backend_blit_images` 调用点遗漏新参数（仅 Framebuffer.cpp 一处调用）。

## 阶段 5：运行验证
- [ ] T5.1 启动应用，确认主界面无红屏（F1）。
- [ ] T5.2 进入游戏，确认画面正常、有声音（F2）。
- [ ] T5.3 验证 GUI 渲染正确（F3）。
- [ ] T5.4 验证光效 / 水面反射（F4）。
- [ ] T5.5 验证深度测试 / 遮挡关系（F5）。
- [ ] T5.6 验证 GL_CULL_FACE 启用时几何体不被错误剔除（F6）。
- [ ] T5.7 验证用户 FBO 纹理采样方向正确（F7）。
- [ ] T5.8 验证 default framebuffer 方向正确（F8）。
- [ ] T5.9 长时间运行（>30 分钟）无回归（F9）。
- [ ] T5.10 无新增崩溃（F10）。

## 阶段 6：收尾

- [ ] T6.1 若 blit 路径出现 Y 翻转问题，记录为后续单独修复项（不阻塞本次）。
- [ ] T6.2 对照 checklist.md 逐项核查并勾选。
- [ ] T6.3 在 main 分支本地提交（commit message 描述修复的三个根因）。
- [ ] T6.4 向用户报告修复结果，询问是否需要推送（使用提供的令牌）；**未获明确允许不推送、不开 PR**。
