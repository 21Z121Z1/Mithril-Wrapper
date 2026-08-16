# DirectVulkan GUI closure Actions

This branch intentionally has one workflow: `directvulkan-gui-validation.yml`.

It owns two independent platform contracts:

- `iPhoneOS arm64 ABI`: cross-builds the shipping dylib with pinned iOS CMake/MoltenVK inputs and verifies the Mach-O platform plus required EGL/GL exports.
- `macOS 26 MoltenVK / Metal GUI runtime`: builds once and runs GL state, real-GPU rendering, synthetic Minecraft GUI (headless and EGL), EGL sync, CAMetalLayer surface, and synchronization/surface source-contract checks.

Do not recreate a second generic `build.yml` or a separate hardening workflow on this branch. Add durable ABI checks to the iOS job and runtime/GUI checks to the macOS job. Temporary diagnosis and patch application belong in normal commits or local/agent tooling, not GitHub Actions.
