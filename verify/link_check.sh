#!/usr/bin/env bash
# Catch header/definition signature drift *before* CI's link step does.
#
# syntax_check.sh compiles each translation unit in isolation, so a function
# whose header declaration disagrees with its definition (e.g. a parameter added
# to the .cpp but not the .h) passes cleanly — every TU is individually valid.
# The mismatch only shows up when the objects are linked together, which on this
# project happens on a macOS runner:
#
#   Undefined symbols for architecture arm64:
#     "mithril::vk::create_buffer(mithril::vk::BufferEntry&, unsigned long long,
#                                 unsigned int, void const*)"
#
# This script reproduces that locally: compile every .cpp to an object file,
# then diff referenced-but-never-defined mithril:: symbols against the set of
# symbols the objects define.
#
# .mm files (Objective-C++, Apple SDK only) cannot be built here, so symbols
# they define are listed in MM_DEFINED below and treated as satisfied.
#
#   apt-get install -y libvulkan-dev glslang-dev spirv-cross
set -u
ROOT="$(cd "$(dirname "$0")/../Mithril-Wrapper-cpp" && pwd)"
OBJ="${TMPDIR:-/tmp}/mithril-linkcheck-obj"
CXX=${CXX:-g++}

rm -rf "$OBJ"; mkdir -p "$OBJ"

FLAGS=(-std=c++17 -c -O0
       -DVK_ENABLE_BETA_EXTENSIONS=1
       -DMITHRIL_COMMIT_ID="\"sandbox\""
       -I"$ROOT/include"
       -I/usr/include/glslang
       -I/usr/include/spirv_cross
       -w)

# Symbols defined by .mm translation units, which are not built on Linux.
MM_DEFINED='mithril::vk::(create_metal_surface|swapchain_metal_)|mithril::metal_'

fail=0
for f in $(find "$ROOT" -name '*.cpp' | sort); do
    o="$OBJ/$(echo "${f#$ROOT/}" | tr '/' '_').o"
    if ! "$CXX" "${FLAGS[@]}" -o "$o" "$f" 2>"$OBJ/err.log"; then
        echo "COMPILE FAIL  ${f#$ROOT/}"
        grep -m6 -E 'error:' "$OBJ/err.log" | sed 's/^/      /'
        fail=1
    fi
done
rm -f "$OBJ/err.log"
if [ $fail -ne 0 ]; then
    echo "compile stage failed — fix those first (see verify/syntax_check.sh)"
    exit 1
fi

nm -C --defined-only "$OBJ"/*.o 2>/dev/null \
    | grep -oP '(?<=^[0-9a-f]{16} [A-Za-z] ).*' | sort -u > "$OBJ/defined.txt"
nm -C -u "$OBJ"/*.o 2>/dev/null \
    | sed 's/^ *U //' | sort -u > "$OBJ/undef.txt"

comm -23 "$OBJ/undef.txt" "$OBJ/defined.txt" \
    | grep -E '^mithril::' \
    | grep -vE "$MM_DEFINED" > "$OBJ/missing.txt"

echo "-----------------------------------------------"
if [ -s "$OBJ/missing.txt" ]; then
    echo "UNDEFINED mithril:: SYMBOLS (the link would fail):"
    sed 's/^/  /' "$OBJ/missing.txt"
    echo
    echo "Usually a declaration in a header drifted from its definition."
    exit 1
fi
echo "NO UNDEFINED mithril:: SYMBOLS"
