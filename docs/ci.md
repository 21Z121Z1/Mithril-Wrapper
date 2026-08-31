# Continuous integration as evidence infrastructure

GitHub Actions is an evidence execution plane. It should answer “what does this exact implementation prove in this environment?” without becoming a second build system or a branch-mutation engine.

See `docs/evidence-model.md` for evidence tiers and exact-SHA claim rules.

## Durable workflow ownership

### `.github/workflows/build.yml` — normal product gate

Owns the cheap, repeatable evidence required for `main` and pull requests to `main`:

- clean DirectMetal macOS configure/build;
- DirectMetal semantic/regression CTest label;
- Vulkan-free shipping boundary verification;
- iPhoneOS arm64 shipping build/ABI/Mach-O/signature checks;
- Linux Vulkan reference regression;
- agent-facing manifest/document contract validation.

This workflow is read-only (`contents: read`). Keep it deterministic enough to be a required merge gate.

### `.github/workflows/hosted-metal-gpu-probe.yml` — unique platform runtime evidence

Manual/deep lane for real Apple runtime evidence the normal matrix should not duplicate. It may exercise a hosted Metal GPU or simulator presentation seam, but it should remain an evidence producer, not a source mutator.

## Test ownership versus workflow ownership

Tests belong to contracts. Workflows supply environments.

Prefer:

```text
new semantic rule
  -> focused test in tests/
  -> register under existing CTest label
  -> normal build workflow runs it
```

Do not prefer:

```text
new semantic rule
  -> new top-level workflow
  -> another bespoke build of the same library
  -> source grep used as behavioral proof
```

If a test needs a special environment, add a narrowly scoped job/selector to the existing relevant plane before creating a new durable workflow.

## Agent contract validation

The repository exposes `docs/agent/manifest.json` as machine-readable navigation and ownership data. `scripts/validate_agent_contract.py` verifies the manifest schema, required document references, layer ordering/IDs, branch-role uniqueness and referenced repository paths.

This check is intentionally cheap and runs before expensive renderer jobs. Its purpose is to prevent the agent-facing system map from silently rotting while code/branches move.

It does **not** claim that the live GitHub branch inventory matches a static snapshot; `docs/agent/status.md` and `docs/agent/branch-ledger.md` are explicitly dated and require human/agent reconciliation when topology changes.

## Exact product identity

When a workflow validates its own checkout, `GITHUB_SHA` is the implementation identity.

When a validation branch checks out another candidate, the workflow must record at least:

- harness SHA;
- candidate product SHA;
- loaded artifact SHA-256 when applicable;
- platform/toolchain/runtime identity relevant to the claim.

A trigger-only commit is evidence orchestration, not the product implementation. Keep those identities separate in logs and PR descriptions.

## Branch-specific workflows

Temporary workflows on experiment/validation branches are tolerated when necessary to answer a question that cannot be expressed by current durable planes. They are not architectural assets by default.

Before that branch is retired:

1. decide whether the result is a reusable semantic oracle, platform capability, artifact/provenance record or rejected hypothesis;
2. move reusable pieces into the durable test/evidence structure;
3. delete one-off workflow files rather than merging them into the clean tree by inertia.

The 2026-08 DirectVulkan investigation family contains many candidate/A-B workflows; they should be mined for oracles and provenance, not reproduced wholesale on `main`.

## Forbidden CI roles

Do not keep workflows whose primary role is to:

- `apply-*`, `materialize-*`, `bootstrap-*`, `stage-*`, `finalize-*`, `recover-*` or `*-once` source changes;
- commit/push implementation edits from Actions;
- locate source once and preserve the answer as workflow code;
- duplicate another workflow’s build/test matrix without unique evidence;
- weaken/fork an oracle merely to make a branch green;
- treat source-text grep as a substitute for runtime semantics when an executable oracle is practical.

Automation that changes source belongs in an explicit agent/PR workflow with reviewable commits, not a recurring CI gate.

## Evidence artifact policy

Artifacts are useful when they make a claim independently inspectable:

- shipping dylib/package and checksum;
- framebuffer/pixel output that is itself an oracle;
- concise logs/provenance metadata;
- benchmark distributions and configuration.

Avoid uploading:

- generated Minecraft reference source/client JAR;
- full dependency/build directories without diagnostic value;
- giant unthrottled runtime logs;
- duplicate artifacts from multiple jobs when one exact product artifact is sufficient.

## Heavy validation

Real Minecraft, stress, performance, long-running diagnostics and physical-device claims are expensive and often environment-sensitive. Run them when they add information that E0-E3 cannot provide.

A normal PR should not pay for every historic experiment. Conversely, do not declare a release/device claim from cheap CI alone because the heavy lane is inconvenient.

## Failure interpretation

A failed job is evidence too. Classify it before rerunning:

- product implementation/semantic failure;
- test/harness defect;
- dependency/toolchain incompatibility;
- environment/capability absence;
- flaky/infrastructure failure.

Only rerun without code/harness changes when evidence supports a transient infrastructure failure. Do not use reruns to hide deterministic red states.

## Workflow change checklist

Before adding or modifying CI, ask:

1. What claim does this job uniquely prove?
2. Which evidence tier is it?
3. Can an existing test label/workflow host it?
4. Is the exact product SHA unambiguous?
5. Is the workflow read-only?
6. Does it produce a reusable oracle/artifact rather than branch-specific noise?
7. What will delete or simplify this machinery later?

The best CI architecture is not the one with the most checks. It is the smallest graph that gives an agent high-confidence, exact-SHA feedback for every meaningful contract boundary.
