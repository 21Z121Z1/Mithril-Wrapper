# Mithril-Wrapper agent operating contract

This is the coding/review entry point. Do not begin breadth-first. The repository is intentionally designed so an agent can compile a small task-local world model, act on one owning seam, prove the result cheaply, and leave the next agent with less uncertainty than before.

## Bootstrap

Start with:

```bash
python3 scripts/agent-context.py --task "<task>"
```

The capsule reports exact HEAD/tree identity, history universe/nearest anchor, changed-path or task-inferred ownership, semantic boundary risk, focused existing oracles, the smallest read set, and an ordered proof plan.

Treat its epistemic labels literally:

- `diff` ownership is stronger than task-text inference;
- `task_inference` is a routing hypothesis, not a source fact;
- unclassified paths are model gaps, not permission to guess;
- `legacy_experimental` means semantic/oracle transplant only.

If branch topology affects a decision, refresh the graph:

```bash
python3 scripts/audit-branches.py --fetch-graph --markdown
```

Only after those two projections should you read raw branch history or broad documentation.

## Stable mental model

Read `docs/system-model.md` for the abstraction tower. In compressed form:

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

Higher layers define meaning. Lower layers execute it.

The long-term center of gravity is `src/backend/*`: queued native work should be understandable from explicit resolved draw/resource/state/lifetime identity without re-running the mutable GL state machine in a backend.

## History model

The repository contains two different Git reasoning modes.

### Clean shipping universe

`main -> integration/directmetal-next`

Use normal Git ancestry plus semantic/evidence proof. Keep this relation simple; governance/control changes promoted to `main` should be converged into the active clean integration line rather than allowing two canonical clean refs to drift indefinitely.

### Legacy/experimental universe

`integration/directvulkan-reference`, `integration/legacy-capability-port`, `Mithril-Wrapper-cpp/*` and many experiment/replay/evidence refs belong to a disconnected historical family.

They are valuable sources of invariants, tests and provenance. They are **not** wholesale merge targets for `src/*`.

Before reusing a branch, establish:

1. history universe and ancestry/no-common-ancestor state;
2. unique semantic/test delta rather than commit count;
3. exact current proof location for the behavior being retained.

Squash merges mean ancestry alone is insufficient in either direction.

## Ownership map

Machine routing lives in `docs/agent/manifest.json`.

Common owners:

- EGL/host lifecycle: `src/egl/*`;
- GL state/object/error/FBO/pixel-store/query/sync semantics: `src/gl/*`, `src/state/*`;
- GLSL/SPIR-V/reflection/interface semantics: `src/shader/*`;
- resolved draw/resource contract: `src/backend/*`;
- DirectMetal execution only: `src/metal/*`;
- Vulkan execution only: `src/vk/*`;
- host/display seam: presentation tests + EGL/backend window seam;
- validation: `tests/*`, `cmake/MithrilSmokeTests.cmake`;
- agent/evidence control: `AGENTS.md`, `docs/agent/*`, `docs/ci.md`, `docs/evidence-model.md`, agent scripts and durable workflows.

If the same generic GL rule appears necessary in both `src/metal/*` and `src/vk/*`, first test whether it belongs in GL/shader/lowering instead.

## Investigation loop

Use:

`observable failure -> owning contract -> smallest falsifier -> implementation -> exact-subject proof -> broader acceptance`

Do not start from a giant Minecraft log if a 50-line semantic oracle can distinguish the hypothesis. Conversely, do not claim a real host/device behavior from a headless oracle.

For Minecraft 26.2 source behavior:

```bash
SRC="$(bash scripts/minecraft-reference.sh --print-path)"
```

The generated reference tree is local analysis input only and must never be committed or uploaded as an artifact.

## Oracle routing

`docs/agent/oracles.json` is a small search-cost index over stable tests. It does not replace test bodies. The context compiler uses it to surface likely focused oracles such as framebuffer, shader, texture, draw, sync or Amethyst-surface tests.

If no indexed oracle distinguishes the bug, inspect the owning test slice and add a focused reusable oracle before implementation when practical.

Do not add an oracle-index entry for a one-off experiment.

## Proof DAG

`docs/agent/proof-graph.json` defines prerequisite order over proof profiles in the manifest.

Key rules:

- control validation precedes semantic proof;
- focused semantic proof precedes backend suites;
- shared EGL/GL/shader/lowering changes require both DirectMetal and Vulkan regressions;
- hosted platform proof comes after DirectMetal semantic proof;
- physical presentation and paired performance are terminal claim proofs, not debugging starting points;
- a red cheaper prerequisite blocks escalation until explained.

For pull requests, candidate HEAD and GitHub synthetic merge result are separate proof subjects. See `docs/ci.md` and `docs/evidence-model.md`.

## Performance work

Do not bypass the abstraction tower for speed.

Preferred order:

1. identify a measured or structurally repeated cost;
2. locate the owner of redundant work;
3. make identity/lifetime explicit;
4. preserve the semantic oracle;
5. use structural counters where they prove the intended shape;
6. only then run matched performance measurement.

Avoid optimizing through hidden ownership, mutable-state reachback, accidental lifetime extension or backend-specific semantic forks.

## Knowledge accumulation

Store knowledge at the narrowest durable level:

- repeated semantic lesson -> test/type/contract;
- ownership/routing rule -> manifest + validator;
- proof dependency -> proof graph;
- reusable falsifier -> oracle index + test;
- stable architectural rationale -> ADR/system model;
- current frontier -> dated status;
- branch reconciliation -> live Git audit + dated ledger;
- one experiment -> PR/commit/artifact provenance.

Do not append incident history to README, CHECKLIST, AGENTS or stable architecture docs.

The desired invariant is:

> after a successful investigation, a future agent should need fewer tokens and fewer Git/test operations to reach the same understanding.

## Completion

Before declaring a task complete:

- exact source/tree subject is known;
- ownership layer is explicit;
- smallest relevant oracle passed;
- proof-DAG prerequisites for the claimed scope passed;
- integration/device/performance claims are not inferred from weaker evidence;
- legacy provenance is separated from clean implementation;
- durable knowledge was distilled into the proper executable/stable layer;
- remaining uncertainty is named rather than hidden.
