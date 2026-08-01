#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
MODEL="$ROOT/spec/concurrency/schedule-trace/model.mjs"
CORPUS="$ROOT/tests/concurrency/schedule-replay"
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kofun-schedule-trace.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM

command -v node >/dev/null 2>&1 || {
    printf '%s\n' 'FAIL: schedule-trace: node is required' >&2
    exit 1
}

node --check "$MODEL"
scenarios=0
for program in "$CORPUS"/programs/*.json
do
    name=$(basename "$program" .json)
    node "$MODEL" run fifo "$program" >"$WORK/$name.fifo.1.json"
    node "$MODEL" run fifo "$program" >"$WORK/$name.fifo.2.json"
    cmp "$WORK/$name.fifo.1.json" "$WORK/$name.fifo.2.json"
    node "$MODEL" replay "$program" "$WORK/$name.fifo.1.json" >/dev/null

    node "$MODEL" run seeded "$program" 735736 >"$WORK/$name.seeded.1.json"
    node "$MODEL" run seeded "$program" 735736 >"$WORK/$name.seeded.2.json"
    cmp "$WORK/$name.seeded.1.json" "$WORK/$name.seeded.2.json"
    node "$MODEL" replay "$program" "$WORK/$name.seeded.1.json" >/dev/null
    scenarios=$((scenarios + 1))
done

test "$scenarios" -eq 8 || {
    printf '%s\n' "FAIL: schedule-trace: expected 8 scenarios, found $scenarios" >&2
    exit 1
}

node "$MODEL" replay "$CORPUS/programs/valid-replay.json" \
    "$CORPUS/valid/fifo-witness.json" >/dev/null

node "$MODEL" exhaustive "$CORPUS/programs/ownership-conflict.json" \
    >"$WORK/exhaustive.json"
grep -Fq '"failure_witness"' "$WORK/exhaustive.json"
grep -Fq '"ownership_event"' "$WORK/exhaustive.json"
node "$MODEL" replay-witness "$WORK/exhaustive.json" >/dev/null

node "$MODEL" exhaustive "$CORPUS/programs/budget-bound.json" \
    >"$WORK/budget.json"
grep -Fq '"decisions"' "$WORK/budget.json"

rejections=0
for rejection in "$CORPUS"/rejections/*.json
do
    node "$MODEL" rejection-test "$rejection" >/dev/null
    rejections=$((rejections + 1))
done
test "$rejections" -eq 10 || {
    printf '%s\n' "FAIL: schedule-trace: expected 10 rejection fixtures, found $rejections" >&2
    exit 1
}

printf '%s\n' \
    'PASS: FIFO, seeded, strict replay, and exhaustive policies share one bounded task model' \
    'PASS: cancellation, failure, nesting, joins, channels, and ownership conflicts are deterministic' \
    'PASS: replay rejects every drift class and witnesses reproduce exact portable observations'
