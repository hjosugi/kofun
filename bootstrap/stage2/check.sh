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
grep '^function-count|11$' "$temporary/stage1.ir" >/dev/null

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

# The frozen self-host source S (bootstrap/stage1/compiler.kofun) now clears
# the complete lexical binding layer, including every `for index` loop. Its
# current frontier is the unregistered Unicode builtin from the 46-row
# profile; #653 moves this boundary, not the binding layer.
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
test "$selfhost_frontier_status" -eq 1
! grep 'E2S35' "$temporary/selfhost-S.stdout" >/dev/null
grep 'error\[E2S16\]: unknown Core function `is_xid_continue`' \
    "$temporary/selfhost-S.stdout" >/dev/null
test ! -e "$temporary/selfhost-S.c"

echo "PASS: Stage 2 statically compiled Copy Int borrowed-return slice"
echo "PASS: Stage 2 and kofun check rejected non-Copy Text move with E007"
echo "PASS: Stage 2 C11 calls support recursion, arity checks, and forward references"
echo "PASS: for-range loop variables bind in the loop body lexical scope"
echo "PASS: the frozen self-host S clears lexical binding; frontier is E2S16 builtins"
echo "stage2 semantic frontend check passed"
