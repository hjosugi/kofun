#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
SUITE="$ROOT/tests/diagnostics/unicode"
WORK=${KOFUN_UNICODE_DIAGNOSTIC_WORK:-"$ROOT/build/diagnostics-unicode"}
CC=${CC:-cc}
. "$ROOT/bootstrap/stage2/build.sh"
ASSERT_CONTEXT='diagnostics unicode'
. "$ROOT/tests/assertions/assert.sh"

rm -rf "$WORK"
mkdir -p "$WORK"

kofun_stage2_build "$ROOT" "$WORK/kofun-stage2"

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

assert_num "non nfc status" "$non_nfc_status" -eq 1
assert_num "confusable status" "$confusable_status" -eq 1
assert_file_empty "non-nfc.stderr" "$WORK/non-nfc.stderr"
assert_file_empty "confusable.stderr" "$WORK/confusable.stderr"
assert_absent "non-nfc.c" "$WORK/non-nfc.c"
assert_absent "confusable.c" "$WORK/confusable.c"
cmp "$SUITE/eunicode005.stdout" "$WORK/non-nfc.stdout"
cmp "$SUITE/eunicode006.stdout" "$WORK/confusable.stdout"

"$CC" -std=c11 -O2 -Wall -Wextra -Werror -pedantic \
    -I"$ROOT/bootstrap/stage2" \
    "$ROOT/bootstrap/stage2/confusable_visible_set.c" \
    "$ROOT/bootstrap/stage2/sha256.c" \
    "$ROOT/unicode/kofun_unicode.c" \
    "$ROOT/tests/conformance/modules/confusable-visible-set/driver.c" \
    -o "$WORK/confusable-visible-set"
set +e
"$WORK/confusable-visible-set" --diagnostic \
    >"$WORK/eunicode008.stdout" 2>"$WORK/eunicode008.stderr"
visible_status=$?
set -e
assert_num "visible-set status" "$visible_status" -eq 1
assert_file_empty "eunicode008.stderr" "$WORK/eunicode008.stderr"
cmp "$SUITE/eunicode008.stdout" "$WORK/eunicode008.stdout"

printf '%s\n' \
    "PASS: localized and visible-set Unicode diagnostics preserve stdout, status, spans, and artifacts"
