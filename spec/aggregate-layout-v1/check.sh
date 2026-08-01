#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
HERE="$ROOT/spec/aggregate-layout-v1"
LAYOUT="$HERE/layout.mjs"
SPEC="$ROOT/spec/aggregate-layout-v1.md"
TMP_PARENT="$ROOT/build/tmp"
mkdir -p "$TMP_PARENT"
TMP_DIR=$(mktemp -d "$TMP_PARENT/aggregate-layout.XXXXXX")
trap 'rm -rf "$TMP_DIR"' EXIT HUP INT TERM
ASSERT_CONTEXT='aggregate layout v1'
. "$ROOT/tests/assertions/assert.sh"

node --check "$LAYOUT"
node "$LAYOUT" schema > /dev/null
node "$LAYOUT" self-test-limits > /dev/null

# The golden descriptors are recomputed rather than merely read, so a change
# to any layout rule fails here instead of being absorbed by a stale file.
for target in x86_64-linux wasm32; do
    node "$LAYOUT" describe \
        "$HERE/targets/$target.json" "$HERE/vectors/core.json" \
        > "$TMP_DIR/$target.json"
    cmp "$HERE/examples/core.$target.json" "$TMP_DIR/$target.json" ||
        {
            printf '%s\n' "FAIL: aggregate-layout: $target descriptors differ from the checked-in vectors" >&2
            exit 1
        }
    node "$LAYOUT" describe \
        "$HERE/targets/$target.json" "$HERE/vectors/core.json" \
        > "$TMP_DIR/$target.second.json"
    cmp "$TMP_DIR/$target.json" "$TMP_DIR/$target.second.json" ||
        {
            printf '%s\n' "FAIL: aggregate-layout: $target descriptors are not deterministic" >&2
            exit 1
        }
done

# The two targets must actually disagree. A contract that computed identical
# bytes for a 4-byte and an 8-byte reference would pass every positive check
# above while having silently decided pointer width, which is exactly the
# failure option A was rejected for.
if cmp -s "$HERE/examples/core.x86_64-linux.json" "$HERE/examples/core.wasm32.json"; then
    printf '%s\n' "FAIL: aggregate-layout: the two targets produced identical descriptors" >&2
    exit 1
fi

expect_rejected() {
    label=$1
    target=$2
    document=$3
    if node "$LAYOUT" describe "$target" "$document" \
        > "$TMP_DIR/$label.out" 2> "$TMP_DIR/$label.err"; then
        printf '%s\n' "FAIL: aggregate-layout: $label was accepted but must be rejected" >&2
        exit 1
    else
        status=$?
    fi
    test "$status" -eq 1 ||
        {
            printf '%s\n' "FAIL: aggregate-layout: $label exited $status, expected 1" >&2
            exit 1
        }
    # A rejection writes no descriptor: a partial layout is worse than none,
    # because a consumer cannot tell it apart from a complete one.
    test ! -s "$TMP_DIR/$label.out" ||
        {
            printf '%s\n' "FAIL: aggregate-layout: $label wrote a descriptor while failing" >&2
            exit 1
        }
    assert_num "lines in $TMP_DIR/$label.err" \
        "$(wc -l < "$TMP_DIR/$label.err")" -eq 1
    grep -q '^aggregate-layout: ' "$TMP_DIR/$label.err"
}

expect_rejected overflow-elements \
    "$HERE/targets/x86_64-linux.json" "$HERE/invalid/overflow-elements.json"
expect_rejected recursive \
    "$HERE/targets/x86_64-linux.json" "$HERE/invalid/recursive.json"
expect_rejected size-overflow \
    "$HERE/invalid/tiny-target.json" "$HERE/vectors/core.json"
expect_rejected big-endian \
    "$HERE/invalid/big-endian-target.json" "$HERE/vectors/core.json"

# The normative rules the golden vectors cannot express on their own.
assert_grep "SPEC" -q 'option B' "$SPEC"
assert_grep "SPEC" -q 'niche optimization' "$SPEC"
assert_grep "SPEC" -q 'declaration order' "$SPEC"
assert_grep "SPEC" -q 'decimal string' "$SPEC"
assert_grep "SPEC" -q 'not a compatibility requirement' "$SPEC"

printf '%s\n' \
    'PASS: AggregateLayout v1 descriptors are deterministic and target-parameterized' \
    'PASS: overflow, recursive layout, and unsupported targets are refused without a descriptor'
