# Agent status snapshot

As of: 2026-09-01 (pre-change repository audit)

Scope note: the audit found 55 pre-existing branch refs. PR #38 creates temporary branch `architecture/agent-operating-model-20260901`; while that PR is open the live repository therefore has one additional ref. The temporary architecture branch is not counted as part of the 55-branch input set and should be deleted after convergence.

This file is intentionally transient. It answers “where is the work now?” while `docs/system-model.md` answers “what is the system?” Update this snapshot whenever branch ownership, a major merge, or a release-quality evidence gate materially changes.

## Executive state

- Shipping baseline: `main` at `32ee89a041649231a1e6c328fad8ce8ca1b11415`.
- Clean-tree DirectMetal integration: `integration/directmetal-next` at `296ee3b14ef2753e4abe8d4853baae38b84a6cb2`.
- Those two refs are currently divergent. A GitHub compare on this snapshot reports `integration/directmetal-next` 150 commits ahead and 10 commits behind `main`, with merge base `4b02c653795d4067a0a3820ef9c0e4e615d7312c`.
- `integration/directvulkan-reference` and `integration/legacy-capability-port` currently have no common Git ancestor with `main`. They are legacy migration/reference histories, not clean-tree merge bases.
- The pre-change repository audit covers 55 branch refs. Most dated `experiment/*`, `codex/*`, `ci/*`, `fix/*` and `perf/*` refs are evidence or reconciliation sources, not independent product lines.

## DirectMetal frontier

`integration/directmetal-next` is the current clean-tree development line for native Metal work.

### Current shared-contract shape

The code already supports the system-model direction rather than merely aspiring to it:

- `src/backend/types.h` explicitly defines backend-neutral descriptions as **resolved GL semantics** with no Vulkan or Metal handles.
- Hot draw/resource data carries stable lifetime IDs, content versions, previous versions and partial-update ranges where native resources can outlive frontend mutation. `DrawParams` also captures pipeline/dynamic state and borrowed UBO/texture views with an explicit synchronous-Draw lifetime rule.
- `src/backend/backend.cpp` is primarily a narrow backend dispatcher over that shared contract instead of a second semantic model.
- `MetalDeviceSession` is deliberately a process-level shared migration seam around the mature Metal engine. Its own contract says the renderer still lives behind the older engine implementation and that the abstraction does **not** yet imply multiple independent Metal devices/sessions.

Implication for future work: preserve and strengthen this seam. Do not let a native backend reach back into mutable GL state after lowering, and do not assume `MetalDeviceSession` already solved complete device/session ownership merely because the class name exists.

Important recent convergence facts:

- PR #35 (`fix/minecraft26-directmetal-runtime-closure-20260819`) was merged into `integration/directmetal-next`; merge result is current integration head `296ee3b14ef2753e4abe8d4853baae38b84a6cb2`.
- That PR recorded green final CI for DirectMetal macOS semantics/boundary, iPhoneOS arm64 packaging and Vulkan reference regression on its final product changes, while explicitly leaving long device soak/memory-pressure stability as a separate claim.
- PR #34 (`fix/directmetal-incomplete-fbo-20260819`, head `a09c8227be9ba599bab0747326f199770f8ebd91`) remains open and mergeable into `integration/directmetal-next`; it is a distinct fail-closed incomplete-framebuffer candidate and must not be treated as absorbed merely because a later-numbered runtime PR was merged.
- Performance refs from the 2026-08-17/18 sequence often remain even after squash-merging their PRs. For example PR #22 and PR #32 are merged, but their source refs are not ancestors of the current integration head. Use PR/tree evidence, not ancestry alone, to decide deletion or semantic containment.

## DirectVulkan frontier

The recent DirectVulkan Minecraft work is split between the clean `src/vk/*` reference implementation on `main` and a much larger disconnected legacy tree using `Mithril-Wrapper-cpp/*`.

Important facts:

