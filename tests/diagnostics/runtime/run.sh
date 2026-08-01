#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
WORK=${KOFUN_RUNTIME_DIAGNOSTIC_WORK:-"$ROOT/build/diagnostics-runtime"}
CC=${CC:-cc}
ASSERT_CONTEXT='diagnostics runtime'
. "$ROOT/tests/assertions/assert.sh"

rm -rf "$WORK"
mkdir -p "$WORK"

"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    -I"$ROOT/unicode" \
    "$ROOT/bootstrap/selfhost/c11/trap_division.c" \
    -o "$WORK/trap-division"

set +e
"$WORK/trap-division" \
    >"$WORK/trap-division.stdout" 2>"$WORK/trap-division.stderr"
status=$?
set -e

assert_num "trap-division exit status" "$status" -eq 1
assert_file_empty "trap-division.stdout" "$WORK/trap-division.stdout"
cmp "$ROOT/bootstrap/selfhost/c11/trap_division.stderr" \
    "$WORK/trap-division.stderr"

printf '%s\n' \
    "PASS: generated-C R010 preserves stderr, runtime status, and exact message"
