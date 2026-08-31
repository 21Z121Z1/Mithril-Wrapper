# Evidence model

Mithril-Wrapper uses CI as an evidence system, not as a collection of green badges. The purpose of an evidence plane is to make a specific claim falsifiable for an exact implementation.

## Evidence tuple

A durable claim should be reducible to:

```text
claim
+ exact implementation SHA/tree identity
+ oracle
+ execution environment
+ result/output
+ artifact or reproducible log when useful
+ stated limitation
```

If one element is missing, narrow the claim rather than silently filling the gap.

A green workflow on one SHA does not validate another SHA. A packaging job does not establish rendering correctness. A synthetic smoke does not establish a real Minecraft call pattern. Device evidence does not automatically establish source-level causality.

## Evidence tiers

### E0 — static / build boundary

Examples:

- CMake configuration succeeds;
- required symbols are exported;
- shipping DirectMetal artifact contains no Vulkan/MoltenVK dependency;
- iPhoneOS Mach-O platform/architecture/install-name is correct;
- agent manifest is internally consistent.

Use E0 for structural claims. Do not promote it to a runtime semantic claim.

### E1 — focused semantic oracle

A deterministic small test exercises one GL/EGL/lowering contract and checks observable output/errors/state.

Examples: framebuffer completeness, texture level window, pixel-store/PBO behavior, shader interface linking, index semantics.

This is the preferred place to encode a bug once its root semantic rule is understood. E1 is cheap enough to run on every relevant PR.

### E2 — native backend runtime

The actual DirectMetal or Vulkan backend executes commands and the test verifies pixels/state/lifetime behavior on the applicable runtime.

Examples: `ctest -L directmetal`, `ctest -L vulkan`, real Metal/MoltenVK smoke tests.

E2 proves that lowering plus backend execution honors the focused contract in that environment.

### E3 — host / presentation integration

Exercises the seam a headless renderer cannot prove: EGL lifecycle, CAMetalLayer, drawable sizing, swap/present, launcher/host bridge, packaging and proc resolution.

Examples: `amethyst_egl_smoke`, hosted platform runtime validation.

If the bug occurs before the first present, a pure offscreen readback is insufficient evidence even if pixels are correct.

### E4 — real Minecraft E2E

Runs the production client or a production-equivalent Client GameTest against the actual wrapper and checks renderer identity, call-path oracles, framebuffer/presentation behavior and product SHA identity.

Use E4 only when real client behavior materially adds information beyond E1-E3. Keep the E1 regression anyway; E4 should not be the only way to rediscover a low-level semantic bug.

### E5 — physical-device / longevity / performance claim

Required for claims that hosted CI cannot establish faithfully: sustained memory pressure, thermal behavior, long-run stability, device-specific presentation, frame pacing or performance on target Apple silicon.

A single successful launch is not a soak. A short soak is not a performance distribution. State the window, device and workload.

## Claim scopes

Use precise language:

- **implemented** — code exists; no runtime proof implied;
- **focused-verified** — appropriate E1/E2 oracle passed;
- **platform-verified** — relevant E3 evidence passed;
- **Minecraft-verified** — E4 passed for the exact product identity;
- **device-verified** — stated E5 device/workload passed;
- **release-ready** — only when all explicitly required claim classes have corresponding evidence.

Avoid “fully fixed”, “production-ready” or “100%” when the evidence only covers a narrower layer.

## Exact-SHA identity

The strongest E3/E4/E5 harnesses should record:

- repository and commit SHA;
- artifact SHA-256 where an intermediate dylib/package is loaded;
- renderer/API identity where feasible;
- platform/runner/device identity;
- relevant dependency versions (for example MoltenVK/toolchain when they change behavior).

When a validation branch checks out a separate candidate SHA, record both the harness SHA and candidate product SHA. The harness must never let its own HEAD masquerade as the product under test.

## Evidence graph, not workflow graph

Tests belong to contracts; workflows only provide environments.

Preferred organization:

```text
contract/oracle
  -> normal build plane when portable/cheap
  -> hosted platform plane only if unique Apple runtime evidence is required
  -> Minecraft/device lane only for behavior that cannot be reduced further
```

Do not create a top-level workflow for each bug. Add the oracle to an existing plane and let CTest labels or a small lane selector express scope.

A temporary branch-specific workflow may be useful during an experiment, but its durable output must eventually become one of:

- a reusable test/oracle;
- a stable workflow capability;
- an artifact/provenance record referenced by a reconciliation ledger;
- a documented negative result that eliminates a hypothesis.

Then retire the workflow with the experiment branch.

## Negative evidence

Failed experiments are valuable only when they eliminate a hypothesis under controlled conditions. Record:

- exact compared SHAs/configurations;
- the one intended variable;
- the observed oracle difference or lack of difference;
- what conclusion is and is not justified.

An A/B branch that changes several implementation variables at once is weak evidence even if it renders differently.

## Branch reconciliation evidence

Before deleting or absorbing a divergent branch, establish:

1. **ancestry** — ancestor, diverged, squash-merged, or no common ancestor;
2. **semantic delta** — unique changed behavior/tests, not merely commit count;
3. **proof location** — which current oracle/PR/artifact now represents every still-valued behavior.

Only then mark the branch accounted-for. The repository has already demonstrated why dates alone are unsafe: merged squash branches can remain Git-divergent, while two differently named refs can point at exactly the same SHA.

## Performance evidence

Performance changes need two independent claims:

1. semantics are unchanged;
2. the intended cost shape improved.

For structural hot-path work, counters can prove that a copy/allocation/compile no longer occurs in a test scenario. For actual performance claims, report workload, runner/device, sample count, warmup, metric and distribution (for example median/p95), and compare exact SHAs under the same conditions.

Do not use GitHub-hosted relative GPU timing as a substitute for device performance unless the claim is explicitly limited to that runner environment.

## Evidence maintenance

When implementation ownership moves upward or a stronger oracle subsumes an old one:

- keep the smallest oracle that protects the semantic rule;
- remove duplicate workflow machinery;
- update `docs/agent/status.md` / branch ledger if convergence changed;
- preserve historical artifact links in PR history rather than copying long logs into design documents.

The goal is a compact evidence graph where an agent can answer: “What exactly proves this statement, on which code, and what would still falsify it?”
