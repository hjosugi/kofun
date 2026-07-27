# Kofun RFCs

This directory is the durable record of public semantic decisions. The process
is [`docs/RFC_PROCESS.md`](../docs/RFC_PROCESS.md).

| File | Role |
|---|---|
| `index.json` | The authoritative ledger. Edit this. |
| `TEMPLATE.md` | The proposal template. |
| `NNNN-<slug>.md` | An accepted proposal. Immutable once accepted. |

`make rfc-registry` checks the ledger, and `make verify` runs it.

## What this is not

It is not the work tracker. Issues own work state, scheduling and evidence, and
[`docs/ISSUE_TRIAGE.md`](../docs/ISSUE_TRIAGE.md) governs them. The ledger owns
one thing issues cannot: the durable statement of what was decided about the
language, separated from whether anything was built.

It is not a capability claim either. `release/claims.json` owns what the
compiler can currently do, and the ledger points at it rather than restating
it — a decision may only be recorded as `implemented` if the capability
manifest already evidences the claims it names.

## The distinction the ledger exists to keep

`accepted` means the decision is made. It does not mean an implementation
exists, and the checker refuses any accepted decision that carries an
implementation record.

`implemented` means the behaviour is enabled on the target branch, named by a
gate, and backed by capability claims that the manifest records as implemented
or checkpoint.

Between those, an amendment is how accepted semantics change. It preserves the
original wording, states the delta, repeats the compatibility analysis, and is
announced in the document that carries the decision text — so a reader of that
document sees the semantics moved rather than reading a superseded sentence as
current.

## Current contents

The ledger starts with four decisions migrated from
[`docs/DESIGN_DECISIONS.md`](../docs/DESIGN_DECISIONS.md), chosen to exercise
the states that matter:

| Decision | State | Why it is here |
|---|---|---|
| `DD-010` | implemented | Carries a real amendment: `/` on `Int` became a refusal. |
| `DD-012` | implemented | A bounded capability, joined to three manifest claims. |
| `DD-013` | accepted | Accepted design with nothing built, and no implementation record. |
| `DD-018` | accepted | An accepted target artifact that nothing emits yet. |

Migrated decisions predate this process, so they carry `recorded_on` — the date
their text entered the repository — rather than a review window. Inventing an
`opened_on` for them would be exactly the fabricated evidence this ledger
exists to prevent, and the checker refuses it.

No decision is currently `superseded`; that branch of the checker is proved by
a mutation fixture rather than by a live row.