- `integration/directvulkan-reference` at `c54927fd8e17a702fa6517c4c1074635de68285a` is a legacy reference/convergence line created in August, but it has no common ancestor with `main`.
- PR #16 from `fix/directvulkan-mc262-gui-closure` to that reference line remains open and currently reports non-mergeable. It therefore cannot serve as a reliable current convergence mechanism without reconciliation.
- `ci/minecraft-on-mithril-e2e-vulkan-20260826` at `6d2354c05593ba3c5ce8d24cd9029f4c4a64cfe3` is the base of the later production-GUI investigation family.
- `codex/dvk-gui-production-20260830` at `cd89c481a0c4f0b91db996f926ce3a8db68dae34` is a direct descendant of that 2026-08-26 branch by 36 commits. Its delta includes DirectVulkan GUI/PBO/unpack/readback/orientation work plus multiple experiment workflows.
- `codex/dvk-ios-fbo-orientation-20260831` diverges from the GUI-production line rather than cleanly superseding it; its observed delta is predominantly iPhoneOS FBO-orientation candidate workflows.
- `codex/dvk-ios-artifact-20260831` is packaging/evidence-oriented. Its workflow verifies an iPhoneOS arm64 DirectVulkan artifact against an exact native baseline and static MoltenVK payload; do not interpret that branch as a new product implementation line.
- The A11 `codex/dvk-a11-single-mvk-shader-oracle-20260831` / `ci/minecraft-on-mithril-e2e-vulkan-a11-oracle-20260831` refs are E2E trigger/evidence branches. Their HEAD commits primarily record an exact source SHA and launch cloud E2E; they are not, by themselves, a semantic merge target.

### DirectVulkan architectural decision

Do not attempt to make the disconnected legacy tree the permanent architecture. Use it as a source of verified semantics and real-Minecraft oracles, then port those semantics into the clean `src/gl/*` / `src/backend/*` / `src/vk/*` tower with focused regressions.

When a sustained clean-tree Vulkan port begins, create its integration ref from the clean shipping lineage. Do not reuse the name `integration/directvulkan-reference` as if its ancestry were clean unless the history is deliberately replaced/recreated and the ledger records that event.

## Agent tooling frontier

PR #37 created a useful one-command Minecraft 26.2 reference-source materializer and a small `AGENTS.md`, but it was merged into the legacy `fix/dvk-gui-text-render-20260829` line rather than `main`. The agent-operating-model change ports the reusable tooling and expands the entrypoint on the clean shipping lineage.

The same change adds `scripts/audit-branches.py` so agents can regenerate live branch graph facts instead of relying on this dated snapshot. The deep mode fetches commit graphs with `--filter=blob:none` and never checks out or edits product branches.

## Repository hygiene priorities

1. Make this status + branch ledger the authoritative audited snapshot, and use the live branch-audit tool when current topology affects a decision.
2. Reconcile `main` and `integration/directmetal-next` deliberately; do not allow the shipping baseline to remain indefinitely detached from the clean integration line.
3. Keep recent DirectVulkan legacy branches until their unique semantics, tests and evidence are accounted for; then retire redundant experiment/evidence refs aggressively.
4. When two refs point to the same SHA, retain only the name that best expresses durable ownership after checking PR/artifact references. Current example: `fix/dvk-pixel-unpack-state-20260829` and `experiment/dvk-full-unpack-combined-20260829` both point to `b6bc7b04ccb3d92a859e1a80959a044a77d62e4d`.
5. Keep branch-specific temporary workflows out of the clean tree. Port durable oracles into the existing evidence planes instead.

## What changes this snapshot

Update this file when any of the following occurs:

- `main` or a canonical clean integration line is promoted/rebased/replaced;
- DirectVulkan obtains a clean-tree integration line;
- a major legacy family is fully reconciled and deleted;
- a release-quality Minecraft/device gate changes the product claim;
- the branch-role assumptions above become false.
