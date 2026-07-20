#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
SUITE="$ROOT/tests/diagnostics/stage2"
WORK=${KOFUN_DIAGNOSTIC_BLESS_WORK:-"$ROOT/build/diagnostics-stage2-bless"}
CC=${CC:-cc}

rm -rf "$WORK"
mkdir -p "$WORK/goldens"

"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$ROOT/bootstrap/stage2/compiler.c" \
    -o "$WORK/kofun-stage2"

for source in "$SUITE"/*.kofun; do
    stem=$(basename "${source%.kofun}")
    mode=$(sed -n 's/^# diagnostic-mode: //p' "$source")
    code=$(sed -n 's/^# expect-code: //p' "$source")
    span=$(sed -n 's/^# expect-span: //p' "$source")
    actual="$WORK/goldens/$stem.stderr"

    set +e
    if test "$mode" = ownership; then
        "$WORK/kofun-stage2" --check-ownership "$source" \
            >"$actual" 2>"$WORK/$stem.internal.stderr"
    elif test "$mode" = compile; then
        "$WORK/kofun-stage2" \
            "$source" "$WORK/$stem.c" "$WORK/$stem.ir" "$WORK/$stem.tokens" \
            >"$actual" 2>"$WORK/$stem.internal.stderr"
    else
        printf '%s\n' \
            "diagnostics bless: unknown mode '$mode' in $source" >&2
        exit 1
    fi
    status=$?
    set -e

    test "$status" -eq 1
    test ! -s "$WORK/$stem.internal.stderr"
    grep -F "error[$code]:" "$actual" >/dev/null
    if test "$span" != none; then
        grep -F "at $span" "$actual" >/dev/null
    fi
done

for generated in "$WORK/goldens"/*.stderr; do
    cp "$generated" "$SUITE/$(basename "$generated")"
done

printf '%s\n' \
    "Blessed Stage 2 diagnostic goldens; review the resulting diff."
