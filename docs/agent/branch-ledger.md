# Branch convergence map

This file is deliberately **not** a branch inventory and contains no live HEAD SHA table.

Mithril has enough historical refs that copying branch state into prose creates a second, stale source of truth. Live topology, lifecycle classification, open semantic work, accounted negative findings and historical reconstruction therefore have separate authorities.

## Read branch state in this order

1. **Live Git topology**

   ```bash
   python3 scripts/audit-branches.py --fetch-graph --markdown
   ```

   Authority for current refs, HEAD/tree identity, ancestry, nearest history anchor, duplicate heads/trees and descendant coverage.

2. **Branch lifecycle family** — `docs/agent/branch-families.json`

   Answers what a branch name is *for*: canonical clean line, legacy migration anchor, experiment, evidence subject, absorbed provenance, temporary review branch, and so on. A family classification never proves semantic containment or authorizes deletion.

3. **Semantic convergence memory** — `docs/agent/migration-queue.json`

   `items` contains only unresolved cross-branch semantic/oracle/proof questions. `accounted_findings` records conclusions already extracted from historical code, including implementation strategies that should be translated rather than transplanted or are not current clean requirements. Both use semantic identity rather than branch identity.

4. **Historical snapshot** — `docs/history/branch-ledger-2026-09-01.md`

   Use only when reconstructing how the repository arrived here. Its SHAs and interpretations are frozen provenance and may be stale immediately after publication.

## Canonical rule

The clean shipping universe should remain structurally simple:

```text
main -> integration/directmetal-next
```

`main` is the shipping baseline. `integration/directmetal-next` is the active clean DirectMetal development line. After a control/governance change lands on `main`, converge it into the active integration line with an explicit reviewed merge while preserving integration product work; do not allow the two canonical clean refs to become long-lived parallel histories.

The legacy anchors `integration/directvulkan-reference` and `integration/legacy-capability-port` belong to a disconnected history universe. Their descendants are semantic/oracle/provenance sources only. Clean adoption means a focused transplant into the owning `src/*` abstraction plus exact proof, never wholesale historical-tree promotion.

## Semantic accounting rule

Reading a historical branch should reduce future archaeology. Every high-confidence conclusion should become one of:

- an open migration item with clean owner/oracle/proof/exit condition;
- an accounted finding explaining how the legacy idea maps to the clean architecture or why it should not be transplanted;
- merged-PR accounting on an absorbed-provenance branch family;
- a stable source/test/contract change.

Do not leave the only useful conclusion trapped in a chat transcript or branch name.

## Retirement rule

A generated topology fact such as `covered_by`, `same_tree_as` or `duplicate_head` is evidence but not deletion authorization.

A non-canonical ref is a retirement candidate only when:

- its lifecycle family permits retirement;
- no open migration item cites unique behavior from it;
- merged-PR/tree equivalence, an accounted finding or explicit semantic rejection accounts for its unique delta;
- important artifacts/provenance are durable outside the branch;
- no open PR, workflow or external process still depends on the ref.

Branch deletion is an explicit repository-maintenance action. The agent control plane reports candidates; it does not silently delete refs.

## Why this shape

The repository should scale by accumulating contracts and evidence, not required reading. Ten experiments about one invariant should converge into one semantic item and eventually one clean oracle/implementation or one durable rejection. A future agent should reason about that invariant once while Git remains provenance until retirement is separately authorized.
