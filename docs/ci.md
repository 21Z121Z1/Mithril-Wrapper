# Continuous integration as evidence infrastructure

GitHub Actions is an **evidence execution plane**. It answers: “what does this exact subject prove, in this environment?” It is not a second build system, branch database, patch engine or substitute for semantic tests.

## Clean-tree ownership

The clean shipping family shares one normal gate:

- `main` — shipping/governance baseline;
- `integration/directmetal-next` — active clean DirectMetal integration while it exists.

`.github/workflows/build.yml` runs for pushes and pull requests on both refs. Keeping one definition is deliberate: clean branch topology should not create two subtly different notions of correctness.

Manual `.github/workflows/hosted-metal-gpu-probe.yml` owns Apple runtime evidence the normal matrix cannot faithfully duplicate.

## Proof subjects

A pull request has at least two distinct Git subjects:

1. **candidate source** — the PR head selected by the author/agent;
2. **integration subject** — GitHub's synthetic merge result against the target branch.

The cheap agent-system job explicitly checks out and asserts candidate identity. Product jobs use the integration checkout and record `source_sha` + `source_tree`.

For a push, both identities collapse to the pushed commit, but logs still name the proof subject.

Never say “CI passed on SHA X” when the product job actually executed a different synthetic merge SHA.

## Durable jobs

The normal gate owns:

- agent-control-plane validation;
- DirectMetal macOS build + registered semantic suite;
- Vulkan-free DirectMetal shipping boundary;
- iPhoneOS arm64 shipping build/package checks;
- Linux Vulkan reference regression.

The hosted platform workflow owns:

- real hosted Apple-Silicon Metal execution where available;
- iOS Simulator runtime execution where useful;
- platform/presentation evidence that is meaningfully different from a cross-build.

Tests belong to semantic contracts; workflows supply environments.

## Proof DAG

`docs/agent/proof-graph.json` defines prerequisite ordering over proof profiles from `docs/agent/manifest.json`.

General shape:

```text
control
  |
focused semantic oracle
  |\
  | +--> Vulkan regression
  +----> DirectMetal regression --> hosted Metal --> physical presentation
  |
  +----> Minecraft E2E --> paired performance
```

Exact obligations depend on the owning component. Shared EGL/GL/shader/lowering changes require both backend regressions even if one backend exposed the bug first.

A failed cheap prerequisite blocks promotion to expensive evidence until the failure is explained. Device/performance lanes are not debugging starting points.

## Focused oracle routing

`docs/agent/oracles.json` maps stable semantic topics/components to existing small tests. `scripts/agent-context.py` combines that index with task text and changed-path ownership.

The index is navigation, not truth: source and the test body remain authoritative. Add an oracle-index entry only for a stable reusable test, not a one-off experiment.

## Temporary workflows

A branch-specific workflow is acceptable only when the existing evidence plane cannot answer a concrete experiment. Before that branch is retired, its value must converge into one of:

- a focused reusable oracle;
- a durable existing workflow capability;
- exact artifact/provenance referenced by a PR/ledger;
- a controlled negative result that eliminates a hypothesis.

Do not merge experiment/candidate/replay/apply/recovery workflows into the clean tree by inertia.

CI must remain read-only with respect to repository source. Source mutations belong in explicit commits/PRs.

## Evidence tiers

The detailed claim vocabulary is in `docs/evidence-model.md`. In shorthand:

- static/build boundary;
- focused semantic oracle;
- native backend runtime;
- host/presentation integration;
- Minecraft E2E;
- physical-device/longevity/performance.

A higher tier does not erase the need for a lower reusable regression. Real Minecraft may expose the bug, but once understood, the durable semantic rule should usually be protected below E2E too.

## Failure interpretation

Classify a red job before rerunning:

- product semantic/implementation failure;
- oracle/harness defect;
- dependency/toolchain incompatibility;
- environment/capability absence;
- transient infrastructure failure.

Only rerun unchanged when evidence supports the last category. Repeated reruns are not a substitute for a causal model.

## Workflow change checklist

Before adding or changing CI, answer:

1. Which exact claim and proof subject does this uniquely establish?
2. Which semantic/component owner does it observe?
3. Can an existing focused oracle + evidence plane express it?
4. Is the source/tree/binary identity explicit?
5. Is it read-only?
6. What cheaper prerequisite must pass first?
7. What lets this machinery be removed or folded back later?

The optimal CI graph is the smallest graph that gives an agent exact, falsifiable feedback at every important boundary.
