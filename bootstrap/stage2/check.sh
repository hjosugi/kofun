#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
stage2="$root/bootstrap/stage2"

(
    cd "$root"
    sha256sum -c bootstrap/stage2/SHA256SUMS
)

if command -v cc >/dev/null 2>&1; then
    compiler=cc
elif command -v clang >/dev/null 2>&1; then
    compiler=clang
elif command -v gcc >/dev/null 2>&1; then
    compiler=gcc
else
    echo "stage2 check: a C11 compiler is required" >&2
    exit 1
fi

temporary=${TMPDIR:-/tmp}/kofun-stage2-check.$$
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary"

"$compiler" -std=c11 -O2 -Wall -Wextra -Werror \
    "$stage2/compiler.c" -o "$temporary/kofun-stage2"

round_trip() {
    name=$1
    source=$2
    "$temporary/kofun-stage2" \
        "$source" \
        "$temporary/$name.kofun" \
        "$temporary/$name.ir" \
        "$temporary/$name.tokens" >/dev/null
    cmp "$source" "$temporary/$name.kofun"
    test -s "$temporary/$name.ir"
    test -s "$temporary/$name.tokens"
    grep '^kofun-stage2-ir/v1$' "$temporary/$name.ir" >/dev/null
    grep '^kofun-token-tape/v1$' "$temporary/$name.tokens" >/dev/null
}

round_trip fixture "$stage2/fixture.kofun"
grep '^function|classify|1|' "$temporary/fixture.ir" >/dev/null
grep '^function|main|0|' "$temporary/fixture.ir" >/dev/null
grep '^function-count|2$' "$temporary/fixture.ir" >/dev/null

# Numeric token contract (#717, docs/DECIMAL.md). Each literal must become
# exactly one token of the stated kind, which is what the byte spans assert —
# a span one byte short would mean the token split. The spans are pinned rather
# than counted because "one token per literal" is the property under test.
round_trip decimal-tokens "$stage2/fixtures/decimal_tokens.kofun"
grep -Fxq 'decimal|312|315|6' "$temporary/decimal-tokens.tokens"
grep -Fxq 'decimal|337|344|7' "$temporary/decimal-tokens.tokens"
grep -Fxq 'decimal|373|377|8' "$temporary/decimal-tokens.tokens"
grep -Fxq 'float|401|406|9' "$temporary/decimal-tokens.tokens"
grep -Fxq 'float|432|438|10' "$temporary/decimal-tokens.tokens"
grep -Fxq 'decimal|457|468|11' "$temporary/decimal-tokens.tokens"
test "$(grep -c '^decimal|' "$temporary/decimal-tokens.tokens")" -eq 4
test "$(grep -c '^float|' "$temporary/decimal-tokens.tokens")" -eq 2

# The maximal-munch range exception: `0..3` stays Int, `..`, Int. Asserting the
# three spans is the point — a lexer that merged `0.` would still round-trip.
round_trip range-exception "$stage2/fixtures/range_exception.kofun"
grep -Fxq 'integer|178|179|4' "$temporary/range-exception.tokens"
grep -Fxq 'punctuation|179|181|4' "$temporary/range-exception.tokens"
grep -Fxq 'integer|181|182|4' "$temporary/range-exception.tokens"
test "$(grep -c '^decimal|' "$temporary/range-exception.tokens")" -eq 0
test "$(grep -c '^float|' "$temporary/range-exception.tokens")" -eq 0

# A Decimal literal has no representation yet, so reaching lowering must be an
# explicit, source-located refusal that writes nothing and names the slice that
# would implement it. Reusing `E2S12` said the literal was invalid, which is
# not what is true: the literal is well formed and the compiler is unfinished.
printf 'fn main() {\n    print(1.5)\n}\n' >"$temporary/decimal-lowering.kofun"
set +e
"$root/bin/kofun" check "$temporary/decimal-lowering.kofun" \
    >"$temporary/decimal-lowering.stdout" 2>"$temporary/decimal-lowering.stderr"
decimal_lowering_status=$?
set -e
test "$decimal_lowering_status" -eq 1
test ! -s "$temporary/decimal-lowering.stdout"
grep -Fxq \
    'error[E2S99]: Decimal literal at byte 22 has no type yet (#710 slice 3)' \
    "$temporary/decimal-lowering.stderr"

