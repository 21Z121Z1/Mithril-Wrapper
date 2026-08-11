# Tasks

- [x] Task 1: 新增 `normalize_vulkan_incompatible_layouts()` 预处理 pass
  - [x] SubTask 1.1: 实现对 UBO 块 `layout(packed)` → `layout(std140)` 的改写
  - [x] SubTask 1.2: 实现对 UBO 块 `layout(shared)` → `layout(std140)` 的改写
  - [x] SubTask 1.3: 实现对 SSBO 块 `layout(packed/shared)` → `layout(std430)` 的改写
  - [x] SubTask 1.4: 保证幂等（`std140`/`std430` 不被重复改写）与注释跳过（复用 `is_in_comment`）

- [x] Task 2: 新增 `normalize_gl_legacy_constructs()` 预处理 pass
  - [x] SubTask 2.1: 实现 `gl_FragColor` → 合成 `layout(location=0) out vec4 _mithril_FragColor;` 声明 + 引用改写
  - [x] SubTask 2.2: 审计 relaxed 回退此前接受、但 Task 1 未覆盖的构造，仅在 Minecraft/模组着色器实际出现时增加改写（避免过度设计）；如无其它高频构造，本子任务记为无操作并注明

- [x] Task 3: 重构 `glsl_to_spirv()` 至单一 `EShClientOpenGL` 方言
  - [x] SubTask 3.1: 在预处理阶段（`wrap_loose_uniforms` / `inject_opaque_bindings` 之前）为所有编译尝试调用 Task 1 + Task 2 的归一化 pass
  - [x] SubTask 3.2: 移除 `shader3`（第三级 relaxed）整块，包括 `setEnvInputVulkanRulesRelaxed()` 调用与 `EShClientVulkan` 输入方言设置
  - [x] SubTask 3.3: grep 验证 `Shader.cpp` 内不再残留 `EShClientVulkan` 引用

- [x] Task 4: 更新 `verify/shader_relaxed_fallback_test.cpp`
  - [x] SubTask 4.1: 把 strict-vs-relaxed 方言对比改为「归一化 pass 后 strict `EShClientOpenGL` 编译 `layout(packed)` + `gl_FragColor` 着色器」的验证
  - [x] SubTask 4.2: 保留真实 Minecraft vertex/fragment 着色器的无回归检查（含 `gl_VertexID` 重命名路径）

- [x] Task 5: 更新 `README.md` 与代码注释
  - [x] SubTask 5.1: 重写 README「着色器翻译」小节，描述单路径 `EShClientOpenGL` 管线 + 归一化 pass
  - [x] SubTask 5.2: 更新 `Shader.cpp` 顶部管线注释块
  - [x] SubTask 5.3: 移除/更新行内引用 `EShClientVulkan` / 三级回退的注释

- [~] Task 6: 构建 & 冒烟验证（部分受环境限制）
  - [ ] SubTask 6.1: 用 `cmake` + `ios-cmake` 工具链构建 iOS arm64 dylib（`PLATFORM=OS64`，`DEPLOYMENT_TARGET=15.0`）— **本 Linux 沙箱不可行（无 macOS/Xcode/MoltenVK），须 CI/macOS 验证**
  - [x] SubTask 6.2: 主机运行 `verify/syntax_check.sh` 与更新后的 relaxed 测试（若主机可构建）— **relaxed 测试已构建并通过（7 用例 / 11 检查全过，exit=0，glslang 11.1.0-1501-g2eb8a581）；`syntax_check.sh` 因主机缺 `libvulkan-dev`/`glslang-dev`/`spirv-cross` 系统包未能全绿（3 通过 / 26 缺头文件，非代码缺陷）**
  - [ ] SubTask 6.3: `nm -gU` 确认所构建 dylib 导出 `gl*` 与 `egl*` 符号 — **依赖 6.1 的 dylib 产出，本环境不可行，须 CI/macOS 验证**

# Task Dependencies

- Task 2 依赖 Task 1（共享 regex / 注释跳过辅助）
- Task 3 依赖 Task 1 与 Task 2（归一化 pass 必须先存在再接线）
- Task 4 依赖 Task 3（测试须反映新管线）
- Task 5 依赖 Task 3（文档反映最终代码形态）
- Task 6 依赖 Task 3、Task 4、Task 5（验证完整改动）
