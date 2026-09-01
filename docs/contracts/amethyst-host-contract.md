# Amethyst / LWJGL host contract

This document defines the stable **host seam** between Mithril-Wrapper and a Minecraft Java launcher such as Amethyst. It describes ownership and observable behavior; implementation details remain authoritative in `src/egl/*`, public exports and focused host tests.

## Scope

The host seam is responsible for:

1. loading the Mithril library and resolving exported EGL/GL ABI;
2. EGL display/config/context/surface negotiation;
3. passing an Apple native-window/layer handle to the backend;
4. current-context and default drawable state initialization;
5. swap/present entry and surface sizing;
6. clean teardown and error reporting.

It does **not** own general OpenGL semantics. Once a GL call enters the frontend, state/object/error behavior belongs to `src/gl/*`, `src/state/*`, `src/shader/*` and the backend-neutral lowering contract.

## Renderable-API negotiation

Mithril exposes desktop OpenGL semantics to LWJGL, but the current Amethyst bridge may negotiate through EGL's OpenGL ES 3 bit/API. `src/egl/impl.cpp` therefore treats the ES3 renderable bit as an explicit **host compatibility alias**, alongside `EGL_OPENGL_BIT`.

Invariant:

> EGL host negotiation aliases must not silently change the GL profile/semantics exposed by Mithril's exported desktop OpenGL ABI.

If host compatibility needs another negotiation alias, add it at the EGL seam and prove it with a host oracle; do not fork renderer semantics throughout the backend.

## Display/config/context

The EGL implementation owns one process-level display/config/context/surface model today. Callers must treat returned handles as opaque.

The config matcher accepts the color/depth/surface/renderable constraints represented by the current implementation and rejects unsupported requirements rather than pretending they exist. In particular, configuration capability must remain consistent with the actual default/native target implementation.

Context binding must support both:

- binding the valid Mithril context/surface pair;
- explicit unbind using `EGL_NO_CONTEXT` + `EGL_NO_SURFACE`.

On the first successful current-context bind, Mithril initializes default viewport and scissor from backend target dimensions. Native backends must not later reinterpret that initialization as a different coordinate-space contract.

## Native window and surface ownership

`eglCreateWindowSurface` / `eglCreatePlatformWindowSurface` pass the supplied native-window pointer through `backend::SetNativeWindow`.

Invariant:

> A native handle that cannot establish the required backend surface/presentation state must fail closed at surface creation. It must not return a nominally valid EGL surface that can never present.

Current code maps rejection to `EGL_BAD_NATIVE_WINDOW`.

Pbuffer surfaces do not own a native window and explicitly clear the backend native-window binding.

Destroying a surface clears the backend native window before invalidating the local surface state.

## Surface dimensions

`eglQuerySurface(EGL_WIDTH/EGL_HEIGHT)` reflects backend target dimensions rather than maintaining an unrelated EGL-side size cache.

The presentation/backend seam owns synchronization between the real native drawable size and the default framebuffer target. A renderer fix must not create separate unsynchronized notions of window extent in EGL, frontend state and backend resources.

## Swap semantics

For a window surface, `eglSwapBuffers` delegates to `backend::SwapBuffers()` and reports `EGL_BAD_SURFACE` if that operation cannot be performed.

For a non-window/pbuffer surface, swap is a flush boundary rather than a display operation and currently calls `backend::SubmitFlush(false)`.

Invariant:

> `EGL_TRUE` from a window swap is evidence that the backend accepted the swap operation, not by itself proof that pixels appeared correctly on a physical display.

Presentation correctness therefore needs its own evidence plane (`amethyst_egl_smoke`, hosted platform runtime, and physical-device evidence when the claim requires it).

## GL symbol resolution

LWJGL-visible OpenGL entry points must exist as exported symbols in the Mithril library. The current `eglGetProcAddress` implementation returns null and intentionally lets callers fall back to direct symbol resolution.

Therefore:

- do not claim proc-address coverage merely because the exported symbol exists;
- do not remove required exported GL symbols assuming EGL proc lookup will replace them;
- if `eglGetProcAddress` becomes a real resolver, add a focused ABI/proc oracle before changing this contract.

## Error ownership

EGL errors are thread-local host/API state and are consumed by `eglGetError`-style semantics. Invalid context/surface/native-window cases must be rejected at the host seam before malformed state reaches native rendering.

GL errors remain owned by the GL semantic layer; do not translate generic GL failures into launcher-specific EGL behavior unless the EGL operation itself failed.

## Coordinate/origin boundary

Internal GL framebuffer/texture semantics must remain stable regardless of native platform conventions. Any unavoidable Apple display-origin/format conversion belongs at the explicit backend-to-presentation seam, not scattered through GL draw, texture and readback rules.

This is especially important for DirectVulkan migration work: a legacy y-flip fix is only reusable after identifying whether it represents GL storage semantics, native backend viewport behavior, or the final presentation conversion.

## Evidence

Use the cheapest applicable prefix:

1. EGL/GL focused contract smoke;
2. backend semantic/readback oracle;
3. `amethyst_egl_smoke` or equivalent host bridge oracle;
4. hosted Apple runtime where the runner can exercise the seam;
5. physical device only for claims the hosted environment cannot establish.

Every result must be bound to the exact source/tree/binary identity under test. A harness commit and a separately checked-out product candidate are two different identities.

## Change rule

A change at this seam is complete only when it answers all four questions:

- What host-visible behavior changed?
- Which layer owns the rule?
- What is the smallest oracle that would fail if the rule regressed?
- Does the change alter only host adaptation, or does it imply a shared GL/lowering invariant that belongs higher in the tower?

If the same fix appears necessary in both Metal and Vulkan backend code, first test whether the real owner is the shared GL/lowering layer.
