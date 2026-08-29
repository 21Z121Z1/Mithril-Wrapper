#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

DEFAULT_MC_VERSION="26.2"
if [[ -f "${REPO_ROOT}/gradle.properties" ]]; then
  detected_version="$(awk -F= '$1 == "minecraft_version" { print $2; exit }' "${REPO_ROOT}/gradle.properties" | tr -d '[:space:]')"
  if [[ -n "${detected_version}" ]]; then
    DEFAULT_MC_VERSION="${detected_version}"
  fi
fi

MC_VERSION="${MINECRAFT_REFERENCE_VERSION:-${DEFAULT_MC_VERSION}}"
REFERENCE_ROOT="${MINECRAFT_REFERENCE_DIR:-${REPO_ROOT}/.minecraft-reference}"
VERSION_DIR="${REFERENCE_ROOT}/${MC_VERSION}"
CACHE_DIR="${REFERENCE_ROOT}/.cache"
SOURCES_DIR="${VERSION_DIR}/sources"
STATE_FILE="${VERSION_DIR}/REFERENCE_INFO.txt"

VERSION_MANIFEST_URL="${MINECRAFT_VERSION_MANIFEST_URL:-https://piston-meta.mojang.com/mc/game/version_manifest_v2.json}"
VINEFLOWER_VERSION="1.12.0"
VINEFLOWER_URL="https://github.com/Vineflower/vineflower/releases/download/${VINEFLOWER_VERSION}/vineflower-${VINEFLOWER_VERSION}.jar"
VINEFLOWER_SHA256="1dfcfe974395734fa467ce620661c7623d05ba83670de0529b1fbd63ff548b9d"
JAVA_OPTS="${MINECRAFT_REFERENCE_JAVA_OPTS:--Xmx3G}"

usage() {
  cat <<USAGE
Usage: bash scripts/minecraft-reference.sh [--force|--clean|--print-path]

Generate a local, git-ignored decompiled Minecraft client source tree for agents.

Environment overrides:
  MINECRAFT_REFERENCE_VERSION   Minecraft version (default: project version or 26.2)
  MINECRAFT_REFERENCE_DIR       Output root (default: .minecraft-reference)
  MINECRAFT_REFERENCE_JAVA_OPTS Java options for Vineflower (default: -Xmx3G)
USAGE
}

FORCE=0
CLEAN=0
PRINT_PATH=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --force) FORCE=1 ;;
    --clean) CLEAN=1 ;;
    --print-path) PRINT_PATH=1 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
  shift
done

if [[ "${CLEAN}" -eq 1 ]]; then
  rm -rf "${VERSION_DIR}"
  echo "Removed ${VERSION_DIR}"
  exit 0
fi

if [[ "${FORCE}" -eq 0 && -f "${STATE_FILE}" && -f "${SOURCES_DIR}/net/minecraft/client/Minecraft.java" ]]; then
  if grep -Fqx "minecraft_version=${MC_VERSION}" "${STATE_FILE}" \
    && grep -Fqx "vineflower_version=${VINEFLOWER_VERSION}" "${STATE_FILE}" \
    && grep -Fqx "vineflower_sha256=${VINEFLOWER_SHA256}" "${STATE_FILE}"; then
    if [[ "${PRINT_PATH}" -eq 1 ]]; then
      printf '%s\n' "${SOURCES_DIR}"
    else
      echo "Minecraft ${MC_VERSION} reference sources are ready: ${SOURCES_DIR}"
    fi
    exit 0
  fi
fi

for tool in curl python3 java; do
  if ! command -v "${tool}" >/dev/null 2>&1; then
    echo "Required tool not found: ${tool}" >&2
    exit 1
  fi
done

mkdir -p "${CACHE_DIR}" "${VERSION_DIR}"

sha1_verify() {
  python3 - "$1" "$2" <<'PY'
import hashlib
import pathlib
import sys
path = pathlib.Path(sys.argv[1])
expected = sys.argv[2].lower()
h = hashlib.sha1()
with path.open('rb') as f:
    for chunk in iter(lambda: f.read(1024 * 1024), b''):
        h.update(chunk)
actual = h.hexdigest()
if actual != expected:
    raise SystemExit(f"SHA-1 mismatch for {path}: expected {expected}, got {actual}")
PY
}

sha256_verify() {
  python3 - "$1" "$2" <<'PY'
import hashlib
import pathlib
import sys
path = pathlib.Path(sys.argv[1])
expected = sys.argv[2].lower()
h = hashlib.sha256()
with path.open('rb') as f:
    for chunk in iter(lambda: f.read(1024 * 1024), b''):
        h.update(chunk)
actual = h.hexdigest()
if actual != expected:
    raise SystemExit(f"SHA-256 mismatch for {path}: expected {expected}, got {actual}")
PY
}

