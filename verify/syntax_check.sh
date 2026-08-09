#!/usr/bin/env bash
# Syntax-check every C++ translation unit of Mithril-Wrapper against the system
# Vulkan / glslang / SPIRV-Cross headers and the project's own GL/EGL headers.
#
# .mm files are skipped (Objective-C++, Apple SDK only).
#
#   apt-get install -y libvulkan-dev glslang-dev spirv-cross
set -u
ROOT="$(cd "$(dirname "$0")/../Mithril-Wrapper-cpp" && pwd)"
CXX=${CXX:-g++}

FLAGS=(-std=c++17 -fsyntax-only
       -DVK_ENABLE_BETA_EXTENSIONS=1
       -DMITHRIL_COMMIT_ID="\"sandbox\""
       -I"$ROOT/include"
       -I/usr/include/glslang
       -I/usr/include/spirv_cross
       -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable
       -Wno-unused-but-set-variable -Wno-missing-field-initializers)

pass=0; fail=0; failed_files=()
for f in $(find "$ROOT" -name '*.cpp' | sort); do
    if ! "$CXX" "${FLAGS[@]}" "$f" 2>"/tmp/sc_$$.log"; then
        fail=$((fail+1)); failed_files+=("${f#$ROOT/}")
        echo "FAIL  ${f#$ROOT/}"
        grep -m6 -E 'error:' "/tmp/sc_$$.log" | sed 's/^/      /'
    else
        pass=$((pass+1))
        echo "ok    ${f#$ROOT/}"
    fi
done
rm -f "/tmp/sc_$$.log"
echo "-----------------------------------------------"
echo "passed: $pass   failed: $fail"
if [ $fail -eq 0 ]; then
    echo "ALL TRANSLATION UNITS COMPILE"
else
    printf 'failing: %s\n' "${failed_files[@]}"
fi
exit $fail
