# RFC-NNNN: <title>

- Shepherd: <name>
- Opened: <YYYY-MM-DD>
- Status: proposed

Copy this file to `rfcs/NNNN-<slug>.md`, fill every section, and add the row to
`rfcs/index.json`. A section that does not apply says why in one line; it is
never deleted, because a missing section reads as an oversight rather than a
decision.

Once this document is accepted it is immutable. Changed semantics arrive as an
amendment recorded in the ledger, not as an edit here.

## Summary

One paragraph. What changes for someone writing Kofun.

## Motivation

The problem, and what it currently costs. Prefer a case that exists in this
repository over a hypothetical one.

## Detailed design

The proposal in enough detail that two people would implement the same thing.
Include the grammar, if syntax changes.

## Semantics

What programs mean under this proposal, stated so it can be moved into
`spec/semantics.md` without rewriting. Name the cases that are deliberately
left undefined.

## Diagnostics

Every new refusal, its stable code, and the message a reader sees. A proposal
that adds a way for a program to be wrong owns the diagnostic for it.

## Ownership and effects

How the proposal interacts with `read`/`edit`/`take`, affine resources, and the
effect discipline. Say "no interaction" only if that is true.

## Alternatives

What else was considered, and why it was not chosen. Include the option of
doing nothing.

## Drawbacks

What gets worse. A proposal with no drawbacks has not been examined.

## Compatibility and migration

Classify the change: `none`, `additive`, `breaking` or `conditional`.

A `conditional` claim must name a command another person can run and the number
it returns. "Likely compatible" is not a compatibility analysis, and the ledger
checker refuses it.

Describe the migration path for any program the change breaks.

## Implementation plan

Which backends and stages, in what order, and what may be enabled separately
from what is accepted. Acceptance of this proposal is not a commitment to a
schedule.

## Validation

The gate that will prove the behaviour on the target branch, and the negative
fixture that proves its boundary. This becomes the ledger's `implementation`
record, and its capability claims in `release/claims.json`.

## Unresolved questions

What is deliberately left open, and what would settle it. An empty section
means every question was answered, which is worth stating.
