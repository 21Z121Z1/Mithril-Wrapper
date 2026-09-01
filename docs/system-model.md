# Mithril-Wrapper system model

This document defines the stable mental model of the repository. It is deliberately more stable than branch names, individual bugs or milestone checklists.

The project is not a collection of GL wrappers plus two renderers. It is a compatibility system whose job is to preserve an observable host contract while progressively lowering that contract into native GPU work and then proving that the resulting behavior is the same behavior Minecraft expects.

## The abstraction tower

```text
L7  Convergence / release identity
    exact product SHA + branch ownership + evidence bundle
                         |
L6  Evidence and oracles
    semantic tests -> backend runtime -> host seam -> Minecraft E2E -> device
                         |
L5  Platform / presentation seam
    EGL host lifecycle -> CAMetalLayer / iPhoneOS package / swap-present
                         |
L4  Native execution engines
    DirectMetal (shipping)            Vulkan (reference/fallback)
                         \           /
L3  Backend-neutral lowering contract
    immutable draw/resource/state identity + lifetime/version information
                         |
L2  Observable GL semantic model
    state, objects, shaders, buffers, textures, FBO, queries, sync, errors
                         |
L1  Host/API contract
    exported EGL/GL ABI + LWJGL call behavior + context/surface semantics
                         |
L0  Acceptance target
    Minecraft Java renders correctly, predictably and efficiently on Apple
```

The direction matters. Higher layers define meaning; lower layers implement it. A backend may exploit Metal/Vulkan capabilities aggressively, but it may not silently redefine the observable semantics owned above it.

## Layer ownership

### L0 — acceptance target

Questions answered here: What user-visible behavior is the system trying to preserve? What does “working” mean?

Primary references: `README.md`, `CHECKLIST.md`, Minecraft E2E oracles.

A milestone or screenshot is evidence about L0, not an implementation boundary.

### L1 — host/API contract

Owns exported EGL/GL entry points, context and surface lifecycle, proc lookup, and the behavior that LWJGL observes at the ABI boundary.

Primary code: `src/egl/*`, generated GL exports, public headers.

Invariant: host adaptation is narrow. Amethyst/GLFW/platform peculiarities should not leak through every renderer subsystem.

### L2 — observable GL semantic model

Owns API state and object semantics: error generation, program state, vertex input, buffer storage, texture state, framebuffer completeness, queries, synchronization and all rules that are observable before choosing a native backend.

Primary code: `src/gl/*`, `src/state/*`, `src/shader/*`.

Invariant: when DirectMetal and Vulkan should behave identically because OpenGL says so, encode that fact once here or in L3. Duplicating it in both backends creates semantic forks.

### L3 — backend-neutral lowering contract

This is the most important architectural seam for long-term agent work. It converts mutable GL state into explicit native-work descriptions.

Primary code: `src/backend/*` plus shared structures passed to native engines.

Good L3 data is:

- explicit rather than reconstructed from global GL state;
- numeric/structural rather than presentation-string based on hot paths;
- immutable for the lifetime of queued native work;
- annotated with object generation/version/lifetime where native resources may outlive frontend mutation;
- separated into state shared by many subdraws and geometry unique to one draw when doing so removes redundant work;
- strong enough that native backends do not need to query the GL frontend behind the contract.

A recurring design test is: “Could an agent understand one draw by inspecting this contract without mentally executing the entire GL state machine?” The closer the answer is to yes, the better the seam.

### L4 — native execution engines

DirectMetal is the Apple shipping engine. Vulkan remains an isolated reference/fallback engine.

Primary code: `src/metal/*`, `src/vk/*`.

DirectMetal may own Metal-specific residency, PSO/sampler caching, command encoding, render-target construction and synchronization. Vulkan may own the equivalent Vulkan mechanisms. Neither should own generic GL error/completeness/state semantics merely because that is where a symptom became visible.

Build boundary: `mithril_direct` must stay free of Vulkan/MoltenVK source and link dependencies. `CMakeLists.txt` and `scripts/verify_directmetal_artifact.sh` make this an executable invariant rather than prose.

### L5 — platform/presentation seam

Owns the last transition between the renderer and the real Apple host: CAMetalLayer, drawable sizing, swap/present, iPhoneOS packaging and host integration.

This layer deserves independent evidence because a headless semantic test can be perfect while the real presentation chain is broken.

Primary evidence includes `amethyst_egl_smoke`, shipping Mach-O/ABI checks and hosted platform runtime validation.

### L6 — evidence and oracles