# The Decimal resource profile (#721, `docs/DECIMAL.md` "Profile v1"). Frozen
# decision 8 requires its limits to be cross-backend *observable*, so the
# compiler constructs each literal and reports the limit at the literal's own
# byte. Both boundaries are here beside their one-over cases: a limit that
# clamped instead of failing would show as a written artifact on the `-over`
# lines, and a limit set one too tight would fail the `-at` lines.
#
# The digit cases are fractional on purpose. A 4097-digit literal with no `.`
# or `e` is an `Int` token and never reaches the Decimal profile at all, which
# is correct — `Int` has its own width — and is why an integer spelling here
# would have measured nothing.
decimal_limit_case() {
    label=$1
    program=$2
    expected=$3
    printf '%s' "$program" >"$temporary/decimal-limit.kofun"
    rm -f "$temporary/decimal-limit.c"
    set +e
    "$temporary/kofun-stage2" "$temporary/decimal-limit.kofun" \
        "$temporary/decimal-limit.c" "$temporary/decimal-limit.ir" \
        "$temporary/decimal-limit.tokens" \
        >"$temporary/decimal-limit.stdout" 2>"$temporary/decimal-limit.stderr"
    decimal_limit_status=$?
    set -e
    test "$decimal_limit_status" -eq 1 || {
        echo "stage2 check: $label was accepted" >&2
        exit 1
    }
    # The byte is not anchored to end of line: a resource diagnostic ends with
    # `at byte N` while `E2S99` carries it mid-sentence.
    grep -q "^error\\[$expected\\]:.*byte 22" \
        "$temporary/decimal-limit.stdout" || {
        echo "stage2 check: $label did not report $expected at byte 22" >&2
        cat "$temporary/decimal-limit.stdout" >&2
        exit 1
    }
    test ! -e "$temporary/decimal-limit.c" || {
        echo "stage2 check: $label emitted C" >&2
        exit 1
    }
}

decimal_nines=$(awk 'BEGIN { while (i++ < 4095) printf "9" }')
decimal_limit_case "significand at the digit limit" \
    "fn main() {
    print(1.$decimal_nines)
}
" E2S99
decimal_limit_case "significand one digit over" \
    "fn main() {
    print(1.${decimal_nines}9)
}
" D001
decimal_limit_case "scale at the limit" \
    'fn main() {
    print(1e-6144)
}
' E2S99
decimal_limit_case "scale one step over" \
    'fn main() {
    print(1e-6145)
}
' D002
echo "PASS: the Decimal resource profile is enforced at the literal's byte"

# The malformed forms `docs/DECIMAL.md` lists are lexical errors, so they must
# report before a token tape exists and write no artifact. `1..2` is checked
# alongside them because the range exception is the reason the scanner stays
# permissive about `.` and this pass reads tokens instead of tightening it.
for malformed in '1.' '.5' '1e' '1e+' '1_' '1._0' '1.0_' '1.0e'; do
    printf 'fn main() {\n    print(%s)\n}\n' "$malformed" \
        >"$temporary/malformed.kofun"
    rm -f "$temporary/malformed.c"
    set +e
    "$temporary/kofun-stage2" "$temporary/malformed.kofun" \
        "$temporary/malformed.c" "$temporary/malformed.ir" \
        "$temporary/malformed.tokens" \
        >"$temporary/malformed.stdout" 2>"$temporary/malformed.stderr"
    malformed_status=$?
    set -e
    test "$malformed_status" -eq 1 ||
        { echo "stage2 check: $malformed was accepted" >&2; exit 1; }
    grep -q '^error\[E2S98\]: malformed numeric literal at byte ' \
        "$temporary/malformed.stdout" ||
        { echo "stage2 check: $malformed did not report E2S98" >&2; exit 1; }
    test ! -e "$temporary/malformed.c" ||
        { echo "stage2 check: $malformed emitted C" >&2; exit 1; }
    test ! -e "$temporary/malformed.tokens" ||
        { echo "stage2 check: $malformed emitted a token tape" >&2; exit 1; }
done
echo "PASS: malformed numeric literals are lexical errors with no artifact"

