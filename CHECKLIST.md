# CHECKLIST — compatibility index

`CHECKLIST.md` 这个路径为了旧链接保留，但它**不再是当前实现状态或路线图的权威来源**。

旧版里程碑清单（M0–M6、当时的 GL/Vulkan/DirectMetal 状态与大量一次性说明）已经被冻结到：

- `docs/history/implementation-checklist-2026-08.md`

需要当前信息时使用下面的来源：

- 系统架构与 ownership：`docs/system-model.md`
- agent 工作入口：`AGENTS.md`
- 当前 convergence checkpoint：`docs/agent/status.md`
- 实时 branch topology：`python3 scripts/audit-branches.py --fetch-graph --markdown`
- DirectMetal capability ledger：`docs/directmetal-gl33-semantic-matrix.md`
- Amethyst/LWJGL host seam：`docs/contracts/amethyst-host-contract.md`
- evidence / exact-SHA 规则：`docs/evidence-model.md`
- CI ownership：`docs/ci.md`

如果历史 checklist 中的一条经验仍然影响当前实现，应把它提升为 source invariant、focused test、machine-readable rule 或稳定 contract；不要继续在本文件里追加事件日志。
