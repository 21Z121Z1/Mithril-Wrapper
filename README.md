# Mithril-Wrapper

Mithril-Wrapper 是面向 Minecraft Java / LWJGL 的 OpenGL compatibility system。目标不是“把 OpenGL 调用逐个翻译成另一套 API”，而是把 Minecraft 可观察到的 GL/EGL 语义解析成明确、可验证的中间意图，再由原生 GPU backend 高效执行。

Apple 平台的 shipping path 是 **DirectMetal**。Vulkan 路径保留为隔离的 reference/fallback execution engine；在 Apple 上可借助 MoltenVK，在 Linux 上可用于独立回归。两条 backend 不应各自发明不同的 OpenGL 语义。

## 系统模型

```text
Minecraft / LWJGL observable behavior
              |
              v
EGL / host ABI and lifecycle
              |
              v
OpenGL + shader observable semantics
              |
              v
backend-neutral resolved intent
(draw/resource/state + lifetime/version identity)
              |
        +-----+-----+
        |           |
        v           v
 DirectMetal      Vulkan
 shipping        reference
        |           |
        +-----+-----+
              v
platform / presentation seam
              |
              v
exact-subject evidence -> promotion
```

完整 ownership/invariant 定义见 `docs/system-model.md`。最重要的架构 seam 是 `src/backend/*`：mutable GL state 应在被观察时解析成显式 snapshot，native backend 只执行已经解析好的意图。

## 分支不是架构

仓库包含两个 Git history universe：

- clean shipping family：`main -> integration/directmetal-next`；
- disconnected legacy/experimental family：以 `integration/directvulkan-reference`、`integration/legacy-capability-port` 及大量 `Mithril-Wrapper-cpp/*` 实验线为主要来源。

legacy family 的价值是语义、oracle 和 provenance，不是可整体合并的产品架构。跨 history universe 的正确动作是 **semantic transplant**：提炼 invariant -> 建 focused oracle -> 放入 clean owner -> exact-subject 验证。

不要从 README 中猜当前 branch 状态。需要实时拓扑时运行：

```bash
python3 scripts/audit-branches.py --fetch-graph --markdown
```

## Agent 入口

编码、审查或恢复历史工作前先读 `AGENTS.md`，不要 breadth-first 扫整个仓库。

最小上下文入口：

```bash
python3 scripts/agent-context.py --task "describe the task"
```

它会给出当前 HEAD/tree、history universe、nearest anchor、ownership、boundary risk、最小读取集合和 proof plan。涉及 Minecraft 26.2 原始行为时：

```bash
SRC="$(bash scripts/minecraft-reference.sh --print-path)"
```

生成内容仅是本地分析输入，不进入 Git/CI artifact。

## Clean-tree 目录

```text
src/egl       host/EGL 生命周期与 surface seam
src/gl        OpenGL observable semantics
src/state     GL 状态与错误模型
src/shader    GLSL/SPIR-V translation + reflection contract
src/backend   backend-neutral resolved intent
src/metal     DirectMetal native execution
src/vk        Vulkan reference/fallback execution
include       public / diagnostic ABI
tests         focused semantic and backend oracles
cmake         shared test registration
scripts       build, verification and agent tools
```

`Mithril-Wrapper-cpp/*` 属于 disconnected legacy history 中的迁移来源，不是 clean architecture 的第二棵长期产品树。

## 构建与验证

Linux / Vulkan reference：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DMITHRIL_BUILD_LEGACY=ON -DMITHRIL_BUILD_DIRECT=OFF
cmake --build build --parallel
ctest --test-dir build -L vulkan --output-on-failure
```

macOS / DirectMetal：

```bash
cmake -S . -B build-direct -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DMITHRIL_BUILD_LEGACY=OFF -DMITHRIL_BUILD_DIRECT=ON \
  -DMITHRIL_ENABLE_SHADER_TOOLCHAIN=ON
cmake --build build-direct --parallel
ctest --test-dir build-direct -L directmetal --output-on-failure
```

iPhoneOS shipping artifact 使用：

```bash
scripts/build_iphoneos.sh
```

正常 GitHub Actions gate 同时服务 `main` 与 `integration/directmetal-next`。PR 中 candidate source identity 与 GitHub synthetic merge/integration subject 是两个不同 proof subject；详见 `docs/evidence-model.md` 与 `docs/ci.md`。

## 什么是“已支持”

README 不维护函数级 capability snapshot，因为那会随实现快速过期。

- DirectMetal capability / exact-partial-unsupported 账本：`docs/directmetal-gl33-semantic-matrix.md`
- GL 3.3 core symbol/domain inventory：`docs/gl33_core_list.md`
- EGL symbol inventory：`docs/egl_list.md`
- Amethyst/LWJGL host seam：`docs/contracts/amethyst-host-contract.md`
- 当前 convergence checkpoint：`docs/agent/status.md`（明确是 dated snapshot）

任何 capability claim 的优先级都是：shipping source/tests + exact tree/binary evidence > stable contract docs > dated status/ledger > historical experiment prose。

## 文档地图

从 `docs/README.md` 开始。核心文档：

- `AGENTS.md` — agent operating contract
- `docs/system-model.md` — stable abstraction tower
- `docs/evidence-model.md` — evidence/claim semantics
- `docs/branches.md` — branch/history-universe policy
- `docs/ci.md` — durable evidence-plane policy
- `docs/agent/manifest.json` — machine-readable ownership/router
- `docs/agent/status.md` — dated current frontier

历史 milestone/checklist 已从默认入口移出；Git 历史及 `docs/history/` 保留 provenance。