copy_fixture="$stage2/fixtures/borrowed_copy_int.kofun"
move_fixture="$stage2/fixtures/borrowed_move_text.kofun"
move_diagnostic="$stage2/fixtures/borrowed_move_text.stderr"

round_trip borrowed-copy "$copy_fixture"
grep '^function|first|1|' "$temporary/borrowed-copy.ir" >/dev/null
round_trip borrowed-move "$move_fixture"
grep '^function|first|1|' "$temporary/borrowed-move.ir" >/dev/null

"$temporary/kofun-stage2" --check-ownership "$copy_fixture" \
    >"$temporary/borrowed-copy.stdout" \
    2>"$temporary/borrowed-copy.stderr"
test ! -s "$temporary/borrowed-copy.stdout"
test ! -s "$temporary/borrowed-copy.stderr"

set +e
"$temporary/kofun-stage2" --check-ownership "$move_fixture" \
    >"$temporary/borrowed-move.stdout" \
    2>"$temporary/borrowed-move.stderr"
borrowed_move_status=$?
"$temporary/kofun-stage2" --check-ownership "$stage2/fixture.kofun" \
    >"$temporary/ownership-unsupported.stdout" \
    2>"$temporary/ownership-unsupported.stderr"
ownership_unsupported_status=$?
set -e
test "$borrowed_move_status" -eq 1
cmp "$move_diagnostic" "$temporary/borrowed-move.stdout"
test ! -s "$temporary/borrowed-move.stderr"
test "$ownership_unsupported_status" -eq 1
grep 'error\[E2S20\]' "$temporary/ownership-unsupported.stdout" >/dev/null
test ! -s "$temporary/ownership-unsupported.stderr"

KOFUN_BUILD_DIR="$temporary/cli-stage1" \
KOFUN_STAGE2_BUILD_DIR="$temporary/cli-stage2" \
    "$root/bin/kofun" check "$copy_fixture" \
    >"$temporary/cli-borrowed-copy.stdout" \
    2>"$temporary/cli-borrowed-copy.stderr"
grep -F \
    "ok: $copy_fixture (Stage 2 Copy/borrow ownership slice; codegen unavailable)" \
    "$temporary/cli-borrowed-copy.stdout" >/dev/null
test ! -s "$temporary/cli-borrowed-copy.stderr"

set +e
KOFUN_BUILD_DIR="$temporary/cli-stage1" \
KOFUN_STAGE2_BUILD_DIR="$temporary/cli-stage2" \
    "$root/bin/kofun" check "$move_fixture" \
    >"$temporary/cli-borrowed-move.stdout" \
    2>"$temporary/cli-borrowed-move.stderr"
cli_borrowed_move_status=$?
set -e
test "$cli_borrowed_move_status" -eq 1
test ! -s "$temporary/cli-borrowed-move.stdout"
cmp "$move_diagnostic" "$temporary/cli-borrowed-move.stderr"

round_trip stage1 "$root/bootstrap/stage1/compiler.kofun"
grep '^function|emit_c|1|' "$temporary/stage1.ir" >/dev/null
grep '^function|compile_file|2|' "$temporary/stage1.ir" >/dev/null
grep '^function-count|23$' "$temporary/stage1.ir" >/dev/null

round_trip stage2 "$stage2/compiler.kofun"
grep '^function|lex|1|' "$temporary/stage2.ir" >/dev/null
grep '^function|parse_program|1|' "$temporary/stage2.ir" >/dev/null
grep '^function|parse_pattern_trees|1|' "$temporary/stage2.ir" >/dev/null
grep '^function|parse_patterns_file|2|' "$temporary/stage2.ir" >/dev/null
grep '^function|borrowed_collection_check|1|' "$temporary/stage2.ir" >/dev/null
grep '^function|lower_c|2|' "$temporary/stage2.ir" >/dev/null
grep '^function|emit_kofun|2|' "$temporary/stage2.ir" >/dev/null
grep '^function|compile_file|4|' "$temporary/stage2.ir" >/dev/null
grep '^function|check_ownership_file|1|' "$temporary/stage2.ir" >/dev/null

"$temporary/kofun-stage2" \
    "$stage2/compiler.kofun" \
    "$temporary/stage2-second.kofun" \
    "$temporary/stage2-second.ir" \
    "$temporary/stage2-second.tokens" >/dev/null
