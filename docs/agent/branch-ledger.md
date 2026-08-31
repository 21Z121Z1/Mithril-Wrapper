# Branch ledger

Snapshot date: 2026-09-01

This is an inventory and reconciliation ledger, not a list of 55 active product lines. Branch refs are cheap historical handles; product ownership is defined by `docs/branches.md` and current convergence by `docs/agent/status.md`.

Rules for reading this table:

- `clean` means the modern `src/*` architecture lineage.
- `legacy` means the disconnected/older `Mithril-Wrapper-cpp/*` family or a reconciliation branch rooted there.
- `evidence` means the branch primarily exists to run/record a validation or package; it is not automatically an implementation owner.
- `source` means preserve until unique semantics/tests/evidence are accounted for elsewhere.
- “merged PR” does not imply Git ancestry after squash merge.
- For branches not explicitly proven absorbed below, containment is intentionally **unknown**. Do not delete them from this document alone.

## Canonical ownership refs

| Branch | HEAD | Tree | Role / current interpretation |
| --- | --- | --- | --- |
| `main` | `32ee89a041649231a1e6c328fad8ce8ca1b11415` | clean | Shipping baseline. Stable destination for promoted clean-tree work. |
| `integration/directmetal-next` | `296ee3b14ef2753e4abe8d4853baae38b84a6cb2` | clean | Current DirectMetal integration. Diverged from `main`; reconcile deliberately before promotion. |
| `integration/directvulkan-reference` | `c54927fd8e17a702fa6517c4c1074635de68285a` | legacy/disconnected | DirectVulkan migration/reference source. No common ancestor with `main`; not a clean-tree merge base. |
| `integration/legacy-capability-port` | `5993dc7c689a26a704fda45c8ada7fb40effa60e` | legacy/disconnected | Broad legacy semantic reconciliation source. No common ancestor with `main`; semantic-port only. |

## Clean-tree DirectMetal development and evidence refs

| Branch | HEAD | Interpretation / exit |
| --- | --- | --- |
| `fix/directmetal-incomplete-fbo-20260819` | `a09c8227be9ba599bab0747326f199770f8ebd91` | Open PR #34 to `integration/directmetal-next`; distinct fail-closed FBO candidate. Keep until that PR is resolved and exact-SHA evidence is accounted for. |
| `fix/ios-amethyst-runtime-isolation` | `15ba6e9c5c1e7812683890eaa69914effa6ce5c4` | DirectMetal/iOS host-runtime source. Containment not assumed; compare semantics/tests before retirement. |
| `fix/minecraft26-directmetal-runtime-closure-20260819` | `3937f25dae40a10682591c0d17e25d542e617581` | PR #35 was squash-merged into `integration/directmetal-next`. Candidate for deletion after verifying the ref has no post-merge unique work/artifact dependency. |
| `perf/directmetal-hotpath-phase1-20260817` | `3faf1bb42c0f7ebcd8b136e8be592ebad60d7255` | PR #22 merged. Source ref may remain Git-divergent after squash; use PR/tree evidence for retirement. |
| `perf/directmetal-buffer-streaming-20260817` | `a8b90cd8bdde4c8ae8b94e11d59796a575c21131` | Performance phase source. Verify merged PR/tree containment before deletion. |
| `perf/directmetal-lazy-buffer-storage-20260817` | `ec66214b79d0b9344e603bf1f4ad1d059c69ecbb` | Performance phase source. Verify merged PR/tree containment before deletion. |
| `perf/directmetal-numeric-cache-keys-20260817` | `7f517ea282215f4f4ed375862fea0c548b1306c5` | Performance phase source. Verify merged PR/tree containment before deletion. |
| `perf/directmetal-resident-index-20260817` | `33320b4e880e94558360fe4bc77b8de4be290eab` | Performance phase source. Verify merged PR/tree containment before deletion. |
| `perf/directmetal-uniform-snapshots-20260817` | `039b86fb8016ce3521ddd6fd59a6f6c54f7ba5ab` | Performance phase source. Verify merged PR/tree containment before deletion. |
| `perf/directmetal-multidraw-lowering-20260818` | `e0dc9a294e46f69bd19066de6001d035a88e7521` | Performance phase source. Verify merged PR/tree containment before deletion. |
| `perf/directmetal-borrowed-draw-metadata-20260818` | `e96a410b7749c781e8f0b266e8e0677e4a77d7e2` | Performance phase source. Git compare with current integration is divergent; this alone does not prove missing semantics because squash merges are possible. |
| `perf/directmetal-fixed-hot-metadata-20260818` | `56c1ebc779a601d9baa3c88ba8ab5459fc86c3b9` | Performance phase source. Verify PR/tree containment before deletion. |
| `perf/directmetal-program-prewarm-20260818` | `8189a8528e9a7112ded6bbd961c27e362d41c3a1` | Performance phase source. Verify PR/tree containment before deletion. |
| `perf/directmetal-async-pso-precompile-20260818` | `b9894620ee5b6d1f1d4e4f33d00104bf5e582174` | PR #32 merged. Current integration and source ref remain Git-divergent after squash; use PR/tree evidence, not ancestry, for retirement. |
| `validation/directmetal-performance-e2e-20260818` | `a4c76f20b8d3adfbbd5ec41e588810f6754772c2` | Performance evidence source; not product ownership. Preserve only while its unique oracle/artifacts are not represented durably elsewhere. |
| `validation/final-minecraft-macos15-20260819` | `4f743cd8a8b66f9b804a8e1a90de9b7919dd508d` | Final-candidate evidence/harness source; do not merge as product code by name. Account for exact candidate SHA and oracle before retirement. |

