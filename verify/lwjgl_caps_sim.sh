#!/usr/bin/env bash
# Mithril / LWJGL core ABI contract checker
# ============================================================================
# This script checks only the ABI precondition for the core version that Mithril
# actually advertises in ci/e2e/gl_semantic_contract.json. Symbol presence is
# necessary for LWJGL capability construction, but it is NOT evidence that the
# corresponding OpenGL semantics are implemented correctly.
#
# Semantic support is enforced separately by:
#   ci/e2e/check_gl_semantic_contract.py
#   ci/e2e/check_oracle_ledger.py
#   .github/workflows/gl-semantic-closure.yml
#
# In particular, the existence of compatibility symbols above the advertised
# core version must never be used to infer OpenGL 4.x support.
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ROOT="$REPO_ROOT/Mithril-Wrapper-cpp"
SYMLIST="$REPO_ROOT/verify/gl46_core_symbols.txt"
MANIFEST="$REPO_ROOT/ci/e2e/gl_semantic_contract.json"
GETTER="$REPO_ROOT/Mithril-Wrapper-cpp/MG_Impl/Getter.cpp"
OBJDIR=$(mktemp -d)
trap 'rm -rf "$OBJDIR"' EXIT

TARGET_VERSION=$(python3 - "$MANIFEST" <<'PY'
import json, sys
m=json.load(open(sys.argv[1]))
print(m['core']['version'])
PY
)
TARGET_MAJOR=${TARGET_VERSION%%.*}
TARGET_MINOR=${TARGET_VERSION##*.}

# First ensure source advertisement and the machine-readable contract agree.
python3 "$REPO_ROOT/ci/e2e/check_gl_semantic_contract.py" \
  --manifest "$MANIFEST" \
  --getter "$GETTER" >/dev/null

CXX=${CXX:-g++}
FLAGS=(-std=c++17 -c -O0
       -DVK_ENABLE_BETA_EXTENSIONS=1
       -DMITHRIL_COMMIT_ID="\"sandbox\""
       -I"$ROOT/include"
       -I/usr/include/glslang
       -I/usr/include/spirv_cross
       -w)

echo "Advertised core contract: OpenGL $TARGET_VERSION"
echo "Compiling C++ translation units for ABI symbol inspection..."
n=0
for f in $(find "$ROOT" -name '*.cpp' | sort); do
    obj="$OBJDIR/$(echo "${f#$ROOT/}" | tr '/' '_').o"
    if "$CXX" "${FLAGS[@]}" -o "$obj" "$f" 2>/dev/null; then
        n=$((n+1))
    else
        echo "  !! compile failed: ${f#$ROOT/}"
    fi
done
echo "  compiled $n object files"

nm --defined-only -g "$OBJDIR"/*.o 2>/dev/null \
  | awk '$2=="T" || $2=="W" {print $3}' \
  | sed 's/^_//' | sort -u > "$OBJDIR/defined.txt"

echo "  exported symbols observed: $(wc -l < "$OBJDIR/defined.txt")"

required="$OBJDIR/required.txt"
python3 - "$SYMLIST" "$TARGET_MAJOR" "$TARGET_MINOR" > "$required" <<'PY'
import re, sys
path, maj, min_ = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
target=(maj,min_)
required=[]
for raw in open(path):
    raw=raw.strip()
    if not raw or raw.startswith('#'):
        continue
    ver, fn = raw.split('\t', 1)
    m=re.fullmatch(r'GL_VERSION_(\d+)_(\d+)', ver)
    if not m:
        raise SystemExit(f'malformed core-symbol row: {raw}')
    if (int(m.group(1)),int(m.group(2))) <= target:
        required.append(fn)
for fn in sorted(set(required)):
    print(fn)
PY

missing="$OBJDIR/missing.txt"
comm -23 "$required" "$OBJDIR/defined.txt" > "$missing"
required_count=$(wc -l < "$required" | tr -d ' ')
missing_count=$(wc -l < "$missing" | tr -d ' ')

echo "Required cumulative OpenGL $TARGET_VERSION core symbols: $required_count"
if [ "$missing_count" -ne 0 ]; then
    echo "Missing $missing_count advertised-core symbols:"
    sed 's/^/  /' "$missing"
    exit 1
fi

echo "ABI contract passed: all $required_count advertised OpenGL $TARGET_VERSION core symbols are present."
echo "No OpenGL version above $TARGET_VERSION is inferred from extra exported compatibility symbols."
echo "Semantic correctness still requires the GL semantic closure GPU/production gates."
