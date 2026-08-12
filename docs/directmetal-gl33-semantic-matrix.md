# DirectMetal OpenGL 3.3 semantic matrix

This is the capability ledger for the production DirectMetal path. A symbol is
not considered supported merely because it is exported. Update this document
from source plus executable evidence whenever a capability changes.

Snapshot: 2026-08-12, based on
`codex/autonomous-direct-metal-next@ae8c1fb` plus the captured-drawable
presentation work that follows that base.

Reference heads inspected for this snapshot:

- `origin/feature/direct-metal@ca05c06` and
  `origin/feature/direct-metal-ios-amethyst@0a3c678`;
- `uniaball/main@2fa0e63`;
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
| EGL config/context/window surface | partial | `contract_smoke`, `amethyst_egl_smoke` | macOS `CAMetalLayer` presentation is covered through captured drawable pixels. Real iPhoneOS lifecycle, multiple contexts/surfaces, background/resume, and current Amethyst main integration are untested. |
| Default framebuffer size/present | partial | `amethyst_egl_smoke` same-command-buffer BGRA drawable/reference capture | `eglSwapBuffers` submits a pending clear without an explicit GL flush, converts the RGBA default target through the production presentation encoder, and synchronizes non-square `drawableSize` changes before acquiring the replacement drawable. Physical Apple GPU runs require both real drawable and same-pipeline reference bytes. Apple Paravirtual reports an explicit drawable-readback capability skip only when the reference passes and the drawable is all zero. Logical size vs physical pixels under UIKit scale, visible WindowServer/real-device presentation, nil drawable recovery, and surface replacement still need a standalone lifecycle app. |
| Clear/viewport/scissor | exact for tested color/depth/stencil and masks; partial overall | `state_smoke`, `fbo_smoke`, `directmetal_fbo_smoke`, `query_smoke` | Conditional draw, `glClear`, and `glClearBuffer*` now consume real occlusion results. Per-draw-buffer indexed state and every integer clear format remain incomplete. |
| Depth/stencil/blend/cull/polygon | partial | `fbo_smoke`, `stencil_persistence_smoke` pixel assertions | Basic depth, stencil, blend, cull, front-face and line polygon mode execute. Default and packed offscreen depth/stencil targets preserve stencil across completed Metal command buffers. Blend enum validation, independent indexed blend/masks, point polygon mode, and several uncommon factors are incomplete or rejected. |
| Multisample/sample/raster state | partial / silent-risk | MSAA FBO and `sampler2DMS` cases in `fbo_smoke` | `glGetMultisamplefv` is a stub. Sample coverage/mask, alpha-to-coverage, rasterizer discard, dithering, logic op, program point size and line/point width are shadowed but are not all materialized by DirectMetal. Do not count state getters as execution support. |
| Shader compile/link and interface locations | partial | `shader_smoke`, `draw_smoke`, `provoking_vertex_smoke` | Vertex/fragment GLSL, explicit/bound attributes and fragment outputs execute. Geometry shader execution and transform-feedback varyings are not connected; dual-source outputs fail closed. |
| Loose uniforms and arrays | exact for reflected VS/FS scalar/vector/matrix cases | `matrix_uniform_smoke`, `uniform_array_smoke`, `uniform_type_smoke`, `uniform_integer_getter_smoke` | Cross-context sharing and larger Minecraft shader corpus remain untested. |
| Uniform blocks | exact for tested VS/FS blocks | `ubo_smoke` | Up to 12 blocks per stage use resident versioned buffers. Geometry-stage blocks and broader layout corpus remain untested. |
| Buffer objects / mapping | partial | `state_smoke`, `draw_smoke`, `ubo_smoke`, pixel pack/unpack cases | Array, element, uniform and pixel pack/unpack buffers execute. Transform-feedback buffer binding/storage is not implemented. Mapping flags and synchronization are not a complete GL 3.3 implementation. |
| Vertex arrays and typed formats | partial | `typed_vertex_smoke`, `draw_smoke` | Resident float/half/normalized 8/16-bit and integer 8/16/32-bit formats plus exact current integer attributes execute. Eight `glVertexAttribP*` packed setters remain stubs. |
| Draw/topology/restart/provoking vertex | exact for tested triangle/line families | `draw_smoke`, `typed_vertex_smoke`, `provoking_vertex_smoke` | Arrays/elements/instance/base-vertex/multidraw, primitive restart and FIRST/LAST flat semantics execute. Points and geometry-shader topologies remain incomplete. |
| 2D textures, mipmaps, pixel store | partial | `texture_smoke`, `sampler_smoke`, PBO cases | Common RGBA8 uploads, subimages, mips and pack/unpack offsets execute. The legal GL format/type matrix, compressed native formats, immutable storage extensions, and 3D mip-chain sampling are incomplete. |
| Sampler objects / multiple samplers | exact for tested GL 3.3 state | `sampler_smoke`, `sampler_array_smoke` plus binding diagnostics | Separate sampler lifetime, wrap/filter/compare, cross-stage use, fixed arrays and deletion-after-draw execute. Arbitrary/integer border color and nonzero LOD bias fail closed. |
| Buffer textures | partial | `buffer_texture_smoke` | Typed `RGBA8/R8I/R8UI/R32I/R32UI/R32F` execute with buffer versioning. Other legal texel formats fail closed. |
| FBO / MRT / MSAA / subresources | partial | `fbo_smoke`, `directmetal_fbo_smoke`, `stencil_persistence_smoke` | Color/depth/renderbuffer FBOs, MRT, 4x resolve, mip selection, 2D-array slice, 3D depth-plane readback and packed depth/stencil persistence execute. Scaled/filtered, depth/stencil and multisample blits plus multisample array/depth textures are unsupported. |
| Readback | partial | draw/texture/FBO smokes | RGBA8 framebuffer readback and selected PBO layouts execute. The complete GL format/type conversion matrix and depth/stencil readback are incomplete. |
| GLsync | exact for current single-context queue contract | `sync_smoke` | Fence submission, finite/infinite client wait, server wait and deletion lifetime execute without an unconditional CPU drain. Cross-context sharing is unclaimed. |
| Occlusion query | exact for one active occlusion target | `query_smoke` | Samples-passed/any-samples-passed, reuse, availability, multi-command-buffer aggregation and deletion lifetime execute. Simultaneous overlap of both occlusion target classes fails closed. |
| Conditional render | exact for completed occlusion results | `query_smoke` real Metal pixel assertions | WAIT/BY_REGION_WAIT block for the result; NO_WAIT modes execute while unavailable and use a completed result when observable. Draw, `glClear`, and `glClearBuffer*` are gated. |
| Timer / primitive-count query | explicit unsupported | `query_smoke` timer error assertion | No fake timestamps or primitive counts. A native counter-sample design is required before support is advertised. |
| Transform feedback | stub / unsupported | none | Begin/end, varying declaration/query and transform-feedback buffer execution are not implemented. This is a capability gap, not supported-by-export. |
| Resource/binding lifecycle and draw cost | partial | resident-buffer tests and `DIRECTMETAL_BINDING_STATS` in `sampler_smoke` | Texture/sampler state shadowing, stable resource versions, frame upload arenas and redundant bind elision exist. Uniform/pipeline/buffer mutation counters and Minecraft-scale paired measurements remain missing. |

