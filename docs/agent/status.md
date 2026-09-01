# Agent status checkpoint

As of: 2026-09-01 (agent-control-plane promotion / clean-line convergence checkpoint)

This file is deliberately a dated checkpoint, not live repository state. Never choose, merge, retire or declare a branch absorbed from this document alone. For exact current heads and ancestry run:

```bash
python3 scripts/audit-branches.py --fetch-graph --markdown
```

For a task-local model run:

```bash
python3 scripts/agent-context.py --task "<task>"
```

## Current ownership

- `main` is the clean shipping/governance baseline. PR #38 promoted the agent-operating model, multi-universe branch compiler, task-context compiler and exact-subject evidence rules to `main` on 2026-09-01.
- `integration/directmetal-next` is the active clean-tree DirectMetal product line. Its product/runtime tree is newer than the shipping baseline and should carry the same agent/evidence control plane while it remains active.
- `integration/directvulkan-reference` and `integration/legacy-capability-port` are disconnected legacy-history anchors. They are semantic/oracle/provenance sources, not clean-tree merge bases.
- Dated `experiment/*`, `codex/*`, `ci/*`, `fix/*`, `perf/*`, `validation/*` and tooling refs are not independent product lines merely because the refs still exist.

## Stable DirectMetal direction

The latest clean DirectMetal work already follows the intended architecture:

- `src/gl/*`, `src/state/*` and `src/shader/*` own observable semantics;
- `src/backend/*` is the resolved-intent seam carrying explicit draw/resource/state and lifetime/version identity;
- `src/metal/*` executes that contract without owning generic GL policy;
- `src/vk/*` remains the clean reference/fallback execution path;
- presentation/host behavior is verified independently because correct offscreen semantics do not prove CAMetalLayer/device presentation.

Recent DirectMetal performance PRs #22-#32 were merged through squash-style history. Their source refs can therefore remain Git-divergent after semantic absorption; use PR/tree/oracle evidence rather than ancestry alone when retiring them.

PR #35 (Minecraft 26.2 DirectMetal runtime closure) is merged into `integration/directmetal-next`. PR #34 (fail-closed incomplete framebuffer operations) remains a distinct open product candidate and must be reconciled explicitly rather than assumed absorbed.

## DirectVulkan migration frontier

Recent DirectVulkan Minecraft work remains largely in the disconnected `Mithril-Wrapper-cpp/*` universe. Important families include GUI/PBO/pixel-unpack/readback/orientation work, iPhoneOS/A11 validation, rollout replay and many A/B experiment refs.

Do not promote that historical architecture wholesale. For each valuable result:

1. identify the observable GL/shader/lifetime rule;
2. preserve or create the smallest falsifiable oracle;
3. map the rule to the clean L1-L5 owner;
4. implement it through the clean resolved-intent seam;
5. verify exact clean product identity;
6. retain the legacy SHA only as provenance once represented durably.

The live branch compiler should be used to identify nearest lineage anchors, descendants, same-tree aliases and evidence-only deltas before spending context on raw history.

## Reconciliation queue

Highest-value unresolved convergence work is currently:

1. keep the clean `main -> integration/directmetal-next` history relation simple whenever governance/control changes are promoted;
2. resolve PR #34 against the current DirectMetal integration tree with focused framebuffer semantics and exact-SHA evidence;
3. mine the DirectVulkan legacy family into clean semantic/oracle transplants instead of accumulating another permanent backend tree;
4. resolve the two explicitly remaining PR #17 questions with focused oracles: `VK_FORMAT_X8_D24_UNORM_PACK32` depth-only aspect semantics and normalized 32-bit integer vertex-input behavior;
5. retire redundant experiment/evidence refs only after semantic delta, proof location and external references are accounted for.

## Documentation rule

Stable meaning belongs in `docs/system-model.md`, `docs/evidence-model.md`, source-adjacent contracts, tests and `docs/agent/manifest.json`. This file only records a useful convergence checkpoint. If its narrative conflicts with Git, source, tests or exact evidence, the stronger executable/live source wins.
