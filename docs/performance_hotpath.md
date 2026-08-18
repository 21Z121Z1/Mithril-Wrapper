# Hot-path metadata invariants

Renderer metadata whose cardinality is fixed by the OpenGL or shader contract
should use fixed-capacity storage rather than heap-backed containers in the
steady-state draw path.

Current bounds used by the shared frontend are:

- vertex attribute locations: 16;
- user uniform blocks: 12 per shader stage, therefore at most 24 resolved
  stage-specific bindings;
- texture units: 16 per shader stage, therefore at most 32 resolved sampled
  bindings.

These bounds apply only to small metadata. Transient vertex/index byte payloads
remain owning dynamic storage when compatibility lowering genuinely requires
it; do not copy bulk payloads into a fixed command arena merely to make the
command type look simpler.

A fixed-capacity overflow is a renderer-invariant violation. It must not be
silently converted into heap spill, because doing so would hide a mismatch
between the advertised GL limits and the hot-path data model.

## First-use compilation boundaries

Native shader-program creation should happen at an existing compilation or
program-use boundary once the backend is initialized, rather than being hidden
inside the first draw. The draw path remains a correctness fallback for
programs linked before backend initialization.

Program prewarm and render-pipeline compilation are intentionally separate
problems. DirectMetal program prewarm covers SPIR-V translation, MSL library
creation, and native shader functions. A render PSO additionally depends on
vertex layout, render-target formats/sample count, raster state, blending, and
depth/stencil state; PSO compilation therefore needs its own cache/precompile
strategy and must not be reported as solved by program prewarm.

Because the program-creation contract is shared, every prewarm change must also
pass the Vulkan reference build/regression gate in addition to the DirectMetal
macOS semantic suite and iPhoneOS arm64 shipping build. Platform-specific
first-use optimization is not allowed to silently fork shared GL semantics.