cmp "$temporary/stage2.kofun" "$temporary/stage2-second.kofun"
cmp "$temporary/stage2.ir" "$temporary/stage2-second.ir"
cmp "$temporary/stage2.tokens" "$temporary/stage2-second.tokens"

"$temporary/kofun-stage2" \
    "$stage2/core_fixture.kofun" \
    "$temporary/core.c" \
    "$temporary/core.ir" \
    "$temporary/core.tokens" >/dev/null
"$temporary/kofun-stage2" \
    "$stage2/core_fixture.kofun" \
    "$temporary/core-second.c" \
    "$temporary/core-second.ir" \
    "$temporary/core-second.tokens" >/dev/null
cmp "$temporary/core.c" "$temporary/core-second.c"
cmp "$temporary/core.ir" "$temporary/core-second.ir"
cmp "$temporary/core.tokens" "$temporary/core-second.tokens"
grep '^function|main|0|' "$temporary/core.ir" >/dev/null
grep 'kofun_mul' "$temporary/core.c" >/dev/null
grep 'kofun_floor_div' "$temporary/core.c" >/dev/null
grep 'kofun_floor_mod' "$temporary/core.c" >/dev/null
awk '
    /int64_t kofun_replacement =/ { state = 1; next }
    state == 1 && /if \(kofun_failed\) return 1;/ { state = 2; next }
    state == 2 && /k_b[0-9][0-9]* = kofun_replacement;/ { found = 1 }
    END { if (!found) exit 1 }
' "$temporary/core.c"
"$compiler" -std=c11 -O2 -Wall -Wextra -Werror \
    "$temporary/core.c" -o "$temporary/core-program"
"$temporary/core-program" >"$temporary/core.stdout" 2>"$temporary/core.stderr"
cmp "$stage2/core_fixture.stdout" "$temporary/core.stdout"
test ! -s "$temporary/core.stderr"

"$temporary/kofun-stage2" \
    "$stage2/functions_fixture.kofun" \
    "$temporary/functions.c" \
    "$temporary/functions.ir" \
    "$temporary/functions.tokens" >/dev/null
"$temporary/kofun-stage2" \
    "$stage2/functions_fixture.kofun" \
    "$temporary/functions-second.c" \
    "$temporary/functions-second.ir" \
    "$temporary/functions-second.tokens" >/dev/null
cmp "$temporary/functions.c" "$temporary/functions-second.c"
cmp "$temporary/functions.ir" "$temporary/functions-second.ir"
cmp "$temporary/functions.tokens" "$temporary/functions-second.tokens"
grep '^function|fib|1|' "$temporary/functions.ir" >/dev/null
grep '^function|forward_answer|0|' "$temporary/functions.ir" >/dev/null
grep 'static int64_t kofun_fn_fib' "$temporary/functions.c" >/dev/null
"$compiler" -std=c11 -O2 -Wall -Wextra -Werror \
    "$temporary/functions.c" -o "$temporary/functions-program"
"$temporary/functions-program" \
    >"$temporary/functions.stdout" 2>"$temporary/functions.stderr"
cmp "$stage2/functions_fixture.stdout" "$temporary/functions.stdout"
test ! -s "$temporary/functions.stderr"

KOFUN_BUILD_DIR="$temporary/cli-stage1-functions" \
KOFUN_STAGE2_BUILD_DIR="$temporary/cli-stage2-functions" \
    "$root/bin/kofun" run "$stage2/functions_fixture.kofun" \
    >"$temporary/cli-functions.stdout" \
    2>"$temporary/cli-functions.stderr"
cmp "$stage2/functions_fixture.stdout" "$temporary/cli-functions.stdout"
test ! -s "$temporary/cli-functions.stderr"

set +e
"$temporary/kofun-stage2" \
    "$stage2/function_arity_error.kofun" \
    "$temporary/function-arity-error.c" \
    "$temporary/function-arity-error.ir" \
    "$temporary/function-arity-error.tokens" \
    >"$temporary/function-arity-error.stdout" \
    2>"$temporary/function-arity-error.stderr"
