#!/bin/bash
set -euo pipefail

required_egl_symbols='eglBindAPI eglChooseConfig eglCreateContext eglCreateWindowSurface eglDestroyContext eglDestroySurface eglGetConfigAttrib eglGetCurrentContext eglGetCurrentSurface eglGetDisplay eglGetError eglGetPlatformDisplay eglInitialize eglMakeCurrent eglReleaseThread eglSwapBuffers eglSwapInterval eglTerminate'

verify_symbols_file() {
    symbols_file=$1
    missing=0
    for sym in $required_egl_symbols; do
        if grep -qE " T _?${sym}$" "$symbols_file"; then
            echo "ok  $sym"
        else
            echo "MISS $sym"
            missing=1
        fi
    done
    gl_count=$(grep -cE ' T _?gl[A-Z0-9]' "$symbols_file" || true)
    egl_count=$(grep -cE ' T _?egl[A-Z0-9]' "$symbols_file" || true)
    echo "gl* defined exports: $gl_count"
    echo "egl* defined exports: $egl_count"
    [[ "$missing" -eq 0 && "$gl_count" -ge 342 ]]
}

self_test() {
    root=${TMPDIR:-/tmp}
    valid=$(mktemp "$root/mithril-contract-valid.XXXXXX")
    missing=$(mktemp "$root/mithril-contract-missing.XXXXXX")
    short=$(mktemp "$root/mithril-contract-short.XXXXXX")
    trap 'rm -f "$valid" "$missing" "$short"' EXIT
    for sym in $required_egl_symbols; do
        printf '0000000000000000 T _%s\n' "$sym" >> "$valid"
        printf '0000000000000000 T _%s\n' "$sym" >> "$short"
    done
    i=0
    while [[ "$i" -lt 342 ]]; do
        printf '0000000000000000 T _glA%03d\n' "$i" >> "$valid"
        i=$((i + 1))
    done
    i=0
    while [[ "$i" -lt 341 ]]; do
        printf '0000000000000000 T _glA%03d\n' "$i" >> "$short"
        i=$((i + 1))
    done
    verify_symbols_file "$valid" >/dev/null
    grep -v ' T _eglSwapBuffers$' "$valid" > "$missing"
    if verify_symbols_file "$missing" >/dev/null 2>&1; then
        echo 'self-test failed: missing EGL symbol accepted' >&2
        return 1
    fi
    if verify_symbols_file "$short" >/dev/null 2>&1; then
        echo 'self-test failed: 341 GL exports accepted' >&2
        return 1
    fi
    echo 'MITHRIL_ARTIFACT_CHECKER_SELF_TEST_PASS'
}

if [[ "${1:-}" == '--self-test' ]]; then
    self_test
    exit 0
fi

platform=${1:-}
dylib=${2:-}
[[ "$platform" == 'macos' || "$platform" == 'ios' ]] || {
    echo "usage: $0 {macos|ios} /path/to/libmithril.dylib" >&2
    exit 2
}
[[ -n "$dylib" && -f "$dylib" ]] || {
    echo "artifact not found: ${dylib:-<empty>}" >&2
    exit 2
}

root=${TMPDIR:-/tmp}
symbols_file=$(mktemp "$root/mithril-symbols.XXXXXX")
lipo_file=$(mktemp "$root/mithril-lipo.XXXXXX")
deps_file=$(mktemp "$root/mithril-deps.XXXXXX")
build_file=$(mktemp "$root/mithril-build.XXXXXX")
install_file=$(mktemp "$root/mithril-install.XXXXXX")
trap 'rm -f "$symbols_file" "$lipo_file" "$deps_file" "$build_file" "$install_file"' EXIT

file "$dylib"
lipo -info "$dylib" > "$lipo_file"
cat "$lipo_file"
grep -q 'arm64' "$lipo_file"

nm -gU "$dylib" > "$symbols_file"
verify_symbols_file "$symbols_file"
gl_count=$(grep -cE ' T _?gl[A-Z0-9]' "$symbols_file" || true)
egl_count=$(grep -cE ' T _?egl[A-Z0-9]' "$symbols_file" || true)

otool -L "$dylib" > "$deps_file"
cat "$deps_file"
grep -Eq '/System/Library/Frameworks/Metal\.framework/(Versions/[^/]+/)?Metal([[:space:]]|$)' "$deps_file"
grep -Eq '/System/Library/Frameworks/QuartzCore\.framework/(Versions/[^/]+/)?QuartzCore([[:space:]]|$)' "$deps_file"

if [[ "$platform" == 'ios' ]]; then
    vtool -show-build "$dylib" > "$build_file"
    cat "$build_file"
    grep -q 'platform IOS' "$build_file"
    otool -D "$dylib" > "$install_file"
    cat "$install_file"
    [[ "$(sed -n '2p' "$install_file")" == '@rpath/libmithril.dylib' ]]
fi

echo "MITHRIL_ARTIFACT_CONTRACT_PASS platform=$platform gl_exports=$gl_count egl_exports=$egl_count"
