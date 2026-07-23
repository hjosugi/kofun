#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CASES="$ROOT/tests/conformance/adt"
CC=${CC:-cc}
WORK=${KOFUN_ADT_FRONTEND_WORK:-"$ROOT/build/adt-frontend"}

fail() {
    printf '%s\n' "FAIL: $*" >&2
    exit 1
}

command -v "$CC" >/dev/null 2>&1 || fail 'a C11 compiler is required'
case $WORK in
    */adt-frontend|*/adt-frontend.*) ;;
    *) fail "work directory must end in adt-frontend[.suffix]: $WORK" ;;
esac
rm -rf "$WORK"
mkdir -p "$WORK"

"$CC" -std=c11 -O2 -Wall -Wextra -Werror -pedantic \
    "$ROOT/bootstrap/stage2/adt_frontend.c" \
    -o "$WORK/kofun-adt-frontend"

"$WORK/kofun-adt-frontend" "$CASES/maybe_int.kofun" \
    "$WORK/maybe_int.ir" "$WORK/maybe_int.tokens"
"$WORK/kofun-adt-frontend" "$CASES/maybe_int.kofun" \
    "$WORK/maybe_int.second.ir" "$WORK/maybe_int.second.tokens"
cmp "$WORK/maybe_int.ir" "$WORK/maybe_int.second.ir" ||
    fail 'repeated ADT IR differs'
cmp "$WORK/maybe_int.tokens" "$WORK/maybe_int.second.tokens" ||
    fail 'repeated ADT token tape differs'
cmp "$CASES/maybe_int.ir" "$WORK/maybe_int.ir" ||
    fail 'MaybeInt typed IR golden differs'
"$WORK/kofun-adt-frontend" "$CASES/maybe_int_reordered.kofun" \
    "$WORK/maybe_int_reordered.ir" "$WORK/maybe_int_reordered.tokens"
grep -E '^(adt|constructor)\|' "$WORK/maybe_int.ir" |
    sed 's/|span=[0-9][0-9]*\.\.[0-9][0-9]*//' >"$WORK/maybe.ids"
grep -E '^(adt|constructor)\|' "$WORK/maybe_int_reordered.ir" |
    sed 's/|span=[0-9][0-9]*\.\.[0-9][0-9]*//' >"$WORK/reordered.ids"
cmp "$WORK/maybe.ids" "$WORK/reordered.ids" ||
    fail 'declaration/function reordering changed nominal identities'

grep -F 'adt|adt-id=adt:MaybeInt|name=MaybeInt' "$WORK/maybe_int.ir" \
    >/dev/null || fail 'MaybeInt nominal identity is missing'
grep -F 'constructor-id=constructor:adt:MaybeInt:0' \
    "$WORK/maybe_int.ir" >/dev/null || fail 'Missing identity is unresolved'
grep -F 'constructor-id=constructor:adt:MaybeInt:1' \
    "$WORK/maybe_int.ir" >/dev/null || fail 'Present identity is unresolved'
grep -F 'construct|function=present|constructor-id=constructor:adt:MaybeInt:1' \
    "$WORK/maybe_int.ir" >/dev/null ||
    fail 'constructor use before declaration was not resolved'
grep -F 'identifier|Present|113|120' "$WORK/maybe_int.tokens" \
    >/dev/null || fail 'constructor use span is missing from token tape'

"$WORK/kofun-adt-frontend" "$CASES/arithmetic_payload.kofun" \
    "$WORK/arithmetic_payload.ir" "$WORK/arithmetic_payload.tokens"
grep -F \
    'construct|function=arithmetic|constructor-id=constructor:adt:MaybeInt:1' \
    "$WORK/arithmetic_payload.ir" |
    grep -F '|payload=Int' >/dev/null ||
    fail 'arithmetic constructor payload did not type as Int'
for operator in + '*' // %; do
    grep -F "punctuation|$operator|" "$WORK/arithmetic_payload.tokens" \
        >/dev/null || fail "arithmetic payload token $operator is missing"
done

expect_failure() {
    stem=$1
    code=$2
    set +e
    "$WORK/kofun-adt-frontend" "$CASES/$stem.kofun" \
        "$WORK/$stem.ir" "$WORK/$stem.tokens" \
        >"$WORK/$stem.actual" 2>"$WORK/$stem.internal.stderr"
    status=$?
    set -e
    test "$status" -eq 1 || fail "$stem exited $status instead of 1"
    test ! -s "$WORK/$stem.internal.stderr" ||
        fail "$stem wrote internal stderr"
    test ! -e "$WORK/$stem.ir" || fail "$stem emitted rejected IR"
    test ! -e "$WORK/$stem.tokens" || fail "$stem emitted rejected tokens"
    cmp "$CASES/$stem.stderr" "$WORK/$stem.actual" ||
        fail "$stem diagnostic differs"
    grep -F "error[$code]:" "$WORK/$stem.actual" >/dev/null ||
        fail "$stem expected $code"
    printf '%s\n' "PASS ADT diagnostic: $stem"
}

expect_failure duplicate_type E2S36
expect_failure duplicate_constructor E2S37
expect_failure ambiguous_constructor E2S38
expect_failure one_variant E2S39
expect_failure unknown_payload_type E2S40
expect_failure multiple_payload_fields E2S41
expect_failure wrong_constructor_arity E2S42
expect_failure payload_type_mismatch E2S43
expect_failure unknown_constructor E2S44
expect_failure generic_unsupported E2S45
expect_failure recursive_unsupported E2S45
expect_failure missing_leading_variant_bar E2S46

test -z "$(find "$WORK" -type f \
    \( -name '*.generated.c' -o -name '*.o' -o -name '*.native' \) -print)" ||
    fail 'ADT frontend emitted a backend/runtime artifact'

printf '%s\n' \
    'PASS: MaybeInt declarations and uses carry deterministic nominal IDs' \
    'PASS: declarations are collected before constructor body resolution' \
    'PASS: bounded parenthesized Int arithmetic payloads type without evaluation' \
    'PASS: zero/one-Int arity and payload typing diagnostics are exact' \
    'PASS: generic, recursive, multi-field, and malformed forms are explicit'
