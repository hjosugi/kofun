# Issue readiness

[`docs/CONTRIBUTING.md`](CONTRIBUTING.md) sends every contributor to issues
labelled `curated` and `ready`. The `ready` label's own description says
*"Definition of Ready is satisfied"*. That definition was written nowhere, so
the label meant whatever the person applying it thought it meant, and nothing
could tell a contributor why one issue was `ready` and the next was not.

This document is that definition. It is descriptive of what the repository's
best issues already do, not an invention: every rule below is taken from an
issue that passed review.

## States

The state is carried twice — as a label, and as the `State:` line in the
issue's `## Metadata` block. **They must agree.** Two copies of one fact with
nothing binding them is the drift this repository gates against everywhere
else, and it had already happened: see *Measured evidence* below.

| State | Label description | Exit criterion |
|---|---|---|
| `needs-detail` | Outcome known; scope or validation needs refinement | the Definition of Ready below is satisfied |
| `needs-decision` | Blocked on an explicit design or product decision | the decision is recorded in the issue or a `spec/` document |
| `ready` | Definition of Ready is satisfied | the work is done and its gate is green |
| `blocked` | — | a named blocker remains open |

`blocked` has no label. An issue whose body says `State: blocked` while
carrying the `ready` label is not ready, whatever the label says. Use the
blocker's issue number in `## Dependencies` and remove the `ready` label.

A `blocked` issue with a nonempty blocker list must be re-refined when every
named blocker has closed. Closure does not prove the blocked work is now
`ready` — a blocker can close as not planned, or expose a missing split — but
it does prove that the recorded dependency list and state no longer explain
why the issue cannot start. Update the issue or record the drift in the debt
ledger; do not leave a closed dependency advertising permanent blockage.

`curated` is orthogonal: it marks an independently refinable working issue, and
a `curated` issue may be in any state.

## Agent claims

An agent takes exclusive implementation ownership by posting one visible,
top-level block in an issue comment. The wrapper and the two load-bearing keys
are exact:

```markdown
### agent-claim:v1
- agent_id: codex-example-123
- status: active
```

The wrapper is the level-three heading `### agent-claim:v1`. `agent_id` and
`status` are unquoted Markdown list keys spelled exactly as above; each must
occur once. Extra list keys may document a baseline, scope, or PR, but the gate
does not read them. HTML comments, fenced examples, the old bare
`agent-claim:v1` line, and the old `- agent:` spelling are not claims. This
keeps the canonical format visible in the rendered issue and leaves historical
comments untouched.

`status` has a closed vocabulary:

| Status | Ownership |
|---|---|
| `active` | live; implementation or integration is in progress |
| `pr-open` | live; a PR exists but has not merged or been abandoned |
| `released` | not live; the agent deliberately stopped or skipped the work |
| `merged` | not live; the implementation merged |

Claims are an append-only event log. A later canonical block with the same
`agent_id` supersedes that agent's earlier status. Two different agents whose
latest statuses are live are an error, as is a live claim on a closed issue.
Post `released` when a blocker or overlap makes you skip the issue; silence is
not a release.

The normal GitHub list request returns open issues only. To make an
open-to-closed transition observable, refresh seeds a bounded follow-up from
the prior committed snapshot: only issue numbers whose latest canonical claim
was live and which disappeared from the open result are re-read by number with
their comments. A closed row stays in the snapshot, without increasing
`open_issues`, until that same agent posts `released` or `merged`.

