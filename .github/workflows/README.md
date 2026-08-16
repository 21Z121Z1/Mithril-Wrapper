# GitHub Actions

This directory intentionally contains only durable CI evidence planes.

- `build.yml`: the normal main/pull-request build and CTest gate.
- `hosted-metal-gpu-probe.yml`: manual macOS 26 real-Metal and iOS Simulator runtime validation; it appears as **platform-runtime-validation** in the Actions UI.

Temporary patching, source-location, migration, recovery, and branch-materialization tasks do not belong here. See [`docs/ci.md`](../../docs/ci.md) for the ownership and branch policy.
