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
| `blocked` | — | the named blocker closes |

`blocked` has no label. An issue whose body says `State: blocked` while
carrying the `ready` label is not ready, whatever the label says. Use the
blocker's issue number in `## Dependencies` and remove the `ready` label.

`curated` is orthogonal: it marks an independently refinable working issue, and
a `curated` issue may be in any state.

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

## Not yet enforced

Nothing checks any of this. The natural gate would assert that the label and
the `State:` line agree, that no `ready` issue names an open blocker, and that
every evidence stamp is a real ancestor of `main` — but issue state lives on
GitHub, and `task verify` is hermetic. The workable shape is the one
`artifacts/release-evidence/` already uses: a committed snapshot that a
separate refresh command regenerates, with the gate checking the snapshot is
current. That is [#914](https://github.com/hjosugi/kofun/issues/914); until it
lands, the checks above are manual.
