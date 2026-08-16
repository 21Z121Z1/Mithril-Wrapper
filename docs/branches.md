# Development branches

Mithril keeps one canonical branch for each active line of work. Git history is the archive; old implementation snapshots, exact duplicate refs, and completed probe branches should not remain indefinitely as parallel-looking development branches.

## Canonical active lines

| Branch | Purpose |
| --- | --- |
| `main` | Shipping baseline and normal pull-request integration target. |
| `integration/directmetal-unified-20260815` | Current DirectMetal integration line beyond `main`; owns DirectMetal build/runtime validation until those changes are integrated. |
| `ci/minecraft-on-mithril-e2e-20260815` | Real Minecraft 26.2 production E2E, render differential, semantic validation, and distribution proof. |
| `fix/gl-semantic-closure-integration-20260816` | Current GL/DirectMetal semantic-closure integration line. |
| `codex/directvulkan-overnight-recovery-20260814` | DirectVulkan recovery/diagnostic line; stable gates and known-open semantics are separated in its consolidated CI. |
| `codex/mobilegl-ios-live-20260815` | Latest descendant of the older GLSL/MobileGL preflight lineage. |
| `fix/directvulkan-mc262-gui-closure` | DirectVulkan Minecraft-GUI closure line while its branch-specific fixes remain independent. |
| `fix/mobilegl-style-mc262-startup-preflight` | MobileGL-style Minecraft startup line while its branch-specific fixes remain independent. |
| `fix/dual-backend-metal-ios-ci` | Independent-history dual-backend iOS line; do not assume ancestry with current `main`. |
| `fix/gl-semantic-closure-20260816` | Historical semantic-closure implementation line that still diverges materially from the newer integration branch; retain until its unique commits are explicitly reconciled. |

## Safe retirement candidates

These refs no longer represent distinct active development work and should be deleted once branch-ref deletion is performed:

- `agent/directmetal-fbo-sampling-a17pro` — fully contained by `integration/directmetal-unified-20260815`.
- `refactor/directmetal-clean-shipping` — fully contained by `integration/directmetal-unified-20260815`.
- `snapshot/codex-directvulkan-overnight-20260814` — fully contained by `codex/directvulkan-overnight-recovery-20260814`.
- `integration/glsl-mobilegl-preflight-20260814` and `sync/glsl-conversion-mobilegl-style-20260814` — exact duplicate refs.
- `ready/glsl-mobilegl-mc262-20260814` and `refactor/glsl-mobilegl-preflight-20260814` — exact duplicate refs, both superseded by `codex/mobilegl-ios-live-20260815`.
- `probe/gl-pixel-store-semantics-20260816`
- `probe/gl-raster-semantics-20260816`
- `probe/gl-sampler-semantics-20260816`
- `probe/gl-semantic-mrt-20260816`

The four semantic probe branches above have their authoritative oracle files absorbed byte-for-byte into `fix/gl-semantic-closure-integration-20260816`, whose retained semantic-closure workflow has passed. Their temporary probe/materialization workflows are not a reason to keep the refs.

## Cleanup branches

Branches named `chore/*-ci-cleanup-20260816` or `chore/actions-workflow-cleanup-*` exist only to carry the corresponding cleanup pull requests. Delete each cleanup branch after its PR is merged or otherwise resolved.

## Rules for new branches

1. Start from the canonical line that actually owns the work; do not fork from an arbitrary probe or snapshot branch.
2. Do not copy a complete `.github/workflows` directory merely to obtain CI. Open a pull request to the canonical branch and reuse its gate.
3. A probe branch may add a test oracle, but not an `apply-*`, `materialize-*`, `stage-*`, source-location, or self-pushing workflow.
4. When a probe or fix is absorbed, delete the branch. Do not retain several names pointing at the same commit.
5. Before deleting a divergent branch, compare ancestry and unique commits. A newer date or a greener CI run is not sufficient proof of supersession.
