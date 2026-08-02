#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)
CASES="$ROOT/tests/conformance/modules/confusable-visible-set"
WORK=${KOFUN_CONFUSABLE_VISIBLE_SET_WORK:-"$ROOT/build/confusable-visible-set"}
CC=${CC:-cc}

fail() {
    printf '%s\n' "FAIL: $*" >&2
    exit 1
}

command -v "$CC" >/dev/null 2>&1 || fail 'a C11 compiler is required'
case $WORK in
    */confusable-visible-set|*/confusable-visible-set.*) ;;
    *) fail "work directory must end in confusable-visible-set[.suffix]: $WORK" ;;
esac
rm -rf "$WORK"
mkdir -p "$WORK"

"$CC" -std=c11 -O2 -Wall -Wextra -Werror -pedantic \
    -I"$ROOT/bootstrap/stage2" \
    "$ROOT/bootstrap/stage2/confusable_visible_set.c" \
    "$ROOT/bootstrap/stage2/sha256.c" \
    "$ROOT/unicode/kofun_unicode.c" \
    "$CASES/driver.c" \
    -o "$WORK/confusable-visible-set"

"$WORK/confusable-visible-set"
set +e
"$WORK/confusable-visible-set" --diagnostic \
    >"$WORK/eunicode008.stdout" 2>"$WORK/eunicode008.stderr"
status=$?
set -e
test "$status" -eq 1 || fail "EUNICODE008 fixture exited $status instead of 1"
test ! -s "$WORK/eunicode008.stderr" || fail 'EUNICODE008 fixture wrote stderr'
cmp "$CASES/eunicode008.stdout" "$WORK/eunicode008.stdout" ||
    fail 'EUNICODE008 bytes differ'

printf '%s\n' \
    'PASS: EUNICODE008 is deterministic, disclosure-safe, and transactional'
