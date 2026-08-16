# Legacy capability-port Actions

`integration/legacy-capability-port` is a migration source, not a shipping branch. It converges validated behavior from the historical `Mithril-Wrapper-cpp` tree so tests and semantics can be ported into the clean `src/*` product tree. New shipping DirectMetal work belongs on `integration/directmetal-next`; Vulkan fallback/reference convergence belongs on `integration/directvulkan-reference`.

The automatic `capability-port-validation.yml` gate is deliberately small: it proves that conflict resolution keeps the legacy dual-backend tree buildable and preserves representative real-GPU GL semantics.

Three inherited evidence planes remain while the historical branches are reconciled:

- `gl-semantic-closure.yml` — full DirectMetal GPU semantic oracle ledger plus real Minecraft 26.2 production semantic trace and advertised-capability contract.
- `minecraft-client-e2e.yml` — broader production-client runtime/framebuffer E2E evidence.
- `minecraft-render-differential.yml` — deterministic native OpenGL reference versus Mithril DirectMetal rendering comparison.

Those heavy workflows are evidence sources during migration, not permission to grow this architecture indefinitely. When #17–#19 have been semantically reconciled, route the surviving evidence to this canonical line, port reusable tests and missing behavior into the clean tree, then retire the dated source branches and eventually this migration line itself.
