#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
SUITE="$ROOT/tests/diagnostics/unicode"
WORK=${KOFUN_UNICODE_DIAGNOSTIC_WORK:-"$ROOT/build/diagnostics-unicode"}
CC=${CC:-cc}

rm -rf "$WORK"
mkdir -p "$WORK"

"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$ROOT/bootstrap/stage2/compiler.c" -o "$WORK/kofun-stage2"

set +e
KOFUN_DIAGNOSTIC_LOCALE=ja_JP \
    "$WORK/kofun-stage2" \
    "$ROOT/tests/unicode/non_nfc_identifier.kofun" \
    "$WORK/non-nfc.c" "$WORK/non-nfc.ir" "$WORK/non-nfc.tokens" \
    >"$WORK/non-nfc.stdout" 2>"$WORK/non-nfc.stderr"
non_nfc_status=$?
"$WORK/kofun-stage2" \
    "$ROOT/tests/unicode/confusable_identifier.kofun" \
    "$WORK/confusable.c" "$WORK/confusable.ir" "$WORK/confusable.tokens" \
    >"$WORK/confusable.stdout" 2>"$WORK/confusable.stderr"
confusable_status=$?
set -e

test "$non_nfc_status" -eq 1
test "$confusable_status" -eq 1
test ! -s "$WORK/non-nfc.stderr"
test ! -s "$WORK/confusable.stderr"
test ! -e "$WORK/non-nfc.c"
test ! -e "$WORK/confusable.c"
cmp "$SUITE/eunicode005.stdout" "$WORK/non-nfc.stdout"
cmp "$SUITE/eunicode006.stdout" "$WORK/confusable.stdout"

printf '%s\n' \
    "PASS: localized Unicode diagnostics preserve stdout, status, spans, and artifacts"
