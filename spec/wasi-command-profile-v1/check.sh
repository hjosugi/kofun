#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
HERE="$ROOT/spec/wasi-command-profile-v1"
WORK=${KOFUN_WASI_COMMAND_PROFILE_WORK:-"$ROOT/build/wasi-command-profile"}
TARGET=wasm32-wasi-command1
ASSERT_CONTEXT='WASI command capability profile v1'
. "$ROOT/tests/assertions/assert.sh"

case $WORK in
    */wasi-command-profile|*/wasi-command-profile.*) ;;
    *) assert_fail "work directory must end in wasi-command-profile[.suffix]: $WORK" ;;
esac

command -v node >/dev/null 2>&1 || assert_fail 'node is required'
rm -rf "$WORK"
mkdir -p "$WORK"

node --check "$HERE/contract.mjs"
node --check "$HERE/model.mjs"
node --check "$HERE/check.mjs"
node "$HERE/check.mjs"

node "$HERE/model.mjs" vectors >"$WORK/vectors.first.json"
node "$HERE/model.mjs" vectors >"$WORK/vectors.second.json"
cmp "$WORK/vectors.first.json" "$WORK/vectors.second.json"
cmp "$HERE/vectors/canonical.json" "$WORK/vectors.first.json"
node "$HERE/model.mjs" compare "$HERE/vectors/canonical.json" >"$WORK/compare.stdout"
assert_grep 'the canonical-vector comparison' -Fq 'is canonical' "$WORK/compare.stdout"

# The decision reserves a target; it does not enable one. Until a separate
# backend issue lands, the user-facing command must keep failing atomically.
set +e
"$ROOT/bin/kofun" build "$ROOT/examples/wasm_arithmetic.kofun" \
    --target "$TARGET" -o "$WORK/refused.wasm" \
    >"$WORK/refused.stdout" 2>"$WORK/refused.stderr"
status=$?
set -e
assert_num "the reserved target status" "$status" -eq 2
assert_absent 'the reserved target artifact' "$WORK/refused.wasm"
assert_file_empty 'the reserved target stdout' "$WORK/refused.stdout"
assert_grep 'the reserved target refusal' \
    -Fxq "kofun: unsupported target: $TARGET" "$WORK/refused.stderr"

printf '%s\n' \
    'PASS: canonical profile vectors are deterministic and byte-identical' \
    'PASS: the reference model refusal matrix and Node engine fixture pass' \
    "PASS: $TARGET remains reserved, unsupported, and artifact-free"
