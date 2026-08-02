#!/bin/sh
set -eu

# kofun-wasm-host-abi-v1 gate.
#
# Three things are checked, in the order they would mislead a reader if they
# were wrong:
#
#   1. every boundary vector is *recomputed* from the AggregateLayout v1
#      descriptors, so a hand-edited vector is rejected and a layout rule
#      change moves the vectors instead of being absorbed by a stale file;
#   2. a missing import, a wrong-signature import, and a wrong ABI version each
#      fail at instantiation with their own named diagnostic and with zero
#      guest execution; and
#   3. the host-call lifetime rule has a fixture that fails when it is broken.
#
# Nothing here compiles Kofun source and nothing here touches wasm code
# generation. This gate is the contract that lowering will later be measured
# against.

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
HERE="$ROOT/spec/wasm-host-abi-v1"
HOSTABI="$HERE/hostabi.mjs"
SPEC="$ROOT/spec/wasm-host-abi-v1.md"
GOLDEN="$HERE/vectors/boundary.wasm32.json"
TMP_PARENT="$ROOT/build/tmp"
mkdir -p "$TMP_PARENT"
TMP_DIR=$(mktemp -d "$TMP_PARENT/wasm-host-abi.XXXXXX")
trap 'rm -rf "$TMP_DIR"' EXIT HUP INT TERM
ASSERT_CONTEXT='wasm host ABI v1'
. "$ROOT/tests/assertions/assert.sh"

command -v node >/dev/null 2>&1 ||
    {
        printf '%s\n' 'FAIL: wasm host ABI v1: node is required to run the reference host' >&2
        exit 1
    }

node --check "$HERE/contract.mjs"
node --check "$HERE/wasm.mjs"
node --check "$HOSTABI"
node "$HOSTABI" schema > /dev/null
node "$HOSTABI" self-test > /dev/null

# The golden vectors are recomputed rather than merely read: `hostabi.mjs` runs
# `spec/aggregate-layout-v1/layout.mjs` over the pinned wasm32 target and
# derives every offset, stride, address, and object image from the descriptors
# it produces. A layout rule change therefore fails here.
node "$HOSTABI" vectors > "$TMP_DIR/vectors.json"
cmp "$GOLDEN" "$TMP_DIR/vectors.json" ||
    {
        printf '%s\n' \
            "FAIL: wasm host ABI v1: $GOLDEN differs from the recomputed boundary vectors" >&2
        exit 1
    }
node "$HOSTABI" vectors > "$TMP_DIR/vectors.second.json"
cmp "$TMP_DIR/vectors.json" "$TMP_DIR/vectors.second.json" ||
    {
        printf '%s\n' 'FAIL: wasm host ABI v1: the boundary vectors are not deterministic' >&2
        exit 1
    }
node "$HOSTABI" compare "$GOLDEN" > /dev/null

expect_rejected() {
    label=$1
    diagnostic=$2
    shift 2
    if node "$HOSTABI" "$@" > "$TMP_DIR/$label.out" 2> "$TMP_DIR/$label.err"; then
        printf '%s\n' \
            "FAIL: wasm host ABI v1: $label was accepted but must be refused" >&2
        exit 1
    else
        status=$?
    fi
    assert_num "exit status for $label" "$status" -eq 1
    # A refusal writes no report: a partial answer is worse than none, because
    # a consumer cannot tell it apart from a complete one.
    assert_file_empty "the report for $label" "$TMP_DIR/$label.out"
    assert_num "lines in $TMP_DIR/$label.err" "$(wc -l < "$TMP_DIR/$label.err")" -eq 1
    assert_grep "the $label diagnostic" -q "^wasm-host-abi: $diagnostic: " "$TMP_DIR/$label.err"
}

# Editing a vector by hand, without changing the rule that produces it, is
# rejected. This is the property that makes the file above evidence rather than
# a snapshot of whatever someone last typed.
sed 's/"reference_width": "4"/"reference_width": "8"/' "$GOLDEN" \
    > "$TMP_DIR/hand-edited.json"
if cmp -s "$GOLDEN" "$TMP_DIR/hand-edited.json"; then
    printf '%s\n' \
        'FAIL: wasm host ABI v1: the hand-edit fixture changed nothing, so it proves nothing' >&2
    exit 1
fi
expect_rejected hand-edited vector-drift compare "$TMP_DIR/hand-edited.json"

# The vectors must actually depend on the target parameters. A contract that
# computed the same bytes for a 4-byte and an 8-byte reference would pass every
# positive check above while having silently decided reference width — the
# failure AggregateLayout v1 chose option B to avoid.
node "$HOSTABI" derive "$HERE/decoy-target.json" > "$TMP_DIR/decoy.json"
assert_grep "the decoy reference width" -q '"reference_width": "8"' "$TMP_DIR/decoy.json"
if cmp -s "$GOLDEN" "$TMP_DIR/decoy.json"; then
    printf '%s\n' \
        'FAIL: wasm host ABI v1: a wider reference produced identical boundary vectors' >&2
    exit 1
fi

