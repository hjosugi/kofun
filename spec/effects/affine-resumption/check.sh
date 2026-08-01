#!/usr/bin/env sh
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
MODEL="$HERE/model.mjs"
FIXTURES="$HERE/fixtures"
ASSERT_CONTEXT="affine-resumption"
. "$ROOT/tests/assertions/assert.sh"

command -v node >/dev/null 2>&1 || {
    printf '%s\n' 'FAIL: affine-resumption: node is required' >&2
    exit 1
}

node --check "$MODEL"
count=0
for fixture in "$FIXTURES"/positive/*.json "$FIXTURES"/negative/*.json
do
    node "$MODEL" check "$fixture" >/dev/null
    count=$((count + 1))
done

runtime=$(node "$MODEL" runtime)
runtime_file=$(mktemp "${TMPDIR:-/tmp}/kofun-affine-resumption.XXXXXX")
trap 'rm -f "$runtime_file"' EXIT HUP INT TERM
printf '%s\n' "$runtime" >"$runtime_file"
assert_grep "stable runtime diagnostic" -Fq '"stderr": "EAFR01: affine resumption already consumed\n"' "$runtime_file"
assert_grep "single cleanup" -Fq '"cleanup_runs": 1' "$runtime_file"
assert_grep "single ownership transfer" -Fq '"ownership_transfers": 1' "$runtime_file"

test "$count" -eq 17 || {
    printf '%s\n' "FAIL: affine-resumption: expected 17 fixtures, found $count" >&2
    exit 1
}

printf '%s\n' \
    'PASS: affine consume, alternative consume, checked transfer, and explicit drop accepted' \
    'PASS: double use, every escape lane, recursion, loops, multi-shot clone, and ownerless take rejected' \
    'PASS: diagnostics pin capture/use spans and the runtime backstop preserves cleanup and take ownership'