## Remaining exported stubs

After conditional rendering moves to a real implementation, 18 generated GL
3.3 entry points remain stubs:

- transform feedback: `glBeginTransformFeedback`, `glEndTransformFeedback`,
  `glTransformFeedbackVaryings`, `glGetTransformFeedbackVarying`;
- point/sample state: `glPointParameterf`, `glPointParameterfv`,
  `glPointParameteri`, `glPointParameteriv`, `glGetMultisamplefv`;
- multisample arrays: `glTexImage3DMultisample`;
- packed current attributes: `glVertexAttribP1ui`, `glVertexAttribP1uiv`,
  `glVertexAttribP2ui`, `glVertexAttribP2uiv`, `glVertexAttribP3ui`,
  `glVertexAttribP3uiv`, `glVertexAttribP4ui`, `glVertexAttribP4uiv`.

The generated stub body currently logs but does not itself enqueue a GL error.
Each family must either gain exact behavior or be converted to a precise
fail-closed path; it must not be marked supported by symbol count.

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
- Herbrine's black/red-screen history identifies permanent test targets:
  drawable size, default-vs-offscreen Y orientation, render area bounds,
  attachment replacement, sampler binding and presentation lifetime. Existing
  macOS smokes cover sampler binding, drawable resize and uniform-color
  presentation; asymmetric orientation and lifecycle interruption remain open.
- Amethyst main currently creates a `CAMetalLayer` and forwards that layer to
  EGL, but its main branch does not expose Mithril as a selectable renderer.
  The `Amethyst-iOS-MyRemastered` branch is integration reference evidence,
  not proof that current Amethyst + iPhoneOS + Minecraft executes DirectMetal.

## Next candidate queue

Scores use 1–5 for correctness, Minecraft relevance, reference evidence,
architectural leverage, reproducibility, automatic verification, performance,
and inverse change risk.

| Candidate | Score | Closure target |
| --- | ---: | --- |
| Separate depth/stencil FBO attachment semantics | 34/40 | Represent depth and stencil attachment selection honestly; support the common same-packed-object case and reject unsupported distinct layouts without aliasing. |
| CAMetalLayer interruption and surface replacement | 33/40 | Exercise nil-drawable failure, detach/rebind, replacement size and recovery without relying on Minecraft logs. |
| Packed vertex attribute setters | 32/40 | Decode legal packed current attributes with getter, error and shader-pixel evidence; remove eight generated stubs. |
| Fail-closed remaining silent state/stub families | 31/40 | Remove false support, prioritizing sample/raster state and transform-feedback declarations used by real workloads. |
