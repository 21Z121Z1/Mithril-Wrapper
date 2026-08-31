# GitHub Actions

This directory intentionally contains only durable CI evidence planes.

- `build.yml`: the normal main/pull-request gate. It first validates the agent-facing system contract, then runs DirectMetal macOS semantics/boundary, DirectMetal iPhoneOS arm64 packaging, and Linux Vulkan reference regression.
- `hosted-metal-gpu-probe.yml`: manual macOS 26 real-Metal and iOS Simulator runtime validation; it appears as **platform-runtime-validation** in the Actions UI.

Tests belong to repository contracts; workflows provide execution environments. Extend existing CTest labels/evidence planes before adding another top-level workflow.

Temporary patching, source-location, migration, recovery, branch-materialization and one-bug/one-workflow tasks do not belong in the clean tree. Branch-specific experiment workflows may exist temporarily on legacy/investigation refs, but durable oracles must converge back into the shared evidence system before those branches are retired.

See [`docs/ci.md`](../../docs/ci.md) and [`docs/evidence-model.md`](../../docs/evidence-model.md).
