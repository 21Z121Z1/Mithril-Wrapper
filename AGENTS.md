# Mithril-Wrapper agent operating contract

This is the coding/review entry point. Do not begin breadth-first. Compile the smallest task-local world model, act on one owning seam, prove the result at the cheapest sufficient level, then distill any reusable knowledge so the next agent needs less context.

## Bootstrap

Start with:

```bash
python3 scripts/agent-context.py --task "<task>"
```

The capsule reports exact HEAD/tree identity, history universe/nearest anchor, changed-path or task-inferred ownership, semantic boundary risk, focused existing oracles, a small read set and ordered proof plan.

Treat its labels literally: diff-derived ownership is stronger than task inference; unclassified paths are model gaps; legacy history means semantic/oracle transplant rather than wholesale merge.

If branch/history affects the task, use this three-step projection before reading raw branch history:

```bash
python3 scripts/audit-branches.py --fetch-graph --markdown
```

1. live topology comes from that generated Git audit;
2. lifecycle meaning comes from `docs/agent/branch-families.json`;
3. unresolved cross-branch semantics come from `docs/agent/migration-queue.json`.

`docs/agent/branch-ledger.md` explains this composition. It intentionally does not duplicate live HEADs. Frozen old snapshots live under `docs/history/`.

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
4. whether that semantic question already exists in `migration-queue.json`;
5. exact current proof location for any behavior being retained.

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
- disconnected migration/provenance: legacy tree, historical snapshots and migration queue.

If the same generic GL rule seems necessary in both native backends, first test whether it belongs in GL/shader/lowering.

## Investigation loop

Use:

```text
observable failure
  -> owning contract
  -> smallest falsifier
  -> implementation
  -> exact-subject proof
  -> broader acceptance
```

Do not start from a giant Minecraft log if a focused semantic oracle can distinguish the hypothesis. Do not claim real host/device behavior from a headless oracle.

For Minecraft 26.2 source behavior:

```bash
SRC="$(bash scripts/minecraft-reference.sh --print-path)"
```

Generated reference sources are local analysis input only; never commit or upload them.

## Oracle and proof routing

`docs/agent/oracles.json` is a search-cost index over stable tests. Test bodies remain authoritative.

`docs/agent/proof-graph.json` defines prerequisite order. Core rules:

- control validation precedes semantic proof;
- focused semantic proof precedes backend suites;
- shared EGL/GL/shader/lowering changes require both DirectMetal and Vulkan regressions;
- hosted platform proof follows DirectMetal semantic proof;
- physical presentation and paired performance are terminal claim proofs, not debugging starting points;
- a red cheaper prerequisite blocks escalation until explained.

For PRs, candidate source and GitHub's synthetic integration result are distinct proof subjects. See `docs/ci.md` and `docs/evidence-model.md`.

## Branch-family and migration discipline

A branch family describes lifecycle, not correctness. `covered_by`, `same_tree_as`, merged PR status or a family disposition may make a ref a retirement candidate, but none authorizes deletion.

A migration item describes one semantic question, not one branch. Many experiment refs may support one item; one comprehensive branch may support several items. Close the semantic item only when its clean owner, oracle and proof are explicit or the hypothesis is explicitly rejected.

Do not create a migration item merely because a branch exists. Do not remove an item merely because one source branch was merged or deleted.

## Performance work

Do not bypass the abstraction tower for speed. Identify measured/repeated cost, locate the owner, make identity/lifetime explicit, preserve correctness oracles, use structural counters when appropriate, then run matched performance measurement. Avoid hidden ownership, mutable-state reachback and backend-specific semantic forks.

## Knowledge accumulation

Store knowledge at the narrowest durable level:

- repeated semantic lesson -> test/type/contract;
- ownership/routing rule -> manifest + validator;
- proof dependency -> proof graph;
- reusable falsifier -> oracle index + test;
- branch lifecycle rule -> branch-family registry;
- unresolved cross-history behavior -> semantic migration queue;
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
- any changed semantic convergence is reflected in the migration queue rather than a copied branch table;
- reusable knowledge was distilled into the proper executable/stable layer;
- remaining uncertainty is named rather than hidden.
