# Continuous integration

Mithril keeps the number of GitHub Actions workflows deliberately small. A workflow must represent a durable evidence plane, not a temporary implementation step.

## Mainline workflows

### `build.yml` — required development gate

Runs on pushes to `main`, pull requests targeting `main`, and manual dispatch.

It owns the normal build and regression contract:

- DirectMetal macOS build and the registered `directmetal` CTest suite.
- Verification that the shipping DirectMetal artifact is Vulkan-free.
- iPhoneOS arm64 shipping build and package checks.
- Linux DirectVulkan reference build and the registered `vulkan` CTest suite.

Feature, fix, refactor, probe, and agent branches do **not** get their own copy of this matrix. They are validated by opening a pull request to the branch that owns the relevant gate.

### `hosted-metal-gpu-probe.yml` — manual platform runtime validation

The workflow is named `platform-runtime-validation` in the Actions UI and is intentionally manual.

It exists only for evidence that the normal build matrix cannot provide:

- macOS 26 hosted Apple Silicon running the DirectMetal suite on a real Metal GPU;
- arm64 iOS Simulator loading the produced DirectMetal dylib and executing representative GL/GPU smokes inside Simulator application sandboxes.

The iPhoneOS cross-build is not repeated here because `build.yml` already owns that contract.

## Workflow policy

Do not add workflows whose purpose is to apply a patch, materialize generated source, locate source text, stage a migration, recover a branch, commit changes, or push changes back to the repository. Names such as `apply-*`, `materialize-*`, `bootstrap-*`, `stage-*`, `finalize-*`, `one-shot-*`, `recover-*`, `*-once`, or `*-source-locate` are a strong sign that the task belongs in a normal commit or a local/agent tool rather than GitHub Actions.

CI is read-only with respect to repository contents. Workflow permissions should default to `contents: read`.

A new top-level workflow is justified only when it provides a durable evidence plane that cannot be expressed clearly as a job in an existing workflow. Prefer one build followed by several test jobs or steps over rebuilding the same artifact in several workflow files.

Heavy platform, stress, packaging, or diagnostic lanes should default to `workflow_dispatch` unless they are required for every pull request.

## Branch policy

Short-lived development branches contain source and tests, not bespoke copies of the repository CI. Duplicate branch names pointing at the same commit should be removed. Once a branch is fully contained by a newer canonical branch, the older branch should be deleted rather than kept as a permanent snapshot; Git history already preserves the commits.

Keep one canonical branch per active development line and make its purpose obvious from the name.
