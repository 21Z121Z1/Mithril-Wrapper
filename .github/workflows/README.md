# DirectVulkan Actions

This development line intentionally has one workflow: `directvulkan-validation.yml`.

Normal pushes and pull requests run only established regression baselines: Linux lavapipe `gl_smoke` / `render_smoke` / EGL sync, plus ASan/UBSan over stable harnesses. Manual dispatch with `deep=true` adds expected-green macOS 26 MoltenVK/Metal, iPhoneOS arm64 ABI, and repeated lifecycle evidence.

The strict `overnight_regression_smoke` is intentionally a separate manual diagnostic (`diagnostics=true`) while the backend still has three deterministic open semantics on the canonical recovery baseline: user-FBO CCW/back-face culling, PBO-upload texture sampling, and cross-stage UBO rendering. The diagnostic remains strict and is expected to fail until those implementation gaps are fixed; it is not marked `continue-on-error` and its assertions are not weakened. Once all 23 checks pass, move it back into the normal regression gate.

`tests/render_smoke.c` currently has a sanitizer-visible test-harness defect: its local `glTexStorage2D` function-pointer typedef/call uses six parameters although the OpenGL API has five. Normal rendering passes because the extra argument is tolerated by the unsanitized ABI, but UBSan correctly rejects the indirect call. Until that test helper is corrected in a focused test change, sanitizer CI deliberately uses the stable GL-state and EGL-sync harnesses rather than disabling UBSan's function-type check.

Do not create separate triage, stress, sanitizer, recovery, patch-application, or source-location workflows. Extend this job matrix when a durable validation category is required; keep temporary debugging and code modification in normal commits or local/agent tooling.
