#!/bin/sh
set -eu

# Value-producing concrete-enum match gate for #921.
#
# Four things are checked, in this order:
#
#   1. a two-constructor enum — one payload-free, one carrying an `Int` — is
#      matched exhaustively in `let`, `print`, assignment, and `return`, and
#      the emitted program prints its golden;
#   2. the emitted C reads the scrutinee exactly once per match, before any arm
#      is tested, and every arm test reads that one copy rather than the
#      binding, which is the "scrutinee evaluated once" contract in the shape
#      the backend actually runs;
#   3. only the selected arm's result expression executes: every arm that must
#      not run divides by zero, so a lowering that evaluated an unselected arm
#      would fail the program instead of printing its golden;
#   4. every named failure mode is refused before a backend artifact exists,
#      with the exact diagnostic — `E2S25` for a missing constructor and for
#      guard-only coverage, `E2S26` for a duplicate arm and an unreachable
#      catch-all, `E2S30` for a Void, empty, or multi-value arm, and `E2S32`
#      for a constructor of another enum.
#
# The dispatch shape is deliberately the ordered if-chain the value positions
# already emit. Dense `switch` lowering belongs to #554, which owns statement
# position; this gate does not assert a dispatch shape for value position.

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CASES="$ROOT/tests/enum-match-value"
CC=${CC:-cc}
ASSERT_CONTEXT="enum match value"
. "$ROOT/tests/assertions/assert.sh"
. "$ROOT/bootstrap/stage2/build.sh"

WORK=$(mktemp -d "${TMPDIR:-/tmp}/kofun-enum-match-value.XXXXXX")
trap 'rm -rf "$WORK"' 0 1 2 15

fail() {
    printf 'enum match value: FAIL: %s\n' "$*" >&2
    exit 1
}

command -v "$CC" >/dev/null 2>&1 || fail 'a C11 compiler is required'

kofun_stage2_build "$ROOT" "$WORK/kofun-stage2"

# ------------------------------------------------------- corpus hygiene

find "$CASES" -type f \( -name '*.py' -o -name '*.kf' \) >"$WORK/forbidden"
assert_file_empty 'forbidden Python or .kf source in the corpus' \
    "$WORK/forbidden"

# ------------------------------------------------- positives and goldens

compile_and_run() {
    stem=$1
    source="$CASES/$stem.kofun"
    expected="$CASES/$stem.stdout"
    assert_regular_file "positive source $stem" "$source"
    assert_regular_file "positive golden $stem" "$expected"

    "$WORK/kofun-stage2" "$source" \
        "$WORK/$stem.c" "$WORK/$stem.ir" "$WORK/$stem.tokens" \
        >"$WORK/$stem.compile.stdout" 2>"$WORK/$stem.compile.stderr" ||
        fail "$stem did not compile: $(cat "$WORK/$stem.compile.stdout")"
    assert_file_empty "$stem compile stderr" "$WORK/$stem.compile.stderr"

    "$CC" -std=c11 -O2 -Wall -Wextra -Werror \
        "$WORK/$stem.c" -o "$WORK/$stem" ||
        fail "$stem: emitted C did not compile under -Werror"
    "$WORK/$stem" >"$WORK/$stem.stdout" 2>"$WORK/$stem.stderr" ||
        fail "$stem: the emitted program exited non-zero"
    assert_file_empty "$stem runtime stderr" "$WORK/$stem.stderr"
    cmp "$expected" "$WORK/$stem.stdout" ||
        fail "$stem: program output differs from the golden"

    # Same source, same C: the lowering is a function of the program.
    "$WORK/kofun-stage2" "$source" \
        "$WORK/$stem.second.c" "$WORK/$stem.second.ir" \
        "$WORK/$stem.second.tokens" >/dev/null
    cmp "$WORK/$stem.c" "$WORK/$stem.second.c" ||
        fail "$stem: repeated lowering is not deterministic"
    printf 'enum match value: value-position enum match runs: %s\n' "$stem"
}

compile_and_run positions
compile_and_run selected_arm

# `positions.kofun` carries five value-position enum matches: three in `main`
# (`let`, `print`, assignment) and one in `describe`, whose `return` is reached
# from two calls. Each must read the scrutinee into `kofun_match_value` exactly
# once, so the count is four reads for four written matches.
scrutinee_reads=$(grep -c \
    'KofunEnumValue kofun_match_value = k_b' "$WORK/positions.c")
