# Development branches

Mithril keeps one canonical branch for each active line of work. Git history is the archive; old implementation snapshots, exact duplicate refs, completed probes, and cleanup branches should not remain indefinitely as parallel-looking development branches.

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

All previously audited cleanup, snapshot, duplicate, and absorbed probe refs were retired after their replacements were merged and verified. Historical branch names remain discoverable through merged pull requests and Git history rather than as live refs.

## Rules for new branches

1. Start from the canonical line that actually owns the work; do not fork from an arbitrary probe or snapshot branch.
2. Do not copy a complete `.github/workflows` directory merely to obtain CI. Open a pull request to the canonical branch and reuse its gate.
3. A probe branch may add a test oracle, but not an `apply-*`, `materialize-*`, `stage-*`, source-location, or self-pushing workflow.
4. When a probe or fix is absorbed, delete the branch. Do not retain several names pointing at the same commit.
5. Before deleting a divergent branch, compare ancestry and unique commits. A newer date or a greener CI run is not sufficient proof of supersession.
