# DirectMetal next Actions

`integration/directmetal-next` is the only clean-tree DirectMetal product branch intended to land on `main`.

This development line has two durable Actions entry points:

- `build.yml` — the normal build and regression gate for pushes to, and pull requests targeting, `integration/directmetal-next`. It owns macOS DirectMetal CTest/boundary verification, iPhoneOS arm64 packaging, and the Linux Vulkan reference regression.
- `hosted-metal-gpu-probe.yml` — manual `platform-runtime-validation` for evidence the normal matrix cannot provide: macOS 26 real-Metal execution and arm64 iOS Simulator runtime execution.

Do not add a second CTest workflow or repeat the iPhoneOS cross-build in platform validation. Add registered semantic tests to CTest and let the existing gate execute them. Temporary probes and patch-materialization workflows do not belong in this directory.
