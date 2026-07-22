#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
VALIDATOR="$ROOT/spec/typed-sidecar/validate.mjs"
GENERATOR="$ROOT/spec/typed-sidecar/make-invalid.mjs"
EXAMPLES="$ROOT/spec/typed-sidecar/examples"
TMP_PARENT="$ROOT/build/tmp"
mkdir -p "$TMP_PARENT"
TMP_DIR=$(mktemp -d "$TMP_PARENT/typed-sidecar.XXXXXX")
trap 'rm -rf "$TMP_DIR"' EXIT HUP INT TERM

node --check "$VALIDATOR"
node --check "$GENERATOR"
node "$VALIDATOR" schema
node "$VALIDATOR" self-test-limits

for example in complete partial cancelled; do
    node "$VALIDATOR" validate "$EXAMPLES/$example.json"
done

node "$GENERATOR" remapped "$EXAMPLES/complete.json" "$TMP_DIR/complete-remapped.json"
node "$VALIDATOR" validate "$TMP_DIR/complete-remapped.json"
node "$VALIDATOR" project "$EXAMPLES/complete.json" > "$TMP_DIR/complete.projected.json"
node "$VALIDATOR" project "$TMP_DIR/complete-remapped.json" > "$TMP_DIR/remapped.projected.json"
cmp "$TMP_DIR/complete.projected.json" "$TMP_DIR/remapped.projected.json"

node "$VALIDATOR" replace \
    "$EXAMPLES/complete.json" "$EXAMPLES/partial.json" \
    6666666666666666666666666666666666666666666666666666666666666666
node "$VALIDATOR" replace \
    "$EXAMPLES/partial.json" "$EXAMPLES/cancelled.json" \
    7777777777777777777777777777777777777777777777777777777777777777

expect_denied() {
    label=$1
    shift
    if "$@" > "$TMP_DIR/$label.out" 2> "$TMP_DIR/$label.err"; then
        printf '%s\n' "FAIL: $label unexpectedly succeeded" >&2
        exit 1
    else
        status=$?
    fi
    test "$status" -eq 1
    test ! -s "$TMP_DIR/$label.out"
    test "$(wc -l < "$TMP_DIR/$label.err")" -eq 1
    grep -q '^typed-sidecar: ' "$TMP_DIR/$label.err"
}

expect_denied stale-sequence node "$VALIDATOR" replace \
    "$EXAMPLES/partial.json" "$EXAMPLES/complete.json" \
    aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
expect_denied equal-sequence node "$VALIDATOR" replace \
    "$EXAMPLES/complete.json" "$TMP_DIR/complete-remapped.json" \
    aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
expect_denied wrong-current-digest node "$VALIDATOR" replace \
    "$EXAMPLES/complete.json" "$EXAMPLES/partial.json" \
    aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa

expect_invalid() {
    label=$1
    fixture=$2
    if node "$VALIDATOR" validate "$fixture" > "$TMP_DIR/$label.out" 2> "$TMP_DIR/$label.err"; then
        printf '%s\n' "FAIL: invalid case $label unexpectedly succeeded" >&2
        exit 1
    else
        status=$?
    fi
    test "$status" -eq 1
    test ! -s "$TMP_DIR/$label.out"
    test "$(wc -l < "$TMP_DIR/$label.err")" -eq 1
    grep -q '^typed-sidecar: ' "$TMP_DIR/$label.err"
}

expect_invalid duplicate-key "$ROOT/spec/typed-sidecar/invalid/duplicate-key.json"

for case in authoritative bad-pair absolute-path span-overflow non-validated-complete noncanonical; do
    node "$GENERATOR" "$case" "$EXAMPLES/complete.json" "$TMP_DIR/$case.json"
    expect_invalid "$case" "$TMP_DIR/$case.json"
done

for case in duplicate-id dangling-diagnostic validated-dependency hidden-leak node-order; do
    node "$GENERATOR" "$case" "$EXAMPLES/partial.json" "$TMP_DIR/$case.json"
    expect_invalid "$case" "$TMP_DIR/$case.json"
done

grep -q 'must be the JSON boolean `false`' "$ROOT/spec/tooling/typed-sidecar.md"
grep -q 'new sequence is greater than the stored sequence' "$ROOT/spec/tooling/typed-sidecar.md"
grep -q '16 MiB canonical JSON bytes' "$ROOT/spec/tooling/typed-sidecar.md"
grep -q 'private name, path, span, or ID' "$ROOT/spec/tooling/typed-sidecar.md"

printf '%s\n' 'PASS: typed semantic sidecar v1 specification'
