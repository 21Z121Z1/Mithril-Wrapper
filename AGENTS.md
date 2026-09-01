# Mithril-Wrapper agent operating contract

This is the coding/review entry point. Do not begin breadth-first. Compile the smallest task-local world model, act on one owning seam, prove the result at the cheapest sufficient level, then distill reusable knowledge so the next agent needs less context.

## Bootstrap

Start with:

```bash
python3 scripts/agent-context.py --task "<task>"
```

The capsule reports exact HEAD/tree identity, history universe/nearest anchor, diff/task ownership, boundary risk, focused existing oracles, a small read set and ordered proof plan. For history-sensitive tasks it additionally projects only the relevant open migration items and already-accounted historical findings; ordinary renderer work does not pay that context cost.

Treat labels literally: diff-derived ownership is evidence about the current change, task ownership is evidence about the requested work, and proof/read routing uses both when they differ. Unclassified paths are model gaps. Legacy history means semantic/oracle transplant rather than wholesale merge.

If branch/history affects the task, refresh the graph before raw archaeology:

```bash
python3 scripts/audit-branches.py --fetch-graph --markdown
```

Then interpret it through:

1. live topology from Git;
2. lifecycle meaning from `docs/agent/branch-families.json`;
3. open semantic work **and already-accounted negative/translated findings** from `docs/agent/migration-queue.json`.

`docs/agent/branch-ledger.md` explains the composition and intentionally duplicates no live HEAD table. Frozen old snapshots live under `docs/history/`.

## Stable mental model

Read `docs/system-model.md` when architecture matters. Compressed:

```text
Minecraft acceptance
  -> host/EGL contract
  -> observable GL + shader semantics
  -> backend-neutral resolved intent
  -> DirectMetal / Vulkan execution
  -> platform presentation
  -> evidence
  -> convergence/release identity
```

Higher layers define meaning. Lower layers execute it. `src/backend/*` is the long-term lowering seam: queued native work should be understandable from explicit resolved draw/resource/state/lifetime identity without rereading mutable frontend state.

## History model

There are two Git reasoning modes.

### Clean shipping universe

```text
main -> integration/directmetal-next
```

Use normal Git ancestry plus semantic/evidence proof. Keep this relation structurally simple: after governance/control changes land on `main`, converge them into the active clean integration line rather than allowing two canonical clean refs to drift indefinitely.

### Legacy/experimental universe

`integration/directvulkan-reference`, `integration/legacy-capability-port`, `Mithril-Wrapper-cpp/*` and related experiment/replay/evidence refs are disconnected historical sources. They may contain valuable semantics, tests and evidence, but are not wholesale merge targets for clean `src/*`.

Before reusing a historical ref, establish:

1. history universe and actual ancestry/no-common-ancestor state;
2. lifecycle family and whether the ref is product, experiment, evidence, migration source or provenance;
3. unique semantic/test delta rather than commit count;
4. whether the semantic question is already open in `migration-queue.json` **or the legacy strategy has already been accounted there**;
5. exact current proof location for retained behavior.

Squash merges mean ancestry alone is insufficient in either direction.

## Ownership

Machine routing lives in `docs/agent/manifest.json`.

- host/EGL lifecycle: `src/egl/*`;
- observable GL semantics: `src/gl/*`, `src/state/*`;
- shader translation/reflection/interface: `src/shader/*`;
- resolved draw/resource contract: `src/backend/*`;
- DirectMetal execution: `src/metal/*`;
- Vulkan execution: `src/vk/*`;
- host/display seam: presentation tests + EGL/backend window seam;
- validation: `tests/*`, `cmake/MithrilSmokeTests.cmake`;
- agent/evidence control: manifest, proof/oracle/branch registries, agent scripts and durable workflows;
- disconnected migration/provenance: legacy tree, historical snapshots and migration memory.

If the same generic GL rule seems necessary in both native backends, first test whether it belongs in GL/shader/lowering.

## Investigation loop

```text
observable failure
  -> owning contract
  -> smallest falsifier
  -> implementation
  -> exact-subject proof
  -> broader acceptance
  -> distilled reusable or negative knowledge
```

Do not start from a giant Minecraft log if a focused semantic oracle can distinguish the hypothesis. Do not claim real host/device behavior from a headless oracle.

For Minecraft 26.2 source behavior:

```bash
SRC="$(bash scripts/minecraft-reference.sh --print-path)"
```

Generated reference sources are local analysis input only; never commit or upload them.

## Oracle and proof routing

`docs/agent/oracles.json` is a search-cost index over stable tests. Test bodies remain authoritative.

`docs/agent/proof-graph.json` defines prerequisite order:

- control validation precedes semantic proof;
- focused semantic proof precedes backend suites;
- shared EGL/GL/shader/lowering changes require both DirectMetal and Vulkan regressions;
- hosted platform proof follows DirectMetal semantic proof;
- physical presentation and paired performance are terminal claim proofs, not debugging starting points;
- a red cheaper prerequisite blocks escalation until explained.

For PRs, candidate source and GitHub's synthetic integration result are distinct proof subjects. See `docs/ci.md` and `docs/evidence-model.md`.

## Branch-family and migration discipline

A branch family describes lifecycle, not correctness. `covered_by`, `same_tree_as`, merged PR status or an `absorbed_provenance` disposition can reduce the active analysis surface, but none authorizes deletion.

An open migration item describes one semantic question, not one branch. Many experiment refs may support one item; one comprehensive branch may support several items. Close it only when its clean owner, oracle and proof are explicit or the hypothesis is explicitly rejected.

An `accounted_finding` is equally important: it records a historical strategy already translated into the clean abstraction or judged not to be a current clean requirement. Do not reopen it merely because the old branch still exists; reopen only when new source/test/host evidence contradicts the finding.

Do not create migration work merely because a branch exists. Do not remove semantic memory merely because one source branch was merged or deleted.

## Performance work

Do not bypass the abstraction tower for speed. Identify measured/repeated cost, locate the owner, make identity/lifetime explicit, preserve correctness oracles, use structural counters where appropriate, then run matched performance measurement. Avoid hidden ownership, mutable-state reachback and backend-specific semantic forks.

## Knowledge accumulation

Store knowledge at the narrowest durable level:

- repeated semantic lesson -> test/type/contract;
- ownership/routing rule -> manifest + validator;
- proof dependency -> proof graph;
- reusable falsifier -> oracle index + test;
- branch lifecycle/accounted PR lineage -> branch-family registry;
- unresolved cross-history behavior -> migration item;
- historical strategy already translated/rejected -> accounted finding;
- live branch state -> generated Git audit only;
- stable rationale -> ADR/system model;
- current product frontier -> dated status;
- one experiment -> PR/commit/artifact provenance.

Do not append incident history to README, CHECKLIST, AGENTS or stable architecture docs.

Desired invariant:

> after a successful investigation, a future agent should need fewer tokens and fewer Git/test operations to reach the same understanding.

## Completion

Before declaring a task complete:

- exact source/tree subject is known;
- owning abstraction is explicit;
- smallest relevant oracle passed;
- proof-DAG prerequisites for the claimed scope passed;
- integration/device/performance claims are not inferred from weaker evidence;
- legacy provenance is separated from clean implementation;
- changed semantic convergence is reflected as an open item or accounted finding rather than a copied branch table;
- reusable knowledge was distilled into the proper executable/stable layer;
- remaining uncertainty is named rather than hidden.
