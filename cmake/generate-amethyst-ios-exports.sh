#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <output-export-list>" >&2
  exit 2
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
GL_ABI="$ROOT/verify/gl46_core_symbols.txt"
EGL_ABI="$ROOT/cmake/amethyst-ios.exports"
OUT="$1"
TMP="${OUT}.tmp"

[[ -s "$GL_ABI" ]]
[[ -s "$EGL_ABI" ]]

# gl46_core_symbols.txt is the same 657-entry ABI manifest used by the LWJGL
# capability probe. Convert it to Mach-O external names instead of using an
# `_gl*` linker wildcard, which also matches glslang's `glslang_*` C API.
awk -F '\t' '$2 ~ /^gl[A-Z][A-Za-z0-9_]*$/ { print "_" $2 }' "$GL_ABI" > "$TMP"

# The companion file contains the 44 explicit EGL entry points declared by
# Mithril's public EGL header. Ignore comments/blank lines deliberately.
grep -E '^_egl[A-Z][A-Za-z0-9_]*$' "$EGL_ABI" >> "$TMP"
LC_ALL=C sort -u "$TMP" > "$OUT"
rm -f "$TMP"

GL_COUNT=$(grep -Ec '^_gl[A-Z][A-Za-z0-9_]*$' "$OUT")
EGL_COUNT=$(grep -Ec '^_egl[A-Z][A-Za-z0-9_]*$' "$OUT")
TOTAL_COUNT=$(wc -l < "$OUT" | tr -d ' ')

if [[ "$GL_COUNT" -ne 657 ]]; then
  echo "expected 657 OpenGL ABI symbols, generated $GL_COUNT" >&2
  exit 1
fi
if [[ "$EGL_COUNT" -ne 44 ]]; then
  echo "expected 44 EGL ABI symbols, generated $EGL_COUNT" >&2
  exit 1
fi
if [[ "$TOTAL_COUNT" -ne 701 ]]; then
  echo "expected 701 total GL/EGL ABI symbols, generated $TOTAL_COUNT" >&2
  exit 1
fi

if grep -Eq 'glslang|TInterm|spirv_cross|^__Z' "$OUT"; then
  echo "compiler/backend implementation symbol leaked into generated ABI" >&2
  grep -E 'glslang|TInterm|spirv_cross|^__Z' "$OUT" >&2 || true
  exit 1
fi

printf 'Generated Amethyst iOS ABI: %s GL + %s EGL = %s symbols\n' \
  "$GL_COUNT" "$EGL_COUNT" "$TOTAL_COUNT"
