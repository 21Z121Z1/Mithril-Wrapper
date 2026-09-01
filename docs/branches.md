# Branch and convergence policy

Mithril-Wrapper has one product architecture but more than one historical Git lineage. Branch policy exists to prevent history from becoming architecture.

## Four authorities, four questions

Never maintain one document that tries to answer all branch questions.

1. **What refs/SHAs/ancestry exist now?**

   ```bash
   python3 scripts/audit-branches.py --fetch-graph --markdown
   ```

   Live Git is the only authority for HEAD/tree/ancestry/coverage facts.

2. **What is this ref for?**

   Read `docs/agent/branch-families.json`. It classifies lifecycle/provenance roles such as canonical, experiment, evidence, migration source and retirement candidate. Classification is not semantic containment or deletion authorization.

3. **What behavior still has not converged?**

   Read `docs/agent/migration-queue.json`. Items are semantic questions with clean destination components, oracles, proofs and exit conditions. Work-item identity is not a branch name.

4. **How did we get here?**

   Historical snapshots live under `docs/history/`. They are provenance, never current branch authority.

`docs/agent/branch-ledger.md` is the short composition contract for these layers and intentionally contains no live SHA table.

## Canonical roles

### `main`

Clean shipping baseline and stable destination for promoted architecture/governance. It is not “the branch with the most commits.”

### `integration/directmetal-next`

Active clean DirectMetal development line. Keep `main` as an actual ancestor whenever governance/control changes are promoted; do not allow two canonical clean refs to become long-lived parallel histories.

### `integration/directvulkan-reference`

Disconnected legacy DirectVulkan migration anchor. Despite its name, it is not the future clean Vulkan development base and is never wholesale-merged into `src/*`.

### `integration/legacy-capability-port`

Disconnected legacy semantic-reconciliation anchor. Preserve it as provenance/migration source until its remaining semantic questions are accounted for.

Canonical names/history universes live in `docs/agent/manifest.json`; lifecycle families live separately in `branch-families.json`.

## Across disconnected histories: semantic transplant

Use:

```text
source ref / PR / exact tree
  -> one observable semantic or native invariant
  -> smallest focused oracle
  -> clean owning abstraction
  -> exact clean-candidate proof
  -> update/close migration item
```

A giant cross-history tree diff is archaeology, not a promotion argument.

Before reusing historical work, answer independently:

- history relation: ancestor/descendant/diverged/squash-represented/no-common-ancestor;
- lifecycle role: product candidate, experiment, evidence, migration source, provenance;
- semantic delta: the unique behavior/test/contract that matters;
- proof subject: the exact implementation/evidence that now represents it.

A newer validation ref may contain no newer product semantics. A squash-merged source may stay Git-divergent. A branch may be obsolete as ownership while retaining one unique unresolved invariant.

## Migration queue discipline

A migration item is one semantic question, not a ticket for merging a branch.

Many branches can support one migration item. One comprehensive branch can support many migration items. This many-to-many relationship is intentional and prevents branch count from becoming cognitive workload.

Create an item only when there is evidence of a concrete unresolved semantic/oracle/proof question. Do not populate the queue from branch names alone.

Close an item only when one of these is explicit:

- missing behavior landed in the clean owner and required proof passed;
- current clean behavior was shown equivalent and exact evidence recorded;
- historical hypothesis was falsified/rejected and the reason is durable.

Deleting/merging one source ref does not by itself close semantic work.

## Clean versus legacy tree

The clean architecture is rooted at `src/*`. The older family commonly carries `Mithril-Wrapper-cpp/*` plus branch-local investigation workflows.

When a legacy source contains a valid fix:

1. identify the semantic/native-lifetime rule;
2. select the smallest clean owner from the manifest;
3. preserve or create the focused oracle;
4. move shared semantics above backend execution when possible;
5. run the proof DAG on the exact clean candidate;
6. update the semantic migration item;
7. keep old SHA/PR as provenance;
8. consider branch retirement separately.

Do not mechanically transplant old layouts, duplicated backend policy or one-shot workflows.

## Product, evidence and provenance subjects

Keep distinct:

- **product subject** — tree that implements behavior;
- **validation subject** — harness/workflow that tests a product subject;
- **replay/provenance subject** — reconstruction/history source.

A trigger commit is not automatically a product implementation; an evidence-only descendant does not semantically supersede a product-bearing ancestor.

## Creating branches

New long-lived integration refs are exceptional. Prefer bounded refs:

- `fix/<scope>-<date>` correctness candidate;
- `perf/<scope>-<date>` performance phase;
- `experiment/<hypothesis>-<date>` controlled hypothesis;
- `validation/<claim>-<date>` evidence-only subject;
- `tooling/<capability>` reusable tooling.

If a new naming family is introduced, add/adjust `branch-families.json` so the live audit does not leave it `UNMATCHED` indefinitely.

Every experiment needs an exit route: promote a rule/oracle, preserve a negative result, or become removable provenance.

When sustained clean-tree Vulkan development begins, create its integration ref from the clean shipping universe and explicitly change the manifest/family model. Do not repurpose the disconnected historical Vulkan anchor.

## Pull requests and CI

A PR should name owning behavior/layer, source/destination history relation, focused oracle, exact product subject and remaining proof. Validation-only PRs must say so explicitly.

CI supplies environments; tests own contracts. Candidate source and synthetic integration result are distinct proof subjects. See `docs/ci.md` and `docs/evidence-model.md`.

## Retirement

Generated facts such as `covered_by`, same-tree, duplicate-head, or a `retirement_candidate` family disposition are evidence, not deletion authorization.

Retire a non-canonical ref only after:

1. live topology is refreshed;
2. its branch family permits retirement;
3. every migration item that cites unique behavior from it is resolved/rejected;
4. PR/tree/semantic evidence accounts for its unique delta, including squash merges;
5. important provenance remains discoverable;
6. no open PR/workflow/external test depends on the branch name;
7. repository owner authorizes destructive cleanup.

Unknown/unmatched branches and unclassified deltas fail closed: improve the model or investigate rather than inferring absorption.

## Desired steady state

```text
main
  -> integration/directmetal-next
  -> <future clean integration only when actually needed>

small semantic migration queue
legacy anchors / short-lived refs as shrinking provenance
```

The repository may retain many historical refs temporarily without forcing agents to remember them. The system should accumulate memory in contracts, tests, semantic migration items, family rules and exact evidence—not in an ever-growing manual branch encyclopedia.