**`planning` is orthogonal too, and is not a state.** It marks a planning
umbrella or generated catalogue — never directly implementation-ready — and the
umbrella still has a state, because "this is an umbrella" and "this is waiting
on something" are different facts. Eleven of the twelve open `planning` issues
already carry one: [#624](https://github.com/hjosugi/kofun/issues/624), #572,
#543, #298, #289, #32, #31, #30, and #26 are `planning` + `blocked`, #532 is
`planning` + `deferred`, and #276 is `planning` + `needs-decision`.

The twelfth, [#998](https://github.com/hjosugi/kofun/issues/998), wrote `State:
planning` in its `## Metadata` block. Putting an orthogonal marker in the state
slot does not make the issue `planning`; it leaves the issue with no state at
all, which is why nothing could tell whether it was startable. Its `State:` line
now says `blocked`, matching its nine siblings.

## Definition of Ready

An issue is `ready` when a contributor who has not seen the discussion can
start work from the issue alone and know when they are finished.

### 1. It names one bounded artifact

`## Goal` states one deliverable, and `## Scope` names what is deliberately
*out* of it. An issue whose scope is only stated positively grows during
implementation, because nothing says where to stop.

If the artifact cannot be one reviewable change, the issue is not ready — it
needs a child split, and the split is itself the refinement work. Say so in the
issue rather than leaving `size:L` to mean "somebody will figure it out".

### 2. Its current-behavior claim is measured, and stamped

This is the rule that matters most, because it is the one that decays.

`## Current behavior and evidence` must give **the command, its result, and the
commit it was measured on**:

~~~markdown
Current `main` at `59a75a60fd6bbe407f29929555d907abf06cfa0a` has no safe Kofun
`RootAuthority`:

```sh
git grep -nE 'RootAuthority|Environment(Capability|Authority)' -- '*.kofun' 'spec/**'
# expected current result: no matches, exit 1
```
~~~

An unstamped claim — "X is not possible", "Y does not exist" — is true on the
day it is written and silently false later. `main` here moves several times an
hour. Without the stamp nobody can tell whether the premise still holds without
re-deriving it, so nobody does, and the issue sits.

With the stamp, checking is mechanical: re-run the command. If the result
changed, the issue needs re-refinement before anyone starts.

### 3. Its acceptance criteria are checkable

`## Acceptance criteria` is a checkbox list, and each line is something a
reviewer can decide is true or false by running something or reading a named
file. "Diagnostics are good" is not a criterion. "Every rejection is a
registered code in `tests/diagnostics/registry.tsv` with an executable owner"
is.

### 4. It names its gate

`## Validation` is a table of check, command, and expected result, and it names
the gate that will hold the work afterwards — an existing `task` target, or a
new one the issue is expected to add. This repository's rule is that a
capability without a gate is not a capability; the same applies to an issue.

### 5. Nothing open blocks it

`## Dependencies` names blockers by number. If any is open, the state is
`blocked`, not `ready`. "Blocked by: none" is an assertion and should be
written explicitly when true, because its absence is ambiguous.

## Promotion and demotion

**To promote,** re-measure first. Run the commands in `## Current behavior and
evidence` against current `main`. Then either confirm the stamp or update it,
fill the gaps above, and change the label *and* the `State:` line together.

Re-measurement is not a formality. In one afternoon it found that
[#772](https://github.com/hjosugi/kofun/issues/772)'s stated premise was
already false, that
[#848](https://github.com/hjosugi/kofun/issues/848)'s first acceptance
criterion did not hold for a reason unrelated to clocks, and that
[#868](https://github.com/hjosugi/kofun/issues/868)'s "profile split" was four
splits rather than one. None of those were visible from reading the issues.

**To demote,** say what changed. An issue that goes back to `needs-detail`
should gain a comment naming the measurement that stopped holding, so the next
person does not repeat the discovery.

**Anyone may promote a `needs-detail` issue.** Only the decision owner named in
the issue may resolve a `needs-decision` one; promoting it by supplying an
answer they did not give is how a bounded prototype becomes a second, unowned
design.

## What readiness is not

- **Not priority.** `P1` says it matters; `ready` says it can be started.
- **Not size.** A `size:L` issue can be ready if it is genuinely one reviewable
  change. Most are not, which is why they need a split.
- **Not agreement that the work should happen.** That is what `curated` and the
  milestone carry.

## Measured evidence

Taken on `main` at `e2200ef86b9ee537c34847b45970c13bdb0ac4ee`.

| Observation | Result |
|---|---|
| "Definition of Ready" anywhere in the tree | absent — the label references a document that did not exist |
| the state vocabulary in any `.md` | absent — `needs-detail` and friends appear only in issue bodies and label descriptions |
| issues labelled `ready` whose body says `State: blocked` | 2 — [#648](https://github.com/hjosugi/kofun/issues/648) and [#880](https://github.com/hjosugi/kofun/issues/880) |
| issues labelled `needs-detail` whose body says `State: ready` | 0 |

The disagreement runs one way, which is the direction that costs: an issue can
be advertised as startable while its own body names an open blocker.

`docs/LINGUIST_RECOGNITION.md` describes its drafts as "formatted to match this
repository's issue conventions" — conventions that existed as practice and had
never been written down. This document is where they now live.

## What is enforced

`task backlog` reads `artifacts/backlog/issue-state.json` and fails when:

- an issue carries more than one state label;
- a `State:` line names a word that is not a state above;
- the snapshot declares a state vocabulary other than this one;
- a state label and the body's `State:` line disagree;
- a `ready` issue names an open blocker in `## Dependencies`;
- a `blocked` issue names blockers but none of them is still open;
- a `ready` issue carries no evidence stamp;
- a canonical claim uses a status outside the closed vocabulary;
- two agents have a live claim on one open issue;
- a closed issue still has a live claim.

The vocabulary is closed, and the second and third rules are what close it.
Labels cannot go wrong — the extraction keeps only labels drawn from the
vocabulary, so a stray label is invisible rather than wrong — but the `State:`
line is free text. Until those rules landed, a line naming a non-state passed
whenever the issue had no state label: the agreement rule needs both sides, so a
word with nothing to disagree with was never read.

That is not a hypothetical typo. It is what an orthogonal label looks like when
it lands in the state slot, and it costs the issue its state entirely —
[#998](https://github.com/hjosugi/kofun/issues/998) said `State: planning`, and
the gate reported agreement across the whole backlog without having looked at
it. The label is real and the issue was self-consistent, which is exactly why
nothing caught it.

Adding a state is therefore an edit to this document *and* to `STATE_LABELS` in
`tests/backlog/extract.mjs`, in that order. Widening the snapshot alone does not
work: the checker compares the snapshot's declared vocabulary against the
repository's and fails on either a word it adds or one it drops, so a
hand-edited or stale snapshot cannot quietly grant itself a wider one.

The committed snapshot is a **fixture**, not a claim about the backlog right
now. Issues are filed here every few minutes and no commit can keep up, so
demanding that a committed copy match live state would be permanently red and
would teach everyone to ignore it.

`task backlog-refresh` regenerates the snapshot from GitHub, including one
bounded comment request per open issue and bounded transition probes seeded by
prior live claims, runs the same rules against live state, and verifies that
every stamp names a commit reachable from `HEAD`. CI runs it
on `main` but **not** on pull requests: a
`ready` issue somebody else opens mid-review would otherwise turn an unrelated
PR red for a reason its author cannot fix. On `main` the same failure is a true
signal, owned by whoever can act on it.

Refreshing may mean updating `tests/backlog/debt.tsv` in the same change. The
ledger describes reality, and reality moves.

The stamp check is not in `task verify` on purpose. It needs the history, and
`actions/checkout` is shallow by default, so a version of it inside `verify`
would either fail on every shallow clone or pass without looking. A check that
passes because it could not look is the failure this gate exists to remove, so
it refuses outright on a shallow clone instead.

`tests/backlog/debt.tsv` records the cases that already existed when the gate
landed, so it could go green without anyone pretending they were fixed. It
fails in both directions, like `tests/assertions/budget.tsv`: an unlisted
problem is new drift, and a listed row that no longer applies is an improvement
that was not recorded. What it held on the day it landed:

| Kind | Rows | What they are |
|---|---:|---|
| `state-disagreement` | 11 | mostly a bulk relabel to `blocked` that did not update the bodies; which side is right is a decision per issue, not something a gate may guess |
| `closed-blockers` | 1 | [#584](https://github.com/hjosugi/kofun/issues/584) still names completed #583; closure means the dependency record is stale, not that the gate may choose its replacement state |
| `unstamped-ready` | 4 | `ready` before rule 2 existed |
| `unverifiable-stamp` | 1 | [#738](https://github.com/hjosugi/kofun/issues/738) names a commit that is on no branch, so its measurement cannot be re-run |

One rule stays manual: whether an issue's `## Scope`, acceptance criteria, and
`## Validation` actually meet the Definition of Ready. A gate can see that a
stamp is present; it cannot see that a criterion is checkable.
