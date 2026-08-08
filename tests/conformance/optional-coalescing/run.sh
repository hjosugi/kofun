#!/bin/sh
set -eu

# Bounded Optional(Int) coalescing (#314): one Optional(Int) left, one Int
# fallback, lazy selection, and no extraction syntax.

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CASES="$ROOT/tests/conformance/optional-coalescing"
CC=${CC:-cc}
ASSERT_CONTEXT='optional coalescing'
. "$ROOT/tests/assertions/assert.sh"
. "$ROOT/bootstrap/stage2/build.sh"

WORK=$(mktemp -d "${TMPDIR:-/tmp}/kofun-optional-coalescing.XXXXXX")
trap 'rm -rf "$WORK"' 0 1 2 15

command -v "$CC" >/dev/null 2>&1 ||
    assert_fail 'a C11 compiler is required'

kofun_stage2_build "$ROOT" "$WORK/kofun-stage2"
mkdir -p "$WORK/remapped"
cp "$CASES/behavior.kofun" "$WORK/remapped/behavior.kofun"

compile_case() {
    source=$1
    label=$2
    "$WORK/kofun-stage2" "$source" \
        "$WORK/$label.c" "$WORK/$label.ir" "$WORK/$label.tokens" \
        >"$WORK/$label.compile.stdout" 2>"$WORK/$label.compile.stderr" ||
        assert_fail "$label did not lower"
    assert_file_empty "$label wrote internal stderr" \
        "$WORK/$label.compile.stderr"
}

# The same source and the same bytes at another path must emit byte-identical C.
compile_case "$CASES/behavior.kofun" behavior.first
compile_case "$CASES/behavior.kofun" behavior.second
compile_case "$WORK/remapped/behavior.kofun" behavior.remapped
cmp "$WORK/behavior.first.c" "$WORK/behavior.second.c" ||
    assert_fail 'two identical compilations emitted different C'
cmp "$WORK/behavior.first.c" "$WORK/behavior.remapped.c" ||
    assert_fail 'emitted C depends on the source path'

"$CC" -std=c11 -O2 -Wall -Wextra -Werror -pedantic \
    "$WORK/behavior.first.c" -o "$WORK/behavior" ||
    assert_fail 'the emitted behavior C is not strict C11'
"$WORK/behavior" >"$WORK/behavior.stdout" 2>"$WORK/behavior.stderr" ||
    assert_fail 'the behavior fixture exited nonzero'
cmp "$CASES/behavior.stdout" "$WORK/behavior.stdout" ||
    assert_fail 'lazy behavior, positions, or signed payloads changed'
assert_file_empty 'behavior wrote runtime stderr' "$WORK/behavior.stderr"

# These C shapes make the observable output load-bearing: the left is assigned
# once to a site-stable local, then a conditional tag test chooses payload or
# fallback. The conditional operator, not a helper call, owns right-side laziness.
emitted="$WORK/behavior.first.c"
assert_grep 'coalescing has a function-local Optional(Int) carrier' \
    -Eq -- 'KofunOptionalInt kofun_optional_int_coalesce_[0-9]+ = KOFUN_OPTIONAL_INT_NONE;' \
    "$emitted"
assert_grep 'the left is assigned once before selection' \
    -Eq -- '\(\(kofun_optional_int_coalesce_[0-9]+ = kofun_fn_present\(' \
    "$emitted"
assert_grep 'the tag selects payload or fallback lazily' \
    -Eq -- '\.tag != KOFUN_OPTIONAL_INT_NONE_TAG \? kofun_optional_int_coalesce_[0-9]+\.payload : kofun_fn_fallback\(' \
    "$emitted"
assert_grep 'an unselected checked fallback remains inside the conditional arm' \
    -Eq -- '\.payload : kofun_fn_checked_failure\(\)\)\)\)' \
    "$emitted"

# Ordinary primary-expression parentheses are transparent around each exact
# accepted left shape, and comparison parsing remains outside the bounded Int
# fallback. These are separate fixtures so either boundary can fail by name.
for stem in parenthesized_left comparison_precedence; do
    compile_case "$CASES/$stem.kofun" "$stem"
    "$CC" -std=c11 -O2 -Wall -Wextra -Werror -pedantic \
        "$WORK/$stem.c" -o "$WORK/$stem" ||
        assert_fail "$stem emitted invalid C11"
    "$WORK/$stem" >"$WORK/$stem.stdout" 2>"$WORK/$stem.stderr" ||
        assert_fail "$stem exited nonzero"
    cmp "$CASES/$stem.stdout" "$WORK/$stem.stdout" ||
        assert_fail "$stem observation changed"
    assert_file_empty "$stem wrote runtime stderr" "$WORK/$stem.stderr"