download() {
  local url="$1"
  local dest="$2"
  local tmp="${dest}.tmp.$$"
  rm -f "${tmp}"
  curl --fail --location --retry 3 --retry-delay 1 --silent --show-error --output "${tmp}" "${url}"
  mv "${tmp}" "${dest}"
}

MANIFEST_JSON="${CACHE_DIR}/version_manifest_v2.json"
download "${VERSION_MANIFEST_URL}" "${MANIFEST_JSON}"

IFS=$'\t' read -r VERSION_JSON_URL VERSION_JSON_SHA1 <<EOF_META
$(python3 - "${MANIFEST_JSON}" "${MC_VERSION}" <<'PY'
import json
import sys
with open(sys.argv[1], encoding='utf-8') as f:
    manifest = json.load(f)
version = sys.argv[2]
for item in manifest.get('versions', []):
    if item.get('id') == version:
        print(item['url'], item['sha1'], sep='\t')
        break
else:
    raise SystemExit(f"Minecraft version not found in Mojang manifest: {version}")
PY
)
EOF_META

VERSION_JSON="${CACHE_DIR}/${MC_VERSION}.json"
download "${VERSION_JSON_URL}" "${VERSION_JSON}"
sha1_verify "${VERSION_JSON}" "${VERSION_JSON_SHA1}"

IFS=$'\t' read -r CLIENT_URL CLIENT_SHA1 CLIENT_SIZE <<EOF_CLIENT
$(python3 - "${VERSION_JSON}" <<'PY'
import json
import sys
with open(sys.argv[1], encoding='utf-8') as f:
    data = json.load(f)
client = data.get('downloads', {}).get('client')
if not client:
    raise SystemExit('Minecraft version metadata has no client download')
print(client['url'], client['sha1'], client.get('size', ''), sep='\t')
PY
)
EOF_CLIENT

CLIENT_JAR="${CACHE_DIR}/minecraft-${MC_VERSION}-${CLIENT_SHA1}.jar"
if [[ ! -f "${CLIENT_JAR}" ]]; then
  download "${CLIENT_URL}" "${CLIENT_JAR}"
fi
sha1_verify "${CLIENT_JAR}" "${CLIENT_SHA1}"

VINEFLOWER_JAR="${CACHE_DIR}/vineflower-${VINEFLOWER_VERSION}.jar"
if [[ ! -f "${VINEFLOWER_JAR}" ]]; then
  download "${VINEFLOWER_URL}" "${VINEFLOWER_JAR}"
fi
sha256_verify "${VINEFLOWER_JAR}" "${VINEFLOWER_SHA256}"

TMP_SOURCES="${VERSION_DIR}/sources.tmp.$$"
rm -rf "${TMP_SOURCES}"
mkdir -p "${TMP_SOURCES}"

echo "Decompiling Minecraft ${MC_VERSION} for local reference..." >&2
# Keep stdout reserved for the final machine-readable result used by agents.
# shellcheck disable=SC2086
java ${JAVA_OPTS} -jar "${VINEFLOWER_JAR}" --folder -s "${CLIENT_JAR}" "${TMP_SOURCES}" >&2

EXPECTED_SOURCE="${TMP_SOURCES}/net/minecraft/client/Minecraft.java"
if [[ ! -f "${EXPECTED_SOURCE}" ]]; then
  echo "Decompiler completed but expected source is missing: ${EXPECTED_SOURCE}" >&2
  rm -rf "${TMP_SOURCES}"
  exit 1
fi

JAVA_FILE_COUNT="$(find "${TMP_SOURCES}" -type f -name '*.java' | wc -l | tr -d '[:space:]')"
if [[ "${JAVA_FILE_COUNT}" -lt 500 ]]; then
  echo "Decompiler output is unexpectedly small (${JAVA_FILE_COUNT} Java files)" >&2
  rm -rf "${TMP_SOURCES}"
  exit 1
fi

rm -rf "${SOURCES_DIR}"
mv "${TMP_SOURCES}" "${SOURCES_DIR}"

cat > "${STATE_FILE}" <<EOF_STATE
minecraft_version=${MC_VERSION}
version_metadata_sha1=${VERSION_JSON_SHA1}
client_sha1=${CLIENT_SHA1}
client_size=${CLIENT_SIZE}
vineflower_version=${VINEFLOWER_VERSION}
vineflower_sha256=${VINEFLOWER_SHA256}
java_file_count=${JAVA_FILE_COUNT}
sources_dir=${SOURCES_DIR}
EOF_STATE

if [[ "${PRINT_PATH}" -eq 1 ]]; then
  printf '%s\n' "${SOURCES_DIR}"
else
  printf 'Minecraft %s reference sources are ready: %s\n' "${MC_VERSION}" "${SOURCES_DIR}"
fi
