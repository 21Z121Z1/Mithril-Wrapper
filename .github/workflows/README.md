# GL semantic integration Actions

This branch keeps three durable evidence planes:

- `gl-semantic-closure.yml` — DirectMetal GPU semantic oracle ledger plus real Minecraft 26.2 production semantic trace and advertised-capability contract.
- `minecraft-client-e2e.yml` — the broader production-client runtime/framebuffer E2E evidence path.
- `minecraft-render-differential.yml` — deterministic native OpenGL reference versus Mithril DirectMetal rendering comparison.

Terrain, texture/FBO, state, sampler, raster, pixel-store, buffer/UBO, MRT, query/sync, and production-API probes belong in the semantic closure oracle ledger rather than separate workflows. The differential workflow owns the native Linux reference; do not duplicate it as a standalone workflow.