done

# A checked failure from either selected evaluation propagates once. In the
# left case stdout must stay empty, proving the fallback marker never ran.
for stem in left_checked_error selected_fallback_error; do
    compile_case "$CASES/$stem.kofun" "$stem"
    "$CC" -std=c11 -O2 -Wall -Wextra -Werror -pedantic \
        "$WORK/$stem.c" -o "$WORK/$stem" ||
        assert_fail "$stem emitted invalid C11"
    set +e
    "$WORK/$stem" >"$WORK/$stem.stdout" 2>"$WORK/$stem.stderr"
    status=$?
    set -e
    assert_num "$stem runtime status" "$status" -eq 1
    assert_file_empty "$stem wrote stdout" "$WORK/$stem.stdout"
    cmp "$CASES/$stem.stderr" "$WORK/$stem.stderr" ||
        assert_fail "$stem runtime diagnostic changed"
done

# Unsupported payloads, non-Optional lefts, Optional fallbacks, chaining, and a
# missing operand all fail before a C/backend artifact exists, with one stable
# diagnostic family. Stage 2 deliberately retains its parsed IR/token
# diagnostic checkpoints on a lowering refusal; pinning them here keeps
# "backend artifact" from being misread as "no diagnostic output files".
negatives='non_optional_left non_int_fallback optional_fallback chained unsupported_payload missing_operand'
for stem in $negatives; do
    set +e
    "$WORK/kofun-stage2" "$CASES/$stem.kofun" \
        "$WORK/$stem.c" "$WORK/$stem.ir" "$WORK/$stem.tokens" \
        >"$WORK/$stem.actual" 2>"$WORK/$stem.internal.stderr"
    status=$?
    set -e
    assert_num "$stem compile status" "$status" -eq 1
    assert_file_empty "$stem wrote internal stderr" \
        "$WORK/$stem.internal.stderr"
    assert_absent "$stem emitted a C artifact" "$WORK/$stem.c"
    assert_file_nonempty "$stem lost its parsed IR checkpoint" \
        "$WORK/$stem.ir"
    assert_file_nonempty "$stem lost its token checkpoint" \
        "$WORK/$stem.tokens"
    cmp "$CASES/$stem.stderr" "$WORK/$stem.actual" ||
        assert_fail "$stem diagnostic changed"
    assert_grep "$stem uses the registered refusal" \
        -Fq -- 'error[E2S147]:' "$WORK/$stem.actual"
done

# Adding a fixture without adding it to this gate must fail the count.
present_count=$(find "$CASES" -name '*.kofun' ! -name expectations.kofun \
    -type f | wc -l | tr -d ' ')
golden_count=$(find "$CASES" \( -name '*.stdout' -o -name '*.stderr' \) \
    -type f | wc -l | tr -d ' ')
assert_num 'every source fixture is exercised' "$present_count" -eq 11
assert_num 'every source fixture has one golden' "$golden_count" -eq 11

assert_not_grep 'Stage 2 introduced an unchecked extraction spelling' \
    -E -- 'KOFUN_OPTIONAL_INT_UNWRAP|optional_int_unwrap|force_unwrap' \
    "$ROOT/bootstrap/stage2/compiler.c"
assert_grep 'E2S147 remains a registered diagnostic identity' \
    -Eq -- '^E2S147[[:space:]]+optional-construction[[:space:]]+frontend' \
    "$ROOT/tests/diagnostics/registry.tsv"

printf '%s\n' \
    'PASS: Optional(Int) ?? Int executes in let, print, return, and argument position' \
    'PASS: left is evaluated once and fallback only on absence' \
    'PASS: checked failures propagate only from selected evaluation' \
    'PASS: transparent left parentheses and outer comparison precedence execute' \
    'PASS: refusals leave no C/backend artifact and retain IR/token checkpoints' \
    'PASS: emitted C is deterministic strict C11 with no unchecked extraction'