## Legacy capability reconciliation refs

| Branch | HEAD | Interpretation / exit |
| --- | --- | --- |
| `ci/minecraft-on-mithril-e2e-20260815` | `88d0ceb3a924e3feaee4a6aa97d5dc4f8fd26624` | Legacy production+E2E source tracked by PR #19. Port reusable oracles/semantics; do not merge wholesale into clean tree. |
| `fix/dual-backend-metal-ios-ci` | `c0ad351cc16cc736a90d863db1598ff0f702254e` | Legacy reconciliation source tracked by PR #17; remaining items must be resolved semantically, not by wholesale merge. |
| `fix/gl-semantic-closure-20260816` | `becdb9caa40842e970916cae8f8db17753af7973` | Legacy semantic source tracked by PR #18. Retire only after unique GL behavior/oracles are accounted for. |

## DirectVulkan legacy implementation / evidence family

These refs belong to the disconnected legacy family unless a later comparison explicitly proves otherwise. They are useful for reconstructing real Minecraft semantics and experiments, but they are not clean-tree architecture destinations.

### Integration / CI / Codex refs

| Branch | HEAD | Interpretation / exit |
| --- | --- | --- |
| `ci/minecraft-on-mithril-e2e-vulkan-20260826` | `6d2354c05593ba3c5ce8d24cd9029f4c4a64cfe3` | Validation/repair source; draft PR #36. Ancestor of the later GUI-production branch by 36 commits. |
| `ci/minecraft-on-mithril-e2e-vulkan-a11-oracle-20260831` | `3c21cbf4928c7e3b10e29529b3059530db8acb5e` | A11 real-Minecraft E2E trigger/evidence ref. Product source is recorded separately; do not treat trigger commit as implementation. |
| `codex/dvk-a11-single-mvk-shader-oracle-20260831` | `473a040fb4fb950f42d554acb0bef1c6cfc58a96` | A11 single-MoltenVK shader/E2E evidence ref; HEAD is a trigger commit naming source SHA `c62e775c...`. |
| `codex/dvk-gui-production-20260830` | `cd89c481a0c4f0b91db996f926ce3a8db68dae34` | Major legacy DirectVulkan GUI production investigation. Direct descendant of 2026-08-26 E2E line; contains PBO/unpack/readback/orientation fixes plus experiment workflows. High-value migration source. |
| `codex/dvk-ios-artifact-20260831` | `eb269a871cfe897c3d7faf41ea582deb3c661fa5` | iPhoneOS DirectVulkan packaging/evidence ref. Workflow validates native baseline and static MoltenVK artifact; not independent product semantics. |
| `codex/dvk-ios-fbo-orientation-20260831` | `fc36aae0b65a581f4ed1caf93b1cef1751caaa55` | iOS FBO/orientation candidate evidence; diverges from GUI-production and mainly adds candidate workflows. Preserve until orientation result is represented by a clean regression. |
| `codex/dvk-rollout-replay-20260831` | `dd358d9580d1bffc481c087752f0abf8866e92c5` | Rollout reconstruction/replay source. Retire only after every recovered unique patch is mapped to product or rejected with evidence. |

### Experiment refs

All `experiment/*` refs below are hypothesis/evidence branches. They must never become permanent architecture merely because one experiment passed. The exit is: keep the falsifiable result/oracle or port the semantic fix, then delete the branch when no longer referenced.