function_arity_status=$?
"$temporary/kofun-stage2" \
    "$stage2/function_unknown_error.kofun" \
    "$temporary/function-unknown-error.c" \
    "$temporary/function-unknown-error.ir" \
    "$temporary/function-unknown-error.tokens" \
    >"$temporary/function-unknown-error.stdout" \
    2>"$temporary/function-unknown-error.stderr"
function_unknown_status=$?
set -e
test "$function_arity_status" -eq 1
test "$function_unknown_status" -eq 1
cmp \
    "$stage2/function_arity_error.stdout" \
    "$temporary/function-arity-error.stdout"
cmp \
    "$stage2/function_unknown_error.stdout" \
    "$temporary/function-unknown-error.stdout"
test ! -s "$temporary/function-arity-error.stderr"
test ! -s "$temporary/function-unknown-error.stderr"
test ! -e "$temporary/function-arity-error.c"
test ! -e "$temporary/function-unknown-error.c"

"$temporary/kofun-stage2" \
    "$stage2/core_error_fixture.kofun" \
    "$temporary/core-error.c" \
    "$temporary/core-error.ir" \
    "$temporary/core-error.tokens" >/dev/null
"$compiler" -std=c11 -O2 -Wall -Wextra -Werror \
    "$temporary/core-error.c" -o "$temporary/core-error-program"
set +e
"$temporary/core-error-program" \
    >"$temporary/core-error.stdout" 2>"$temporary/core-error.stderr"
core_error_status=$?
set -e
test "$core_error_status" -eq 1
test ! -s "$temporary/core-error.stdout"
cmp "$stage2/core_error_fixture.stderr" "$temporary/core-error.stderr"

set +e
"$temporary/kofun-stage2" \
    >"$temporary/usage.stdout" 2>"$temporary/usage.stderr"
usage_status=$?
"$temporary/kofun-stage2" \
    "$stage2/malformed.kofun" \
    "$temporary/malformed-output.kofun" \
    "$temporary/malformed.ir" \
    "$temporary/malformed.tokens" \
    >"$temporary/malformed.stdout" 2>"$temporary/malformed.stderr"
malformed_status=$?
"$temporary/kofun-stage2" \
    "$stage2/unsupported_core.kofun" \
    "$temporary/unsupported.c" \
    "$temporary/unsupported.ir" \
    "$temporary/unsupported.tokens" \
    >"$temporary/unsupported.stdout" 2>"$temporary/unsupported.stderr"
unsupported_status=$?
"$temporary/kofun-stage2" --compile-outcome \
    "$stage2/malformed.kofun" \
    "$temporary/outcome-malformed.c" \
    "$temporary/outcome-malformed.ir" \
    "$temporary/outcome-malformed.tokens" \
    >"$temporary/outcome-malformed.stdout" \
    2>"$temporary/outcome-malformed.stderr"
outcome_malformed_status=$?
"$temporary/kofun-stage2" --compile-outcome \
    "$stage2/unsupported_core.kofun" \
    "$temporary/outcome-unsupported.c" \
    "$temporary/outcome-unsupported.ir" \
    "$temporary/outcome-unsupported.tokens" \
    >"$temporary/outcome-unsupported.stdout" \
    2>"$temporary/outcome-unsupported.stderr"
outcome_unsupported_status=$?
"$temporary/kofun-stage2" --compile-outcome \
    "$copy_fixture" \
    "$temporary/outcome-ownership.c" \
    "$temporary/outcome-ownership.ir" \
    "$temporary/outcome-ownership.tokens" \
    >"$temporary/outcome-ownership.stdout" \
    2>"$temporary/outcome-ownership.stderr"
outcome_ownership_status=$?
set -e

if test "$usage_status" -ne 2 ||
   test ! -s "$temporary/usage.stdout" ||
   test -s "$temporary/usage.stderr"
then
    echo "stage2 check: usage contract differs from canonical Kofun main" >&2
    exit 1
fi
if test "$malformed_status" -ne 1 ||
   test ! -s "$temporary/malformed.stdout" ||
   test -s "$temporary/malformed.stderr"
then
    echo "stage2 check: malformed-input contract differs from canonical Kofun main" >&2
    exit 1
fi
if test "$unsupported_status" -ne 1 ||
   test ! -s "$temporary/unsupported.stdout" ||
   test -s "$temporary/unsupported.stderr" ||
   test -e "$temporary/unsupported.c"
