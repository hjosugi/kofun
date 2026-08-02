#!/usr/bin/env sh
set -eu

# What tests/backlog/check.mjs refuses.
#
# check.sh runs the checker against the committed snapshot, which is green — so
# it proves the rules pass on good input and nothing about what they catch. A
# rule that stopped firing would keep that gate green forever, which is the
# shape of failure this whole subsystem exists to remove.
#
# #998 is why this exists. It carried `State: planning`, a word in no
# vocabulary, and the checker reported agreement across the entire backlog
# without reading it: the agreement rule compares a label against a line, and an
# issue with no label had nothing to compare. The rule was not weak, it was
# absent, and no fixture would have noticed.
#
# Each case is a snapshot, a debt ledger, the exact bytes the checker must write
# to stderr, and the status it must exit with. Exact bytes rather than a match,
# because a diagnostic that stops naming the issue and both values is a
# regression the way a wrong exit status is: "the backlog is inconsistent" is
# not something anyone can act on.

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
ASSERT_CONTEXT=backlog-self-test
. "$ROOT/tests/assertions/assert.sh"

CASES="$ROOT/tests/backlog/self-test-cases"
assert_dir 'backlog checker rule cases' "$CASES"

WORK=${TMPDIR:-/tmp}/kofun-backlog-rules.$$
trap 'rm -rf "$WORK"' EXIT HUP INT TERM
mkdir -p "$WORK"

# An empty case set would satisfy the loop below without running the checker
# once, which is the vacuous pass this file is here to prevent.
count=0
for case_dir in "$CASES"/*/; do
    test -d "$case_dir" || continue
    name=$(basename "$case_dir")

    for required in snapshot.json debt.tsv expected status; do
        assert_regular_file "case $name $required" "$case_dir$required"
    done

    expected_status=$(cat "$case_dir/status")
    set +e
    node "$ROOT/tests/backlog/check.mjs" \
        "$case_dir/snapshot.json" "$case_dir/debt.tsv" \
        >"$WORK/stdout" 2>"$WORK/stderr"
    actual_status=$?
    set -e

    if test "$actual_status" -ne "$expected_status"; then
        cat "$WORK/stderr" >&2
        assert_fail "$name: the checker exited $actual_status, not $expected_status"
    fi
    if ! cmp -s "$case_dir/expected" "$WORK/stderr"; then
        diff -u "$case_dir/expected" "$WORK/stderr" >&2 || :
        assert_fail "$name: the checker wrote different diagnostics than the case pins"
    fi

    count=$((count + 1))
done

test "$count" -gt 0 ||
    assert_fail "no rule cases ran; the gate would pass without checking anything"

printf 'PASS: %s\n' \
    "$count backlog checker rules refuse their case and pin its diagnostic"