| Branch | HEAD | Experiment topic |
| --- | --- | --- |
| `experiment/dvk-atlas-fbo-probe-20260827` | `e4b0e144931dd5e122d9749947a22632a60afa59` | atlas/FBO behavior |
| `experiment/dvk-atlas-upload-probe-20260827` | `b6ac74dfe1d5429dc59299eaf2899f458e485cbe` | atlas upload behavior |
| `experiment/dvk-atlas-upload-trace-20260827` | `0e2f9d28d1c33419ec205b2d6cb0c6ce1d7cae68` | atlas upload trace |
| `experiment/dvk-buffer-always-busy-20260827` | `00ec9af32e192c2d4dd152547b27768990d1edc5` | buffer busy/lifetime hypothesis |
| `experiment/dvk-descriptor-pool-types-20260827` | `eaeaa0ff78ddab89bee84dec9f96eefd52d4e0c4` | descriptor pool typing |
| `experiment/dvk-fresh-descriptors-20260827` | `693974862ff27d3bc52d951fc97d6176f4d520f4` | fresh descriptor hypothesis |
| `experiment/dvk-full-unpack-combined-20260829` | `b6bc7b04ccb3d92a859e1a80959a044a77d62e4d` | combined pixel-unpack fix; exact duplicate ref with `fix/dvk-pixel-unpack-state-20260829` |
| `experiment/dvk-gui-text-ab-20260829` | `7e1ff65c5e064a8e28a4c45af84e50ad91c84532` | GUI/text A/B; historically important GUI production evidence |
| `experiment/dvk-index-stream-probe-20260827` | `8edaf0fe58c53fe4e59fd0c83a63c79b13ab4332` | index stream |
| `experiment/dvk-nondynamic-ubo-20260826` | `bd9ca5a3bf2b87094908906c87fc02fb8bba8d62` | non-dynamic UBO |
| `experiment/dvk-nondynamic-ubo-current-20260827` | `9ed94896d35b04009a640d3fdc91aeabccb93a14` | current non-dynamic UBO variant |
| `experiment/dvk-pbo-full-unpack-20260827` | `1fcb4dca1009c9c23ab199927e33f58c77f2c320` | PBO + full unpack state |
| `experiment/dvk-pbo-shadow-source-20260827` | `27fae14445a76f27e6c6f6189b77d1ece4e82967` | PBO CPU shadow source |
| `experiment/dvk-persistent-map-direct-20260827` | `beded2a3367e633e54f880677d15b8a20093e738` | persistent mapping direct path |
| `experiment/dvk-real-ubo-alignment-20260827` | `1ad20c03f80a1c348e92a42e43a0efde4afac81d` | real UBO alignment |
| `experiment/dvk-sampler-default-zero-20260827` | `950a5b9beade6181694959611c0be03755703c9b` | default sampler zero semantics |
| `experiment/dvk-stage-app-ubos-20260827` | `05ec7c0783209c606396b06f49aa82107ddbdd0c` | staged application UBOs |
| `experiment/dvk-uv-input-probe-20260827` | `4ac474b1952ca9e2be231c983d7a6b96e63cee09` | UV input |
| `experiment/dvk-vertex-interface-probe-20260827` | `8bb563d9773732b9e330098a40ebb6e3f013f14d` | vertex interface |

### Fix / reconciliation refs

| Branch | HEAD | Interpretation / exit |
| --- | --- | --- |
| `fix/directvulkan-mc262-gui-closure` | `7e1035e693281b2e7e2b2d99d7a7e60bf8ab193c` | Legacy GUI closure source. PR #16 to legacy reference remains open and non-mergeable; preserve unique GUI oracle/fixes for semantic port. |
| `fix/dvk-atlas-fbo-subresources-local-20260827` | `e99cd0d54ad42227723e920acbab4c51314aa068` | atlas/FBO subresource fix source. Verify against later GUI-production lineage before retirement. |
| `fix/dvk-gui-text-render-20260829` | `54da349b1377f3b272bada4c9d63e46b650548e5` | Legacy GUI text fix line. PR #37 agent/Minecraft tooling was merged here, which makes this branch an example of useful cross-cutting tooling stranded in a legacy line. |
| `fix/dvk-pixel-unpack-state-20260829` | `b6bc7b04ccb3d92a859e1a80959a044a77d62e4d` | Exact same HEAD as `experiment/dvk-full-unpack-combined-20260829`; one name should eventually be retired after references are checked. |
| `fix/mobilegl-style-mc262-startup-preflight` | `616689317ac8b505ebed3a8978be25e96b3ff85a` | Legacy startup evidence source tracked by PR #21; port only unique provenance/platform checks, then retire. |

## Tooling ref

| Branch | HEAD | Interpretation / exit |
| --- | --- | --- |
| `tooling/minecraft-reference-26.2` | `5e5a36be3708b290d2f1578dfdc6c3fcf599dc1f` | PR #37 merged its tooling into legacy GUI line. Reusable agent source-materialization tooling is being promoted separately into the clean-tree agent operating model; delete this ref after clean-tree adoption is complete and references are checked. |

## Retirement procedure

A branch may be deleted when all of the following are true:

1. destination/ownership is explicit;
2. ancestry/squash/no-common-ancestor state is understood;
3. unique changed behavior and tests have been enumerated;
4. every valuable behavior is represented by current code + oracle or explicitly rejected with evidence;
5. important artifacts/provenance are linked from durable PR/issue history;
6. no open PR or automation still depends on the ref name;
7. `docs/agent/status.md` and this ledger are updated in the same convergence change.

The desired steady state is not “zero old branches.” It is a small set of product/integration refs plus temporary experiments whose information has an explicit route into the durable system.
