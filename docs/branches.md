# Branch and convergence policy

Mithril-Wrapper has one product architecture but more than one historical Git lineage. Branch policy exists to prevent that history from becoming architecture.

For the dated live inventory, read `docs/agent/branch-ledger.md`. For the current frontier, read `docs/agent/status.md`. This document defines the stable rules.

## Canonical roles

### `main` — shipping baseline

`main` is the clean-tree shipping baseline. It is the default destination for promoted, release-worthy architecture and repository governance.

`main` is not defined as “the branch with the most commits.” A clean integration branch may be newer while a deliberate promotion is pending.

### `integration/directmetal-next` — clean DirectMetal integration

This is the current clean `src/*` development line for DirectMetal product work.

Feature/fix/performance branches that target it should be short-lived. Once a PR is merged and unique artifacts are accounted for, source refs should normally be deleted even if squash merge makes them remain Git-divergent.

### `integration/directvulkan-reference` — legacy DirectVulkan migration source

Despite the historical name, this ref is currently disconnected from `main` and therefore is **not** the canonical clean-tree Vulkan development base.

Its purpose is to preserve/reconcile verified DirectVulkan semantics, diagnostics and tests from the legacy lineage until those capabilities are represented in the clean `src/*` architecture.

### `integration/legacy-capability-port` — legacy semantic reconciliation source

This ref is also disconnected from `main`. It is a temporary knowledge-convergence line for older GL/Metal/Minecraft semantics that still require explicit accounting.

It must not be merged wholesale into the clean product tree.

## Canonical ownership is not Git ancestry

This repository uses squash merges and contains disconnected histories. Therefore two distinct questions must never be conflated:

1. Which branch owns future work in this area?
2. Is another branch an ancestor / already semantically contained?

A branch may have an obsolete ownership role while still containing unique semantics. A squash-merged feature may be semantically contained while not being an ancestor. A branch may have a canonical-sounding name while sharing no ancestor with `main`.

Before reusing, deleting or declaring a divergent branch absorbed, establish the three-part proof:

1. **ancestry** — ancestor, diverged, squash-merged, or explicit no-common-ancestor;
2. **semantic delta** — changed files, tests and observable behavior that remain unique;
3. **evidence** — where the current exact implementation is proved.

Branch date and commit count are only search hints.

## Clean tree versus legacy tree

The clean product architecture is rooted at `src/*` and the current mainline CMake model.

The older DirectVulkan/dual-backend family is commonly rooted at `Mithril-Wrapper-cpp/*` and may carry different CMake/workflow organization. It is a source of hard-won behavior, not a structure to preserve forever.

When a legacy branch contains a valid fix:

1. identify the GL/EGL semantic rule or backend-lifetime rule the fix represents;
2. identify the smallest clean-tree owning layer;
3. add/port a focused oracle in the clean test architecture;
4. implement the rule in `src/*` at the correct layer;
5. run clean-tree evidence planes;
6. record the legacy source branch/commit in the PR for provenance;
7. retire the legacy branch when all unique value is accounted for.

Do not mechanically transplant file layouts, duplicated backend semantics or one-off workflows.

## Creating branches

New long-lived integration refs are exceptional. Prefer:

- `fix/<scope>-<date>` for a bounded correctness repair;
- `perf/<scope>-<date>` for a bounded performance phase;
- `experiment/<hypothesis>-<date>` for an A/B or diagnostic hypothesis;
- `validation/<claim>-<date>` for a harness/evidence-only branch;
- `tooling/<capability>` for reusable developer/agent tooling.

An experiment must have an exit route at creation time: merge/port a rule, preserve a negative result, or delete after falsification.

When the clean-tree Vulkan migration becomes sustained product development, create a new integration branch **from the clean shipping lineage** and record its ownership in `docs/agent/manifest.json` and status. Do not silently repurpose the disconnected historical ref.

## Pull requests

A PR description should make four things discoverable:

- owning layer / behavior being changed;
- source and destination tree/branch;
- exact semantic or performance oracle;
- which evidence is already complete versus still required.

Validation-only PRs must say so explicitly and must identify the exact product SHA under test when it differs from the harness HEAD.

An open PR is not proof of active ownership. A merged PR is not proof that its source ref can be deleted until post-merge unique commits and external references are checked.

## CI ownership

Branches reuse durable evidence planes wherever possible. Do not create a permanent workflow per fix/experiment.

Temporary branch-specific workflows are acceptable during investigation only when the existing workflow cannot express the experiment. Before retirement, move any durable oracle/capability into the normal evidence architecture and delete the temporary workflow with the branch.

See `docs/ci.md` and `docs/evidence-model.md`.

## Branch retirement

Delete branches aggressively **after** their information has converged, not merely because they look old.

Required checks:

- no unresolved unique implementation/test delta;
- PR state and squash-merge semantics understood;
- no workflow, artifact note or automation depends on branch name;
- important provenance remains in PR/commit history;
- branch ledger/status updated.

Exact duplicate refs are strong retirement candidates, but still check open PRs and external references before deleting either name.

## Desired steady state

The target topology is small:

```text
main
  |
  +-- integration/directmetal-next      (only while ahead work exists)
  |
  +-- <future clean Vulkan integration> (only while sustained port work exists)

legacy reconciliation sources           (temporary, shrinking)
short-lived fix/perf/experiment refs      (bounded, evidence-backed)
```

The system should accumulate capabilities in contracts/tests/types/docs, not accumulate permanent branches as memory.
