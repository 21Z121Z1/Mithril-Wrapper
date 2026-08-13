# DirectMetal OpenGL 3.3 semantic matrix

This is the capability ledger for the production DirectMetal path. A symbol is
not considered supported merely because it is exported. Update this document
from source plus executable evidence whenever a capability changes.

Snapshot: 2026-08-13, based on the verified
`agent/directmetal-fbo-sampling-a17pro` closure branch after framebuffer,
lifecycle, sync, layered-slice, fail-closed and artifact-contract hardening.

Reference heads inspected for this snapshot:

- `origin/feature/direct-metal@ca05c06` and
  `origin/feature/direct-metal-ios-amethyst@0a3c678`;
- `uniaball/main@e04f5a4`;
- `herbrine/main@ddc9c3d`,
  `herbrine/implement-gl33-core-renderer@9778de1`, `herbrine/fix@36b6913`,
  and the remaining drawable/black-screen experiment branches;
- Amethyst `main@64c5c9c4` and its Mithril integration reference
  `Amethyst-iOS-MyRemastered@696e230e`.

## Status vocabulary

| Status | Meaning |
| --- | --- |
| exact | Observable GL behavior is implemented and an execution test reaches DirectMetal. |
| partial | A useful subset executes, but legal GL 3.3 cases remain rejected, approximated, or unverified. |
| stub | The exported function only logs/returns and does not implement its specified effect. |
| explicit unsupported | The call fails closed with a GL error or backend failure and a diagnostic; it never fabricates success. |
| untested | Source suggests an implementation, but no relevant DirectMetal execution evidence exists. |

## Minecraft-prioritized matrix

