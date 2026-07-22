#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)
CASES="$ROOT/tests/conformance/modules/shadowing"
CC=${CC:-cc}
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kofun-shadowing.XXXXXX")
trap 'rm -rf "$WORK"' 0 1 2 15

fail() {
    printf '%s\n' "shadowing: $*" >&2
    exit 1
}

if test -n "${KOFUN_STAGE2_COMPILER:-}"; then
    STAGE2=$KOFUN_STAGE2_COMPILER
else
    command -v "$CC" >/dev/null 2>&1 ||
        fail "a C11 compiler is required"
    STAGE2="$WORK/kofun-stage2"
    "$CC" -std=c11 -O2 -Wall -Wextra -Werror \
        "$ROOT/bootstrap/stage2/compiler.c" -o "$STAGE2"
fi

compile_positive() {
    suffix=$1
    source="$CASES/positive.kofun"
    if test "$suffix" = remapped; then
        source="$WORK/remapped/positive.kofun"
    fi
    "$STAGE2" \
        "$source" \
        "$WORK/positive-$suffix.c" \
        "$WORK/positive-$suffix.ir" \
        "$WORK/positive-$suffix.tokens" \
        >"$WORK/compiler-$suffix.stdout" \
        2>"$WORK/compiler-$suffix.stderr"
    test ! -s "$WORK/compiler-$suffix.stderr" ||
        fail "compiler wrote stderr for the positive fixture"
}

compile_positive first
mkdir -p "$WORK/remapped"
cp "$CASES/positive.kofun" "$WORK/remapped/positive.kofun"
compile_positive remapped
cmp "$WORK/positive-first.c" "$WORK/positive-remapped.c" ||
    fail "C output is not deterministic"
cmp "$WORK/positive-first.ir" "$WORK/positive-remapped.ir" ||
    fail "IR output is not deterministic"
cmp "$WORK/positive-first.tokens" "$WORK/positive-remapped.tokens" ||
    fail "token output is not deterministic"

sed -n '/^kofun-scope-hir\/v1$/,$p' "$WORK/positive-first.ir" \
    >"$WORK/positive.scope-hir"
cmp "$CASES/positive.scope-hir" "$WORK/positive.scope-hir" ||
    fail "scope HIR differs from its structural golden"

grep 'int64_t k_b0' "$WORK/positive-first.c" >/dev/null ||
    fail "generated C does not use BindingId storage"
if grep -E 'k_(root|value|signal|reused|level)([^[:alnum:]_]|$)' \
    "$WORK/positive-first.c" >/dev/null; then
    fail "generated C re-resolved a source spelling"
fi

"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$WORK/positive-first.c" -o "$WORK/positive"
"$WORK/positive" >"$WORK/positive.stdout" 2>"$WORK/positive.stderr"
cmp "$CASES/positive.stdout" "$WORK/positive.stdout" ||
    fail "positive runtime output differs"
test ! -s "$WORK/positive.stderr" ||
    fail "positive program wrote stderr"

expect_failure() {
    name=$1
    set +e
    "$STAGE2" \
        "$CASES/$name.kofun" \
        "$WORK/$name.c" \
        "$WORK/$name.ir" \
        "$WORK/$name.tokens" \
        >"$WORK/$name.stdout" 2>"$WORK/$name.stderr"
    status=$?
    set -e
    test "$status" -eq 1 ||
        fail "$name returned $status instead of 1"
    cmp "$CASES/$name.stdout" "$WORK/$name.stdout" ||
        fail "$name diagnostic differs"
    test ! -s "$WORK/$name.stderr" ||
        fail "$name wrote stderr"
    test ! -e "$WORK/$name.c" ||
        fail "$name emitted C after a shadowing error"
}

expect_failure duplicate_parameter
expect_failure duplicate_let
expect_failure duplicate_nested
expect_failure duplicate_match_arm
expect_failure immutable_inner

printf '%s\n' \
    'PASS: ancestor shadowing, sibling reuse, BindingId lowering, and E2S47'
