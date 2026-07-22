#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)
CASES="$ROOT/tests/conformance/syntax/issues_35_47"
SPEC="$ROOT/spec/syntax/FOUNDATIONS_AND_CONTROL.md"
MATCH_SPEC="$ROOT/spec/bool-match-exhaustiveness.md"
ENUM_MATCH_SPEC="$ROOT/spec/enum-match-exhaustiveness.md"
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

"$WORK/kofun-stage2" \
    "$CASES/if_outer_assignment.kofun" \
    "$WORK/if-outer-assignment.c" \
    "$WORK/if-outer-assignment.ir" \
    "$WORK/if-outer-assignment.tokens" >/dev/null
"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$WORK/if-outer-assignment.c" -o "$WORK/if-outer-assignment"
"$WORK/if-outer-assignment" \
    >"$WORK/if-outer-assignment.stdout" \
    2>"$WORK/if-outer-assignment.stderr"
cmp "$CASES/if_outer_assignment.stdout" \
    "$WORK/if-outer-assignment.stdout" ||
    fail "Stage 2 outer mutable assignment output differs"
test ! -s "$WORK/if-outer-assignment.stderr" ||
    fail "Stage 2 outer mutable assignment wrote unexpected stderr"
printf '%s\n' "PASS executable Stage 2 outer mutable assignment"

"$WORK/kofun-stage2" \
    "$CASES/if_value.kofun" \
    "$WORK/if-value.c" \
    "$WORK/if-value.ir" \
    "$WORK/if-value.tokens" >/dev/null
"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$WORK/if-value.c" -o "$WORK/if-value"
"$WORK/if-value" \
    >"$WORK/if-value.stdout" 2>"$WORK/if-value.stderr"
cmp "$CASES/if_value.stdout" "$WORK/if-value.stdout" ||
    fail "Stage 2 value-position if output differs"
test ! -s "$WORK/if-value.stderr" ||
    fail "Stage 2 value-position if wrote unexpected stderr"
KOFUN_BUILD_DIR="$WORK/cli-stage1" \
KOFUN_STAGE2_BUILD_DIR="$WORK/cli-stage2" \
    "$ROOT/bin/kofun" run "$CASES/if_value.kofun" \
    >"$WORK/if-value-cli.stdout" 2>"$WORK/if-value-cli.stderr"
cmp "$CASES/if_value.stdout" "$WORK/if-value-cli.stdout" ||
    fail "public kofun run value-position if output differs"
test ! -s "$WORK/if-value-cli.stderr" ||
    fail "public kofun run value-position if wrote stderr"
printf '%s\n' "PASS executable Stage 2 value-position if"

"$WORK/kofun-stage2" \
    "$CASES/if_value_selected_error.kofun" \
    "$WORK/if-value-error.c" \
    "$WORK/if-value-error.ir" \
    "$WORK/if-value-error.tokens" >/dev/null
"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$WORK/if-value-error.c" -o "$WORK/if-value-error"
set +e
"$WORK/if-value-error" \
    >"$WORK/if-value-error.stdout" 2>"$WORK/if-value-error.stderr"
if_value_error_status=$?
set -e
test "$if_value_error_status" -eq 1 ||
    fail "selected failing value-if branch returned $if_value_error_status"
test ! -s "$WORK/if-value-error.stdout" ||
    fail "selected failing value-if branch wrote stdout"
cmp \
    "$CASES/if_value_selected_error.stderr" \
    "$WORK/if-value-error.stderr" ||
    fail "selected failing value-if branch diagnostic differs"
printf '%s\n' "PASS selected-only Stage 2 value-position if evaluation"

expect_stage2_diagnostic \
    "$ROOT/tests/diagnostics/stage2/e2s27_value_if_else.kofun" \
    "$ROOT/tests/diagnostics/stage2/e2s27_value_if_else.stderr"
expect_stage2_diagnostic \
    "$ROOT/tests/diagnostics/stage2/e2s28_value_if_branch.kofun" \
    "$ROOT/tests/diagnostics/stage2/e2s28_value_if_branch.stderr"
expect_stage2_diagnostic \
    "$CASES/if_value_void_branch.kofun" \
    "$CASES/if_value_void_branch.stdout"

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

"$WORK/kofun-stage2" \
    "$CASES/match_guard.kofun" \
    "$WORK/match-guard.c" \
    "$WORK/match-guard.ir" \
    "$WORK/match-guard.tokens" >/dev/null
"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$WORK/match-guard.c" -o "$WORK/match-guard"
"$WORK/match-guard" \
    >"$WORK/match-guard.stdout" 2>"$WORK/match-guard.stderr"
cmp "$CASES/match_guard.stdout" "$WORK/match-guard.stdout" ||
    fail "Stage 2 guarded Bool match output differs"
test ! -s "$WORK/match-guard.stderr" ||
    fail "Stage 2 guarded Bool match wrote unexpected stderr"
