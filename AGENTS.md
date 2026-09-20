# Agent instructions

Mithril-Wrapper preserves the EGL and OpenGL behavior that Minecraft Java and
LWJGL can observe. DirectMetal is the Apple shipping backend. Vulkan is a
separate reference backend. Exported symbols do not prove implemented behavior.

## Read and ownership

Read `README.md` for build commands, `docs/architecture.md` for ownership and
limits, then the affected source and tests. Read `docs/validation.md` when a
change affects an evidence claim. Obtain live branch, PR, and CI state from Git
and GitHub, not from a Markdown status table.

- `src/egl` owns EGL objects, current bindings, surfaces, and host lifecycle.
- `src/gl` and `src/state` own GL state, objects, errors, queries, pixel transfer,
  framebuffer rules, synchronization semantics, and resource lifetime.
- `src/shader` owns translation, reflection, resource mapping, and linked
  interfaces, including the physical layout of lowered loose uniforms.
- `src/backend` carries resolved intent, not native handles or hidden GL state.
- `src/metal` and `src/vk` own native resources, commands, and synchronization.
  Native backends can use different execution strategies.

Move shared GL rules to their owner. Do not copy those rules into both backends.
Do not add an abstraction only to make the backends look alike.

## Change and verification

Inspect the current commit and affected paths. Run the smallest relevant
baseline. State the observable invariant, change its owner, and add a focused
regression when it can detect a real failure. Review the final diff.

Use the build commands in `README.md`. Run the affected CTest label:

```bash
ctest --test-dir <build-directory> -L directmetal --output-on-failure
ctest --test-dir <build-directory> -L vulkan --output-on-failure
```

Shared GL, EGL, shader, or backend changes need both labels. Native changes need
the affected backend and package checks. Consult `docs/validation.md` for host,
client, device, and performance evidence. Test the latest commit or identify the
exact tested tree. Inspect failed jobs and logs before retrying.

## Repository rules

Use one product candidate for `main`. Do not merge without authorization.
Treat `archive/*` refs as provenance, not as product trees or merge bases.
Extract useful invariants from disconnected history into current owners and
tests. Record a branch's final SHA and preservation evidence before retirement.

Prefer deletion and simplification. Use existing tests and build scripts before
adding a workflow. Do not weaken assertions to obtain a passing result.
Do not commit binaries, logs, private fixtures, decompiled Minecraft source, or
session reports. Generated Minecraft reference files are local research input;
do not upload them as artifacts.

Keep durable documentation small. English is canonical; synchronize existing
Chinese translations. Put changing SHAs, branch counts, and run IDs in PRs or Git
history. Distinguish verified, partial, and unverified claims. Synthetic tests
cannot establish Minecraft correctness, physical presentation, or conformance.
