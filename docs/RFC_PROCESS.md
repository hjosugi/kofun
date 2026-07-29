# RFC process

Kofun records public semantic decisions in a ledger, so that an accepted
proposal is never mistaken for shipped behaviour. The ledger is
[`rfcs/index.json`](../rfcs/index.json); `task rfc-registry` checks it and
`task verify` runs that check.

This process is deliberately small. It is meant to be usable by one maintainer,
and every mandatory field exists because its absence has a specific failure
mode, not because a template asked for it.

## What it is not

Issues remain the work tracker. [`docs/ISSUE_TRIAGE.md`](ISSUE_TRIAGE.md) owns
workflow state, sizing, dependencies, and scheduling. Nothing here replaces
that, and an RFC is not a prerequisite for ordinary work.

Capability claims remain in [`release/claims.json`](../release/claims.json).
The ledger points at claims rather than restating them: a decision may be
recorded as `implemented` only if the capability manifest already records the
claims it names as `implemented` or `checkpoint`. See
[Release evidence](RELEASE_EVIDENCE.md).

An open or closed issue is never evidence of implementation, and the checker
has no way to be told otherwise.

## 1. When an RFC is required

An RFC is required for a change to a **public semantic contract**:

- surface syntax and the grammar;
- type, effect, or ownership semantics;
- diagnostic identity where it forms part of the contract;
- the core library's observable behaviour;
- package, interface, or on-disk formats other tools read.

An RFC is **not** required for internal refactors with no public semantic
effect, performance work that preserves meaning, documentation corrections,
test additions, or anything already governed by an accepted RFC. Those use
ordinary issues.

When it is unclear, open an issue and ask. Guessing wrong in the direction of
an RFC costs two weeks; guessing wrong in the other direction costs a silent
semantic change.

## 2. Review period

A proposal stays open for at least the ledger's `review_period_days`, currently
**14 calendar days**, before it may be decided. The checker enforces this for
every native RFC.

A substantive change during review restarts the period. "Substantive" means the
semantics, the diagnostics, or the compatibility classification moved; fixing a
typo or clarifying prose does not. The shepherd records the restart by moving
`opened_on` forward, which is visible in the diff.

## 3. The shepherd

Every proposal names one shepherd, who is answerable for:

- keeping the proposal moving or closing it;
- recording state transitions in the ledger;
- ensuring the compatibility analysis is a measurement rather than an estimate;
- stating plainly where they have an interest in the outcome.

A shepherd may shepherd their own proposal — with one maintainer, requiring
otherwise would just stop the process working. The disclosure requirement is
what stands in for independence: an interest that is written down can be
weighed by a later reader.

## 4. States, and who records them

| State | Meaning |
|---|---|
| `proposed` | Open for review. |
| `accepted` | The decision is made. Nothing is implemented. |
| `rejected` | Closed with reasons. Kept discoverable. |
| `withdrawn` | Closed by its author. Kept discoverable. |
| `implemented` | The accepted semantics are enabled and gated on the target branch. |
| `superseded` | Replaced by a later decision, which is named. |

The shepherd records transitions in `rfcs/index.json`. The checker refuses a
state that its evidence does not support — most importantly, it refuses any
non-`implemented` decision that carries an implementation record, and any
`implemented` decision that carries none.

## 5. Acceptance

Acceptance answers whether the decision is right, not whether it is convenient
to build. Implementation difficulty belongs in the proposal's drawbacks and its
implementation plan, and is a reason to reject a design only when the cost is
part of the design.

Acceptance creates no schedule. A decision may sit `accepted` indefinitely;
`DD-013` and `DD-018` do exactly that today.

## 6. What `implemented` requires

Four things, all checked:

- **change** — the pull request or issue that landed the behaviour;
- **enablement** — exactly where the behaviour is on: which targets, which
  profile, behind which flag if any;
- **gate** — a command on the target branch that fails if the behaviour
  regresses; and
- **claims** — capability claims in `release/claims.json` that the manifest
  records as `implemented` or `checkpoint`.

The last one is the join that makes the state hard to fake. A decision cannot
be implemented on the strength of a claim the capability manifest does not
evidence, and the manifest has its own gate.

## 7. Compatibility

| Category | Meaning |
|---|---|
| `none` | No user-visible semantic change. |
| `additive` | Only new behaviour; no existing program changes meaning. |
| `breaking` | Previously valid programs change meaning or stop compiling. |
| `conditional` | Breaking only for programs of a stated shape. |

A `conditional` claim must name a command another person can run and the number
it returned. The checker refuses hedged wording — "likely compatible",
"probably", "mostly compatible" — because an estimate in this field reads like
a measurement and is treated as one.

`DD-010` shows the intended shape: a query over every tracked Kofun source, and
the count it returns, rather than a belief about how much code was affected.

## 8. Amendments and supersession

Accepted semantics change through an **amendment**, never through an edit to
the accepted proposal. An amendment is `A01`, `A02`, … within its decision, and
carries:

- the **delta** — what the semantics now say, against what they said before;
- the **original semantics**, preserved verbatim, so the superseded wording
  survives any later edit of the source document;
- a fresh **compatibility analysis**, held to the same standard as the first.

An amendment goes through the ordinary review period.

The amendment must be announced in the document that carries the decision text,
by writing its fully-qualified id — `DD-010/A01` — beside the decision. The
checker enforces this in both directions: an amendment in the ledger that no
document announces fails, and a marker in a document that the ledger does not
record fails. A reader who opens `docs/DESIGN_DECISIONS.md` therefore cannot
read a superseded sentence as current.

**Supersession** is for a decision replaced wholesale rather than adjusted. The
superseded decision names its successor and stays in the ledger.

## 9. Closed decisions stay reachable

`rejected` and `withdrawn` decisions are never deleted, and must record a
rationale. The value of the ledger is largely in the decisions that were not
taken; a reader who cannot find out why an idea was refused will propose it
again.

## 10. Where semantics actually live

The ledger records **that** a decision was made and what evidence bounds it. It
does not hold the semantics.

Normative text lives in `spec/` and `docs/`, stays editable, and is named by
each decision's `normative_spec`. [`docs/DESIGN_DECISIONS.md`](DESIGN_DECISIONS.md)
remains the readable narrative of the decisions and carries amendment markers.
Neither duplicates the other: if the ledger restated semantics, there would be
two editable sources and one of them would go stale.

## Expedited path

A security or correctness emergency may be decided immediately, without the
review period. It is recorded as a native RFC with `opened_on` and
`review_closed_on` set to the day it was decided, which the checker will
refuse — that refusal is the point. The exemption is granted by opening the
ordinary review window retrospectively, within 14 days, and moving the dates to
the window actually served. An emergency decision that never receives its
retrospective review therefore cannot pass the gate, and cannot be quietly
forgotten.

## Adding a decision

1. Copy [`rfcs/TEMPLATE.md`](../rfcs/TEMPLATE.md) to `rfcs/NNNN-<slug>.md`.
2. Open it for review; keep it open for the review period.
3. Add the row to `rfcs/index.json` with `state: proposed`.
4. On a decision, set the state and dates. If `accepted`, stop — do not add an
   implementation record.
5. When the behaviour ships and the capability manifest evidences it, add the
   implementation record and set `implemented`.
6. Run `task rfc-registry`. Each refusal names the decision and the repair.
