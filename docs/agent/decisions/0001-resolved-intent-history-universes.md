# ADR 0001: Resolved intent, history universes and generated agent state

Status: proposed on `architecture/agent-operating-model-20260901`; effective when promoted to `main`.

## Context

Mithril has grown through several overlapping engineering modes:

- a clean `src/*` architecture with DirectMetal and Vulkan reference execution;
- an older `Mithril-Wrapper-cpp/*` implementation lineage;
- DirectVulkan Minecraft/GPU repair experiments;
- DirectMetal performance branches;
- validation-only, replay and candidate-specific workflow branches.

Two independent complexity dimensions emerged.

First, rendering semantics cross EGL/GL state, shader reflection, backend-neutral draw/resource descriptions, native Metal/Vulkan execution and platform presentation. If each backend or experiment re-derives GL meaning independently, correctness fixes become hard to reason about and hard to transfer.

Second, not every branch belongs to the same Git ancestry. `main` / `integration/directmetal-next` and the legacy DirectVulkan/capability family include histories with no merge base between them. A single `relation_to_main` field therefore collapses most of the DirectVulkan experiment graph into the unhelpful label `no_common_ancestor`.

The repository also demonstrated that static branch inventories drift: branch status changed much faster than prose, while validation/replay branches sometimes looked newer than the product source they tested.

## Decision

### 1. Make backend-neutral resolved intent the architectural center

The GL/EGL frontend owns observable API meaning and resolves mutable state into explicit semantic/resource records before native execution. `src/backend/*` is the central seam.

DirectMetal and DirectVulkan consume those records. Backend code may choose native execution policy or reject unsupported capability, but must not silently invent different GL semantics.

When backend execution lacks information needed for correctness, extend the shared lowering contract rather than adding a backend-local guess.

### 2. Model Git history universe before branch role

The machine manifest declares stable history-universe anchors. Live branch membership is computed by merge-base relationships.

A branch is first oriented within its history universe, then compared to the nearest anchor in that universe. This makes legacy experiment lineages inspectable without pretending they share ancestry with `main`.

Disconnected histories use **semantic transplant**:

```text
old exact source/tree
  -> unique invariant/behavior
  -> focused oracle
  -> clean owning abstraction
  -> exact clean-candidate proof
```

A tree comparison across disconnected histories is archaeological input, not evidence of mergeability or semantic absorption.

### 3. Separate product, validation and provenance subjects

A branch whose HEAD is a CI trigger may still contain older product-bearing commits; an evidence-only descendant may add no product semantics; a replay branch may add only reconstruction scripts.

Agents therefore distinguish:

- product implementation subject;
- validation/orchestration subject;
- replay/provenance subject.

CI likewise distinguishes candidate-head proof from synthetic merge-result proof and fixed-external-candidate proof.

### 4. Compile live state; persist stable rules

Persist:

- abstraction layers/components;
- semantic boundaries;
- history-universe anchors;
- proof profiles and durable rationale.

Generate:

- current branch/ref inventory;
- nearest-anchor and lineage coverage;
- duplicate HEAD/tree groups;
- task-local component/boundary/proof routing;
- exact current source/tree identity.

Do not make a dated SHA table or branch count a canonical runtime truth store.

### 5. Fail closed on unknowns

Unknown history-universe membership, unclassified changed paths or missing proof routing remain explicit model gaps. The control plane must not guess a cheaper ownership/proof category simply to produce complete-looking output.

## Consequences

Benefits:

- an agent can orient by stable abstractions instead of reading dozens of refs linearly;
- DirectVulkan discoveries can be transplanted at the correct semantic layer without preserving the legacy architecture;
- common GL meaning has one explicit seam shared by DirectMetal and Vulkan;
- branch/workflow proliferation no longer forces canonical documentation to grow proportionally;
- validation/replay branches are less likely to be mistaken for product authority;
- exact candidate and merge-result evidence answer distinct questions without being conflated.

Costs:

- cross-branch work must explicitly establish history universe before normal Git operations;
- the manifest/validator must evolve when stable subsystem boundaries genuinely change;
- existing experiments remain unresolved until their unique semantic/evidence value is accounted for;
- cleanup becomes knowledge compaction rather than age-based deletion.

## Invariants

- Observable GL/EGL semantics are resolved above backend-specific execution whenever possible.
- Deferred native execution does not reread mutable GL state after its semantic snapshot is captured.
- Cross-universe promotion is semantic/evidence transplant, not wholesale merge by default.
- Branch names/dates/PR state do not establish current product authority.
- A proof is bound to the exact source/tree/binary subject actually tested.
- Generated topology may recommend investigation but never performs destructive branch mutation.
- Unknown ownership/history remains visible and fails closed.
