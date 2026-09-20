# Validation

[中文版本](validation.zh-CN.md)

## Evidence must match the claim

Use the lowest-cost test that can falsify the claim. Do not replace a required
stronger environment with a weaker test.

The normal order is:

1. Static checks and source invariants.
2. A focused semantic regression.
3. The affected CTest backend label.
4. Build, ABI, and package checks.
5. Hosted Apple runtime evidence.
6. Real Minecraft acceptance.
7. Physical-device presentation or matched performance measurement.

A failed lower level blocks a stronger claim until the failure is explained.

## Focused semantic tests

Tests under `tests` are small native programs. CMake registers them in
`cmake/MithrilSmokeTests.cmake`.

Use a focused test to prove an EGL, OpenGL, shader, resource-lifetime, or native
execution invariant. A useful regression fails on the old behavior and passes
on the corrected behavior. It should inspect observable output or an explicit
structural counter, not only the absence of a crash.

Shared semantic changes must run both labels when the environment supports
both:

```bash
ctest --test-dir build-direct -L directmetal --output-on-failure
ctest --test-dir build -L vulkan --output-on-failure
```

A backend-specific change does not require an unrelated backend test, but it
must not silently change the shared contract.

## Package and boundary evidence

The DirectMetal shipping artifact has a narrow C ABI and a Vulkan-free build
boundary. The macOS gate checks the dynamic library and generated boundary
manifest. The iPhoneOS gate checks architecture, platform, deployment target,
install name, exported symbols, and signing viability.

A macOS test result is not proof of an iPhoneOS package. An iPhoneOS cross-build
is not proof of a physical-device frame.

## Platform runtime evidence

The manual `platform-runtime-validation` workflow runs on macOS 26 Apple
Silicon. One job executes the registered DirectMetal suite on a real hosted
Metal GPU. The second job builds for arm64 iOS Simulator and runs selected
smokes through an application bundle.

Run this workflow when a change affects Metal runtime behavior, the
CAMetalLayer seam, Simulator behavior, or a claim that the normal macOS 15
matrix cannot prove.

## Minecraft evidence

A semantic oracle and a real Minecraft run answer different questions.

- A focused oracle identifies the rule and protects it from regression.
- A real Minecraft run proves that the current client, launcher, host bridge,
  library identity, and presentation path work together.

A real rendering claim needs recognizable client output. A changing hash, a
nonblack image, or a synthetic user-framebuffer image is not sufficient by
itself. Inspect terrain, textures, GUI or HUD, and their spatial relationships
under a controlled scene.

Do not commit decompiled Minecraft source or private fixtures. Keep such input
outside Git and outside CI artifacts.

## Performance evidence

Make correctness pass first. Measure performance only with matched conditions:
the same world, camera, render distance, client configuration, renderer path,
build identity, and measurement window.

Structural counters can prove that an allocation, upload, compilation, or
state-resolution event was removed. They do not prove a frame-time improvement.
Do not publish a percentage without paired measurements.

## Capability language

Use precise terms:

- **exported**: the ABI symbol exists.
- **accepted**: the call is parsed and does not immediately reject the input.
- **partial**: some observable cases are implemented.
- **focused-tested**: a registered regression proves stated cases.
- **client-validated**: a named real client completed a stated scenario.
- **unsupported**: the implementation rejects or does not implement the case.

Do not use **conformant** unless an appropriate conformance suite and its exact
scope support that statement.

## Exact proof subject

Record the commit SHA and tree that each result tested. For pull requests, the
candidate head and GitHub's synthetic merge result can be different proof
subjects. State which one ran.

When a job fails, inspect its steps and logs. Classify the failure as product
code, semantic regression, test defect, dependency, environment, permission,
infrastructure, or a known pre-existing failure. Do not use reruns or weaker
assertions to hide a deterministic failure.