then
    echo "stage2 check: unsupported Core lowering contract changed" >&2
    exit 1
fi
if test "$outcome_malformed_status" -ne 1 ||
   test "$outcome_unsupported_status" -ne 3 ||
   test "$outcome_ownership_status" -ne 3 ||
   test -e "$temporary/outcome-malformed.c" ||
   test -e "$temporary/outcome-unsupported.c" ||
   test -e "$temporary/outcome-ownership.c" ||
   test -s "$temporary/outcome-malformed.stderr" ||
   test -s "$temporary/outcome-unsupported.stderr" ||
   test -s "$temporary/outcome-ownership.stderr"
then
    echo "stage2 check: compile-outcome contract changed" >&2
    exit 1
fi

if find "$stage2" -type f \( -name '*.py' -o -name '*.kf' \) | grep . >/dev/null
then
    echo "stage2 check: Python or .kf file found" >&2
    exit 1
fi

KOFUN_STAGE2_COMPILER="$temporary/kofun-stage2" \
    sh "$root/tests/conformance/modules/lexical-scopes/run.sh"
KOFUN_STAGE2_COMPILER="$temporary/kofun-stage2" \
    sh "$root/tests/conformance/modules/shadowing/run.sh"
KOFUN_STAGE2_COMPILER="$temporary/kofun-stage2" \
    sh "$root/tests/conformance/patterns/run.sh"

# For-range loop variables are lexical bindings owned by the loop body scope:
# the header name is a declaration rather than a use, and body uses resolve to
# the loop binding. Valid `for` sources must not be misclassified as invalid
# E2S35; they reach their true lowering boundary instead (#619/#652).
"$temporary/kofun-stage2" --emit-scope-hir \
    "$stage2/fixture.kofun" \
    "$temporary/fixture.scopes"
grep '^binding|2|6|value|immutable|Int|copy|initialized|283|288|288$' \
    "$temporary/fixture.scopes" >/dev/null
grep '^use|325|330|6|2|read$' "$temporary/fixture.scopes" >/dev/null
! grep '^candidate-use|283|' "$temporary/fixture.scopes" >/dev/null

printf '%s\n' \
    'fn main() {' \
    '    let mut total = 0' \
    '    for value in 0 .. 4 {' \
    '        total = total + value' \
    '    }' \
    '    print(total)' \
    '}' >"$temporary/for-range-int.kofun"
set +e
"$temporary/kofun-stage2" --compile-outcome \
    "$temporary/for-range-int.kofun" \
    "$temporary/for-range-int.c" \
    "$temporary/for-range-int.ir" \
    "$temporary/for-range-int.tokens" \
    >"$temporary/for-range-int.stdout" \
    2>"$temporary/for-range-int.stderr"
for_range_status=$?
set -e
test "$for_range_status" -eq 3
grep 'error\[E2S10\]' "$temporary/for-range-int.stdout" >/dev/null
! grep 'E2S35' "$temporary/for-range-int.stdout" >/dev/null
test ! -s "$temporary/for-range-int.stderr"
test ! -e "$temporary/for-range-int.c"

# The 16 profile builtins are known, arity-checked names. A builtin call with
# wrong arity is a real E2S17 frontend fact; a well-formed builtin call is
# valid source outside the bounded Int C11 slice (unsupported lowering, exit
# 3), and only genuinely undeclared names remain invalid E2S16.
printf 'fn main() {\n    print(len(1, 2))\n}\n' \
    >"$temporary/builtin-arity.kofun"
printf 'fn main() {\n    print(len("x"))\n}\n' \
    >"$temporary/builtin-call.kofun"
set +e
"$temporary/kofun-stage2" --compile-outcome \
    "$temporary/builtin-arity.kofun" \
    "$temporary/builtin-arity.c" \
    "$temporary/builtin-arity.ir" \
    "$temporary/builtin-arity.tokens" \
    >"$temporary/builtin-arity.stdout" \
    2>"$temporary/builtin-arity.stderr"
builtin_arity_status=$?
"$temporary/kofun-stage2" --compile-outcome \
    "$temporary/builtin-call.kofun" \
    "$temporary/builtin-call.c" \
    "$temporary/builtin-call.ir" \
    "$temporary/builtin-call.tokens" \
    >"$temporary/builtin-call.stdout" \
    2>"$temporary/builtin-call.stderr"
