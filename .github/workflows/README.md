# Clean-tree CI evidence planes

The clean shipping family (`main` and `integration/directmetal-next`) intentionally shares two durable Actions entry points.

- `build.yml` is the normal read-only gate for pushes and pull requests on both clean canonical refs. It separates candidate-source identity from GitHub's integration subject, validates the agent control plane first, then owns DirectMetal macOS semantics/boundary, iPhoneOS arm64 packaging, and Linux Vulkan-reference regression.
- `hosted-metal-gpu-probe.yml` is manual `platform-runtime-validation` for evidence the normal matrix cannot provide: macOS 26 real-Metal execution and arm64 iOS Simulator runtime execution.

Tests belong to contracts; workflows provide environments. Extend registered CTest/oracle coverage before adding another top-level workflow.

Temporary patching, source-location, migration, recovery, branch-materialization, candidate and one-bug/one-workflow jobs do not belong in the clean tree. Legacy/investigation branches may carry temporary workflows only while answering a specific hypothesis; any durable semantic oracle or platform capability must converge back into this shared evidence system before retirement.

See `docs/ci.md` and `docs/evidence-model.md`.
