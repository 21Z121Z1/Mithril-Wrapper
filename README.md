# Mithril-Wrapper

[中文说明](README.zh-CN.md)

Mithril-Wrapper is an EGL and OpenGL compatibility layer for Minecraft Java
and LWJGL on Apple platforms. It resolves observable API behavior into explicit
backend-neutral draw and resource intent. Native backends execute that intent.

The Apple shipping target uses **DirectMetal**. The Vulkan target is a separate
reference backend. Linux CI uses the Vulkan target for cross-backend regression.
Apple builds can use it for explicit research, but it is not part of the
DirectMetal shipping artifact.

Mithril-Wrapper does not claim general OpenGL conformance. Exported symbols,
accepted calls, focused tests, Minecraft acceptance, and conformance are
different levels of evidence.

## Architecture

```text
Minecraft / LWJGL observable behavior
                 |
                 v
        EGL and host lifecycle
                 |
                 v
 OpenGL state, objects, errors, shaders
                 |
                 v
  resolved backend-neutral intent
                 |
          +------+------+
          |             |
          v             v
     DirectMetal      Vulkan
      shipping       reference
          |             |
          +------+------+
                 v
       platform presentation
```

The semantic owners are:

- `src/egl`: EGL objects, lifecycle, and the host surface seam.
- `src/gl` and `src/state`: OpenGL-visible state and object behavior.
- `src/shader`: shader translation, reflection, and linked interfaces.
- `src/backend`: immutable intent and resource identity for native execution.
- `src/metal`: DirectMetal execution.
- `src/vk`: Vulkan execution.

A generic EGL, OpenGL, or shader rule must not have separate Metal and Vulkan
interpretations. See [Architecture](docs/architecture.md).

## Build and test

Clone the submodules before the first build.

### Linux Vulkan reference

```bash
git submodule update --init --recursive
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DMITHRIL_BUILD_LEGACY=ON -DMITHRIL_BUILD_DIRECT=OFF
cmake --build build --parallel
ctest --test-dir build -L vulkan --output-on-failure
```

### macOS DirectMetal

```bash
git submodule update --init \
  third_party/SPIRV-Cross third_party/SPIRV-Headers third_party/glslang
cmake -S . -B build-direct -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DMITHRIL_BUILD_LEGACY=OFF -DMITHRIL_BUILD_DIRECT=ON \
  -DMITHRIL_OUTPUT_DIRECTORY="$PWD/build-direct/artifacts"
cmake --build build-direct --parallel
ctest --test-dir build-direct -L directmetal --output-on-failure
scripts/verify_directmetal_artifact.sh \
  build-direct/artifacts/libmithril.dylib \
  build-direct/mithril_direct.boundary.json
```

Set `MITHRIL_OUTPUT_DIRECTORY` if the build must place the library outside the
default `output` directory.

### iPhoneOS DirectMetal package

The package script requires Xcode and an iPhoneOS CMake toolchain file.
GitHub Actions fetches the pinned `ios-cmake` toolchain before it runs the
script.

```bash
MITHRIL_IOS_TOOLCHAIN_FILE=/path/to/ios.toolchain.cmake \
  scripts/build_iphoneos.sh
```

## Evidence

The normal `build-mithril` workflow provides three independent gates on
`main` and on pull requests to `main`:

1. DirectMetal macOS semantics and the Vulkan-free artifact boundary.
2. DirectMetal iPhoneOS arm64 packaging and ABI checks.
3. Vulkan reference regression on Linux.

The manual `platform-runtime-validation` workflow provides hosted Apple Silicon Metal
and iOS Simulator runtime evidence. Use it only when a platform-runtime claim
needs that environment.

Focused CTest programs prove small semantic invariants. They do not by
themselves prove a real Minecraft frame. Real Minecraft evidence does not
replace a focused regression for the underlying rule. See
[Validation](docs/validation.md).

## Minecraft reference source

This helper downloads Mojang's client JAR, verifies published hashes, and
creates a local decompiled source tree for investigation:

```bash
SRC="$(bash scripts/minecraft-reference.sh --print-path)"
```

The generated `.minecraft-reference` directory is git-ignored. Do not commit
or upload its contents.

## Development

Read [AGENTS.md](AGENTS.md) before a repository-wide change. Product work uses
`main`. Historical `archive/*` refs are provenance only. Query GitHub for live
branch, PR, and Actions state; do not infer current state from old prose.
