#!/usr/bin/env bash
# gl_smoke 测试运行器：语法校验 + （可选）对 libmithril 产物做 dlopen 冒烟。
#
# 本项目是 iOS 专用（GL -> Vulkan -> Metal/MoltenVK），完整 libmithril 无法在
# 无 Apple SDK 的宿主上构建，因此本脚本提供两档能力：
#
#   mode 1: 语法校验（任意有 clang/gcc 的机器）
#     用项目自带的 GL/EGL 头对 tests/gl_smoke.c 做 -fsyntax-only，确保测试
#     期望的符号/常量与导出契约一致。这是 Linux 开发循环里的默认档。
#
#   mode 2: dlopen 冒烟（有 libmithril.{so,dylib} 产物、且允许 dlopen 的宿主）
#     编译并运行 tests/gl_smoke，对状态机 + 版本 + 错误队列 + 符号契约断言。
#     iOS 上通常通过注入路径获得 dylib；macOS/模拟器或将来打通 Linux 构建时
#     可直接指定库路径运行。
#
# 用法：
#   ./tests/run_gl_smoke.sh                 # 仅语法校验
#   ./tests/run_gl_smoke.sh <libmithril路径> # 语法校验 + 对该库运行冒烟
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TEST_SRC="$ROOT/tests/gl_smoke.c"
INC="$ROOT/Mithril-Wrapper-cpp/include"
CC=${CC:-clang}
fail=0

if [ ! -f "$TEST_SRC" ]; then
    echo "missing test source: $TEST_SRC"
    exit 1
fi

# ---------- 语法校验 ----------
echo "== syntax check: $TEST_SRC =="
if ! "$CC" -std=c11 -fsyntax-only -Wall -Wextra -I"$INC" "$TEST_SRC"; then
    echo "SYNTAX CHECK FAILED"
    fail=1
else
    echo "SYNTAX CHECK OK"
fi

# ---------- 可选 dlopen 冒烟 ----------
LIB=""
if [ $# -ge 1 ]; then
    LIB="$1"
elif [ -f "$ROOT/output/libmithril.so" ]; then
    LIB="$ROOT/output/libmithril.so"
elif [ -f "$ROOT/output/libmithril.dylib" ]; then
    LIB="$ROOT/output/libmithril.dylib"
fi

if [ -n "$LIB" ]; then
    echo "== running gl_smoke against $LIB =="
    BIN="$ROOT/tests/gl_smoke"
    if ! "$CC" -std=c11 -O0 -Wall -Wextra -I"$INC" -o "$BIN" "$TEST_SRC" -ldl; then
        echo "COMPILE FAILED"
        fail=1
    else
        "$BIN" "$LIB"
        [ $? -eq 0 ] || fail=1
        rm -f "$BIN"
    fi
else
    echo "== no libmithril artifact found; skipped dlopen run (syntax check only) =="
fi

echo "-----------------------------------------------"
if [ $fail -eq 0 ]; then
    echo "GL SMOKE RUNNER: ALL PASSED"
    exit 0
fi
echo "GL SMOKE RUNNER: FAILED"
exit 1