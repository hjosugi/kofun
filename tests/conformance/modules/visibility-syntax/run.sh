#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)
CASES="$ROOT/tests/conformance/modules/visibility-syntax"
CC=${CC:-cc}

command -v "$CC" >/dev/null 2>&1 || {
    printf '%s\n' "visibility syntax: a C11 compiler is required" >&2
    exit 1
}

WORK=$(mktemp -d "${TMPDIR:-/tmp}/kofun-visibility-syntax.XXXXXX")
trap 'rm -rf "$WORK"' 0 1 2 15

fail() {
    printf '%s\n' "FAIL: $*" >&2
    exit 1
}

assert_visibility() {
    ir=$1
    name=$2
    level=$3
    origin=$4
    awk -F '|' -v name="$name" -v level="$level" -v origin="$origin" '
        $1 == "function" && $2 == name && $6 == level && $7 == origin {
            if ($12 != "file:0" || $13 !~ /^symbol:[0-9]+$/) exit 2
            if (origin == "implicit" &&
                ($8 != -1 || $9 != -1 || $10 != $4)) exit 3
            if (origin == "explicit" &&
                !($8 >= 0 && $9 > $8 && $10 == $8 && $10 < $4)) exit 4
            found = 1
        }
        END { if (!found) exit 1 }
    ' "$ir" || fail "$name: incorrect $level/$origin visibility IR"
}

compile_positive() {
    stem=$1
    name=$2
    level=$3
    origin=$4
    source="$CASES/$stem.kofun"

    "$WORK/kofun-stage2" \
        "$source" "$WORK/$stem.c" "$WORK/$stem.ir" "$WORK/$stem.tokens" \
        >/dev/null
    "$WORK/kofun-stage2" \
        "$source" "$WORK/$stem.second.c" "$WORK/$stem.second.ir" \
        "$WORK/$stem.second.tokens" >/dev/null
    cmp "$WORK/$stem.c" "$WORK/$stem.second.c" ||
        fail "$stem: C lowering is nondeterministic"
    cmp "$WORK/$stem.ir" "$WORK/$stem.second.ir" ||
        fail "$stem: structural IR is nondeterministic"
    cmp "$WORK/$stem.tokens" "$WORK/$stem.second.tokens" ||
        fail "$stem: token tape is nondeterministic"

    assert_visibility "$WORK/$stem.ir" "$name" "$level" "$origin"
    grep '^kofun-token-tape/v1$' "$WORK/$stem.tokens" >/dev/null ||
        fail "$stem: missing token tape header"
    "$CC" -std=c11 -O2 -Wall -Wextra -Werror \
        "$WORK/$stem.c" -o "$WORK/$stem"
    output=$("$WORK/$stem")
    test "$output" = 42 || fail "$stem: runtime output is '$output', not 42"
    printf '%s\n' "PASS visibility syntax: $stem"
}

expect_failure() {
    stem=$1
    code=$2
    set +e
    "$WORK/kofun-stage2" \
        "$CASES/$stem.kofun" \
        "$WORK/$stem.c" "$WORK/$stem.ir" "$WORK/$stem.tokens" \
        >"$WORK/$stem.actual" 2>"$WORK/$stem.internal.stderr"
    status=$?
    set -e
    test "$status" -eq 1 || fail "$stem: exited $status instead of 1"
    test ! -s "$WORK/$stem.internal.stderr" ||
        fail "$stem: wrote internal stderr"
    test ! -e "$WORK/$stem.c" || fail "$stem: emitted rejected C"
    test ! -e "$WORK/$stem.ir" || fail "$stem: emitted rejected IR"
    test ! -e "$WORK/$stem.tokens" || fail "$stem: emitted rejected tokens"
    cmp "$CASES/$stem.stderr" "$WORK/$stem.actual" ||
        fail "$stem: diagnostic differs"
    grep -F "error[$code]:" "$WORK/$stem.actual" >/dev/null ||
        fail "$stem: expected $code"
    printf '%s\n' "PASS visibility diagnostic: $stem"
}

"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$ROOT/bootstrap/stage2/compiler.c" -o "$WORK/kofun-stage2"

compile_positive implicit_private_fn helper private implicit
compile_positive explicit_private_fn helper private explicit
compile_positive internal_fn helper internal explicit
compile_positive public_fn helper public explicit
compile_positive mixed_forward_calls public_answer public explicit
compile_positive contextual_modifier_identifiers pub private implicit

assert_visibility "$WORK/mixed_forward_calls.ir" internal_answer internal explicit
assert_visibility "$WORK/mixed_forward_calls.ir" \
    explicit_private_answer private explicit
assert_visibility "$WORK/mixed_forward_calls.ir" \
    implicit_private_answer private implicit

expect_failure duplicate_private E2S33
expect_failure conflicting_pub_private E2S33
expect_failure modifier_without_declaration E2S33
expect_failure modifier_on_local_statement E2S33
expect_failure misspelled_public E2S33
expect_failure public_alias E2S34
expect_failure protected_alias E2S34
expect_failure restricted_pub_deferred E2S34
expect_failure rust_pub_crate_alias E2S34
expect_failure rust_pub_super_alias E2S34
expect_failure rust_pub_in_alias E2S34
expect_failure malformed_pub_parentheses E2S34

printf '%s\n' "visibility syntax: 6 positive and 12 negative cases passed"
