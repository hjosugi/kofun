#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
WORK=${KOFUN_RUNTIME_DIAGNOSTIC_WORK:-"$ROOT/build/diagnostics-runtime"}
CC=${CC:-cc}

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

test "$status" -eq 1
test ! -s "$WORK/trap-division.stdout"
cmp "$ROOT/bootstrap/selfhost/c11/trap_division.stderr" \
    "$WORK/trap-division.stderr"

printf '%s\n' \
    "PASS: generated-C R010 preserves stderr, runtime status, and exact message"
