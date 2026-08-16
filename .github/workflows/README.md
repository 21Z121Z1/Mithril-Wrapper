# Minecraft E2E Actions

This branch keeps four durable evidence planes. They are intentionally separate because each answers a different release question.

- `directmetal-semantic-validation.yml` — does one production Mithril build satisfy GL/DirectMetal ABI and GPU semantics on hosted Metal?
- `minecraft-client-e2e.yml` — can the real Minecraft 26.2 production client run through Mithril DirectMetal and produce authoritative framebuffer/runtime evidence?
- `minecraft-render-differential.yml` — does Mithril rendering agree with the pinned native OpenGL reference for the same deterministic workload?
- `minecraft-directmetal-distribution.yml` — can the validated runtime be assembled into a portable launcher-facing package and still run correctly?

Do not add one workflow per bug, probe, oracle, patch, or source-location task. Add new semantic oracles to `directmetal-semantic-validation.yml`; add new production-client assertions to the Minecraft E2E harness; extend differential/package workflows only when their existing evidence plane requires it.
