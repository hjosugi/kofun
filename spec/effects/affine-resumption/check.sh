#!/usr/bin/env sh
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
MODEL="$HERE/model.mjs"
FIXTURES="$HERE/fixtures"

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
printf '%s\n' "$runtime" | grep -Fq '"stderr": "EAFR01: affine resumption already consumed\n"'
printf '%s\n' "$runtime" | grep -Fq '"cleanup_runs": 1'
printf '%s\n' "$runtime" | grep -Fq '"ownership_transfers": 1'

test "$count" -eq 17 || {
    printf '%s\n' "FAIL: affine-resumption: expected 17 fixtures, found $count" >&2
    exit 1
}

printf '%s\n' \
    'PASS: affine consume, alternative consume, checked transfer, and explicit drop accepted' \
    'PASS: double use, every escape lane, recursion, loops, multi-shot clone, and ownerless take rejected' \
    'PASS: diagnostics pin capture/use spans and the runtime backstop preserves cleanup and take ownership'