| API family | DirectMetal status | Current execution evidence | Remaining semantic boundary / highest-value gap |
| --- | --- | --- | --- |
| EGL config/context/window surface | partial | `contract_smoke`, `amethyst_egl_smoke` | Hosted DirectMetal covers nil-drawable failure/recovery, old-surface destruction, same-context rebind to a replacement CAMetalLayer and replacement physical size/pixels. Physical iPhoneOS background/resume, multiple concurrent contexts/surfaces, and current Amethyst main integration remain device-only gates. |
| Default framebuffer size/present | partial | `amethyst_egl_smoke` asymmetric source/reference/drawable capture on direct and buffer-backed paths | `eglSwapBuffers` appends RGBA-to-BGRA presentation to the pending GL frame command buffer, submits without an implicit CPU wait, and synchronizes non-square `drawableSize` changes before drawable acquisition. All GL targets keep row zero as the GL bottom; only the CAMetalLayer seam converts that storage to Apple's top-origin display order. Top-blue/bottom-red pixels now verify both ends after resize. A one-time, current-size byte probe selects direct render-target sampling when exact; if and only if direct fails while texture→private buffer→fragment `uchar4` passes, the backend reuses one 256-byte-row-aligned compatibility buffer. Both failures reject surface creation. Physical Apple GPU runs require source, reference, and real drawable bytes; Apple Paravirtual may skip only unreadable all-zero drawable bytes after source/reference pass. Logical size vs UIKit scale and visible WindowServer/real-device presentation remain device-only; nil-drawable recovery and surface replacement are permanent hosted execution regressions. |
| Clear/viewport/scissor | exact for tested color/depth/stencil and masks; partial overall | `state_smoke`, `fbo_smoke`, `directmetal_fbo_smoke`, `query_smoke` | Conditional draw, `glClear`, and `glClearBuffer*` now consume real occlusion results. Per-draw-buffer indexed state and every integer clear format remain incomplete. |
| Depth/stencil/blend/cull/polygon | partial | `fbo_smoke`, `stencil_persistence_smoke` pixel assertions | Basic depth, stencil, blend, cull, front-face and line polygon mode execute. Packed D24S8 keeps distinct GL depth/stencil attachment slots while the common same-object case executes exactly; different depth/stencil objects fail framebuffer completeness rather than aliasing. Blend enum validation, independent indexed blend/masks, point polygon mode, and several uncommon factors remain incomplete or rejected. |
| Multisample/sample/raster state | partial / explicit unsupported gaps | MSAA FBO and `sampler2DMS` cases in `fbo_smoke`; `unsupported_stub_smoke` | `glGetMultisamplefv` is explicitly unsupported with `GL_INVALID_OPERATION`. Sample coverage/mask, alpha-to-coverage, rasterizer discard, dithering, logic op, program point size and line/point width are not all materialized by DirectMetal; unsupported exported entry points must fail closed. |
| Shader compile/link and interface locations | partial | `shader_smoke`, `draw_smoke`, `provoking_vertex_smoke` | Vertex/fragment GLSL, explicit/bound attributes and fragment outputs execute. Geometry shader execution and transform-feedback varyings are not connected; dual-source outputs fail closed. |
| Loose uniforms and arrays | exact for reflected VS/FS scalar/vector/matrix cases | `matrix_uniform_smoke`, `uniform_array_smoke`, `uniform_type_smoke`, `uniform_integer_getter_smoke` | Cross-context sharing and larger Minecraft shader corpus remain untested. |
| Uniform blocks | exact for tested VS/FS blocks | `ubo_smoke` | Up to 12 blocks per stage use resident versioned buffers. Geometry-stage blocks and broader layout corpus remain untested. |
| Buffer objects / mapping | partial | `state_smoke`, `draw_smoke`, `ubo_smoke`, pixel pack/unpack cases | Array, element, uniform and pixel pack/unpack buffers execute. Transform-feedback buffer binding/storage is not implemented. Mapping flags and synchronization are not a complete GL 3.3 implementation. |
| Vertex arrays and typed formats | partial | `typed_vertex_smoke`, `packed_vertex_attrib_smoke`, `draw_smoke` | Resident float/half/normalized 8/16-bit and integer 8/16/32-bit formats execute. Current integer attributes and all eight packed `glVertexAttribP*` setters preserve context/array state and reach DirectMetal pixels. Packed `glVertexAttribPointer` array formats and the complete legal conversion matrix remain gaps. |
| Draw/topology/restart/provoking vertex | exact for tested triangle/line families | `draw_smoke`, `typed_vertex_smoke`, `provoking_vertex_smoke` | Arrays/elements/instance/base-vertex/multidraw, primitive restart and FIRST/LAST flat semantics execute. Points and geometry-shader topologies remain incomplete. |
| 2D textures, mipmaps, pixel store | partial | `texture_smoke`, `sampler_smoke`, PBO cases | Common RGBA8 uploads, subimages, mips and pack/unpack offsets execute. The legal GL format/type matrix, compressed native formats, immutable storage extensions, and 3D mip-chain sampling are incomplete. |
| Sampler objects / multiple samplers | exact for tested GL 3.3 state | `sampler_smoke`, `sampler_array_smoke` plus binding diagnostics | Separate sampler lifetime, wrap/filter/compare, cross-stage use, fixed arrays and deletion-after-draw execute. Arbitrary/integer border color and nonzero LOD bias fail closed. |
| Buffer textures | partial | `buffer_texture_smoke` | Typed `RGBA8/R8I/R8UI/R32I/R32UI/R32F` execute with buffer versioning. Other legal texel formats fail closed. |
| FBO / MRT / MSAA / subresources | partial | `fbo_smoke`, `layered_fbo_smoke`, `directmetal_fbo_smoke`, `stencil_persistence_smoke` | Color/depth/renderbuffer FBOs, MRT, 4x resolve, mip selection, concrete 2D-array slices, 3D depth-plane readback and packed depth/stencil persistence execute. `layered_fbo_smoke` writes layer 1 green, layer 0 red, returns to layer 1 green, and proves whole-level layered `glFramebufferTexture` fails closed without changing the prior slice. Whole-level layered rendering, scaled/filtered depth/stencil blits and multisample array/depth textures remain explicit unsupported boundaries. |
| Readback | partial | draw/texture/FBO smokes | RGBA8 default and offscreen framebuffer readback use the same GL row order and selected PBO layouts execute. The complete GL format/type conversion matrix and depth/stencil readback are incomplete. |
| GLsync | exact for current single-context queue contract | `sync_smoke` | Fence submission, finite/infinite client wait, server wait, explicit `glFlush`/`glFinish`, in-flight deletion and 32-frame alternating submission/name reuse execute without ordering or lifetime failures. Cross-context sharing is unclaimed. |
| Occlusion query | exact for one active occlusion target | `query_smoke` | Samples-passed/any-samples-passed, reuse, availability, multi-command-buffer aggregation and deletion lifetime execute. Simultaneous overlap of both occlusion target classes fails closed. |
| Conditional render | exact for completed occlusion results | `query_smoke` real Metal pixel assertions | WAIT/BY_REGION_WAIT block for the result; NO_WAIT modes execute while unavailable and use a completed result when observable. Draw, `glClear`, and `glClearBuffer*` are gated. |
| Timer / primitive-count query | explicit unsupported | `query_smoke` timer error assertion | No fake timestamps or primitive counts. A native counter-sample design is required before support is advertised. |
| Transform feedback | explicit unsupported | `unsupported_stub_smoke` | Begin/end, varying declaration/query and transform-feedback buffer execution are not implemented; all exported entry points fail closed with `GL_INVALID_OPERATION` rather than fabricating success. |
| Resource/binding lifecycle and draw cost | partial | resident-buffer tests and `DIRECTMETAL_BINDING_STATS` in `sampler_smoke` | Texture/sampler state shadowing, stable resource versions, frame upload arenas and redundant bind elision exist. Uniform/pipeline/buffer mutation counters and Minecraft-scale paired measurements remain missing. |