builtin_call_status=$?
set -e
test "$builtin_arity_status" -eq 1
grep 'error\[E2S17\]: Core function `len` expects 1 arguments, got 2' \
    "$temporary/builtin-arity.stdout" >/dev/null
test "$builtin_call_status" -eq 3
grep 'error\[E2S10\]: unsupported Core builtin call `len`' \
    "$temporary/builtin-call.stdout" >/dev/null
test ! -e "$temporary/builtin-arity.c"
test ! -e "$temporary/builtin-call.c"

# The frozen self-host source S (bootstrap/stage1/compiler.kofun) clears the
# complete lexical binding layer, including every `for index` loop, and every
# call resolves to a declared function or profile builtin. S is therefore
# valid source outside the bounded lowering slice: compile-outcome reports
# unsupported (3), never an invalid-source classification. Typed lowering of
# the builtins is the remaining #653/#620 work.
set +e
"$temporary/kofun-stage2" --compile-outcome \
    "$root/bootstrap/stage1/compiler.kofun" \
    "$temporary/selfhost-S.c" \
    "$temporary/selfhost-S.ir" \
    "$temporary/selfhost-S.tokens" \
    >"$temporary/selfhost-S.stdout" \
    2>"$temporary/selfhost-S.stderr"
selfhost_frontier_status=$?
set -e
test "$selfhost_frontier_status" -eq 3
! grep 'E2S35' "$temporary/selfhost-S.stdout" >/dev/null
! grep 'E2S16' "$temporary/selfhost-S.stdout" >/dev/null
grep 'error\[E2S10\]: unsupported Core builtin call `is_xid_continue`' \
    "$temporary/selfhost-S.stdout" >/dev/null
test ! -e "$temporary/selfhost-S.c"

# Unannotated `let` bindings carry inferred types in the scope-HIR:
# literal kinds, builtin and user-function result types, List indexing to
# its Text element, and top-level comparisons to Bool. The frozen S is the
# exact evidence corpus; annotation and value-control defaults are
# unchanged.
"$temporary/kofun-stage2" --emit-scope-hir \
    "$root/bootstrap/stage1/compiler.kofun" \
    "$temporary/selfhost-S.scopes"
grep '^binding|2|3|symbols|immutable|List|gc|initialized|769|776|790$' \
    "$temporary/selfhost-S.scopes" >/dev/null
grep '^binding|21|22|symbol|immutable|Text|gc|initialized|2382|2388|2398$' \
    "$temporary/selfhost-S.scopes" >/dev/null
grep '^binding|69|77|marker|immutable|Text|gc|initialized|8902|8908|8919$' \
    "$temporary/selfhost-S.scopes" >/dev/null
grep '^binding|70|77|start|immutable|Int|copy|initialized|8928|8933|8954$' \
    "$temporary/selfhost-S.scopes" >/dev/null
grep '^binding|96|109|emitted|mutable|Text|gc|initialized|12213|12220|12225$' \
    "$temporary/selfhost-S.scopes" >/dev/null

printf '%s\n' \
    'fn main() {' \
    '    let ok = 1 < 2' \
    '    print(0)' \
    '}' >"$temporary/bool-infer.kofun"
"$temporary/kofun-stage2" --emit-scope-hir \
    "$temporary/bool-infer.kofun" \
    "$temporary/bool-infer.scopes"
grep '|ok|immutable|Bool|copy|' "$temporary/bool-infer.scopes" >/dev/null

# Builtin calls are checked against their frozen parameter types: a
# mismatched argument is a real E2S15 frontend fact naming the builtin,
# expected type, argument index, and byte; `len` accepts its Text/List
# overload; Text-literal arguments count correctly toward arity.
printf 'fn main() {\n    print(0)\n    chars(1)\n}\n' \
    >"$temporary/builtin-argument.kofun"
printf 'fn main() {\n    print(0)\n    find("a", 1)\n}\n' \
    >"$temporary/builtin-argument-two.kofun"
printf 'fn main() {\n    print(0)\n    contains("abc", "a")\n}\n' \
    >"$temporary/builtin-text-args.kofun"
