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

round_trip arrow-lambda "$stage2/arrow_lambda.kofun"
grep '^function|arrow_fixture|0|' "$temporary/arrow-lambda.ir" >/dev/null
grep '^function-count|1$' "$temporary/arrow-lambda.ir" >/dev/null
set +e
"$temporary/kofun-stage2" \
    "$stage2/arrow_lambda.kofun" \
    "$temporary/arrow-lambda.c" \
    "$temporary/arrow-lambda.c.ir" \
    "$temporary/arrow-lambda.c.tokens" \
    >"$temporary/arrow-lambda.stdout" \
    2>"$temporary/arrow-lambda.stderr"
arrow_lambda_status=$?
set -e
test "$arrow_lambda_status" -eq 1
cmp "$stage2/arrow_lambda.stdout" "$temporary/arrow-lambda.stdout"
test ! -s "$temporary/arrow-lambda.stderr"
test ! -e "$temporary/arrow-lambda.c"

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
grep '^function|borrowed_collection_check|1|' "$temporary/stage2.ir" >/dev/null
grep '^function|lower_c|1|' "$temporary/stage2.ir" >/dev/null
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

set +e
"$temporary/kofun-stage2" \
    "$stage2/recovery.kofun" \
    "$temporary/recovery-normal.kofun" \
    "$temporary/recovery-normal.ir" \
    "$temporary/recovery-normal.tokens" \
    >"$temporary/recovery-normal.stdout" \
    2>"$temporary/recovery-normal.stderr"
recovery_normal_status=$?
set -e
test "$recovery_normal_status" -eq 1
test ! -e "$temporary/recovery-normal.kofun"
test ! -e "$temporary/recovery-normal.ir"
test ! -e "$temporary/recovery-normal.tokens"
test -s "$temporary/recovery-normal.stdout"
test ! -s "$temporary/recovery-normal.stderr"

set +e
"$temporary/kofun-stage2" \
    "$stage2/recovery.kofun" \
    "$temporary/recovery-output.kofun" \
    "$temporary/recovery.ir" \
    "$temporary/recovery.tokens" \
    --recover \
    >"$temporary/recovery.stdout" 2>"$temporary/recovery.stderr"
recovery_status=$?
"$temporary/kofun-stage2" \
    "$stage2/recovery.kofun" \
    "$temporary/recovery-second-output.kofun" \
    "$temporary/recovery-second.ir" \
    "$temporary/recovery-second.tokens" \
    --recover \
    >"$temporary/recovery-second.stdout" \
    2>"$temporary/recovery-second.stderr"
recovery_second_status=$?
"$temporary/kofun-stage2" \
    "$stage2/recovery_limit.kofun" \
    "$temporary/recovery-limit-output.kofun" \
    "$temporary/recovery-limit.ir" \
    "$temporary/recovery-limit.tokens" \
    --recover \
    >"$temporary/recovery-limit.stdout" \
    2>"$temporary/recovery-limit.stderr"
recovery_limit_status=$?
set -e
test "$recovery_status" -eq 1
test "$recovery_second_status" -eq 1
test "$recovery_limit_status" -eq 1
test ! -e "$temporary/recovery-output.kofun"
test ! -e "$temporary/recovery-second-output.kofun"
test ! -e "$temporary/recovery-limit-output.kofun"
test ! -s "$temporary/recovery.stderr"
test ! -s "$temporary/recovery-second.stderr"
test ! -s "$temporary/recovery-limit.stderr"
cmp "$stage2/recovery.expected.ir" "$temporary/recovery.ir"
cmp "$stage2/recovery.expected.ir" "$temporary/recovery-second.ir"
cmp "$stage2/recovery_limit.expected.ir" "$temporary/recovery-limit.ir"
cmp "$temporary/recovery-normal.stdout" "$temporary/recovery.stdout"
cmp "$temporary/recovery.tokens" "$temporary/recovery-second.tokens"
cmp "$temporary/recovery.stdout" "$temporary/recovery-second.stdout"
test "$(grep -c '^diagnostic|' "$temporary/recovery.ir")" -eq 3
test "$(grep -c '^function|' "$temporary/recovery.ir")" -eq 3
test "$(grep -c '^diagnostic|' "$temporary/recovery-limit.ir")" -eq 8
grep '^truncated|true$' "$temporary/recovery-limit.ir" >/dev/null

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
"$compiler" -std=c11 -O2 -Wall -Wextra -Werror \
    "$temporary/core.c" -o "$temporary/core-program"
"$temporary/core-program" >"$temporary/core.stdout" 2>"$temporary/core.stderr"
cmp "$stage2/core_fixture.stdout" "$temporary/core.stdout"
test ! -s "$temporary/core.stderr"

"$temporary/kofun-stage2" \
    "$stage2/control_fixture.kofun" \
    "$temporary/control.c" \
    "$temporary/control.ir" \
    "$temporary/control.tokens" >/dev/null
"$temporary/kofun-stage2" \
    "$stage2/control_fixture.kofun" \
    "$temporary/control-second.c" \
    "$temporary/control-second.ir" \
    "$temporary/control-second.tokens" >/dev/null
cmp "$temporary/control.c" "$temporary/control-second.c"
cmp "$temporary/control.ir" "$temporary/control-second.ir"
cmp "$temporary/control.tokens" "$temporary/control-second.tokens"
grep 'for (;;)' "$temporary/control.c" >/dev/null
grep '&&' "$temporary/control.c" >/dev/null
grep '||' "$temporary/control.c" >/dev/null
grep 'kofun_assignment' "$temporary/control.c" >/dev/null
"$compiler" -std=c11 -O2 -Wall -Wextra -Werror \
    "$temporary/control.c" -o "$temporary/control-program"
