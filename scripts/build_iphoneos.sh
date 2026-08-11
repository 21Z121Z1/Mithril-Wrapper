#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${MITHRIL_IOS_BUILD_DIR:-${ROOT_DIR}/build-ios}"
OUTPUT_DIR="${MITHRIL_IOS_OUTPUT_DIR:-${BUILD_DIR}/output}"
DEPLOYMENT_TARGET="${MITHRIL_IOS_DEPLOYMENT_TARGET:-14.0}"
DYLIB="${OUTPUT_DIR}/libmithril.dylib"

for tool in cmake xcrun file lipo vtool otool nm; do
    command -v "${tool}" >/dev/null 2>&1 || {
        echo "missing required tool: ${tool}" >&2
        exit 1
    }
done

SDKROOT="$(xcrun --sdk iphoneos --show-sdk-path)"

echo "== Configure Mithril for iPhoneOS =="
echo "SDKROOT=${SDKROOT}"
echo "DEPLOYMENT_TARGET=${DEPLOYMENT_TARGET}"
echo "OUTPUT_DIR=${OUTPUT_DIR}"
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT="${SDKROOT}" \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="${DEPLOYMENT_TARGET}" \
    -DMITHRIL_IOS=ON \
    -DMITHRIL_OUTPUT_DIRECTORY="${OUTPUT_DIR}"

cmake --build "${BUILD_DIR}" -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

[[ -f "${DYLIB}" ]] || {
    echo "iPhoneOS dylib not found: ${DYLIB}" >&2
    exit 1
}

echo "== Mach-O contract =="
file "${DYLIB}"
lipo -info "${DYLIB}"
file "${DYLIB}" | grep -q 'Mach-O 64-bit dynamically linked shared library arm64'
lipo -info "${DYLIB}" | grep -q 'arm64'

BUILD_INFO="$(vtool -show-build "${DYLIB}")"
echo "${BUILD_INFO}"
echo "${BUILD_INFO}" | grep -q 'platform IOS'

echo "== Install name =="
otool -D "${DYLIB}"
otool -D "${DYLIB}" | tail -n +2 | grep -qx '@rpath/libmithril.dylib'

echo "== Amethyst EGL contract =="
NM_OUT="$(nm -gU "${DYLIB}")"
missing=0
for sym in \
    eglBindAPI eglChooseConfig eglCreateContext eglCreateWindowSurface \
    eglDestroyContext eglDestroySurface eglGetConfigAttrib \
    eglGetCurrentContext eglGetCurrentSurface eglGetDisplay eglGetError \
    eglGetPlatformDisplay eglInitialize eglMakeCurrent eglReleaseThread \
    eglSwapBuffers eglSwapInterval eglTerminate; do
    if echo "${NM_OUT}" | grep -qE " T _?${sym}$"; then
        printf 'ok   %s\n' "${sym}"
    else
        printf 'MISS %s\n' "${sym}" >&2
        missing=1
    fi
done
[[ "${missing}" -eq 0 ]]

GL_COUNT="$(echo "${NM_OUT}" | grep -cE ' T _?gl[A-Z0-9]')"
echo "desktop GL exports: ${GL_COUNT}"
[[ "${GL_COUNT}" -ge 342 ]]

echo "== Metal linkage =="
otool -L "${DYLIB}"
otool -L "${DYLIB}" | grep -q '/System/Library/Frameworks/Metal.framework/Metal'
otool -L "${DYLIB}" | grep -q '/System/Library/Frameworks/QuartzCore.framework/QuartzCore'

echo
echo "IPHONEOS MITHRIL CONTRACT PASSED"
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

The Amethyst CMake integration stages this dylib into Natives/build, and the
existing payload recipe copies build/*.dylib into AngelAuraAmethyst.app/Frameworks.
EOF
fi
