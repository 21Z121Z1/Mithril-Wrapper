#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${MITHRIL_IOS_BUILD_DIR:-${ROOT_DIR}/build-ios}"
OUTPUT_DIR="${MITHRIL_IOS_OUTPUT_DIR:-${BUILD_DIR}/output}"
DEPLOYMENT_TARGET="${MITHRIL_IOS_DEPLOYMENT_TARGET:-16.0}"
TOOLCHAIN_FILE="${MITHRIL_IOS_TOOLCHAIN_FILE:-}"
PLATFORM="${MITHRIL_IOS_PLATFORM:-OS64}"
DYLIB="${OUTPUT_DIR}/libmithril.dylib"
BOUNDARY="${BUILD_DIR}/mithril_direct.boundary.json"

for tool in cmake xcrun file lipo vtool otool nm; do
    command -v "${tool}" >/dev/null 2>&1 || {
        echo "missing required tool: ${tool}" >&2
        exit 1
    }
done

COMMON_ARGS=(
    -DCMAKE_BUILD_TYPE=Release
    -DMITHRIL_IOS=ON
    -DMITHRIL_BUILD_LEGACY=OFF
    -DMITHRIL_BUILD_DIRECT=ON
    -DMITHRIL_ENABLE_SHADER_TOOLCHAIN=ON
    -DBUILD_TESTING=OFF
    -DMITHRIL_OUTPUT_DIRECTORY="${OUTPUT_DIR}"
)

if [[ -n "${TOOLCHAIN_FILE}" ]]; then
    [[ -f "${TOOLCHAIN_FILE}" ]] || {
        echo "MITHRIL_IOS_TOOLCHAIN_FILE does not exist: ${TOOLCHAIN_FILE}" >&2
        exit 1
    }
    TOOLCHAIN_ARGS=(
        -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}"
        -DPLATFORM="${PLATFORM}"
        -DDEPLOYMENT_TARGET="${DEPLOYMENT_TARGET}"
        -DENABLE_BITCODE=OFF
        -DENABLE_ARC=ON
    )
    TOOLCHAIN_LABEL="${TOOLCHAIN_FILE} (${PLATFORM})"
else
    SDKROOT="$(xcrun --sdk iphoneos --show-sdk-path)"
    TOOLCHAIN_ARGS=(
        -DCMAKE_SYSTEM_NAME=iOS
        -DCMAKE_OSX_SYSROOT="${SDKROOT}"
        -DCMAKE_OSX_ARCHITECTURES=arm64
        -DCMAKE_OSX_DEPLOYMENT_TARGET="${DEPLOYMENT_TARGET}"
    )
    TOOLCHAIN_LABEL="Xcode iPhoneOS SDK ${SDKROOT}"
fi

echo "== Configure Vulkan-free Mithril DirectMetal for iPhoneOS =="
echo "TOOLCHAIN=${TOOLCHAIN_LABEL}"
echo "DEPLOYMENT_TARGET=${DEPLOYMENT_TARGET}"
echo "OUTPUT_DIR=${OUTPUT_DIR}"
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
    "${TOOLCHAIN_ARGS[@]}" \
    "${COMMON_ARGS[@]}"

cmake --build "${BUILD_DIR}" --target mithril_direct \
    -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

[[ -f "${DYLIB}" ]] || {
    echo "iPhoneOS DirectMetal dylib not found: ${DYLIB}" >&2
    exit 1
}

echo "== iPhoneOS Mach-O contract =="
FILE_INFO="$(file "${DYLIB}")"
LIPO_INFO="$(lipo -info "${DYLIB}")"
printf '%s\n' "${FILE_INFO}"
printf '%s\n' "${LIPO_INFO}"
grep -q 'Mach-O 64-bit dynamically linked shared library arm64' <<<"${FILE_INFO}"
grep -q 'arm64' <<<"${LIPO_INFO}"

BUILD_INFO="$(vtool -show-build "${DYLIB}")"
printf '%s\n' "${BUILD_INFO}"
grep -q 'platform IOS' <<<"${BUILD_INFO}"
grep -q "minos ${DEPLOYMENT_TARGET}" <<<"${BUILD_INFO}" || {
    echo "unexpected iPhoneOS deployment target; wanted ${DEPLOYMENT_TARGET}" >&2
    exit 1
}

echo "== Install name =="
INSTALL_NAME="$(otool -D "${DYLIB}")"
printf '%s\n' "${INSTALL_NAME}"
DYLIB_ID="$(printf '%s\n' "${INSTALL_NAME}" | tail -n +2)"
grep -qx '@rpath/libmithril.dylib' <<<"${DYLIB_ID}"

echo "== Shipping boundary + public ABI =="
"${ROOT_DIR}/scripts/verify_directmetal_artifact.sh" "${DYLIB}" "${BOUNDARY}"

echo
echo "IPHONEOS DIRECTMETAL CONTRACT PASSED"
echo "artifact: ${DYLIB}"

if [[ -n "${AMETHYST_DIR:-}" ]]; then
    if [[ ! -f "${AMETHYST_DIR}/Makefile" || ! -d "${AMETHYST_DIR}/Natives" ]]; then
        echo "AMETHYST_DIR does not look like an Amethyst checkout: ${AMETHYST_DIR}" >&2
        exit 1
    fi
    cat <<EOF

Amethyst staging command:
  cd "${AMETHYST_DIR}"
  MITHRIL_DYLIB="${DYLIB}" gmake payload PLATFORM=2

The Amethyst integration stages this exact arm64 iPhoneOS DirectMetal dylib
into the app Frameworks directory. The dylib install name is
@rpath/libmithril.dylib and its shipping boundary has already been verified.
EOF
fi
