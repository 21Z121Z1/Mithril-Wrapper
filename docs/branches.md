# Development branches

Mithril uses one shipping baseline, two implementation-focused integration lines, and one temporary legacy migration source. Every other live branch is now a frozen reconciliation input with exactly one exit path.

## Canonical active lines

| Branch | Purpose | Exit condition |
| --- | --- | --- |
| `main` | Shipping baseline. DirectMetal is the Apple default backend; Vulkan remains the reference/fallback backend. | Always retained. |
| `integration/directmetal-next` | The only clean `src/*` DirectMetal product line intended to land on `main`. Owns normal macOS semantics/boundary, iPhoneOS arm64, Linux Vulkan-reference and manual hosted runtime validation. | Merge validated product slices into `main`, then continue from updated `main`. |
| `integration/directvulkan-reference` | Single convergence line for the Vulkan fallback/reference implementation and its stable regression/sanitizer/platform evidence. | Port validated behavior/tests into the clean `src/*` Vulkan backend; do not turn this into a second shipping tree. |
| `integration/legacy-capability-port` | Temporary convergence source for validated `Mithril-Wrapper-cpp` DirectMetal/GL semantics and Minecraft E2E evidence. It exists to reconcile old branches and port capabilities into the clean tree. | Delete after all retained semantics/E2E evidence has been ported or explicitly rejected. |

All three integration lines have passed their canonical automatic gates under these stable names. The superseded dated baseline aliases and analysis-only duplicate aliases have been deleted.

## Frozen reconciliation sources

Do not add new product work to these branches. Their only purpose is to provide reviewable source material for the listed reconciliation PR; delete each branch when that PR's unique value is represented on its canonical target.

| Branch | Reconciliation target |
| --- | --- |
| `fix/directvulkan-mc262-gui-closure` | PR #16 → `integration/directvulkan-reference`: reconcile 7 GUI-specific commits, including DirectVulkan fixes and the Minecraft GUI oracle. |
| `fix/dual-backend-metal-ios-ci` | PR #17 → `integration/legacy-capability-port`: reconcile only the still-missing behavior from 3 unique Metal/iOS commits. |
| `fix/gl-semantic-closure-20260816` | PR #18 → `integration/legacy-capability-port`: reconcile 43 historical GL-semantic commits against the newer contract/oracle-ledger implementation. |
| `ci/minecraft-on-mithril-e2e-20260815` | PR #19 → `integration/legacy-capability-port`: preserve the useful production hardening and reusable Minecraft 26.2 Client GameTest/render-differential evidence from 53 unique commits. |
| `fix/mobilegl-style-mc262-startup-preflight` | PR #21 → `integration/directvulkan-reference`: no unique production code; port only stronger startup evidence such as iPhoneOS `vtool` verification, provenance, hashes and artifacts. |

## Migration order

1. Keep new product development off all five frozen reconciliation sources.
2. Resolve PR #16 into `integration/directvulkan-reference`, preserving GUI-specific production fixes and the Minecraft GUI oracle.
3. Resolve PR #21 into the same DirectVulkan canonical line as an evidence-only transplant; do not revive a second MobileGL workflow family.
4. Resolve PR #17 into `integration/legacy-capability-port` by transplanting only still-missing dual-backend Metal/iOS behavior.
5. Resolve PR #18 semantically: keep the newer contract/oracle-ledger architecture and port only old GL behavior that remains valid and unrepresented.
6. Resolve PR #19 by preserving production hardening and reusable Minecraft 26.2 Client GameTest/render-differential evidence.
7. Port the resulting tests and missing behavior from the legacy migration source into `integration/directmetal-next` and the clean Vulkan backend as appropriate.
8. Land clean-tree product slices into `main` through normal review and CI.
9. Delete each frozen source immediately after its unique value is represented elsewhere. Git history and closed/merged PRs are the archive.

## Rules for new branches

1. Start from the canonical line that owns the work: `main`, `integration/directmetal-next`, `integration/directvulkan-reference`, or the temporary `integration/legacy-capability-port` only for reconciliation work.
2. Do not fork new product work from a dated reconciliation source.
3. Do not copy a complete `.github/workflows` directory merely to obtain CI; extend the owning canonical gate.
4. A probe may add a test oracle, but not an `apply-*`, `materialize-*`, `stage-*`, source-location, or self-pushing workflow.
5. When a probe or fix is absorbed, delete its branch. Git history and merged PRs are the archive.
6. Before deleting a divergent branch, compare ancestry, unique commits, changed files, and CI evidence. A newer date or greener CI alone is insufficient proof.
