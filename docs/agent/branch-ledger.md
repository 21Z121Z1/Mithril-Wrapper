# Branch convergence map

This file is deliberately **not** a branch inventory and contains no live HEAD SHA table.

Mithril has enough historical refs that manually copying branch state into prose creates a second, stale source of truth. Live topology, lifecycle classification and unresolved semantics therefore have separate authorities.

## Read branch state in this order

1. **Live Git topology**

   ```bash
   python3 scripts/audit-branches.py --fetch-graph --markdown
   ```

   This is the authority for current refs, HEAD/tree identity, ancestry, nearest history anchor, duplicate heads/trees and descendant coverage.

2. **Branch lifecycle family** — `docs/agent/branch-families.json`

   This answers what a branch name is *for*: canonical clean line, legacy migration anchor, experiment, evidence subject, performance provenance, temporary review branch, and so on. A family classification never proves semantic containment or authorizes deletion.

3. **Unresolved semantic work** — `docs/agent/migration-queue.json`

   This is the only curated queue for cross-branch behavior that still needs a clean owner, focused oracle, proof, or explicit rejection. Work-item identity is semantic, not a branch name.

4. **Historical snapshot** — `docs/history/branch-ledger-2026-09-01.md`

   Use only when reconstructing how the repository arrived here. Its SHAs and interpretations are frozen provenance and may be stale immediately after publication.

## Canonical rule

The clean shipping universe should remain structurally simple:

```text
main -> integration/directmetal-next
```

`main` is the shipping baseline. `integration/directmetal-next` is the active clean DirectMetal development line. After a control/governance change lands on `main`, converge it into the active integration line with an explicit reviewed merge while preserving integration product work; do not allow the two canonical clean refs to become long-lived parallel histories.

The legacy anchors `integration/directvulkan-reference` and `integration/legacy-capability-port` belong to a disconnected history universe. Their descendants are semantic/oracle/provenance sources only. Clean adoption means a focused transplant into the owning `src/*` abstraction plus exact proof, never wholesale historical-tree promotion.

## Retirement rule

A generated topology fact such as `covered_by`, `same_tree_as` or `duplicate_head` is useful evidence but is not deletion authorization.

A non-canonical ref is a retirement candidate only when:

- its lifecycle family permits retirement;
- no unresolved migration item cites unique behavior from it;
- merged-PR/tree equivalence or explicit semantic rejection accounts for its unique delta;
- important artifacts/provenance are durable outside the branch;
- no open PR, workflow or external process still depends on the ref.

Branch deletion is an explicit repository-maintenance action. The agent control plane reports candidates; it does not silently delete refs.

## Why this shape

The repository should scale by accumulating contracts and evidence, not by accumulating required reading. Ten experiments about one rendering invariant should converge into one semantic migration item and eventually one clean oracle/implementation. A future agent should reason about that invariant once, while Git preserves every experiment as provenance until retirement is separately authorized.
