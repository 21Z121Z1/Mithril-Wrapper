# 重构 GLSL 转换管线至 EShClientOpenGL Spec

## Why

`Mithril-Wrapper-cpp/MG_Impl/Shader.cpp` 的 `glsl_to_spirv()` 当前混合使用两种
glslang 输入方言：strict 路径走 `EShClientOpenGL`，第三级 relaxed 回退走
`EShClientVulkan`。这种方言混用是脆弱的——relaxed 回退之所以有效，是因为
glslang 的 `TranslateEnvironment`（ShaderLang.cpp）仅在输入方言为
`EShClientVulkan` 时才把 `vulkanRulesRelaxed` 复制进 `spvVersion`；在
`EShClientOpenGL` 下 `setEnvInputVulkanRulesRelaxed()` 是静默 no-op。

把整个管线收敛到单一 `EShClientOpenGL` 输入方言、并在 GLSL 源码层归一化
Vulkan 不兼容的构造，可以：

- 移除一处隐藏的方言依赖，让编译器配置统一、可预测
- 简化为两级 strict 回退（wrapped → unwrapped），删除整个第三级 relaxed 分支
- 保留此前 `fix-red-screen-root-causes-vwx` 引入 relaxed 回退所要解决的根因
  修复意图（`layout(packed/shared)` UBO、GL 遗留构造），改由源码级归一化承担

开发分支：`feat/ios-metal2-moltenvk-compat-and-perf`（已检出，干净工作树）。

## What Changes

- **新增** GLSL 预处理 pass `normalize_vulkan_incompatible_layouts()`：在
  glslang 看到源码之前，把 `layout(packed)` / `layout(shared)` UBO 块改写为
  `layout(std140)`、SSBO 块改写为 `layout(std430)`，使 strict
  `EShClientOpenGL + EShMsgVulkanRules` 路径能直接编译。
- **新增** GLSL 预处理 pass `normalize_gl_legacy_constructs()`：归一化 strict
  Vulkan 规则拒绝、此前由 relaxed 回退兜底的 GL 遗留构造（首要是
  `gl_FragColor` → 合成命名输出 + 引用改写；其它构造仅在 Minecraft/模组
  着色器实际出现时才增加改写，避免过度设计）。
- **修改** `glsl_to_spirv()`：在预处理阶段（`wrap_loose_uniforms` /
  `inject_opaque_bindings` 之前）对**所有**编译尝试调用上述两个归一化 pass。
- **移除** 第三级 relaxed 回退 `shader3` 整块，包括 `setEnvInputVulkanRulesRelaxed()`
  调用与 `EShClientVulkan` 输入方言设置。
- **移除** `Shader.cpp` 中全部 `EShClientVulkan` 引用（grep 确认其仅出现在
  `shader3` 块内）。
- **修改** `verify/shader_relaxed_fallback_test.cpp`：把"strict vs relaxed 方言
  对比"改为"归一化 pass 后 strict `EShClientOpenGL` 编译 `layout(packed)` +
  `gl_FragColor` 着色器"的验证；保留真实 Minecraft VS/FS 的无回归检查。
- **修改** `README.md`「着色器翻译」小节与 `Shader.cpp` 顶部管线注释 / 行内
  注释，反映单路径 `EShClientOpenGL` 管线 + 归一化 pass。
- **新增** 构建/冒烟验证步骤：用 `cmake` + `ios-cmake` 工具链构建 iOS arm64
  dylib，运行 `verify/syntax_check.sh` 与更新后的 relaxed 测试（若主机可构建），
  并用 `nm -gU` 确认 `gl*` / `egl*` 符号导出。

## Impact

- **受影响 specs**：
  - `specs/fix-red-screen-root-causes-vwx/`（引入被移除的 relaxed 回退——其意图
    由预处理归一化保留）
  - `specs/fix-red-screen-root-causes-klm/`、`specs/fix-red-black-screen-final-root-causes/`
    （上下文中引用三级回退）
- **受影响代码**：
  - `Mithril-Wrapper-cpp/MG_Impl/Shader.cpp`（核心改动）
  - `Mithril-Wrapper-cpp/MG_Impl/Shader.h`（若接口注释需同步）
  - `verify/shader_relaxed_fallback_test.cpp`（重写）
  - `README.md`（文档）
- **受影响构建**：无。不改动 `CMakeLists.txt` / 依赖；glslang 已支持所用全部
  `EShClientOpenGL` 路径。

## ADDED Requirements

### Requirement: Vulkan 不兼容 layout 归一化

系统 SHALL 在 glslang 编译之前，于 GLSL 源码层把 `layout(packed)` /
`layout(shared)` UBO 块改写为 `layout(std140)`、SSBO 块改写为 `layout(std430)`，
使 strict `EShClientOpenGL + EShMsgVulkanRules` 路径能编译此前依赖 relaxed
回退的着色器。

