#!/usr/bin/env sh
set -eu

# Binds an issue's state label to the `State:` line in its body.
#
# The state was carried twice with nothing holding the two together — the same
# shape as docs/MVP_IMPLEMENTED.md against release/claims.json, and
# examples/README.md against the checks that own each example, both of which
# this repository already gates. It had drifted: #648, #880, and #885 all
# carried `ready` while their bodies named an open blocker.
#
# This reads only artifacts/backlog/issue-state.json, so it needs no network
# and runs inside `task verify`. `task backlog-refresh` regenerates that
# snapshot, and CI proves the committed copy still matches by regenerating and
# diffing it — a stale snapshot cannot quietly satisfy this gate.
#
# One rule from docs/ISSUE_READINESS.md is deliberately absent: that an
# evidence stamp names a commit reachable from `main`. Checking it needs the
# history, and `actions/checkout` is shallow by default, so a gate inside
# `task verify` would either fail on every shallow clone or pass without
# looking. It runs in the refresh lane instead, where the checkout is full.

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
ASSERT_CONTEXT=backlog
. "$ROOT/tests/assertions/assert.sh"

SNAPSHOT="$ROOT/artifacts/backlog/issue-state.json"
DEBT="$ROOT/tests/backlog/debt.tsv"

assert_regular_file 'backlog issue-state snapshot' "$SNAPSHOT"
assert_regular_file 'backlog debt ledger' "$DEBT"

node "$ROOT/tests/backlog/check.mjs" "$SNAPSHOT" "$DEBT"
