# Architecture

[中文版本](architecture.zh-CN.md)

## System boundary

Mithril-Wrapper implements the EGL and OpenGL behavior that its clients can
observe. Minecraft Java and LWJGL are the main clients. The project is a
compatibility system, not a direct one-call-to-one-call API translator.

The frontend resolves mutable API state before it submits native work. The
backend contract carries explicit draw state, resource identity, content
version, and lifetime information. A native backend must not reconstruct the
OpenGL state machine from hidden frontend state.

## Ownership layers

### Host and EGL

`src/egl` owns display, context, surface, current-binding, swap, and host-window
lifecycle behavior. Platform objects can enter through this layer, but generic
OpenGL policy must not.

Some Amethyst host bridges negotiate with `EGL_OPENGL_ES3_BIT` and
`EGL_OPENGL_ES_API` before LWJGL loads the desktop OpenGL symbols. Mithril
treats that request as a host compatibility alias. It must not change the GL
profile or semantics that the frontend exposes.

### OpenGL state and objects

`src/gl` and `src/state` own OpenGL-visible behavior. This includes object
names, binding, error generation, framebuffer completeness, draw and read
selection, pixel store, buffer and texture state, queries, synchronization,
and deletion semantics.

A command that must fail before native execution must fail in this layer.
Examples include an incomplete framebuffer, an invalid object relationship,
or an unsupported observable state combination.

### Shader semantics

`src/shader` owns GLSL rewriting, SPIR-V generation, reflection, uniform and
resource mapping, and linked vertex-to-fragment interface behavior. Backend
code consumes the linked result. A backend must not invent a different shader
interface rule. The shader owner resolves loose-uniform offsets and strides
once per linked stage. Native code consumes this layout and chooses its storage.

### Backend-neutral intent

`src/backend` defines the native execution contract. It contains resolved
pipeline state, dynamic state, vertex and index sources, uniform sources,
sampled resources, render-target identity, and lifetime/version data.

The contract has no Metal or Vulkan handle. Borrowed frontend data is valid only
for the synchronous backend call. A deferred backend must retain or copy the
required native resource before the call returns.

### Native execution

`src/metal` owns DirectMetal resource creation, pipeline creation, command
encoding, synchronization with Metal, and presentation through Metal objects.
It is the Apple shipping backend.

`src/vk` owns Vulkan resource creation, pipeline creation, command encoding,
and synchronization with Vulkan. It is a reference backend. It is also a
cross-backend regression target on Linux.

Native backends can differ in execution strategy. They cannot differ in a rule
that comes from EGL, OpenGL, or the linked shader program.

## Build boundaries

The `mithril_direct` target contains the shared frontend and DirectMetal. It
must not compile or link Vulkan source, Vulkan-Headers, MoltenVK, or a Vulkan
loader. CMake and `scripts/verify_directmetal_artifact.sh` enforce this boundary.

The `mithril_legacy` target contains the shared frontend and the Vulkan backend.
It is a separate artifact. On Apple platforms it uses a distinct output name.

## Presentation boundary

A semantic test can use an offscreen or default framebuffer to isolate a rule.
The final host behavior also depends on the real surface and presentation path.
`tests/amethyst_egl_smoke.mm` exercises the CAMetalLayer seam. Real Minecraft or
physical-device evidence is required for a claim that extends beyond this seam.

## Implementation limits

The implementation uses one global GL state and one EGL context and surface.
Thread-local EGL bindings do not provide isolated GL contexts. Independent
contexts, shared-context concurrency, and simultaneous surfaces are not supported.

The Vulkan reference backend has no host presentation path. Its swap operation
flushes offscreen work. It rejects user uniform blocks and texture-buffer
samplers. Its test label covers a subset of DirectMetal behavior, not an
independent implementation of all GL rules. Shared frontend bugs can affect both
backends. Cross-backend agreement alone is not conformance evidence.

## Placement rules

Use these rules when a change could fit in more than one directory:

- Put EGL lifecycle and surface rules in `src/egl`.
- Put API validation, state resolution, object identity, and error behavior in
  `src/gl` or `src/state`.
- Put shader-language and linked-interface rules in `src/shader`.
- Put explicit native-ready data shapes in `src/backend`.
- Put API-specific resource creation and command encoding in `src/metal` or
  `src/vk`.
- If both native backends need the same generic condition, move the condition
  upward instead of copying it.

Do not add an abstraction only to make the directory graph look symmetric. Add
one only when it removes duplicated meaning, makes ownership explicit, or makes
an invariant executable.

## Historical code

`main` is the product line. Historical `archive/*` refs preserve disconnected
research and old execution trees. They are not product branches and are not
wholesale merge sources.

To reuse historical work:

1. State the observable invariant.
2. Find or create a focused current test.
3. Implement the invariant in the current owner.
4. Record the historical commit or PR as provenance.
5. Validate the current tree.