## Remaining exported stubs

After packed current attributes move to a real implementation, 10 generated GL
3.3 entry points remain exported but explicitly unsupported:

- transform feedback: `glBeginTransformFeedback`, `glEndTransformFeedback`,
  `glTransformFeedbackVaryings`, `glGetTransformFeedbackVarying`;
- point/sample state: `glPointParameterf`, `glPointParameterfv`,
  `glPointParameteri`, `glPointParameteriv`, `glGetMultisamplefv`;
- multisample arrays: `glTexImage3DMultisample`.

The generated unsupported body logs and enqueues `GL_INVALID_OPERATION`.
`unsupported_stub_smoke` dynamically resolves and calls all ten entries, checks
the error before/after each call, and prevents symbol-count-only false support.

## Reference findings converted into DirectMetal requirements

- Uniaball's descriptor exhaustion and dangling `pImageInfo` fixes translate
  into stable DirectMetal texture/sampler ownership and bounded binding
  materialization, not Vulkan descriptor sets. The current binding diagnostic
  ABI covers only part of this acceptance surface.
- Uniaball's stencil-store fix exposed a cross-render-pass persistence
  requirement. `stencil_persistence_smoke` now forces separate Metal command
  buffers and pixel-checks both default and packed offscreen targets.
- Uniaball's pending-frame and zero-sized-layer fixes exposed a presentation
  acceptance gap. `amethyst_egl_smoke` now captures the real BGRA drawable and
  checks pending clear submission plus physical-size replacement pixels.
- Uniaball's packed-current implementation exposed the Minecraft-facing API
  family, while Khronos requires P1/P2/P3 to consume only the first 1/2/3
  fields and leave vertex-array state intact. `packed_vertex_attrib_smoke`
  permanently covers all eight entry points, both signedness modes, normalized
  extrema, invalid type/index recovery, disabled current values and re-enabled
  VBO execution.
- Uniaball's latest MoltenVK presentation fixes expose three backend-neutral
  requirements: format/channel correctness, resize/suboptimal recovery and a
  distinction between offscreen submission and live-layer presentation
  failure. DirectMetal keeps its GL default target RGBA and uses a BGRA render
  pipeline rather than adopting Vulkan target retargeting; the drawable pixel
  test guards that conversion. Transient drawable failure/recovery and same-context
  surface replacement are now permanent hosted regressions.
- Uniaball `e04f5a4` adds first-query physical sizing, D32S8 consistency and
  viewport Y conversion after an iPhone X black/top-left render. DirectMetal
  already initializes from `CAMetalLayer.drawableSize` and uses native
  `Depth32Float_Stencil8`; its missing requirement was asymmetric row identity.
  DirectMetal now keeps every GL target in bottom-origin GL storage order and
  converts only at CAMetalLayer presentation, with draw/read/texel/blit and
  top/bottom drawable pixels as permanent guards.
- Herbrine's black/red-screen history identifies permanent test targets:
  drawable size, default-vs-offscreen Y orientation, render area bounds,
  attachment replacement, sampler binding and presentation lifetime. Existing
  macOS smokes cover sampler binding, drawable resize, asymmetric
  default/offscreen orientation, transient nil-drawable recovery and same-context
  CAMetalLayer replacement. Physical iOS background/resume remains open.
- Amethyst main currently creates a `CAMetalLayer` and forwards that layer to
  EGL, but its main branch does not expose Mithril as a selectable renderer.
  The `Amethyst-iOS-MyRemastered` branch is integration reference evidence,
  not proof that current Amethyst + iPhoneOS + Minecraft executes DirectMetal.

## Closed hardening queue

Scores use 1–5 for correctness, Minecraft relevance, reference evidence,
architectural leverage, reproducibility, automatic verification, performance,
and inverse change risk.

| Candidate | Score | Verified closure |
| --- | ---: | --- |
| Separate depth/stencil FBO attachment semantics | 34/40 | Closed: independent frontend slots; same packed object executes; distinct native layouts fail completeness without aliasing. |
| CAMetalLayer interruption and surface replacement | 33/40 | Closed in hosted execution: nil drawable fails observably, pending work survives, and the same context presents through a replacement 72x40 CAMetalLayer. |
| Fail-closed remaining silent state/stub families | 31/40 | Closed for the remaining 10 generated exports: every call returns observable `GL_INVALID_OPERATION`, covered individually by CTest. |
| iPhoneOS artifact validator fail-closed behavior | 30/40 | Closed: file-based symbol checks avoid SIGPIPE; framework paths accept macOS versioned/iOS unversioned forms; deterministic self-test rejects a missing EGL symbol and 341 GL exports. |
