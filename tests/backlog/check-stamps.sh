#!/usr/bin/env sh
set -eu

# Every evidence stamp in the snapshot names a commit reachable from HEAD.
#
# This is NOT part of `task backlog`, and the split is deliberate.
# `actions/checkout` is shallow by default, so a history check inside
# `task verify` would either fail on every shallow clone or quietly pass
# without looking — and a check that passes because it could not look is the
# failure mode this gate exists to remove. It runs in the refresh lane, where
# the checkout is full, and refuses outright on a shallow clone.

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
ASSERT_CONTEXT=backlog-stamps
. "$ROOT/tests/assertions/assert.sh"

SNAPSHOT="$ROOT/artifacts/backlog/issue-state.json"
DEBT="$ROOT/tests/backlog/debt.tsv"

assert_regular_file 'backlog issue-state snapshot' "$SNAPSHOT"
assert_regular_file 'backlog debt ledger' "$DEBT"

cd "$ROOT"
node tests/backlog/check-stamps.mjs "$SNAPSHOT" "$DEBT"
