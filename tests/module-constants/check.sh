#!/bin/sh
set -eu

# Top-level `let NAME = <integer literal>` module constants.
#
# Five things are checked, in this order:
#
#   1. every accepted initializer shape — small, negative, and both Int64
#      bounds — reaches the backend and prints its golden;
#   2. a constant is a file-scope C constant, not a lexical binding: the
#      emitted C declares one `static const int64_t` per constant and never
#      names a constant through the `k_b` binding prefix;
#   3. constants interleave with a record and an enum in either order, which
#      is what proves every top-level walker steps over a constant rather
#      than mistaking it for the declaration next to it;
#   4. a local binding of the same name shadows the constant inside its own
#      scope only;
#   5. every named failure mode is refused before a backend artifact exists,
#      with the exact diagnostic — `E2S158` for a non-literal initializer and
#      for a name that collides with a function or type, `E2S159` for a
#      duplicate constant, and `E2S160` for `let mut`.
#
# Constants carry no visibility modifier in this slice, so they stay internal
# to their compilation unit and add nothing to KIF.

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CASES="$ROOT/tests/module-constants"
CC=${CC:-cc}
ASSERT_CONTEXT='module constants'
. "$ROOT/tests/assertions/assert.sh"
. "$ROOT/bootstrap/stage2/build.sh"

WORK=$(mktemp -d "${TMPDIR:-/tmp}/kofun-module-constants.XXXXXX")
trap 'rm -rf "$WORK"' 0 1 2 15

fail() {
    printf 'module constants: FAIL: %s\n' "$*" >&2
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
    printf 'module constants: runs: %s\n' "$stem"
}

compile_and_run values
compile_and_run ordering
compile_and_run shadowing

# ------------------------------------------- constants are file-scope C

# `values.kofun` declares four constants, so the emitted C declares four
# file-scope constants and the IR counts the same four.
declared=$(grep -c '^static const int64_t kofun_k_' "$WORK/values.c")
assert_num 'file-scope constant declarations in values.c' "$declared" -eq 4
written=$(grep -c '^let ' "$CASES/values.kofun")
assert_num 'constants written in values.kofun' "$written" -eq 4
assert_grep 'the IR counts the declared constants' -Fq -- \
    'constant-count|4' "$WORK/values.ir"
assert_grep 'the IR records the negative initializer verbatim' -Fq -- \
    'constant|ERROR_LOW|-4095|' "$WORK/values.ir"

# Both Int64 bounds survive the round trip into C rather than being folded or
# truncated on the way.
assert_grep 'the Int64 maximum reaches C intact' -Fq -- \
    'static const int64_t kofun_k_INT_MAX = 9223372036854775807;' \
    "$WORK/values.c"
assert_grep 'the negative Int64 bound reaches C intact' -Fq -- \
    'static const int64_t kofun_k_INT_MIN_PLUS_ONE = -9223372036854775807;' \
    "$WORK/values.c"

# A constant is not a lexical binding, so it is never reached through the
# `k_b` prefix that local bindings and parameters use.
assert_not_grep 'a constant was lowered as a lexical binding' -qE -- \
    'k_b[0-9]*_?(MAX_RETRIES|ERROR_LOW|INT_MAX|INT_MIN_PLUS_ONE)' \
    "$WORK/values.c"

# Declaration order in C follows source order, so a constant is always declared
# before the functions that read it.
awk '
    /^static const int64_t kofun_k_MAX_RETRIES/ { first = NR }
    /kofun_fn_remaining/ { if (!use) use = NR }
    END { if (!first || !use || first > use) exit 1 }
' "$WORK/values.c" ||
    fail 'a constant is declared after the function that reads it'

# ---------------------------------------------------- shadowing is local

# The shadowed function reads its own binding; the constant is unchanged for
# every other reader, which the golden pins as 7, 99, 7.
assert_grep 'the shadowing local is a real binding' -qE -- \
    'int64_t k_b[0-9]+ = INT64_C\(99\);' "$WORK/shadowing.c"
assert_grep 'the unshadowed readers use the file-scope constant' -Fq -- \
    'static const int64_t kofun_k_LIMIT = 7;' "$WORK/shadowing.c"

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
    printf 'module constants: refused as designed: %s (%s)\n' "$stem" "$code"
}

expect_refused non_literal E2S158
expect_refused function_clash E2S158
expect_refused type_clash E2S158
expect_refused duplicate E2S159
expect_refused mutable E2S160

# Each refusal names the constant it is about, not merely its code.
assert_grep 'duplicate names the repeated constant' -Fq -- \
    '`LIMIT`' "$CASES/duplicate.stdout"
assert_grep 'function_clash names the colliding function' -Fq -- \
    '`helper`' "$CASES/function_clash.stdout"
assert_grep 'type_clash names the colliding type' -Fq -- \
    '`Choice`' "$CASES/type_clash.stdout"
assert_grep 'mutable explains that a module constant is immutable' -Fq -- \
    'a top-level `let` is immutable' "$CASES/mutable.stdout"

# ------------------------------------------------ a local `let` is intact

# The statement form is untouched by the declaration form: a function-local
# `let` still binds, so this change is additive rather than a repurposing of
# the keyword.
printf 'fn main() {\n    let value = 41\n    print(value + 1)\n}\n' \
    >"$WORK/local_let.kofun"
"$WORK/kofun-stage2" "$WORK/local_let.kofun" \
    "$WORK/local_let.c" "$WORK/local_let.ir" "$WORK/local_let.tokens" \
    >/dev/null
"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$WORK/local_let.c" -o "$WORK/local_let"
"$WORK/local_let" >"$WORK/local_let.stdout"
printf '42\n' >"$WORK/local_let.expected"
cmp "$WORK/local_let.expected" "$WORK/local_let.stdout" ||
    fail 'a function-local `let` no longer binds'
assert_grep 'the IR reports no module constants for a local-only program' \
    -Fq -- 'constant-count|0' "$WORK/local_let.ir"

printf 'every accepted initializer shape reaches the backend: PASS\n'
printf 'a constant is one file-scope C constant, not a binding: PASS\n'
printf 'constants interleave with records and enums in either order: PASS\n'
printf 'a local binding shadows a constant in its own scope only: PASS\n'
printf 'every named failure mode is refused with its exact code: PASS\n'
