#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SOURCE="$ROOT/bootstrap/stage1/compiler.kofun"
SEED="$ROOT/bootstrap/stage1/compiler.c"
FIXTURE="$ROOT/bootstrap/fixtures/answer.kofun"
BOOL_FIXTURE="$ROOT/bootstrap/selfhost/driver/corpus_bool.kofun"
BOOL_C="$ROOT/bootstrap/selfhost/driver/corpus_bool.c"
BOOL_STDOUT="$ROOT/bootstrap/selfhost/driver/corpus_bool.stdout"
BRANCH_FIXTURE="$ROOT/bootstrap/selfhost/driver/corpus_branch.kofun"
BRANCH_C="$ROOT/bootstrap/selfhost/driver/corpus_branch.c"
BRANCH_STDOUT="$ROOT/bootstrap/selfhost/driver/corpus_branch.stdout"
LOOP_FIXTURE="$ROOT/bootstrap/selfhost/driver/corpus_loop.kofun"
LOOP_C="$ROOT/bootstrap/selfhost/driver/corpus_loop.c"
LOOP_STDOUT="$ROOT/bootstrap/selfhost/driver/corpus_loop.stdout"
WORK="${KOFUN_STAGE1_WORK:-$ROOT/build/bootstrap-stage1}"
CC="${CC:-cc}"

mkdir -p "$WORK"

(
    cd "$ROOT/bootstrap/stage1"
    sha256sum -c SHA256SUMS
)

"$CC" -std=c11 -O2 -Wall -Wextra -Werror "$SEED" -lm -o "$WORK/kofun-stage1"
"$WORK/kofun-stage1" "$FIXTURE" "$WORK/answer.c"
"$CC" -std=c11 -O2 -Wall -Wextra -Werror "$WORK/answer.c" -o "$WORK/answer"
answer=$("$WORK/answer")
test "$answer" = "42"

"$WORK/kofun-stage1" "$BOOL_FIXTURE" "$WORK/bool.c"
cmp "$BOOL_C" "$WORK/bool.c"
"$CC" -std=c11 -O2 -Wall -Wextra -Werror "$WORK/bool.c" -o "$WORK/bool"
"$WORK/bool" >"$WORK/bool.stdout"
cmp "$BOOL_STDOUT" "$WORK/bool.stdout"

# Nested blocks: the emitted C keeps one brace per Kofun block, and executing
# it proves the skipped `else if` condition and the short-circuited `||`
# operand — both `1 // 0` — were never evaluated.
"$WORK/kofun-stage1" "$BRANCH_FIXTURE" "$WORK/branch.c"
cmp "$BRANCH_C" "$WORK/branch.c"
"$CC" -std=c11 -O2 -Wall -Wextra -Werror "$WORK/branch.c" -o "$WORK/branch"
"$WORK/branch" >"$WORK/branch.stdout"
cmp "$BRANCH_STDOUT" "$WORK/branch.stdout"

# Loops: the emitted C keeps one brace pair per loop block, evaluates each
# range end once into the enclosing scope, and scopes the bound name to its
# own block. Executing it proves the bodies of a false `while` and of an empty
# range were never entered — each contains `1 // 0`.
"$WORK/kofun-stage1" "$LOOP_FIXTURE" "$WORK/loop.c"
cmp "$LOOP_C" "$WORK/loop.c"
"$CC" -std=c11 -O2 -Wall -Wextra -Werror "$WORK/loop.c" -o "$WORK/loop"
"$WORK/loop" >"$WORK/loop.stdout"
cmp "$LOOP_STDOUT" "$WORK/loop.stdout"

for fixture in \
    "$ROOT/bootstrap/selfhost/driver/corpus_reject_bool_arithmetic.kofun" \
    "$ROOT/bootstrap/selfhost/driver/corpus_reject_bool_print.kofun" \
    "$ROOT/bootstrap/selfhost/driver/corpus_reject_bool_annotation.kofun" \
    "$ROOT/bootstrap/selfhost/driver/corpus_reject_bool_infer_annotation.kofun" \
    "$ROOT/bootstrap/selfhost/driver/corpus_reject_bool_keyword_binding.kofun" \
    "$ROOT/bootstrap/selfhost/driver/corpus_reject_bool_order.kofun" \
    "$ROOT/bootstrap/selfhost/driver/corpus_reject_logical_int.kofun" \
    "$ROOT/bootstrap/selfhost/driver/corpus_reject_not_int.kofun" \
    "$ROOT/bootstrap/selfhost/driver/corpus_reject_single_pipe.kofun" \
    "$ROOT/bootstrap/selfhost/driver/corpus_reject_branch_condition.kofun" \
    "$ROOT/bootstrap/selfhost/driver/corpus_reject_branch_scope.kofun" \
    "$ROOT/bootstrap/selfhost/driver/corpus_reject_branch_shadow.kofun" \
    "$ROOT/bootstrap/selfhost/driver/corpus_reject_else_without_if.kofun" \
    "$ROOT/bootstrap/selfhost/driver/corpus_reject_else_after_else.kofun" \
    "$ROOT/bootstrap/selfhost/driver/corpus_reject_unclosed_block.kofun" \
    "$ROOT/bootstrap/selfhost/driver/corpus_reject_extra_block_end.kofun" \
    "$ROOT/bootstrap/selfhost/driver/corpus_reject_while_condition.kofun" \
    "$ROOT/bootstrap/selfhost/driver/corpus_reject_range_bound.kofun" \
    "$ROOT/bootstrap/selfhost/driver/corpus_reject_range_separator.kofun" \
    "$ROOT/bootstrap/selfhost/driver/corpus_reject_loop_shadow.kofun" \
    "$ROOT/bootstrap/selfhost/driver/corpus_reject_loop_scope.kofun" \
    "$ROOT/bootstrap/selfhost/driver/corpus_reject_loop_assignment.kofun" \
    "$ROOT/bootstrap/selfhost/driver/corpus_reject_else_after_loop.kofun"
do
    output="$WORK/$(basename "$fixture" .kofun).c"
    rm -f "$output"
    set +e
    "$WORK/kofun-stage1" "$fixture" "$output" >"$output.stdout"
    status=$?
    set -e
    test "$status" -ne 0
    test ! -e "$output"
    cmp "$ROOT/bootstrap/selfhost/driver/corpus_reject.stdout" \
        "$output.stdout"
done

printf '%s\n' \
    "PASS: Python-free Kofun Stage 1 built with $CC" \
    "PASS: compiled fixture returned $answer" \
    "PASS: Int/Bool Core accepts comparisons and refuses typed boundary crossings" \
    "PASS: nested if/else blocks scope their bindings and refuse a misplaced else" \
    "PASS: while and for-range loops nest, bound their range once, and scope their bound name"
