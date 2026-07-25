#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
SUITE="$ROOT/tests/diagnostics/c-abi"
WORK=${KOFUN_C_ABI_DIAGNOSTIC_WORK:-"$ROOT/build/diagnostics-c-abi"}
CC=${CC:-cc}

rm -rf "$WORK"
mkdir -p "$WORK"

"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$ROOT/bootstrap/c_abi/compiler.c" -o "$WORK/kofun-c-abi"

set +e
"$WORK/kofun-c-abi" \
    "$ROOT/tests/ffi/malformed.kofun" "$WORK/malformed.c" \
    >"$WORK/malformed.stdout" 2>"$WORK/malformed.stderr"
malformed_status=$?
(
    cd "$ROOT"
    "$WORK/kofun-c-abi" \
        tests/ffi/diagnostic-missing.kofun "$WORK/missing.c"
) >"$WORK/missing.stdout" 2>"$WORK/missing.stderr"
missing_status=$?
set -e

test "$malformed_status" -eq 1
test "$missing_status" -eq 1
test ! -s "$WORK/malformed.stdout"
test ! -s "$WORK/missing.stdout"
test ! -e "$WORK/malformed.c"
test ! -e "$WORK/missing.c"
cmp "$SUITE/cabi001.stderr" "$WORK/malformed.stderr"
cmp "$SUITE/cabi002.stderr" "$WORK/missing.stderr"

printf '%s\n' \
    "PASS: C ABI syntax and host-I/O diagnostics preserve stderr, status, and artifacts"
