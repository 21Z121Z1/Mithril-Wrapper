# Mithril-Wrapper agent instructions

## Purpose

Mithril-Wrapper preserves the EGL and OpenGL behavior that Minecraft Java and
LWJGL can observe. The Apple shipping target uses DirectMetal. The Vulkan
target is a separate reference backend.

Do not measure progress by exported symbol count. A symbol can exist while its
behavior is partial or unsupported.

## Read order

Read only the material that the task needs:

1. `README.md` for the project boundary and build commands.
2. `docs/architecture.md` for semantic ownership.
3. The target source files and their adjacent tests.
4. `docs/validation.md` when the task changes a claim or proof obligation.
5. Live Git and GitHub data when branch, PR, or CI state affects the decision.

Do not use a Markdown branch table as live Git state.

## Ownership

- `src/egl`: EGL objects, host lifecycle, and surface integration.
- `src/gl` and `src/state`: OpenGL state, object, error, query, framebuffer,
  pixel-transfer, synchronization, and resource-lifetime semantics.
- `src/shader`: GLSL translation, SPIR-V generation, reflection, and linked
  shader-interface semantics.
- `src/backend`: resolved backend-neutral draw and resource intent.
- `src/metal`: DirectMetal execution only.
- `src/vk`: Vulkan execution only.
- `tests` and `cmake/MithrilSmokeTests.cmake`: executable behavior evidence.

A rule that comes from EGL, OpenGL, or linked shader behavior belongs in the
highest shared owner that can express it. Do not implement the same generic
rule independently in `src/metal` and `src/vk`.

## Change process

1. Inspect the current Git subject and the target source path.
2. Run the smallest relevant baseline test when the environment supports it.
3. State the observable invariant that the change must preserve or add.
4. Change the owning layer. Keep native backends free of duplicated GL policy.
5. Add or update one focused regression when practical.
6. Run the focused test, then the affected backend label.
7. Run packaging or real-client evidence only when the claim requires it.
8. Review the final diff for semantic duplication, lifetime errors, and stale
   documentation.

For shared EGL, GL, shader, or backend-neutral changes, validate both the
DirectMetal and Vulkan test labels. For a native-backend-only change, validate
that backend and any affected package boundary.

## Commands

Linux Vulkan reference:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DMITHRIL_BUILD_LEGACY=ON -DMITHRIL_BUILD_DIRECT=OFF
cmake --build build --parallel
ctest --test-dir build -L vulkan --output-on-failure
```

macOS DirectMetal:

```bash
cmake -S . -B build-direct -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DMITHRIL_BUILD_LEGACY=OFF -DMITHRIL_BUILD_DIRECT=ON
cmake --build build-direct --parallel
ctest --test-dir build-direct -L directmetal --output-on-failure
```

iPhoneOS package:

```bash
scripts/build_iphoneos.sh
```

Minecraft reference source for local investigation:

```bash
SRC="$(bash scripts/minecraft-reference.sh --print-path)"
```

The generated Minecraft files are private local research input. Never commit
or upload them.

## Repository rules

- Use `main` for product work. Treat `archive/*` as provenance, not as a merge
  base or a second product tree.
- Do not copy a disconnected historical tree into `main`. Extract an
  observable invariant, add a focused oracle, and implement it in the current
  owner.
- Do not add a permanent workflow for one bug. Put reusable logic in CTest,
  source validators, or build scripts.
- Do not weaken an oracle to make CI green.
- A synthetic smoke test cannot prove real Minecraft presentation. A Minecraft
  frame cannot replace a focused semantic test.
- Do not commit generated binaries, logs, private fixtures, decompiled
  Minecraft source, or transient investigation reports.
- Keep human documentation stable. Put current SHAs, branch counts, run IDs,
  and incident logs in GitHub history or the relevant PR, not in long-lived
  documentation.

## Completion evidence

Report the exact commit that was tested, the commands or Actions jobs that ran,
and each skipped proof with its reason. Separate verified behavior from
partial or unverified claims. Never claim general OpenGL conformance from ABI
coverage or Minecraft acceptance alone.