assert_num 'scrutinee reads in positions.c' "$scrutinee_reads" -eq 4
written_matches=$(grep -c 'match ready {\|match reply {' \
    "$CASES/positions.kofun")
assert_num 'written value-position matches in positions.kofun' \
    "$written_matches" -eq 4

# Every arm test reads the single copy, never the scrutinee binding again, so
# no arm can observe a second evaluation.
arm_tests=$(grep -c 'if (!kofun_match_selected && kofun_match_value.tag' \
    "$WORK/positions.c")
assert_num 'arm tests reading the one scrutinee copy' "$arm_tests" -eq 8
assert_not_grep 'an arm test re-read the scrutinee binding' -qE -- \
    'if \(!kofun_match_selected && k_b' "$WORK/positions.c"

# The one copy is taken before the first arm test, and the selection flag stops
# the walk, which is what makes "only the selected arm runs" structural rather
# than incidental.
awk '
    /KofunEnumValue kofun_match_value = k_b/ { copied = 1; next }
    copied && /bool kofun_match_selected = false;/ { armed = 1; next }
    armed && /if \(!kofun_match_selected && kofun_match_value\.tag/ {
        found = 1
    }
    END { if (!found) exit 1 }
' "$WORK/positions.c" ||
    fail 'the scrutinee copy does not precede the first arm test'

# ---------------------------------------------------- explained refusals

expect_refused() {
    stem=$1
    code=$2
    source="$CASES/$stem.kofun"
    expected="$CASES/$stem.stdout"
    assert_regular_file "negative source $stem" "$source"
    assert_regular_file "negative golden $stem" "$expected"

    set +e
    "$WORK/kofun-stage2" "$source" \
        "$WORK/$stem.c" "$WORK/$stem.ir" "$WORK/$stem.tokens" \
        >"$WORK/$stem.stdout" 2>"$WORK/$stem.stderr"
    status=$?
    set -e
    assert_num "$stem refusal exit status" "$status" -eq 1
    assert_absent "$stem emitted a backend artifact" "$WORK/$stem.c"
    assert_file_empty "$stem refusal stderr" "$WORK/$stem.stderr"
    cmp "$expected" "$WORK/$stem.stdout" ||
        fail "$stem: diagnostic differs from the golden"
    assert_grep "$stem names $code" -Fq -- "error[$code]:" \
        "$WORK/$stem.stdout"
    printf 'enum match value: refused as designed: %s (%s)\n' "$stem" "$code"
}

expect_refused missing_constructor E2S25
expect_refused guard_only_coverage E2S25
expect_refused duplicate_arm E2S26
expect_refused unreachable_catchall E2S26
expect_refused void_arm E2S30
expect_refused empty_arm E2S30
expect_refused multi_value_arm E2S30
expect_refused foreign_constructor E2S32
expect_refused enum_valued_arm E2S32

# The missing-constructor and duplicate-arm refusals must name the constructor
# they are about, not merely carry the code.
assert_grep 'missing_constructor names the missing constructor' -Fq -- \
    '`Pending`' "$CASES/missing_constructor.stdout"
assert_grep 'duplicate_arm names the repeated constructor' -Fq -- \
    '`Ready`' "$CASES/duplicate_arm.stdout"

# ------------------------------------------- statement position unchanged

# The same enum, matched in statement position, still lowers and runs. This is
# the regression side of the change: value position is additive.
statement="$ROOT/tests/conformance/syntax/issues_35_47/enum_payload_match.kofun"
assert_regular_file 'statement-position payload match fixture' "$statement"
"$WORK/kofun-stage2" "$statement" \
    "$WORK/statement.c" "$WORK/statement.ir" "$WORK/statement.tokens" \
    >/dev/null
"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$WORK/statement.c" -o "$WORK/statement"
"$WORK/statement" >"$WORK/statement.stdout"
cmp "$ROOT/tests/conformance/syntax/issues_35_47/enum_payload_match.stdout" \
    "$WORK/statement.stdout" ||
    fail 'statement-position enum match output changed'

printf 'let, print, assignment, and return each produce one Int: PASS\n'
printf 'the scrutinee is read once, before any arm is tested: PASS\n'
printf 'only the selected arm produces its value: PASS\n'
printf 'every named failure mode is refused with its exact code: PASS\n'
