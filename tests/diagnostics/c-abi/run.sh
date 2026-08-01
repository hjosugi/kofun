#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
SUITE="$ROOT/tests/diagnostics/c-abi"
WORK=${KOFUN_C_ABI_DIAGNOSTIC_WORK:-"$ROOT/build/diagnostics-c-abi"}
CC=${CC:-cc}
ASSERT_CONTEXT='diagnostics c-abi'
. "$ROOT/tests/assertions/assert.sh"

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

assert_num "malformed status" "$malformed_status" -eq 1
assert_num "missing status" "$missing_status" -eq 1
assert_file_empty "malformed.stdout" "$WORK/malformed.stdout"
assert_file_empty "missing.stdout" "$WORK/missing.stdout"
assert_absent "malformed.c" "$WORK/malformed.c"
assert_absent "missing.c" "$WORK/missing.c"
cmp "$SUITE/cabi001.stderr" "$WORK/malformed.stderr"
cmp "$SUITE/cabi002.stderr" "$WORK/missing.stderr"

printf '%s\n' \
    "PASS: C ABI syntax and host-I/O diagnostics preserve stderr, status, and artifacts"
