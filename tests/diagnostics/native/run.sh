#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
SUITE="$ROOT/tests/diagnostics/native"
WORK=${KOFUN_NATIVE_DIAGNOSTIC_WORK:-"$ROOT/build/diagnostics-native"}
CC=${CC:-cc}

rm -rf "$WORK"
mkdir -p "$WORK"

"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$ROOT/bootstrap/native/core_compiler.c" -o "$WORK/kofun-native-core"
{
    printf 'fn main() {\n    print("'
    printf '\300\257'
    printf '")\n}\n'
} >"$WORK/invalid-utf8.kofun"

set +e
"$WORK/kofun-native-core" \
    "$WORK/invalid-utf8.kofun" x86_64-linux "$WORK/invalid-utf8.elf" \
    >"$WORK/invalid-utf8.stdout" 2>"$WORK/invalid-utf8.stderr"
status=$?
set -e

test "$status" -eq 1
test ! -s "$WORK/invalid-utf8.stdout"
test ! -e "$WORK/invalid-utf8.elf"
cmp "$SUITE/eunicode001.stderr" "$WORK/invalid-utf8.stderr"

printf '%s\n' \
    "PASS: native invalid-UTF-8 rejection preserves stderr, status, span, and artifact policy"
