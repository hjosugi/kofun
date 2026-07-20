#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)
CASES="$ROOT/tests/conformance/syntax/issues_35_47"
SPEC="$ROOT/spec/syntax/FOUNDATIONS_AND_CONTROL.md"
CC=${CC:-cc}

command -v "$CC" >/dev/null 2>&1 || {
    printf '%s\n' "syntax issues #35-#47: a C11 compiler is required" >&2
    exit 1
}

WORK=$(mktemp -d "${TMPDIR:-/tmp}/kofun-syntax-35-47.XXXXXX")
trap 'rm -rf "$WORK"' 0 1 2 15

fail() {
    printf '%s\n' "FAIL: $*" >&2
    exit 1
}

expect_stage2_unsupported() {
    source=$1
    stem=$(basename "${source%.kofun}")
    set +e
    "$WORK/kofun-stage2" \
        "$source" \
        "$WORK/$stem.c" \
        "$WORK/$stem.ir" \
        "$WORK/$stem.tokens" \
        >"$WORK/$stem.stdout" 2>"$WORK/$stem.stderr"
    status=$?
    set -e

    test "$status" -eq 1 ||
        fail "$stem: Stage 2 unexpectedly returned $status"
    test ! -e "$WORK/$stem.c" ||
        fail "$stem: Stage 2 emitted C for an unsupported feature"
    test ! -s "$WORK/$stem.stderr" ||
        fail "$stem: Stage 2 wrote an unexpected stderr diagnostic"
    grep '^error\[E2S' "$WORK/$stem.stdout" >/dev/null ||
        fail "$stem: missing explicit Stage 2 unsupported diagnostic"
    printf '%s\n' "PASS unsupported: $stem"
}

expect_stage2_diagnostic() {
    source=$1
    expected=$2
    stem=$(basename "${source%.kofun}")
    set +e
    "$WORK/kofun-stage2" \
        "$source" \
        "$WORK/$stem.c" \
        "$WORK/$stem.ir" \
        "$WORK/$stem.tokens" \
        >"$WORK/$stem.stdout" 2>"$WORK/$stem.stderr"
    status=$?
    set -e

    test "$status" -eq 1 ||
        fail "$stem: Stage 2 unexpectedly returned $status"
    test ! -e "$WORK/$stem.c" ||
        fail "$stem: Stage 2 emitted C after a compile error"
    test ! -s "$WORK/$stem.stderr" ||
        fail "$stem: Stage 2 wrote an unexpected stderr diagnostic"
    cmp "$expected" "$WORK/$stem.stdout" ||
        fail "$stem: Stage 2 diagnostic changed"
    printf '%s\n' "PASS diagnostic: $stem"
}

for issue in 35 36 37 38 39 40 41 42 43 44 45 46 47; do
    grep "^## #$issue — " "$SPEC" >/dev/null ||
        fail "normative section for issue #$issue is missing"
done
test "$(grep -c '^### Prior designs and tradeoffs$' "$SPEC")" -eq 13 ||
    fail "each issue must contain a prior-design review"
test "$(grep -c '^### User stories and non-goals$' "$SPEC")" -eq 13 ||
    fail "each issue must contain user stories and non-goals"
test "$(grep -c '^### Normative contract$' "$SPEC")" -eq 13 ||
    fail "each issue must contain a normative contract"
test "$(grep -c '^# valid' "$SPEC")" -eq 13 ||
    fail "each issue must contain a valid canonical example"
test "$(grep -c '^# invalid' "$SPEC")" -eq 13 ||
    fail "each issue must contain an invalid example"
printf '%s\n' "PASS specification shape: issues #35-#47"

stage1_output=$("$ROOT/bin/kofun" run "$CASES/stage1_foundations.kofun")
test "$stage1_output" = 42 ||
    fail "Stage 1 foundations fixture did not print 42"
printf '%s\n' "PASS executable Stage 1 foundations"

