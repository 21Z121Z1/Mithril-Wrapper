# MobileGL-style startup Actions

This branch intentionally has one workflow: `mobilegl-startup-validation.yml`.

It owns two platform contracts:

- `iPhoneOS arm64 ABI`: pinned iOS CMake/MoltenVK cross-build plus platform and required EGL/GL export verification.
- `macOS 26 MoltenVK / Metal startup runtime`: one production dylib build followed by the runtime oracles that actually exist on this branch — GL state, real-GPU rendering, EGL sync, CAMetalLayer surface, and synchronization/surface contract checks.

This branch does not contain `mc_gui_smoke.c`; CI must not imply that it does. If a dedicated startup/GUI oracle is added later, add it to the existing macOS runtime job rather than creating a second workflow.

Do not recreate a generic all-`fix/**` build workflow or a parallel hardening workflow here. Temporary probing, source-location, and patch-application tasks belong in normal commits or local/agent tooling.
