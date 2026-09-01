# Documentation map

Mithril documentation is organized by **authority and lifetime**, not by the date somebody happened to write a note.

## 1. Stable system contracts

Read these when deciding architecture or ownership:

- `system-model.md` — abstraction tower and dependency direction.
- `evidence-model.md` — claim/evidence identity and proof tiers.
- `branches.md` — clean versus disconnected history-universe policy.
- `ci.md` — durable evidence-plane ownership.
- `contracts/amethyst-host-contract.md` — EGL/LWJGL/Apple host seam.

These documents describe invariants. If they conflict with shipping source/tests, the executable implementation wins and the document must be repaired.

## 2. Machine-readable agent control

- `agent/manifest.json` — component/path ownership, semantic boundaries, branch roles and proof profiles.
- `agent/proof-graph.json` — proof prerequisites and cross-backend blast-radius rules.
- `agent/oracles.json` — small focused-oracle routing index.
- `../scripts/agent-context.py` — compiles task-local context.
- `../scripts/audit-branches.py` — compiles live Git topology.
- `../scripts/validate_agent_contract.py` — validates this control plane.

Machine-readable control is a router over source/tests/evidence, not a second renderer truth.

## 3. Current but explicitly dated state

- `agent/status.md` — current convergence checkpoint.
- `agent/branch-ledger.md` — branch reconciliation/provenance snapshot.

These files may become stale. Never prefer them over live Git, source, tests or exact evidence.

## 4. Capability/reference inventories

- `directmetal-gl33-semantic-matrix.md` — DirectMetal semantic support ledger.
- `gl33_core_list.md` — GL 3.3 function/domain inventory.
- `egl_list.md` — EGL symbol inventory.
- `performance_hotpath.md` — performance-shape notes where present on the active integration line.
- `minecraft-reference.md` — local Minecraft reference-source materialization.

## 5. Historical material

`history/` contains frozen milestones/plans that remain useful for provenance but must not drive current implementation without re-validation.

Pull requests, commits and workflow artifacts are the preferred home for experiment-specific narratives. A durable lesson should be promoted out of history into an invariant, type, test, oracle or ADR.

## Reading rule

For normal agent work do **not** read this entire tree. Start with:

```bash
python3 scripts/agent-context.py --task "<task>"
```

Then read only the routed stable contract, source slice, nearby oracle and the exact evidence plane needed for the claim.
