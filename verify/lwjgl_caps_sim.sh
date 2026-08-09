#!/usr/bin/env bash
# LWJGL 能力探测模拟器
# ============================================================================
# Minecraft 通过 LWJGL 的 GL.createCapabilities() 判断"这个上下文到底是不是
# GL 4.6"。LWJGL 的判定方式很粗暴：把某个版本的**全部**核心入口点逐个
# dlsym，只要有一个解析不到（函数指针为 NULL），就认定该版本不受支持，
# 于是 GLCapabilities.OpenGL46 = false。
#
# 后果不是"少一个功能"，而是 Minecraft/Sodium 直接抛异常退出：
#   Sodium 会检查 caps.OpenGL46 / OpenGL45 / OpenGL43，不满足就拒绝启动。
#
# 所以「符号是否存在」是比「实现是否完美」更前置的生死线。这个脚本就在
# 模拟那一步：解析真实编译出的目标文件符号表，逐版本核对 657 个核心入口点。
#
# 用法：
#   bash verify/lwjgl_caps_sim.sh              # 用 .o 目标文件（默认，无需链接）
#
# 退出码：0 = GL 4.6 全部符号齐备；非 0 = 缺失个数
set -u
ROOT="$(cd "$(dirname "$0")/../Mithril-Wrapper/Mithril-Wrapper-cpp" && pwd)"
SYMLIST="$(dirname "$0")/gl46_core_symbols.txt"
OBJDIR=$(mktemp -d)
trap 'rm -rf "$OBJDIR"' EXIT

CXX=${CXX:-g++}
FLAGS=(-std=c++17 -c -O0
       -DVK_ENABLE_BETA_EXTENSIONS=1
       -DMITHRIL_COMMIT_ID="\"sandbox\""
       -I"$ROOT/include"
       -I/usr/include/glslang
       -I/usr/include/spirv_cross
       -w)

echo "编译所有翻译单元为目标文件…"
n=0
for f in $(find "$ROOT" -name '*.cpp' | sort); do
    obj="$OBJDIR/$(echo "${f#$ROOT/}" | tr '/' '_').o"
    if "$CXX" "${FLAGS[@]}" -o "$obj" "$f" 2>/dev/null; then
        n=$((n+1))
    else
        echo "  !! 编译失败: ${f#$ROOT/}"
    fi
done
echo "  $n 个目标文件"

# 收集所有已定义（T = text 段，即真实函数体）的全局符号
nm --defined-only -g "$OBJDIR"/*.o 2>/dev/null \
  | awk '$2=="T" || $2=="W" {print $3}' \
  | sed 's/^_//' | sort -u > "$OBJDIR/defined.txt"

echo "  导出符号 $(wc -l < "$OBJDIR/defined.txt") 个"
echo

# 逐版本核对
declare -A total missing
missing_list=""
while IFS=$'\t' read -r ver fn; do
    total[$ver]=$(( ${total[$ver]:-0} + 1 ))
    if ! grep -qx "$fn" "$OBJDIR/defined.txt"; then
        missing[$ver]=$(( ${missing[$ver]:-0} + 1 ))
        missing_list="$missing_list$ver	$fn
"
    fi
done < "$SYMLIST"

printf '%-18s %8s %8s %8s   %s\n' "版本" "核心数" "已导出" "缺失" "LWJGL 判定"
printf -- '---------------------------------------------------------------\n'
allok=1
grand_missing=0
# 版本是累积的：GL 4.6 要求 1.0~4.6 全部齐备
cum_missing=0
for ver in GL_VERSION_1_0 GL_VERSION_1_1 GL_VERSION_1_2 GL_VERSION_1_3 \
           GL_VERSION_1_4 GL_VERSION_1_5 GL_VERSION_2_0 GL_VERSION_2_1 \
           GL_VERSION_3_0 GL_VERSION_3_1 GL_VERSION_3_2 GL_VERSION_3_3 \
           GL_VERSION_4_0 GL_VERSION_4_1 GL_VERSION_4_2 GL_VERSION_4_3 \
           GL_VERSION_4_4 GL_VERSION_4_5 GL_VERSION_4_6; do
    t=${total[$ver]:-0}
    m=${missing[$ver]:-0}
    [ "$t" -eq 0 ] && continue
    cum_missing=$((cum_missing + m))
    grand_missing=$((grand_missing + m))
    if [ "$cum_missing" -eq 0 ]; then
        verdict="✓ 支持"
    else
        verdict="✗ 不支持 (累计缺 $cum_missing)"
        allok=0
    fi
    printf '%-18s %8d %8d %8d   %s\n' "${ver#GL_VERSION_}" "$t" "$((t-m))" "$m" "$verdict"
done

echo
if [ -n "$missing_list" ]; then
    echo "缺失明细："
    printf '%s' "$missing_list" | sed 's/^/  /'
    echo
fi

if [ "$allok" -eq 1 ]; then
    echo "结论：GL 4.6 Core Profile 全部 657 个入口点均已导出。"
    echo "      LWJGL createCapabilities() 会认定 OpenGL46 = true。"
else
    echo "结论：共缺 $grand_missing 个符号，LWJGL 会在某个版本上判定失败。"
fi
exit $grand_missing