set +e
"$temporary/kofun-stage2" --compile-outcome \
    "$temporary/builtin-argument.kofun" \
    "$temporary/builtin-argument.c" \
    "$temporary/builtin-argument.ir" \
    "$temporary/builtin-argument.tokens" \
    >"$temporary/builtin-argument.stdout" 2>/dev/null
builtin_argument_status=$?
"$temporary/kofun-stage2" --compile-outcome \
    "$temporary/builtin-argument-two.kofun" \
    "$temporary/builtin-argument-two.c" \
    "$temporary/builtin-argument-two.ir" \
    "$temporary/builtin-argument-two.tokens" \
    >"$temporary/builtin-argument-two.stdout" 2>/dev/null
builtin_argument_two_status=$?
"$temporary/kofun-stage2" --compile-outcome \
    "$temporary/builtin-text-args.kofun" \
    "$temporary/builtin-text-args.c" \
    "$temporary/builtin-text-args.ir" \
    "$temporary/builtin-text-args.tokens" \
    >"$temporary/builtin-text-args.stdout" 2>/dev/null
builtin_text_args_status=$?
set -e
test "$builtin_argument_status" -eq 1
grep 'error\[E2S15\]: builtin `chars` expects Text for argument 1, got Int' \
    "$temporary/builtin-argument.stdout" >/dev/null
test "$builtin_argument_two_status" -eq 1
grep 'error\[E2S15\]: builtin `find` expects Text for argument 2, got Int' \
    "$temporary/builtin-argument-two.stdout" >/dev/null
test "$builtin_text_args_status" -eq 3
grep 'error\[E2S10\]: unsupported Core builtin call `contains`' \
    "$temporary/builtin-text-args.stdout" >/dev/null
test ! -e "$temporary/builtin-argument.c"
test ! -e "$temporary/builtin-argument-two.c"
test ! -e "$temporary/builtin-text-args.c"

echo "PASS: Stage 2 statically compiled Copy Int borrowed-return slice"
echo "PASS: Stage 2 and kofun check rejected non-Copy Text move with E007"
echo "PASS: Stage 2 C11 calls support recursion, arity checks, and forward references"
echo "PASS: for-range loop variables bind in the loop body lexical scope"
echo "PASS: profile builtins are known, arity-checked, unsupported-to-lower names"
echo "PASS: the frozen self-host S is valid source outside the bounded slice"
echo "PASS: unannotated let bindings carry inferred scope-HIR types"
# Statement conditions and value returns are typed across the whole
# profile surface, before the unsupported classification: a confidently
# non-Bool while/if condition and a confidently mismatched return are
# invalid source; the frozen S passes both checks completely and keeps
# its builtin frontier.
printf 'fn main() {\n    let t = "abc"\n    while t {\n        print(0)\n    }\n}\n' \
    >"$temporary/while-text.kofun"
printf 'fn wrong() -> Text {\n    return 1\n}\n\nfn main() {\n    print(0)\n}\n' \
    >"$temporary/return-mismatch.kofun"
set +e
"$temporary/kofun-stage2" --compile-outcome \
    "$temporary/while-text.kofun" \
    "$temporary/while-text.c" \
    "$temporary/while-text.ir" \
    "$temporary/while-text.tokens" \
    >"$temporary/while-text.stdout" 2>/dev/null
while_text_status=$?
"$temporary/kofun-stage2" --compile-outcome \
    "$temporary/return-mismatch.kofun" \
    "$temporary/return-mismatch.c" \
    "$temporary/return-mismatch.ir" \
    "$temporary/return-mismatch.tokens" \
    >"$temporary/return-mismatch.stdout" 2>/dev/null
return_mismatch_status=$?
set -e
test "$while_text_status" -eq 1
grep 'error\[E2S23\]: while condition must be Bool' \
    "$temporary/while-text.stdout" >/dev/null
test "$return_mismatch_status" -eq 1
grep 'error\[E2S15\]: Core function `wrong` returns Int, expected Text' \
    "$temporary/return-mismatch.stdout" >/dev/null
test ! -e "$temporary/while-text.c"
test ! -e "$temporary/return-mismatch.c"

echo "PASS: builtin calls check their frozen parameter types"
echo "PASS: statement conditions and value returns are typed profile-wide"
echo "stage2 semantic frontend check passed"