"$temporary/control-program" \
    >"$temporary/control.stdout" 2>"$temporary/control.stderr"
cmp "$stage2/control_fixture.stdout" "$temporary/control.stdout"
test ! -s "$temporary/control.stderr"

"$temporary/kofun-stage2" \
    "$stage2/if_expression.kofun" \
    "$temporary/if-expression.c" \
    "$temporary/if-expression.ir" \
    "$temporary/if-expression.tokens" >/dev/null
"$temporary/kofun-stage2" \
    "$stage2/if_expression.kofun" \
    "$temporary/if-expression-second.c" \
    "$temporary/if-expression-second.ir" \
    "$temporary/if-expression-second.tokens" >/dev/null
cmp "$temporary/if-expression.c" "$temporary/if-expression-second.c"
cmp "$temporary/if-expression.ir" "$temporary/if-expression-second.ir"
cmp "$temporary/if-expression.tokens" "$temporary/if-expression-second.tokens"
grep ') ? (' "$temporary/if-expression.c" >/dev/null
"$compiler" -std=c11 -O2 -Wall -Wextra -Werror \
    "$temporary/if-expression.c" -o "$temporary/if-expression-program"
"$temporary/if-expression-program" \
    >"$temporary/if-expression.stdout" \
    2>"$temporary/if-expression.stderr"
cmp "$stage2/if_expression.stdout" "$temporary/if-expression.stdout"
test ! -s "$temporary/if-expression.stderr"

"$temporary/kofun-stage2" \
    "$stage2/list_fixture.kofun" \
    "$temporary/list.c" \
    "$temporary/list.ir" \
    "$temporary/list.tokens" >/dev/null
"$temporary/kofun-stage2" \
    "$stage2/list_fixture.kofun" \
    "$temporary/list-second.c" \
    "$temporary/list-second.ir" \
    "$temporary/list-second.tokens" >/dev/null
cmp "$temporary/list.c" "$temporary/list-second.c"
cmp "$temporary/list.ir" "$temporary/list-second.ir"
cmp "$temporary/list.tokens" "$temporary/list-second.tokens"
grep 'kofun_list_int' "$temporary/list.c" >/dev/null
grep 'INT64_C(5)' "$temporary/list.c" >/dev/null
grep 'kofun_list_index' "$temporary/list.c" >/dev/null
grep 'index >= list.length' "$temporary/list.c" >/dev/null
grep 'return list.items\[index\]' "$temporary/list.c" >/dev/null
"$compiler" -std=c11 -O2 -Wall -Wextra -Werror \
    "$temporary/list.c" -o "$temporary/list-program"
"$temporary/list-program" \
    >"$temporary/list.stdout" 2>"$temporary/list.stderr"
cmp "$stage2/list_fixture.stdout" "$temporary/list.stdout"
test ! -s "$temporary/list.stderr"

"$temporary/kofun-stage2" \
    "$stage2/list_inferred_fixture.kofun" \
    "$temporary/list-inferred.c" \
    "$temporary/list-inferred.ir" \
    "$temporary/list-inferred.tokens" >/dev/null
"$compiler" -std=c11 -O2 -Wall -Wextra -Werror \
    "$temporary/list-inferred.c" -o "$temporary/list-inferred-program"
"$temporary/list-inferred-program" \
    >"$temporary/list-inferred.stdout" \
    2>"$temporary/list-inferred.stderr"
cmp "$stage2/list_fixture.stdout" "$temporary/list-inferred.stdout"
test ! -s "$temporary/list-inferred.stderr"

"$temporary/kofun-stage2" \
    "$stage2/list_bounds.kofun" \
    "$temporary/list-bounds.c" \
    "$temporary/list-bounds.ir" \
    "$temporary/list-bounds.tokens" >/dev/null
"$compiler" -std=c11 -O2 -Wall -Wextra -Werror \
    "$temporary/list-bounds.c" -o "$temporary/list-bounds-program"
set +e
"$temporary/list-bounds-program" \
    >"$temporary/list-bounds.stdout" 2>"$temporary/list-bounds.stderr"
list_bounds_status=$?
set -e
test "$list_bounds_status" -eq 1
test ! -s "$temporary/list-bounds.stdout"
cmp "$stage2/list_bounds.stderr" "$temporary/list-bounds.stderr"

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

lowering_error() {
    name=$1
    set +e
    "$temporary/kofun-stage2" \
        "$stage2/$name.kofun" \
        "$temporary/$name.c" \
        "$temporary/$name.ir" \
        "$temporary/$name.tokens" \
        >"$temporary/$name.stdout" 2>"$temporary/$name.stderr"
    status=$?
    set -e
    test "$status" -eq 1
    test ! -e "$temporary/$name.c"
    test ! -s "$temporary/$name.stderr"
    cmp "$stage2/$name.stdout" "$temporary/$name.stdout"
}

lowering_error scope_error
lowering_error immutable_error
lowering_error type_error
lowering_error list_element_type_error
lowering_error list_index_type_error
lowering_error list_unknown_index_error
lowering_error if_expression_condition_error
lowering_error if_expression_branch_error
lowering_error if_expression_else_error
lowering_error if_expression_value_type_error

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

if find "$stage2" -type f \( -name '*.py' -o -name '*.kf' \) | grep . >/dev/null
then
    echo "stage2 check: Python or .kf file found" >&2
    exit 1
fi

echo "PASS: Stage 2 statically compiled Copy Int borrowed-return slice"
echo "PASS: Stage 2 and kofun check rejected non-Copy Text move with E007"
echo "stage2 semantic frontend check passed"
