#!/usr/bin/env bash
# ============================================================
# CatOS 运行库依赖检查 —— FR-LIB-007 的参考实现
#
# 用法：
#   tools/check_libc.sh <里程碑目录> <构建目录>
#   例：tools/check_libc.sh m1_sched_core build/m1
#
# 作用：检查"内核核心层/HAL/应用层零宿主库依赖、宿主依赖只存在于移植层"
#       （FR-LIB-001/002/003/006/008/009）。
# 前置：先用同一套工具链构建完成（本脚本只读构建产物，不重新编译）。
# 退出码：0 = 全部通过；1 = 有失败项（失败项会指出具体文件/符号）。
# ============================================================

set -u

MILESTONE="${1:-}"
BUILD="${2:-}"
if [ -z "$MILESTONE" ] || [ -z "$BUILD" ]; then
    echo "用法: tools/check_libc.sh <里程碑目录> <构建目录>" >&2
    echo "例：  tools/check_libc.sh m1_sched_core build/m1" >&2
    exit 2
fi
if [ ! -d "$BUILD/CMakeFiles" ]; then
    echo "错误：$BUILD 下没有 CMakeFiles，请先构建。" >&2
    exit 2
fi

NM="${NM:-nm}"
FAIL=0
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# 允许的独立环境头（编译器提供，FR-LIB-002）
ALLOWED_ANGLE="stdint.h stddef.h stdbool.h limits.h float.h stdarg.h iso646.h"

# 归一化符号名：去掉一个前导下划线、去掉 stdcall 的 @N 后缀（MinGW 32 位命名）
normalize() { sed -e 's/^_//' -e 's/@[0-9]*$//'; }

objs_of() {   # 取某个 CMake 目标的全部对象文件
    find "$BUILD/CMakeFiles" -path "*$1.dir*" -name '*.o' 2>/dev/null | sort
}

CORE_OBJS=$(objs_of catos_kernel)
LIB_OBJS=$(objs_of catos_lib)
PORT_OBJS=$(objs_of catos_port_win32)
APP_OBJS=$(find "$BUILD/CMakeFiles" -maxdepth 1 -type d -name 'catos_*.dir' \
             ! -name 'catos_lib.dir' ! -name 'catos_kernel.dir' ! -name 'catos_port_*.dir' \
             -exec find {} -name '*.o' \; 2>/dev/null | sort)

# 全部 CatOS 对象的已定义符号（归一化后）
nm --defined-only $LIB_OBJS $CORE_OBJS $PORT_OBJS 2>/dev/null \
    | awk 'NF>=3 {print $3}' | normalize | sort -u > "$TMP/defined.txt"

# 允许的未定义符号：libgcc/编译器脚手架（两个及以上前导下划线，如 __udivdi3、__main）
is_scaffold() { case "$1" in __*) return 0 ;; *) return 1 ;; esac; }

echo "== CatOS 运行库依赖检查: $MILESTONE (构建: $BUILD) =="

# ---------------- [1/6] 头文件检查（FR-LIB-002） ----------------
BAD_INC=$(grep -rn '^[[:space:]]*#[[:space:]]*include' \
            "$MILESTONE/kernel/src" "$MILESTONE/kernel/include" \
            "$MILESTONE/lib" "$MILESTONE/apps" 2>/dev/null \
          | grep -v ':[[:space:]]*#[[:space:]]*include[[:space:]]*"')
# 1a. 自有头不得用 <>；1b. <> 只允许独立环境头
INTERNAL_ANGLE=$(echo "$BAD_INC" | grep -E '<[[:space:]]*(catos|test_common)' | grep -v '^$')
OTHER_ANGLE=$(echo "$BAD_INC" | grep -v -E "<[[:space:]]*($(echo $ALLOWED_ANGLE | tr ' ' '|'))[[:space:]]*>" | grep -v '^$')
if [ -z "$INTERNAL_ANGLE" ] && [ -z "$OTHER_ANGLE" ]; then
    N=$(grep -rc '^[[:space:]]*#[[:space:]]*include' "$MILESTONE/kernel/src" "$MILESTONE/kernel/include" "$MILESTONE/lib" "$MILESTONE/apps" 2>/dev/null | awk -F: '{s+=$2} END {print s+0}')
    echo "[1/6] 头文件检查       通过：$N 处 include 全部合规（自有头用 \"\"，<> 仅限独立环境头）"
