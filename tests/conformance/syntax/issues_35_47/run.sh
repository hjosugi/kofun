#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)
CASES="$ROOT/tests/conformance/syntax/issues_35_47"
SPEC="$ROOT/spec/syntax/FOUNDATIONS_AND_CONTROL.md"
MATCH_SPEC="$ROOT/spec/bool-match-exhaustiveness.md"
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
    "$CASES/if_statement.kofun" \
    "$WORK/if-statement.c" \
    "$WORK/if-statement.ir" \
    "$WORK/if-statement.tokens" >/dev/null
"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$WORK/if-statement.c" -o "$WORK/if-statement"
"$WORK/if-statement" \
    >"$WORK/if-statement.stdout" 2>"$WORK/if-statement.stderr"
cmp "$CASES/if_statement.stdout" "$WORK/if-statement.stdout" ||
    fail "Stage 2 assignment/if output differs"
test ! -s "$WORK/if-statement.stderr" ||
    fail "Stage 2 assignment/if wrote unexpected stderr"
printf '%s\n' "PASS executable Stage 2 assignment followed by if"

expect_stage2_diagnostic \
    "$CASES/if_outer_assignment.kofun" \
    "$CASES/if_outer_assignment.stdout"

"$WORK/kofun-stage2" \
    "$CASES/match_bool.kofun" \
    "$WORK/match-bool.c" \
    "$WORK/match-bool.ir" \
    "$WORK/match-bool.tokens" >/dev/null
"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$WORK/match-bool.c" -o "$WORK/match-bool"
"$WORK/match-bool" \
    >"$WORK/match-bool.stdout" 2>"$WORK/match-bool.stderr"
cmp "$CASES/match_bool.stdout" "$WORK/match-bool.stdout" ||
    fail "Stage 2 Bool match output differs"
test ! -s "$WORK/match-bool.stderr" ||
    fail "Stage 2 Bool match wrote unexpected stderr"
printf '%s\n' "PASS executable Stage 2 exhaustive Bool match"

expect_stage2_diagnostic \
    "$CASES/match_missing_false.kofun" \
    "$CASES/match_missing_false.stdout"
expect_stage2_diagnostic \
    "$CASES/match_missing_true.kofun" \
    "$CASES/match_missing_true.stdout"
expect_stage2_diagnostic \
    "$CASES/match_duplicate_true.kofun" \
    "$CASES/match_duplicate_true.stdout"
expect_stage2_diagnostic \
    "$CASES/match_after_catchall.kofun" \
    "$CASES/match_after_catchall.stdout"
expect_stage2_diagnostic \
    "$CASES/match_unreachable_catchall.kofun" \
    "$CASES/match_unreachable_catchall.stdout"
expect_stage2_diagnostic \
    "$CASES/match_guard_unsupported.kofun" \
    "$CASES/match_guard_unsupported.stdout"
expect_stage2_diagnostic \
    "$CASES/match_non_bool.kofun" \
    "$CASES/match_non_bool.stdout"

grep '^# Bounded Bool match exhaustiveness' "$MATCH_SPEC" >/dev/null ||
    fail "bounded Bool match specification is missing"
for code in E2S24 E2S25 E2S26; do
    grep "\`$code\`" "$MATCH_SPEC" >/dev/null ||
        fail "bounded Bool match specification omits $code"
done
printf '%s\n' "PASS bounded Bool match specification"

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
expect_stage2_unsupported "$CASES/unsupported_else_if.kofun"
expect_stage2_unsupported "$CASES/unsupported_for.kofun"
expect_stage2_unsupported "$CASES/unsupported_while.kofun"

set +e
"$WORK/kofun-stage2" \
    "$CASES/invalid_if_condition.kofun" \
    "$WORK/invalid-if.c" \
    "$WORK/invalid-if.ir" \
    "$WORK/invalid-if.tokens" \
    >"$WORK/invalid-if.stdout" 2>"$WORK/invalid-if.stderr"
invalid_if_status=$?
set -e
test "$invalid_if_status" -eq 1 ||
    fail "invalid if condition unexpectedly returned $invalid_if_status"
test ! -e "$WORK/invalid-if.c" ||
    fail "invalid if condition emitted C"
test ! -s "$WORK/invalid-if.stderr" ||
    fail "invalid if condition wrote unexpected stderr"
grep '^error\[E2S23\]: if condition must be Bool or an Int comparison at byte ' \
    "$WORK/invalid-if.stdout" >/dev/null ||
    fail "invalid if condition did not emit E2S23"
printf '%s\n' "PASS diagnostic: invalid if condition"

printf '%s\n' \
    "PASS: syntax issues #35-#47 bootstrap capability checkpoint" \
    "coverage: 13 subjects; 5 partial; 2 Core-implemented; 6 unsupported via 6 fixtures"