Evidence is part of the architecture, not an afterthought. Every important contract should have the cheapest oracle that can falsify it.

See `docs/evidence-model.md` for tiers and exact-SHA rules.

Key invariant: CI status is metadata about an execution. The actual proof is the relation between a claim, an oracle, an environment, an artifact/output and an exact implementation SHA.

### L7 — convergence / release identity

Owns which implementation is considered current, which legacy result is only a migration source, and which evidence belongs to the shipping candidate.

Primary references: `docs/agent/status.md`, `docs/branches.md`, `docs/agent/branch-ledger.md` and PR history.

This is intentionally separated from L0-L6 because branch topology changes much faster than the architecture.

## End-to-end ownership example

Consider “Minecraft samples a texture uploaded through a PBO with non-default unpack state.”

1. L1 receives normal GL calls through the exported ABI.
2. L2 owns `GL_UNPACK_*`, PBO binding/storage semantics and error rules.
3. L3 resolves an explicit upload description: source object/generation, row/image/skip layout, target subresource and format.
4. L4 performs the native Metal or Vulkan transfer without reinterpreting GL pixel-store rules independently.
5. L5 matters only if the resulting image is eventually presented.
6. L6 first proves pixel-store/PBO semantics in a focused oracle, then proves the real client path if Minecraft uses a materially different call pattern.
7. L7 records which exact implementation and evidence may be promoted.

If a DirectVulkan experiment discovers a missing `GL_UNPACK_SKIP_ROWS` rule, the durable result is not “remember branch X.” The result should become an L2/L3 rule plus a regression, then be semantically ported into the clean tree.

## State, ownership and lifetime

Most difficult renderer failures in this repository fall into one of four categories. Agents should classify them before patching.

1. **Semantic state** — the frontend modeled the wrong GL behavior.
2. **Lowering identity** — the right semantics existed but the backend key/snapshot omitted a field or reconstructed mutable state too late.
3. **Native lifetime/synchronization** — GPU work retained a resource, pointer or generation past safe lifetime or crossed a command/presentation boundary incorrectly.
4. **Evidence mismatch** — the test, artifact or SHA did not actually prove the claim being made.

This classification is more reusable than organizing work around symptom names such as “red screen” or “GUI corruption.”

## Performance model

Performance work should preserve the same tower rather than bypass it.

Preferred sequence:

1. identify a measured or structurally obvious repeated cost;
2. decide which layer owns the redundant work;
3. make identity/lifetime explicit before removing copies or synchronization;
4. preserve the semantic oracle;
5. add a structural/performance counter only when it distinguishes the intended shape;
6. prove no backend boundary or host behavior regressed.

Examples already represented in DirectMetal work include fixed-capacity API-bounded metadata, numeric cache keys, resident resource generations, shared MultiDraw state, uniform snapshots, program prewarm and asynchronous PSO preparation. The common architectural direction is to move work from per-draw rediscovery toward stable typed identities while keeping ownership explicit.

## Information architecture

Repository knowledge has four lifetimes and should be stored accordingly:

| Lifetime | Home | Example |
| --- | --- | --- |
| architectural | `docs/system-model.md`, `docs/evidence-model.md` | frontend owns GL semantics |
| current/reconcilable | `docs/agent/status.md`, branch ledger | which branch is a migration source |
| executable | tests, CMake boundaries, validation scripts | incomplete FBO must fail closed |
| historical | PRs, commits, artifacts | why a particular 2026-08 experiment existed |

Do not copy historical narratives upward unless they become a stable rule. Prefer converting repeated lessons into a type, invariant, test, manifest field or CI boundary.

## Dependency direction

Keep conceptual dependencies acyclic:

- L1/L2 may depend on generic L3 types, not on Metal/Vulkan implementation internals.
- L4 depends on the L3 contract and native platform APIs.
- L5 composes L1/L4 with the host platform; renderer semantics should not depend on a specific launcher.
- L6 observes lower layers and may use test-only hooks; production code must not depend on CI harnesses.
- L7 references evidence and histories but must not redefine semantics.

When a proposed shortcut points upward — for example native backend code reaching into mutable GL tables after a draw has been queued — treat it as an architectural warning.

## Extension rule

When adding a capability, add the smallest complete vertical slice:

`semantic contract -> lowering data -> native implementation -> focused oracle -> appropriate evidence plane -> status/ledger update if convergence changes`

A feature is easier for future agents to extend when each layer has one obvious owner and the proof is adjacent to the behavior it protects.
