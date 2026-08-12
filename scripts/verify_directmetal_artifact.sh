#!/usr/bin/env bash
set -euo pipefail

usage() {
  echo "usage: $0 <libmithril.dylib> <mithril_direct.boundary.json>" >&2
  exit 64
}

[[ $# -eq 2 ]] || usage
DYLIB=$1
BOUNDARY=$2

[[ -f "$DYLIB" ]] || { echo "missing DirectMetal dylib: $DYLIB" >&2; exit 1; }
[[ -f "$BOUNDARY" ]] || { echo "missing DirectMetal boundary manifest: $BOUNDARY" >&2; exit 1; }

echo '--- DirectMetal Mach-O dependencies ---'
DEPS=$(otool -L "$DYLIB")
printf '%s\n' "$DEPS"
printf '%s\n' "$DEPS" | grep -q '/System/Library/Frameworks/Metal.framework/Metal'
if printf '%s\n' "$DEPS" | grep -Eqi 'vulkan|moltenvk'; then
  echo 'forbidden Vulkan/MoltenVK runtime dependency in DirectMetal artifact' >&2
  exit 1
fi

echo '--- DirectMetal exported/undefined symbol boundary ---'
SYMS=$(nm -gU "$DYLIB")
if printf '%s\n' "$SYMS" | grep -Eqi ' _vk[A-Z]|moltenvk|directvulkan'; then
  echo 'forbidden Vulkan/MoltenVK symbol in DirectMetal artifact' >&2
  exit 1
fi
UNDEF=$(nm -u "$DYLIB" || true)
if printf '%s\n' "$UNDEF" | grep -Eqi ' _vk[A-Z]|moltenvk|directvulkan'; then
  echo 'forbidden unresolved Vulkan/MoltenVK symbol in DirectMetal artifact' >&2
  exit 1
fi

echo '--- DirectMetal CMake graph boundary ---'
cat "$BOUNDARY"
if grep -Eqi '(^|[/;:\" ])vk([/;:\" ]|$)|vulkan|moltenvk|directvulkan|vulkan::headers' "$BOUNDARY"; then
  echo 'forbidden Vulkan source/target dependency in mithril_direct graph' >&2
  exit 1
fi

for symbol in eglGetDisplay eglInitialize eglChooseConfig eglCreateContext \
              eglCreateWindowSurface eglMakeCurrent eglSwapBuffers eglGetProcAddress \
              glGetString glGetError glGenBuffers glBufferData glGenVertexArrays \
              glCreateShader glCompileShader glCreateProgram glLinkProgram \
              glDrawArrays glDrawElements; do
  if ! printf '%s\n' "$SYMS" | grep -qE " _$symbol$"; then
    echo "missing required shipping export: $symbol" >&2
    exit 1
  fi
done

GL_COUNT=$(printf '%s\n' "$SYMS" | grep -cE ' _gl[A-Z0-9]' || true)
echo "DirectMetal GL exports: $GL_COUNT"
[[ "$GL_COUNT" -ge 342 ]] || {
  echo "DirectMetal GL export surface regressed below 342 symbols" >&2
  exit 1
}

echo 'DIRECTMETAL SHIPPING BOUNDARY PASSED'
