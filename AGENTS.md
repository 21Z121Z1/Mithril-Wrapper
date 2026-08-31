# Mithril-Wrapper agent operating contract

This file is the entry point for coding and review agents. Do not begin by reading the repository breadth-first or by treating branch names as state. Compile the smallest current world model first, then drill into the subsystem that owns the requested behavior.

## Bootstrap and context budget

Start with the task-local capsule:

```bash
python3 scripts/agent-context.py --task "<short task>"
```

It reports exact Git identity, likely owning layer/component, changed paths when a comparable anchor is available, boundary risk, proof obligations and the smallest set of files to read next.

If the task depends on branch ancestry, migration, replay, "latest branch", absorption or cleanup, refresh the live graph before trusting any dated snapshot:

```bash
python3 scripts/audit-branches.py --fetch-graph --markdown
```

The audit is read-only with respect to product refs/worktrees. It refreshes remote-tracking commit graphs with `--filter=blob:none`, discovers history-universe membership/nearest anchors and does not check out every branch.

Then use progressive disclosure:

1. `docs/system-model.md` — stable abstraction tower and invariants.
2. task-routed source/tests returned by `agent-context.py`.
3. `docs/evidence-model.md` when the claim needs runtime/presentation/performance proof.
4. `docs/branches.md` when convergence/branch lifecycle matters.
5. `docs/agent/status.md` / `docs/agent/branch-ledger.md` only as dated reconciliation checkpoints, cross-checked against the live graph.
6. historical PRs, workflows, patch/replay scripts and raw artifacts only to answer a concrete unresolved question.

`docs/agent/manifest.json` is the machine-readable map behind the capsule. `README.md` and `CHECKLIST.md` remain useful product/history references, but they are not current branch/runtime authority by themselves.

## System invariants

- The shipping architecture is the clean `src/*` tree. Do not copy a legacy `Mithril-Wrapper-cpp/*` tree wholesale into it.
- Apple shipping uses the Vulkan-free DirectMetal target. Vulkan is a separate reference/fallback backend and must not leak into the DirectMetal build boundary.
- Observable GL/EGL/Minecraft semantics belong above backend execution whenever possible. Backends consume an explicit lowered contract; they must not independently invent different API semantics.
- `src/backend/*` is the architectural lowering seam: mutable frontend state is resolved into explicit draw/resource/state identity before native execution.
- Queued native work must not reread mutable GL state after the observing operation has captured its semantic snapshot.
- Resource reuse must be keyed by stable generation/lifetime/content identity where native work may outlive frontend mutation.
- Internal framebuffer/texture storage semantics stay stable; platform origin or RGBA/BGRA conversion belongs at an explicit presentation seam.
- Unsupported observable behavior fails closed. A backend capability gap must not silently approximate GL semantics.
- `main` is the shipping baseline, not automatically the newest implementation. `integration/directmetal-next` is the current clean-tree DirectMetal integration line.
- `integration/directvulkan-reference` and `integration/legacy-capability-port` are disconnected Git-history anchors relative to `main`. Treat their universe as semantic/evidence transplant source, not a direct clean-tree merge base.
- A branch name, date, green workflow, PR title or existence of a ref is not proof that behavior is current or absorbed.
- Never claim a capability for candidate B from evidence produced by candidate A unless relevant source/tree/binary equivalence is explicitly established.

## Branch selection protocol

Before editing:

1. Generate the task capsule.
2. Identify the product tree (`src/*` clean tree versus `Mithril-Wrapper-cpp/*` legacy tree).
3. Identify the owning abstraction layer/component.
4. Refresh live topology if branch state affects the decision.
5. Determine the intended destination branch from stable policy plus live topology; use dated status only as supporting context.
6. If reusing another branch, establish:
   - its Git history universe and ancestry/explicit lack of common ancestry;
   - its unique changed-file / semantic delta;
   - the exact product subject and evidence attached to that delta.
7. Prefer a semantic port with a focused regression over a wholesale merge from a disconnected legacy tree.

Do not infer containment from age or commit count. Squash merges make ancestry insufficient in the other direction; use PR/tree/semantic evidence as well.

## Investigation loop

Use the shortest closed evidence loop:

`observable failure -> owning contract -> smallest oracle -> implementation -> exact-subject verification -> broader gate`

Start from an existing oracle. If no oracle can distinguish the bug from adjacent behavior, add one before changing implementation when practical. Do not create a new one-bug/one-workflow Actions file when an existing evidence plane can host the oracle.

For Minecraft 26.2 behavior, materialize the local reference source instead of guessing from logs:

```bash
SRC="$(bash scripts/minecraft-reference.sh --print-path)"
```

The generated `.minecraft-reference/` tree is local analysis input only. Never commit or upload it as an artifact. See `docs/minecraft-reference.md`.

## Change placement

- EGL/host ABI and surface lifecycle: `src/egl/*` plus presentation tests.
- GL state and observable API semantics: `src/gl/*`, `src/state/*` plus semantic smoke tests.
- Shader semantic translation/reflection: `src/shader/*` and GL shader/program code.
- Backend-neutral resource/draw contract: `src/backend/*`.
- DirectMetal execution only: `src/metal/*`.
- Vulkan reference execution only: `src/vk/*`.
- Cross-platform test registration: `cmake/MithrilSmokeTests.cmake`.
- Durable normal CI: `.github/workflows/build.yml`; unique hosted Apple runtime evidence: `hosted-metal-gpu-probe.yml`.
- Legacy/replay source: `Mithril-Wrapper-cpp/*`, `ci/*`; extract semantics/oracles rather than preserving its architecture.

If a change seems to require the same semantic rule in both `src/metal/*` and `src/vk/*`, first check whether the rule belongs in the shared frontend/backend-neutral contract instead.

## Verification ladder

Use the smallest applicable prefix, then expand:

1. agent/manifest/control invariants;
2. focused regression for the changed contract;
3. backend semantic label (`ctest -L directmetal` or `ctest -L vulkan`);
4. shipping boundary/build checks;
5. host/presentation runtime evidence when the seam is touched;
6. Minecraft E2E when behavior depends on real client call patterns;
7. physical-device/long-run evidence only for claims hosted CI cannot establish;
8. paired/fixed-workload performance evidence only after correctness and activation are established.

Compilation is not activation. Activation is not semantic correctness. Semantic correctness is not physical presentation correctness. One average FPS number is not performance acceptance.

On pull requests distinguish **candidate-head proof** from **synthetic merge-result proof**. Record which object a workflow actually checked out; a check attached to a PR is not automatically a check of the PR head.

A task is not complete merely because code exists. Record what was actually verified, exact source/tree/binary identity, and any remaining evidence gap.

## Knowledge accumulation

Put information at the narrowest durable layer:

- stable architecture/invariants -> `docs/system-model.md`;
- current frontier/blockers -> dated `docs/agent/status.md` only when a snapshot is useful;
- live branch topology -> generated `scripts/audit-branches.py` output;
- branch reconciliation decisions -> `docs/agent/branch-ledger.md` plus PR history;
- ownership/boundary/proof routing -> `docs/agent/manifest.json` + validator/self-tests;
- proof rules -> `docs/evidence-model.md`;
- subsystem contract -> focused source-adjacent document/test;
- historical experiment detail -> PR/commit/artifact, referenced from the ledger only if still decision-relevant.

A durable result should be compiled into a type, invariant, test, manifest rule, oracle or evidence contract. Do not grow `README.md`, `CHECKLIST.md`, status, ledger or this file into append-only incident logs.