else
    echo "[1/6] 头文件检查       失败："
    [ -n "$INTERNAL_ANGLE" ] && echo "$INTERNAL_ANGLE" | sed 's/^/        自有头用了尖括号: /'
    [ -n "$OTHER_ANGLE" ]    && echo "$OTHER_ANGLE"    | sed 's/^/        非独立环境头: /'
    FAIL=1
fi

# ---------------- [2/6] 内核核心层未定义符号（FR-LIB-001/003） ----------------
check_objs() {   # $1=对象列表  $2=允许的额外符号（正则）  $3=输出文件
    local objs="$1" extra="$2" out="$3"
    : > "$out"
    [ -z "$objs" ] && return 0
    nm -u $objs 2>/dev/null | awk 'NF>=2 {print $2}' | normalize | sort -u | while read -r s; do
        [ -z "$s" ] && continue
        grep -qx -- "$s" "$TMP/defined.txt" && continue
        is_scaffold "$s" && continue
        if [ -n "$extra" ] && echo "$s" | grep -qE "$extra"; then continue; fi
        echo "$s"
    done >> "$out"
}

check_objs "$CORE_OBJS" "" "$TMP/core_bad.txt"
if [ ! -s "$TMP/core_bad.txt" ]; then
    NDEF=$(nm --defined-only $LIB_OBJS $CORE_OBJS 2>/dev/null | awk 'NF>=3 {print $3}' | normalize | sort -u | wc -l | tr -d ' ')
    echo "[2/6] 内核核心层符号   通过：0 个宿主符号（未定义符号全部由 CatOS 的 $NDEF 个已定义符号满足）"
else
    echo "[2/6] 内核核心层符号   失败，未定义且非 CatOS 提供："
    sed 's/^/        /' "$TMP/core_bad.txt"
    FAIL=1
fi

# ---------------- [3/6] 应用对象未定义符号（FR-LIB-008/009） ----------------
# 应用保留 int main()：32 位 MinGW 会为它生成 __main 启动钩子调用，
# 属编译器脚手架（见需求 FR-LIB-002 例外条款），不是源码级宿主调用。
check_objs "$APP_OBJS" "" "$TMP/app_bad.txt"
APP_MAIN_HOOK=$(nm -u $APP_OBJS 2>/dev/null | awk 'NF>=2 {print $2}' | normalize | sort -u | grep -c '^__main$' || true)
if [ ! -s "$TMP/app_bad.txt" ]; then
    echo "[3/6] 应用对象符号     通过：全部由 CatOS 提供（例外：__main —— 编译器为 main() 生成的启动钩子，$APP_MAIN_HOOK 处）"
else
    echo "[3/6] 应用对象符号     失败，未定义且非 CatOS 提供："
    sed 's/^/        /' "$TMP/app_bad.txt"
    FAIL=1
fi

# ---------------- [4/6] 可执行文件链接期符号（FR-LIB-003 的最硬证据） ----------------
EXES=$(find "$BUILD" -maxdepth 1 -name '*.exe' | sort)
if [ -z "$EXES" ]; then
    echo "[4/6] 可执行文件符号   跳过：未找到 exe"
else
    : > "$TMP/exe_bad.txt"
    for exe in $EXES; do
        nm -u "$exe" 2>/dev/null | awk 'NF>=2 {print $2}' | sort -u | while read -r s; do
            [ -z "$s" ] && continue
            is_scaffold "$s" && continue
            grep -qx -- "$(echo "$s" | normalize)" "$TMP/defined.txt" && continue
            echo "$(basename "$exe"): $s"
        done >> "$TMP/exe_bad.txt"
    done
    if [ ! -s "$TMP/exe_bad.txt" ]; then
        N=$(echo "$EXES" | wc -l | tr -d ' ')
        echo "[4/6] 可执行文件符号   通过：$N 个 exe 均无宿主库符号（仅剩编译器脚手架）"
    else
        echo "[4/6] 可执行文件符号   失败，链接期仍依赖宿主库："
        sed 's/^/        /' "$TMP/exe_bad.txt"
        FAIL=1
    fi
