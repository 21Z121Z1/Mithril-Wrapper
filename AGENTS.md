# Mithril-Wrapper agent operating contract

This file is the entry point for coding and review agents. Do not begin by reading the repository breadth-first. Build a small, verified model first, then drill into the subsystem that owns the requested behavior.

## Read order

1. `docs/system-model.md` — stable abstraction tower, ownership and invariants.
2. `docs/agent/status.md` — dated snapshot of the current product and branch frontier.
3. `docs/agent/manifest.json` — machine-readable paths, roles and evidence planes.
4. `docs/evidence-model.md` — what counts as proof and how to bind proof to an exact product SHA.
5. `docs/branches.md` and `docs/agent/branch-ledger.md` — branch policy and current inventory.
6. Only then read the subsystem code, neighboring tests and relevant CI workflow.

`README.md` and `CHECKLIST.md` are useful product/history references, but they are not the sole source of current branch truth.

## System invariants

- The shipping architecture is the clean `src/*` tree. Do not copy a legacy `Mithril-Wrapper-cpp/*` tree wholesale into it.
- Apple shipping uses the Vulkan-free DirectMetal target. Vulkan is a separate reference/fallback backend and must not leak into the DirectMetal build boundary.
- Observable GL/EGL/Minecraft semantics belong above backend execution whenever possible. Backends consume an explicit lowered contract; they must not independently invent different API semantics.
- `main` is the shipping baseline, not automatically the newest implementation. `integration/directmetal-next` is the current clean-tree DirectMetal integration line. See the dated status file before choosing a base.
- `integration/directvulkan-reference` and `integration/legacy-capability-port` are currently disconnected Git histories relative to `main`. Treat them as migration/reference sources, not direct merge bases for the clean tree.
- A branch name, date, green workflow, PR title or existence of a ref is not proof that behavior is current or absorbed.
- Never claim a capability for SHA B from evidence produced by SHA A unless the relevant tree identity is proven.

## Branch selection protocol

Before editing:

1. Identify the product tree (`src/*` clean tree versus `Mithril-Wrapper-cpp/*` legacy tree).
2. Identify the owning abstraction layer from `docs/system-model.md`.
3. Determine the intended destination branch from `docs/agent/status.md`.
4. If reusing another branch, establish all three facts:
   - Git ancestry or an explicit absence of common ancestry;
   - changed-file / semantic delta;
   - evidence attached to the exact implementation being reused.
5. Prefer a semantic port with a focused regression over a wholesale merge from a disconnected legacy tree.

Do not infer containment from branch age. Squash merges intentionally make ancestry insufficient; use PR state and final tree/diff evidence as well.

## Investigation loop

Use the shortest closed evidence loop:

`observable failure -> owning contract -> smallest oracle -> implementation -> exact-SHA verification -> broader gate`

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

If a change seems to require the same semantic rule in both `src/metal/*` and `src/vk/*`, first check whether the rule belongs in the shared frontend/backend-neutral contract instead.

## Verification ladder

Use the smallest applicable prefix, then expand:

1. source/manifest invariants and formatting;
2. focused regression for the changed contract;
3. backend semantic label (`ctest -L directmetal` or `ctest -L vulkan`);
4. shipping boundary/build checks;
5. host/presentation runtime evidence when the seam is touched;
6. Minecraft E2E when behavior depends on real client call patterns;
7. device/long-run evidence only for claims that hosted CI cannot establish.

A task is not complete merely because code exists. Record what was actually verified, what exact SHA was verified, and any remaining evidence gap.

## Knowledge accumulation

Put information at the narrowest durable layer:

- stable architecture/invariants -> `docs/system-model.md`;
- current frontier/blockers -> `docs/agent/status.md`;
- branch inventory/reconciliation -> `docs/agent/branch-ledger.md`;
- proof rules -> `docs/evidence-model.md`;
- subsystem contract -> a focused subsystem document/test near its owner;
- historical experiment detail -> PR/commit/artifact, referenced from the ledger only if still decision-relevant.

Do not grow `README.md`, `CHECKLIST.md` or this file into an append-only incident log. Promote repeated lessons into invariants, tests or machine-checkable contracts; retire duplicated narrative once the stronger representation exists.