#### Scenario: layout(packed) UBO 在 strict 路径下编译通过
- **WHEN** 片元着色器包含 `layout(packed) uniform Matrices { mat4 MVP; } _m;`
- **THEN** 归一化器将其改写为 `layout(std140) uniform Matrices { mat4 MVP; } _m;`
- **AND** strict `EShClientOpenGL` 路径将其编译为有效 SPIR-V，不调用被移除的
  relaxed 回退

#### Scenario: layout(shared) UBO 在 strict 路径下编译通过
- **WHEN** 着色器包含 `layout(shared) uniform Block { ... };`
- **THEN** 归一化器将其改写为 `layout(std140)`，strict 路径编译成功

#### Scenario: 已是 std140/std430 的保持不变
- **WHEN** 着色器已使用 `layout(std140)` 或 `layout(std430)`
- **THEN** 归一化器幂等，不改写该限定符

#### Scenario: 注释内的 layout 不被改写
- **WHEN** `layout(packed)` 出现在 `//` 或 `/* */` 注释内
- **THEN** 归一化器不改写它

### Requirement: GL 遗留构造归一化

系统 SHALL 在 GLSL 源码层归一化 strict Vulkan 规则拒绝、此前由 relaxed 回退
接受的 GL 遗留构造（与 relaxed 回退覆盖范围一致）。

#### Scenario: gl_FragColor 改写为命名输出
- **WHEN** 片元着色器使用 `gl_FragColor`（GL 遗留内建输出）
- **THEN** 归一化器注入合成声明 `layout(location=0) out vec4 _mithril_FragColor;`
  并改写所有 `gl_FragColor` 引用为 `_mithril_FragColor`
- **AND** strict 路径编译该着色器成功

#### Scenario: 已显式声明输出的着色器不受影响
- **WHEN** 片元着色器已声明 `layout(location=0) out vec4 fragColor;` 且未使用
  `gl_FragColor`
- **THEN** 归一化器不注入合成输出

## MODIFIED Requirements

### Requirement: GLSL → SPIR-V 转换管线

系统 SHALL 通过 glslang 把桌面 GLSL Core Profile 源码翻译为 Vulkan SPIR-V，
**所有**编译尝试统一使用 `EShClientOpenGL` 输入方言并启用
`EShMsgVulkanRules`。管线 SHALL 在编译前对源码依次执行：升级 GLSL 版本、
改写桌面内建、应用顶点属性绑定、归一化 Vulkan 不兼容 layout、归一化 GL 遗留
构造、注入位置 fixup、包装 loose uniform、注入 opaque binding。管线 SHALL
提供两级 strict 回退（wrapped 源码 → unwrapped 源码），二者均使用
`EShClientOpenGL`。管线 SHALL NOT 使用 `EShClientVulkan`，SHALL NOT 调用
`setEnvInputVulkanRulesRelaxed()`。

#### Scenario: strict 路径编译标准 Minecraft 着色器
- **WHEN** 编译 Minecraft 1.21 的 vertex/fragment 着色器
- **THEN** strict `EShClientOpenGL` 路径产出有效 SPIR-V
- **AND** 不调用任何回退路径

#### Scenario: unwrapped 回退仍然可用
- **WHEN** `wrap_loose_uniforms` 误伤边界情况声明导致 strict 路径失败
- **THEN** 尝试第二级 unwrapped 回退（`EShClientOpenGL`）
- **AND** 不再尝试第三级 relaxed 回退（已移除）

#### Scenario: 需要 relaxed 规则的着色器经归一化后编译通过
- **WHEN** 着色器包含 `layout(packed)` UBO 或 `gl_FragColor`（此前需 relaxed 回退）
- **THEN** 预处理归一化 pass 改写源码
- **AND** strict `EShClientOpenGL` 路径编译归一化后的源码成功
- **AND** 不使用 `EShClientVulkan` 方言

## REMOVED Requirements

### Requirement: 第三级 relaxed Vulkan-rules 回退

**Reason**: relaxed 回退依赖 `EShClientVulkan` 作为输入方言
（`setEnvInputVulkanRulesRelaxed()` 在 `EShClientOpenGL` 下是 no-op）。按重构
指令，`EShClientVulkan` 将被完全移除。relaxed 回退覆盖的用例
（`layout(packed/shared)` UBO、GL 遗留构造）改由源码级预处理归一化 pass 承担，
保留根因修复意图而不依赖该方言。

**Migration**: 此前由 relaxed 回退救回的着色器，现由
`normalize_vulkan_incompatible_layouts()` + `normalize_gl_legacy_constructs()`
在 strict 编译前归一化救回。两级 strict 回退（wrapped → unwrapped，均为
`EShClientOpenGL`）保留，用于 regex 包装器的边界情况。