fi

# ---------------- [5/6] 移植层宿主符号（FR-LIB-006，单列报告） ----------------
if [ -z "$PORT_OBJS" ]; then
    echo "[5/6] 移植层宿主符号   跳过：未找到移植层对象"
else
    nm -u $PORT_OBJS 2>/dev/null | awk 'NF>=2 {print $2}' | sort -u > "$TMP/port_undef.txt"
    : > "$TMP/port_bad.txt"
    WIN32=0; CATOS=0
    while read -r s; do
        [ -z "$s" ] && continue
        case "$s" in
            *@[0-9]*) WIN32=$((WIN32+1)); continue ;;               # stdcall → Win32 导入
        esac
        if grep -qx -- "$(echo "$s" | normalize)" "$TMP/defined.txt"; then
            CATOS=$((CATOS+1)); continue
        fi
        is_scaffold "$s" && continue
        echo "$s" >> "$TMP/port_bad.txt"
    done < "$TMP/port_undef.txt"
    if [ ! -s "$TMP/port_bad.txt" ]; then
        echo "[5/6] 移植层宿主符号   报告：$WIN32 个 Win32 导入 + $CATOS 个 CatOS 符号（宿主依赖只出现在这一层）"
    else
        echo "[5/6] 移植层宿主符号   失败，出现既非 Win32 导入也非 CatOS 的符号："
        sed 's/^/        /' "$TMP/port_bad.txt"
        FAIL=1
    fi
fi

# ---------------- [6/6] 跨里程碑共享文件一致性（FR-WIN-004，仅告警） ----------------
SHARED="lib/include/catos_libcfg.h lib/include/catos_backend.h lib/include/catos_string.h
        lib/include/catos_stdio.h lib/include/catos_stdlib.h lib/include/catos_ctype.h
        lib/include/catos_assert.h lib/src/catos_string.c lib/src/catos_stdio.c
        lib/src/catos_stdlib.c lib/src/catos_ctype.c lib/src/catos_assert.c
        kernel/include/catos/catos_atomic.h kernel/include/catos/catos_port.h
        kernel/include/catos/catos_config.h kernel/include/catos/catos_types.h
        kernel/include/catos/catos_list.h kernel/port/win32/port.c
        kernel/port/win32/port_rt.c kernel/port/win32/port_internal.h apps/demo/main.c"
OTHER=""
case "$MILESTONE" in
    *m0*) [ -d m1_sched_core ] && OTHER=m1_sched_core ;;
    *m1*) [ -d m0_kernel_skeleton ] && OTHER=m0_kernel_skeleton ;;
esac
if [ -z "$OTHER" ]; then
    echo "[6/6] 跨里程碑一致性   跳过：未找到另一个里程碑目录"
else
    DRIFT=0; CHECKED=0
    for f in $SHARED; do
        if [ -f "$MILESTONE/$f" ] && [ -f "$OTHER/$f" ]; then
            CHECKED=$((CHECKED+1))
            if ! diff -q "$MILESTONE/$f" "$OTHER/$f" >/dev/null; then
                echo "        共享文件不一致: $f"; DRIFT=$((DRIFT+1))
            fi
        fi
    done
    if [ "$DRIFT" -eq 0 ]; then
        echo "[6/6] 跨里程碑一致性   通过：$CHECKED 个共享文件逐字相同（FR-WIN-004）"
    else
        echo "[6/6] 跨里程碑一致性   告警：$DRIFT/$CHECKED 个共享文件不同（仅提示，不影响退出码）"
    fi
fi

echo
if [ "$FAIL" -eq 0 ]; then
    echo "OK"
    exit 0
fi
echo "FAILED"
exit 1