KOFUN_BUILD_DIR="$WORK/guard-cli-stage1" \
KOFUN_STAGE2_BUILD_DIR="$WORK/guard-cli-stage2" \
    "$ROOT/bin/kofun" run "$CASES/match_guard.kofun" \
    >"$WORK/match-guard-cli.stdout" 2>"$WORK/match-guard-cli.stderr"
cmp "$CASES/match_guard.stdout" "$WORK/match-guard-cli.stdout" ||
    fail "public kofun run guarded Bool match output differs"
test ! -s "$WORK/match-guard-cli.stderr" ||
    fail "public kofun run guarded Bool match wrote stderr"
printf '%s\n' "PASS ordered Stage 2 Bool match guards"

"$WORK/kofun-stage2" \
    "$CASES/match_guard_error.kofun" \
    "$WORK/match-guard-error.c" \
    "$WORK/match-guard-error.ir" \
    "$WORK/match-guard-error.tokens" >/dev/null
"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$WORK/match-guard-error.c" -o "$WORK/match-guard-error"
set +e
"$WORK/match-guard-error" \
    >"$WORK/match-guard-error.stdout" \
    2>"$WORK/match-guard-error.stderr"
match_guard_error_status=$?
set -e
test "$match_guard_error_status" -eq 1 ||
    fail "selected failing match guard returned $match_guard_error_status"
test ! -s "$WORK/match-guard-error.stdout" ||
    fail "selected failing match guard wrote stdout"
cmp \
    "$CASES/match_guard_error.stderr" \
    "$WORK/match-guard-error.stderr" ||
    fail "selected failing match guard diagnostic differs"
printf '%s\n' "PASS selected-only Stage 2 match guard evaluation"

"$WORK/kofun-stage2" \
    "$CASES/match_value.kofun" \
    "$WORK/match-value.c" \
    "$WORK/match-value.ir" \
    "$WORK/match-value.tokens" >/dev/null
"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$WORK/match-value.c" -o "$WORK/match-value"
"$WORK/match-value" \
    >"$WORK/match-value.stdout" 2>"$WORK/match-value.stderr"
cmp "$CASES/match_value.stdout" "$WORK/match-value.stdout" ||
    fail "Stage 2 value-position Bool match output differs"
test ! -s "$WORK/match-value.stderr" ||
    fail "Stage 2 value-position Bool match wrote unexpected stderr"
KOFUN_BUILD_DIR="$WORK/match-value-cli-stage1" \
KOFUN_STAGE2_BUILD_DIR="$WORK/match-value-cli-stage2" \
    "$ROOT/bin/kofun" run "$CASES/match_value.kofun" \
    >"$WORK/match-value-cli.stdout" 2>"$WORK/match-value-cli.stderr"
cmp "$CASES/match_value.stdout" "$WORK/match-value-cli.stdout" ||
    fail "public kofun run value-position Bool match output differs"
test ! -s "$WORK/match-value-cli.stderr" ||
    fail "public kofun run value-position Bool match wrote stderr"
printf '%s\n' "PASS executable Stage 2 value-position Bool match"

"$WORK/kofun-stage2" \
    "$CASES/match_value_error.kofun" \
    "$WORK/match-value-error.c" \
    "$WORK/match-value-error.ir" \
    "$WORK/match-value-error.tokens" >/dev/null
"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$WORK/match-value-error.c" -o "$WORK/match-value-error"
set +e
"$WORK/match-value-error" \
    >"$WORK/match-value-error.stdout" \
    2>"$WORK/match-value-error.stderr"
match_value_error_status=$?
set -e
test "$match_value_error_status" -eq 1 ||
    fail "selected failing value-match arm returned $match_value_error_status"
test ! -s "$WORK/match-value-error.stdout" ||
    fail "selected failing value-match arm wrote stdout"
cmp \
    "$CASES/match_value_error.stderr" \
    "$WORK/match-value-error.stderr" ||
    fail "selected failing value-match arm diagnostic differs"
printf '%s\n' "PASS selected-only Stage 2 value-match arm evaluation"

"$WORK/kofun-stage2" \
    "$CASES/enum_match.kofun" \
    "$WORK/enum-match.c" \
    "$WORK/enum-match.ir" \
    "$WORK/enum-match.tokens" >/dev/null
"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$WORK/enum-match.c" -o "$WORK/enum-match"
"$WORK/enum-match" \
    >"$WORK/enum-match.stdout" 2>"$WORK/enum-match.stderr"
cmp "$CASES/enum_match.stdout" "$WORK/enum-match.stdout" ||
    fail "Stage 2 payload-free enum match output differs"
test ! -s "$WORK/enum-match.stderr" ||
    fail "Stage 2 payload-free enum match wrote unexpected stderr"
