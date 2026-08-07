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
# It also reports its own coverage. Every rule here is keyed on the `State:`
# line, so an issue whose body has none was skipped by all of them without
# reducing the count printed at the end: `PASS: 51 State lines name a state`
# was 51 of 71. Measured 2026-08-07, the 20 it never reached included #955,
# whose label says `blocked` while its body says `State: in-progress.` — a
# disagreement one of these rules exists to catch and structurally could not
# see. An unreadable state is now a failure, recorded in debt.tsv as
# `unreadable-state` while it is being fixed, or as `stateless-tracker` for an
# issue that carries no state by design.
#
# The same treatment now covers the blocker rule. `blockedBy()` needs a
# `Blocked by:` line, so a blocker stated in prose yields an empty dependency
# list and the closed-blocker rule skips the issue without reducing its count.
# Measured 2026-08-07, that count was 4 of 28. Among the 24 it never reached
# was #314, whose own body says `Unblock condition: **fulfilled.**` and cites
# the green run while the issue is still labelled `blocked`. A blocked issue
# that names no blocker the gate can read is now a failure, recorded as
# `unnamed-blocker` while it is being fixed, or as `capability-blocker` when
# the thing it waits on is not an issue at all.
#
# The claim rules got the same treatment. Both are quantified over the claims
# that exist, so with none anywhere they passed without checking anything:
# measured 2026-08-07, `PASS: 0 live claims` across all 70 open issues while
# four of them had an open pull request implementing them. Worse, every
# `agent-claim:v1` comment on the tracker used a wrapped or prose shape that
# `claimEvents()` strips — ten such comments on #645 extract to zero events —
# so their authors believed they had published ownership. `in-progress` is the
# one ownership assertion this offline snapshot can check, so an `in-progress`
# issue with no live claim is now a failure, recorded as `unclaimed-progress`
# until its owner posts a canonical claim.
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

# The run above proves the rules pass on a snapshot that is green. It cannot
# tell a rule that holds from a rule that is gone: both are silent. self-test.sh
# runs each rule against a case it must refuse, so an absent rule is red here
# rather than a pass nobody checked.
sh "$ROOT/tests/backlog/self-test.sh"
