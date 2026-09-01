# Branch and convergence policy

Mithril-Wrapper has one product architecture but more than one historical Git lineage. Branch policy exists to prevent that history from becoming architecture.

**Live Git topology outranks dated branch prose.** When branch state matters, run:

```bash
python3 scripts/audit-branches.py --fetch-graph --markdown
```

`docs/agent/status.md` and `docs/agent/branch-ledger.md` are dated reconciliation checkpoints. Use them for rationale and unresolved accounting, but cross-check roles/containment against the generated graph.

## Canonical roles

### `main` — shipping baseline

`main` is the clean-tree shipping baseline and stable destination for promoted architecture/governance. It is not defined as “the branch with the most commits.”

### `integration/directmetal-next` — clean DirectMetal integration

Current clean `src/*` DirectMetal development line. Feature/fix/performance refs targeting it should be short-lived and reconciled by semantic/tree/PR evidence before retirement.

### `integration/directvulkan-reference` — legacy DirectVulkan migration source

Despite the historical name, this ref has no common ancestor with `main`. It is a migration/reference anchor in the legacy history universe, **not** a clean-tree Vulkan development base.

### `integration/legacy-capability-port` — legacy semantic reconciliation source

Also disconnected from `main`; preserves older semantic/evidence work pending explicit accounting. It must not be wholesale-merged into the clean product tree.

The machine-readable anchors and history universes live in `docs/agent/manifest.json`.

## History universe before branch role

Before merge/rebase/promotion reasoning, first establish whether the relevant refs have a merge base.

The live audit assigns each branch to declared history universes when the commit graph supports that relation, then chooses the nearest anchor inside that universe. This is more informative than comparing every DirectVulkan experiment only to `main`, where all would merely appear `no_common_ancestor`.

Across disconnected histories, use **semantic transplant**:

```text
source exact SHA/tree
  -> unique observable behavior or backend invariant
  -> focused oracle
  -> clean owning layer
  -> exact clean-candidate evidence
```

A direct tree diff across disconnected universes is useful archaeology, not an ancestry/promotion argument.

## Canonical ownership is not Git ancestry

Squash merges and disconnected histories require three independent questions:

1. **history/ancestry** — same, ancestor, descendant, diverged, squash-represented, or explicit no-common-ancestor;
2. **semantic delta** — unique changed behavior/tests/contracts, not commit count;
3. **proof subject** — which exact source/tree/binary and oracle now represent the valuable behavior.

A branch may have obsolete ownership while retaining unique semantics. A merged squash source may remain Git-divergent. A validation branch may be newer while containing no newer product semantics.

Branch date and prefix are search hints only.

## Clean tree versus legacy tree

The clean product architecture is rooted at `src/*`; the older DirectVulkan/dual-backend family commonly carries `Mithril-Wrapper-cpp/*` and many branch-local workflows.

When a legacy branch contains a valid fix:

1. identify the GL/EGL semantic or native-lifetime rule;
2. identify the smallest clean-tree owning layer from `docs/system-model.md` / manifest;
3. preserve or add the focused oracle;
4. implement the rule in the clean architecture, preferably above backend execution when semantics are shared;
5. run clean-tree evidence planes on the exact candidate;
6. retain old SHA/PR only as provenance;
7. retire the legacy ref only after all unique value is represented and deletion is authorized.

Do not mechanically transplant old file layouts, duplicated backend policy or one-shot workflows.

## Product, evidence and provenance subjects

Recent DirectVulkan work demonstrates why one branch name is insufficient. A branch can contain a product lineage whose HEAD is only a trigger commit; another descendant may add only a cloud-E2E trigger. `scripts/audit-branches.py` therefore reports changed-path delta class, nearest anchor, coverage and same-tree groups in addition to the HEAD name.

Keep these concepts separate:

- **product subject** — source/tree that actually implements behavior;
- **validation/orchestration subject** — harness/workflow that tests a product subject;
- **replay/provenance subject** — patch reconstruction/history source.

A trigger commit is not automatically the product implementation, and an evidence-only descendant does not supersede a product-bearing ancestor semantically.

## Creating branches

New long-lived integration refs are exceptional. Prefer bounded task refs:

- `fix/<scope>-<date>` — correctness repair;
- `perf/<scope>-<date>` — performance phase;
- `experiment/<hypothesis>-<date>` — controlled A/B hypothesis;
- `validation/<claim>-<date>` — evidence-only harness;
- `tooling/<capability>` — reusable tooling.

Every experiment needs an exit route: promote a rule/oracle, preserve a negative result, or delete after falsification.

When sustained clean-tree Vulkan development begins, create its integration ref from the clean shipping universe and record that ownership in the manifest. Do not silently repurpose the disconnected historical DirectVulkan anchor.

## Pull requests and CI

A PR should state:

- owning layer / behavior;
- source/destination history universe and branch;
- semantic/performance oracle;
- exact product subject;
- evidence complete vs still required.

Validation-only PRs must say so explicitly. CI supplies environments; tests own contracts. Do not make one permanent workflow per fix/experiment.

See `docs/ci.md` and `docs/evidence-model.md`.

## Same-head / same-tree aliases

Two names can point to the same commit, and different commits can produce the same tree. The live audit reports both forms.

These are strong retirement candidates, not deletion authorization. Check open PRs, external workflows/artifacts and unique rationale first.

## Branch retirement

Retire a ref only after:

1. live topology is refreshed;
2. history universe and ancestry/squash state are understood;
3. unique implementation/test/evidence delta is enumerated;
4. every valuable invariant/oracle is represented by current code or explicitly rejected with evidence;
5. important provenance remains discoverable in PR/commit/artifact history;
6. no active workflow/human testing process requires the branch name;
7. repository owner explicitly authorizes destructive cleanup.

Unknown/unclassified deltas fail closed: investigate rather than infer absorption.

## Desired steady state

```text
main
  +-- integration/directmetal-next      (only while ahead clean work exists)
  +-- <future clean Vulkan integration> (only while sustained clean port exists)

legacy migration anchors                (temporary and shrinking)
short-lived product/experiment/evidence refs
```

The system should accumulate memory in types, contracts, tests, manifest rules and exact evidence—not in an ever-growing branch namespace.