KOFUN_BUILD_DIR="$WORK/enum-match-cli-stage1" \
KOFUN_STAGE2_BUILD_DIR="$WORK/enum-match-cli-stage2" \
    "$ROOT/bin/kofun" run "$CASES/enum_match.kofun" \
    >"$WORK/enum-match-cli.stdout" 2>"$WORK/enum-match-cli.stderr"
cmp "$CASES/enum_match.stdout" "$WORK/enum-match-cli.stdout" ||
    fail "public kofun run payload-free enum match output differs"
test ! -s "$WORK/enum-match-cli.stderr" ||
    fail "public kofun run payload-free enum match wrote stderr"
grep '^type|Signal|3|' "$WORK/enum-match.ir" >/dev/null ||
    fail "Stage 2 IR omitted the Signal type record"
grep '^constructor|Red|Signal|0|' "$WORK/enum-match.ir" >/dev/null ||
    fail "Stage 2 IR omitted the Red constructor tag"
grep '^constructor|Green|Signal|1|' "$WORK/enum-match.ir" >/dev/null ||
    fail "Stage 2 IR omitted the Green constructor tag"
grep '^constructor|Blue|Signal|2|' "$WORK/enum-match.ir" >/dev/null ||
    fail "Stage 2 IR omitted the Blue constructor tag"
printf '%s\n' "PASS executable Stage 2 payload-free enum match"

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
    "$CASES/match_guard_non_exhaustive.kofun" \
    "$CASES/match_guard_non_exhaustive.stdout"
expect_stage2_diagnostic \
    "$CASES/match_non_bool.kofun" \
    "$CASES/match_non_bool.stdout"
expect_stage2_diagnostic \
    "$ROOT/tests/diagnostics/stage2/e2s29_match_guard.kofun" \
    "$ROOT/tests/diagnostics/stage2/e2s29_match_guard.stderr"
expect_stage2_diagnostic \
    "$CASES/match_value_non_exhaustive.kofun" \
    "$CASES/match_value_non_exhaustive.stdout"
expect_stage2_diagnostic \
    "$CASES/match_value_duplicate.kofun" \
    "$CASES/match_value_duplicate.stdout"
expect_stage2_diagnostic \
    "$ROOT/tests/diagnostics/stage2/e2s30_match_value_arm.kofun" \
    "$ROOT/tests/diagnostics/stage2/e2s30_match_value_arm.stderr"
expect_stage2_diagnostic \
    "$CASES/enum_match_missing.kofun" \
    "$CASES/enum_match_missing.stdout"
expect_stage2_diagnostic \
    "$CASES/enum_match_duplicate.kofun" \
    "$CASES/enum_match_duplicate.stdout"
expect_stage2_diagnostic \
    "$CASES/enum_match_guard_non_exhaustive.kofun" \
    "$CASES/enum_match_guard_non_exhaustive.stdout"
expect_stage2_diagnostic \
    "$CASES/enum_match_constructor_mismatch.kofun" \
    "$CASES/enum_match_constructor_mismatch.stdout"
expect_stage2_diagnostic \
    "$CASES/enum_match_tag_escape.kofun" \
    "$CASES/enum_match_tag_escape.stdout"
expect_stage2_diagnostic \
    "$CASES/enum_constructor_escape.kofun" \
    "$CASES/enum_constructor_escape.stdout"
expect_stage2_diagnostic \
    "$ROOT/tests/diagnostics/stage2/e2s31_enum_duplicate_constructor.kofun" \
    "$ROOT/tests/diagnostics/stage2/e2s31_enum_duplicate_constructor.stderr"
expect_stage2_diagnostic \
    "$ROOT/tests/diagnostics/stage2/e2s32_enum_unknown_constructor.kofun" \
    "$ROOT/tests/diagnostics/stage2/e2s32_enum_unknown_constructor.stderr"

grep '^# Bounded Bool match exhaustiveness' "$MATCH_SPEC" >/dev/null ||
    fail "bounded Bool match specification is missing"
for code in E2S24 E2S25 E2S26 E2S29 E2S30; do
    grep "\`$code\`" "$MATCH_SPEC" >/dev/null ||
        fail "bounded Bool match specification omits $code"
done
printf '%s\n' "PASS bounded Bool match specification"

grep '^# Bounded payload-free enum match exhaustiveness' \
    "$ENUM_MATCH_SPEC" >/dev/null ||
    fail "bounded payload-free enum match specification is missing"
for code in E2S25 E2S26 E2S29 E2S31 E2S32; do
    grep "\`$code\`" "$ENUM_MATCH_SPEC" >/dev/null ||
        fail "bounded enum match specification omits $code"
done
grep 'at most 32 enum types' "$ENUM_MATCH_SPEC" >/dev/null ||
    fail "bounded enum match specification omits the type limit"
grep 'at most 64 constructors' "$ENUM_MATCH_SPEC" >/dev/null ||
    fail "bounded enum match specification omits the constructor limit"
printf '%s\n' "PASS bounded payload-free enum match specification"

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