# Instantiation fixtures. Each one differs from the conforming module in
# exactly one respect, so the diagnostic it earns cannot be ambiguous, and each
# report must show that no guest code ran.
cases=0
for case_file in "$HERE"/cases/*.json
do
    name=$(basename "$case_file" .json)
    node "$HOSTABI" case "$case_file" > "$TMP_DIR/case.$name.json"
    assert_grep "guest execution during the $name case" \
        -q '"guest_ran": false' "$TMP_DIR/case.$name.json"
    assert_grep "host calls during the $name case" \
        -q '"host_calls": 0' "$TMP_DIR/case.$name.json"
    cases=$((cases + 1))
done
assert_num "instantiation fixtures" "$cases" -eq 7

assert_grep "the conforming module" -q '"contract": "accepted"' "$TMP_DIR/case.conforming.json"
assert_grep "the missing-import refusal" \
    -q '"diagnostic": "missing-import"' "$TMP_DIR/case.missing-import.json"
assert_grep "the wrong-signature refusal" \
    -q '"diagnostic": "import-signature-mismatch"' "$TMP_DIR/case.wrong-signature-import.json"
assert_grep "the wrong-version refusal in the import namespace" \
    -q '"diagnostic": "abi-version-mismatch"' "$TMP_DIR/case.wrong-abi-version-namespace.json"
assert_grep "the wrong-version refusal in the exported global" \
    -q '"diagnostic": "abi-version-mismatch"' "$TMP_DIR/case.wrong-abi-version-global.json"
assert_grep "the missing-export refusal" \
    -q '"diagnostic": "missing-export"' "$TMP_DIR/case.missing-export.json"
assert_grep "the start-section refusal" \
    -q '"diagnostic": "start-section-forbidden"' "$TMP_DIR/case.start-section.json"

# Distinct means distinct. Reporting two of these failures under one name would
# send a reader to the wrong part of their module.
distinct=$(grep -h '"diagnostic": "' "$TMP_DIR"/case.*.json | sort -u | wc -l | tr -d ' ')
assert_num "distinct instantiation diagnostics" "$distinct" -eq 5

# The engine's own verdict, recorded per fixture. Where an engine cannot help —
# an exported global's value, an absent export — the fixture says so rather
# than implying the engine caught it.
assert_grep "the engine verdict on a missing import" \
    -q '"engine_error": "LinkError"' "$TMP_DIR/case.missing-import.json"
assert_grep "the engine verdict on a wrong signature" \
    -q '"engine_error": "LinkError"' "$TMP_DIR/case.wrong-signature-import.json"
assert_grep "the engine verdict on a start section" \
    -q '"engine": "not-attempted"' "$TMP_DIR/case.start-section.json"

# The boundary itself, executed: Text and List cross in both directions, and an
# empty List aborts with the pinned bounds code instead of reading past its
# last element.
node "$HOSTABI" run > "$TMP_DIR/run.json"
assert_grep "the Text that crossed the boundary" -q '"text": "ok"' "$TMP_DIR/run.json"
assert_grep "the List[Text] that crossed the boundary" -qF -- '"a日本語"' "$TMP_DIR/run.json"
assert_grep "the List[Int] that crossed the boundary" \
    -qF -- '"9223372036854775807"' "$TMP_DIR/run.json"
assert_grep "the bounds abort" -q '"code": "1"' "$TMP_DIR/run.json"

# The host-call lifetime rule. A host that copies during the call keeps what it
# was lent; a host that retains the pointer reads bytes the guest reused after
# the call returned, and that is a failure, not a warning.
node "$HOSTABI" lifetime conforming > "$TMP_DIR/lifetime.json"
assert_grep "the copied payload" -q '"held_after_return": "ok"' "$TMP_DIR/lifetime.json"
expect_rejected retained-pointer retained-guest-pointer lifetime retained

# The document must enumerate every name the contract pins. A contract stated
# in code and summarized differently in prose is two contracts.
node "$HOSTABI" names > "$TMP_DIR/names.txt"
names=0
while IFS= read -r symbol
do
    assert_grep "the $symbol entry in $SPEC" -qF -- "$symbol" "$SPEC"
    names=$((names + 1))
done < "$TMP_DIR/names.txt"
assert_num "pinned names" "$names" -eq 17

# The normative rules the vectors cannot express on their own.
assert_grep "SPEC" -q 'kofun-wasm-host-abi-v1' "$SPEC"
assert_grep "SPEC" -q 'no capacity field' "$SPEC"
assert_grep "SPEC" -q 'UTF-8' "$SPEC"
assert_grep "SPEC" -q 'may not retain' "$SPEC"
assert_grep "SPEC" -q 'recomputed' "$SPEC"
assert_grep "SPEC" -q 'not a compatibility requirement' "$SPEC"

printf '%s\n' \
    'PASS: the boundary vectors are recomputed from the wasm32 AggregateLayout descriptors and reject a hand edit' \
    'PASS: Text and List cross the boundary under a real engine, with UTF-8-only payloads and no capacity field' \
    'PASS: a missing import, a wrong signature, and a wrong ABI version each refuse at instantiation with no guest execution' \
    'PASS: a host that retains a guest pointer past the call is refused by its own fixture'
