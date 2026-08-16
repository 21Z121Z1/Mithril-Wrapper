# DirectVulkan Actions

This development line intentionally has one workflow: `directvulkan-validation.yml`.

Normal pushes and pull requests run the two fast correctness gates: Linux lavapipe regression and ASan/UBSan. Manual dispatch with `deep=true` adds the expensive platform and endurance evidence: macOS 26 MoltenVK/Metal, iPhoneOS arm64 ABI cross-build, and repeated lifecycle stress.

Do not create separate triage, stress, sanitizer, recovery, patch-application, or source-location workflows. Extend the existing job matrix when a durable validation category is required; keep temporary debugging and code modification in normal commits or local/agent tooling.
