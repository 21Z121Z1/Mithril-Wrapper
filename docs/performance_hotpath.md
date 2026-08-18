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
