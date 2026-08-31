# Continuous integration as evidence infrastructure

GitHub Actions is an evidence execution plane. It should answer “what does this exact implementation prove in this environment?” without becoming a second build system, source mutator or branch-status database.

See `docs/evidence-model.md` for evidence tiers and exact-identity claim rules.

## Durable workflow ownership

### `.github/workflows/build.yml` — normal product gate

Owns the cheap, repeatable evidence required for `main` and pull requests to `main`:

- candidate-head agent/control contract proof;
- clean DirectMetal macOS configure/build + semantic CTest;
- Vulkan-free shipping boundary verification;
- iPhoneOS arm64 shipping build/ABI/Mach-O/signature checks;
- Linux Vulkan reference regression;
- merge-result integration proof for renderer/build jobs on pull requests.

This workflow is read-only (`contents: read`). Keep it deterministic enough to be a required merge gate.

### `.github/workflows/hosted-metal-gpu-probe.yml` — unique platform runtime evidence

Manual/deep lane for real Apple runtime evidence the normal matrix should not duplicate. It may exercise a hosted Metal GPU or simulator presentation seam, but remains an evidence producer—not a source mutator and not automatically physical-device proof.

## Proof subject identity

A workflow association and the object it tests are different facts.

For pull requests distinguish explicitly:

- **candidate head** — `github.event.pull_request.head.sha`; answers “does this exact proposed source satisfy the stated contract?”
- **synthetic merge result** — `github.sha` for the PR event/default checkout; answers “does the proposal still satisfy the gate when integrated with the current base?”
- **fixed external product subject** — an explicitly checked out SHA from another branch/history universe; answers only the claim tied to that named product candidate.

`build.yml` deliberately uses both categories: the cheap `agent-contract` job checks out and asserts the exact candidate head, while renderer/build jobs keep GitHub’s merge-result checkout and assert the actual `GITHUB_SHA`. Neither result may masquerade as the other.

Every important runtime/promotion record should preserve, as applicable:

```text
proof_subject
source SHA
source tree SHA
harness SHA when different
binary identity/hash/UUID
backend
platform/runner/device capability
scenario/workload
oracle result + limitation
```

A green check attached to PR X is not automatically proof of PR X’s head source. A PASS on source A is not acceptance of source B without an explicit equivalence argument.

## Test ownership versus workflow ownership

Tests belong to contracts. Workflows supply environments.

Prefer:

```text
new semantic rule
  -> focused test in tests/
  -> register under existing CTest label
  -> normal build workflow runs it
```

Do not prefer:

```text
new semantic rule
  -> new top-level workflow
  -> another bespoke build of the same library
  -> source grep used as behavioral proof
```

If a test needs a special environment, add a narrow job/selector to the existing relevant plane before creating a durable top-level workflow.

## Agent control-plane validation

`docs/agent/manifest.json` is machine-readable navigation, ownership, history-universe, boundary and proof-routing data. `scripts/validate_agent_contract.py` validates the graph and runs self-tests for:

- `scripts/agent-context.py` task/diff routing;
- `scripts/audit-branches.py` multi-universe branch classification;
- exact candidate-head workflow identity;
- layer/component/boundary/proof references.

This gate is intentionally cheap and runs before expensive renderer jobs. It validates rules, not a copied live branch inventory.

## Live branch state is generated

Do not make CI assert that a dated branch ledger exactly equals live GitHub refs. Branch state is generated with:

```bash
python3 scripts/audit-branches.py --fetch-graph
```

`status.md` / `branch-ledger.md` are dated reconciliation context only. A live branch count or SHA list is volatile state, not a canonical schema invariant.

## Branch-specific workflows

Temporary workflows on experiment/validation branches are tolerated when necessary to answer a question current durable planes cannot express. They are not architectural assets by default.

Before that branch is retired:

1. decide whether the result is a reusable semantic oracle, platform capability, artifact/provenance record or rejected hypothesis;
2. move reusable pieces into the durable test/evidence structure;
3. preserve exact source/evidence identity in PR/commit history;
4. delete one-off workflow files rather than merging them into the clean tree by inertia.

The 2026-08 DirectVulkan family contains many A/B/candidate/replay workflows. Mine them for semantics, oracles and provenance; do not reproduce the workflow chronology on `main`.

## Cross-history evidence

The legacy DirectVulkan universe has no common Git ancestor with the clean shipping universe. Evidence on a legacy candidate can establish that a behavior worked there, but it cannot by itself establish that a clean-tree semantic transplant is correct.

Cross-universe closure requires:

```text
legacy source evidence
  + focused reusable oracle
  + clean owning-layer implementation
  + exact clean-candidate proof
```

Do not use a successful legacy workflow as a substitute for validation of the clean destination.

## Forbidden CI roles

Do not keep canonical workflows whose primary role is to:

- `apply-*`, `materialize-*`, `bootstrap-*`, `stage-*`, `finalize-*`, `recover-*`, replay or one-shot source changes;
- commit/push implementation edits from Actions;
- locate source once and preserve the answer as workflow code;
- duplicate another workflow’s matrix without unique evidence;
- weaken/fork an oracle merely to make a branch green;
- treat source-text grep as behavioral proof when an executable oracle is practical.

Automation that changes source belongs in explicit, reviewable agent/PR work—not recurring CI.

## Evidence artifacts

Artifacts are valuable when they make a claim independently inspectable:

- shipping dylib/package + checksum;
- framebuffer/pixel output that is itself an oracle;
- concise structured provenance metadata;
- benchmark distributions/configuration.

Avoid generated Minecraft reference source/client JARs, whole dependency trees, giant unthrottled logs, and duplicate product artifacts with no added proof value.

## Heavy validation

Real Minecraft, stress, performance, long-running diagnostics and physical-device claims are expensive and environment-sensitive. Run them only when they add information E0–E3 cannot provide—but do not make a release/device claim from cheap CI because the heavier proof is inconvenient.

## Failure interpretation

Classify a failed job before rerunning:

- product semantic/implementation failure;
- test/oracle defect;
- dependency/toolchain incompatibility;
- environment/capability absence;
- transient infrastructure failure.

Rerun unchanged only when evidence supports a transient infrastructure failure. Do not use retries to wash out deterministic red states.

## Workflow change checklist

Before adding/modifying CI ask:

1. What claim does this uniquely prove?
2. What is the exact proof subject: candidate, merge result, or fixed external SHA?
3. Which evidence tier/environment is required?
4. Can an existing oracle/label/workflow host it?
5. Is source/tree/binary identity unambiguous?
6. Is the workflow read-only?
7. Does it produce a reusable oracle/artifact rather than branch-specific chronology?
8. What condition removes/simplifies this machinery later?

The best CI architecture is the smallest graph that gives agents high-confidence, exact-subject feedback at every meaningful semantic boundary.