if "$ROOT/bin/kofun" check "$CASES/unsupported_unicode_identifier.kofun" \
    >"$WORK/unicode-stage1.stdout" 2>"$WORK/unicode-stage1.stderr"
then
    fail "Stage 1 unexpectedly accepted a Unicode identifier"
fi
printf '%s\n' "PASS unsupported: Unicode identifiers in Stage 1"

"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$ROOT/bootstrap/stage2/compiler.c" -o "$WORK/kofun-stage2"

"$WORK/kofun-stage2" \
    "$CASES/stage2_mutable_surface.kofun" \
    "$WORK/mutable.c" \
    "$WORK/mutable.ir" \
    "$WORK/mutable.tokens" >/dev/null
"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$WORK/mutable.c" -o "$WORK/mutable"
mutable_output=$("$WORK/mutable")
test "$mutable_output" = 42 ||
    fail "Stage 2 mutable declaration fixture did not print 42"
grep '^function|main|0|' "$WORK/mutable.ir" >/dev/null ||
    fail "Stage 2 IR did not record fn main"
printf '%s\n' "PASS executable Stage 2 immutable/mutable declarations"

expect_stage2_diagnostic \
    "$CASES/immutable_assignment.kofun" \
    "$CASES/immutable_assignment.stdout"
expect_stage2_diagnostic \
    "$CASES/unknown_assignment.kofun" \
    "$CASES/unknown_assignment.stdout"

"$WORK/kofun-stage2" \
    "$CASES/structural_surface.kofun" \
    "$WORK/structural.kofun" \
    "$WORK/structural.ir" \
    "$WORK/structural.tokens" >/dev/null
cmp "$CASES/structural_surface.kofun" "$WORK/structural.kofun" ||
    fail "Stage 2 structural projection changed source bytes"
grep '^function|classify|1|' "$WORK/structural.ir" >/dev/null ||
    fail "Stage 2 IR did not record classify arity"
grep '^function|future_surface|0|' "$WORK/structural.ir" >/dev/null ||
    fail "Stage 2 IR did not record future_surface arity"
grep '^function-count|2$' "$WORK/structural.ir" >/dev/null ||
    fail "Stage 2 IR function count differs"
printf '%s\n' "PASS structural-only Stage 2 surface projection"

set +e
"$WORK/kofun-stage2" \
    "$CASES/unsupported_unicode_identifier.kofun" \
    "$WORK/unicode.c" \
    "$WORK/unicode.ir" \
    "$WORK/unicode.tokens" \
    >"$WORK/unicode-stage2.stdout" 2>"$WORK/unicode-stage2.stderr"
unicode_status=$?
set -e
test "$unicode_status" -eq 1 ||
    fail "Stage 2 unexpectedly accepted a Unicode identifier"
test ! -e "$WORK/unicode.c" ||
    fail "Stage 2 emitted C for a Unicode identifier"
test ! -s "$WORK/unicode-stage2.stderr" ||
    fail "Stage 2 Unicode rejection wrote unexpected stderr"
grep '^error\[E2S11\]' "$WORK/unicode-stage2.stdout" >/dev/null ||
    fail "Stage 2 Unicode rejection was not an explicit Core diagnostic"
printf '%s\n' "PASS unsupported: Unicode identifiers in Stage 2"

expect_stage2_unsupported "$CASES/unsupported_lambda.kofun"
expect_stage2_unsupported "$CASES/unsupported_owned_binding.kofun"
expect_stage2_unsupported "$CASES/unsupported_if.kofun"
expect_stage2_unsupported "$CASES/unsupported_for.kofun"
expect_stage2_unsupported "$CASES/unsupported_match.kofun"
expect_stage2_unsupported "$CASES/unsupported_while.kofun"

printf '%s\n' \
    "PASS: syntax issues #35-#47 bootstrap capability checkpoint" \
    "coverage: 13 subjects; 3 partial; 2 Core-implemented; 8 unsupported via 7 fixtures"
