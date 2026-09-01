# Documentation map

Mithril documentation is organized by **authority and lifetime**, not by the date somebody happened to write a note.

## Stable system contracts

Read these when deciding architecture or ownership:

- `system-model.md` — abstraction tower and dependency direction.
- `evidence-model.md` — claim/evidence identity and proof tiers.
- `branches.md` — clean versus disconnected history and convergence policy.
- `ci.md` — durable evidence-plane ownership.
- `contracts/amethyst-host-contract.md` — EGL/LWJGL/Apple host seam.

These describe invariants. Shipping source/tests outrank them when there is a conflict; repair the document rather than inventing a second truth.

## Machine-readable agent control

- `agent/manifest.json` — component/path ownership, semantic boundaries, canonical branch roles and proof profiles.
- `agent/proof-graph.json` — proof prerequisites and cross-backend blast-radius rules.
- `agent/oracles.json` — focused-oracle routing index.
- `agent/branch-families.json` — stable branch lifecycle/provenance classification plus merged-PR accounting where known; contains no live HEAD table.
- `agent/migration-queue.json` — semantic convergence memory: `items` are unresolved questions, `accounted_findings` are historical strategies already translated/rejected/not currently required.
- `../scripts/agent-context.py` — compiles normal task-local context and conditionally projects migration memory only for history-sensitive work.
- `../scripts/audit-branches.py` — compiles live Git topology and annotates lifecycle families.
- `../scripts/validate_agent_contract.py` — validates the control plane.

Machine-readable control is a router over source/tests/Git/evidence, not a second renderer truth.

## Current state

- `agent/status.md` — dated product/convergence checkpoint. It may become stale and must not outrank source, live Git or exact evidence.
- live branch state is **not** a document. Generate it with `scripts/audit-branches.py --fetch-graph`.
- `agent/branch-ledger.md` is a stable convergence/navigation contract explaining how topology, families and semantic memory compose; it intentionally does not copy current SHAs.

## Capability/reference inventories

- `directmetal-gl33-semantic-matrix.md` — DirectMetal semantic support ledger.
- `gl33_core_list.md` — GL 3.3 function/domain inventory.
- `egl_list.md` — EGL symbol inventory.
- `performance_hotpath.md` — performance-shape notes where present on the active integration line.
- `minecraft-reference.md` — local Minecraft reference-source materialization.

## Historical material

`history/` contains frozen checkpoints and superseded ledgers. These are provenance only. A historical branch ledger may contain exact SHAs that were true once and false immediately after the next merge.

PRs, commits and workflow artifacts are the preferred home for experiment-specific narratives. Promote durable lessons into an invariant, type, test, oracle, branch-family accounting record, open migration item, accounted finding or ADR.

## Reading rule

For normal work, do not read this tree breadth-first:

```bash
python3 scripts/agent-context.py --task "<task>"
```

If history affects the decision, add:

```bash
python3 scripts/audit-branches.py --fetch-graph --markdown
```

The context compiler will project matching migration memory. Read raw branch history only to answer a remaining concrete question, then distill the answer back into durable semantic memory.
