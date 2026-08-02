# ReuseCandidate v1

Status: normative optimization-record contract. This contract defines evidence
that a future compiler pass may carry. It does not add that pass, allocate a
constructor, mutate storage, or change runtime representation.

The schema name is `kofun.reuse-candidate/v1`. A document is one bounded
candidate record. Producers and consumers MUST validate the complete record;
there is no partial or best-effort form.

## Record

Every record has exactly these fields:

- `schema`: exactly `kofun.reuse-candidate/v1`;
- `candidate_id`: a stable, path-independent identifier for the candidate;
- `file_id`: a logical file identity, never an absolute checkout path;
- `matched_source`: the matched constructor's `type_id`, constructor name, and
  half-open UTF-8 byte `span`;
- `constructed_target`: the constructor that would replace it, with the same
  identity and span fields;
- `layout_evidence`: the AggregateLayout v1 target plus copied source and
  target facts;
- `uniqueness_evidence`: proof state, proof provenance, and a stable evidence
  identity when evidence exists;
- `eligibility`: the three state flags and the single disqualifying reason;
- `remark`: the exact unstable text selected below.

Unknown fields are rejected. Numbers from AggregateLayout remain canonical
unsigned decimal strings. Spans and field counts are JSON safe integers.

## Eligibility is one state, never a preference list

The three flags are mutually exclusive and exhaustive. Exactly one MUST be
`true`:

- `ordinary_allocation`: this pair takes the allocation path. It is either
  structurally incompatible or has a hard safety/profile disqualifier.
- `reuse_candidate`: the pair remains visible for analysis or a conditional
  fast path, but no static reuse guarantee exists. The value-semantics fallback
  is ordinary allocation.
- `reuse_statically_guaranteed`: the producer has proved compatible layout and
  constructor-storage uniqueness. A backend MUST implement the guarantee or
  reject the program; it MUST NOT silently weaken this state to allocation.

A statically guaranteed row has `reason: null`. Every other row carries exactly
one closed reason. This single-reason rule is diagnostic provenance, not a
claim that other hazards were absent. Producers report structural conflicts in
this order: size, alignment, then field/scanning layout.

Disabling reuse may change allocation count, peak memory, wall time, retain or
release traffic, and code size only. It MUST NOT change returned values,
observable aliases, errors, panics, effects, cleanup order, or any other value
semantics. No valid program may depend on this optimization firing.

## AggregateLayout v1 evidence

`layout_evidence.contract` is exactly `kofun.aggregate-layout/v1`, and
`target_data_layout` names one committed AggregateLayout target. Each copied
side contains:

- `layout_id`, `size`, and `align` from its `TypeLayout`;
- `field_count`, which is zero for a payload-free constructor and one for a
  constructor with the v1 single payload;
- `scanning_shape.pointers` and `scanning_shape.drop` from the complete tagged
  aggregate layout.

The pointer offsets are the conservative union across constructors defined by
[`aggregate-layout-v1.md`](aggregate-layout-v1.md), not a payload-local bitmap.
The validator joins `layout_id` and constructor name to the checked-in
`spec/aggregate-layout-v1/examples/core.<target>.json` descriptor and rejects
any copied fact that differs.

Reusable layout means size, alignment, field count, pointer offsets, and drop
shape all agree. `reuse_candidate` and `reuse_statically_guaranteed` require
reusable layout. An `ordinary_allocation` row with a structural difference MUST
use the first applicable structural reason.

## Uniqueness evidence is about constructor storage

`proof` is `proved`, `conditional`, or `unproved`. `provenance` is one of:

- `compile-time-ownership-escape-last-use`;
- `owned-or-rc-specialized-representation`;
- `runtime-uniqueness-check`;
- `unproved`.

The first two provenances require `proof: proved`. A runtime check is
`conditional`: it can keep a row as `reuse_candidate`, but cannot justify
`reuse_statically_guaranteed`. `unproved` requires `proof: unproved` and a null
`evidence_id`. Every other provenance requires a stable, non-empty
`evidence_id`.

`compiler.ensure_move(value)` is NOT constructor-storage uniqueness evidence.
It proves a narrow last use of one managed local binding. It does not prove
that the constructor storage named by `matched_source` has no alias. A future
producer may cite a stronger analysis that includes that fact, but MUST NOT
upgrade the existing assertion by name alone.

## Closed disqualifying reasons

The reason vocabulary is closed in v1:

| Reason | Meaning |
| --- | --- |
| `incompatible-size` | Source and target allocation sizes differ. |
| `incompatible-alignment` | Source and target alignments differ. |
| `incompatible-layout` | Field count or scanning/drop shape differs. |
| `pinned` | Source storage cannot move or be repurposed. |
| `ffi-exposed` | Foreign code may retain or observe the storage. |
| `weakly-referenced` | A weak reference can observe storage identity/liveness. |
| `borrowed-view` | A live borrowed view can observe the old storage. |
| `closure-capture` | A closure captures the old storage. |
| `possible-alias` | Constructor-storage uniqueness is not proved. |
| `owned-field-hazard` | Transfer/drop safety for an owned field is not proved. |
| `backend-limitation` | The selected backend cannot implement the candidate. |

#570/#907 do not yet supply the owned-field classification. The vocabulary
records that boundary; it does not resolve it.

## Optimization remarks (unstable)

Remark wording is intentionally unstable compiler tooling surface, but v1 has
one exact spelling so future producers do not invent incompatible messages.
The validator requires the matching text:

| State/reason | Remark |
| --- | --- |
| guaranteed | `reuse guaranteed: compatible constructor storage and proved uniqueness` |
| `incompatible-size` | `reuse refused: source and target allocation sizes differ` |
| `incompatible-alignment` | `reuse refused: source and target alignments differ` |
| `incompatible-layout` | `reuse refused: source and target field or scanning layouts differ` |
| `pinned` | `reuse refused: source storage is pinned` |
| `ffi-exposed` | `reuse refused: source storage is exposed through FFI` |
| `weakly-referenced` | `reuse refused: source storage is weakly referenced` |
| `borrowed-view` | `reuse refused: a borrowed view can observe source storage` |
| `closure-capture` | `reuse refused: a closure captures source storage` |
| `possible-alias` | `reuse refused: source storage may have another live alias` |
| `owned-field-hazard` | `reuse refused: owned fields lack a proven transfer/drop plan` |
| `backend-limitation` | `reuse refused: selected backend cannot implement this candidate` |

Remarks explain a decision; they are not semantic output and MUST NOT be parsed
to recover missing record fields.

## Validation, limits, and determinism

`spec/reuse-candidate-v1/validate.mjs RECORD.json` writes the canonical validated
record to stdout. A rejection exits 1, writes one named error to stderr, and
writes no partial record. Input is capped at 65,536 bytes, text fields at 128
UTF-8 bytes, and pointer arrays at 64 entries.

Canonical output uses the field order in this document, two-space JSON, and one
trailing newline. It contains no ambient path, clock, network, process, random,
or filesystem-discovery data. Repeated validation and validation from a remapped
temporary working directory are therefore byte-identical.

Required rejection identities include:

| Code/name | Rejection |
| --- | --- |
| `RCV101 state-required` | No eligibility state is selected. |
| `RCV102 state-exclusive` | More than one eligibility state is selected. |
| `RCV103 reason-required` | A non-guaranteed row has no reason. |
| `RCV104 reason-closed` | The reason is outside the closed vocabulary. |
| `RCV105 uniqueness-unproved` | Static guarantee lacks proved uniqueness. |
| `RCV106 layout-contradiction` | Copied layout evidence differs from AggregateLayout v1. |
| `RCV111 insufficient-evidence` | `compiler.ensure_move` alone is cited as constructor-storage proof. |

Run the complete gate with:

```sh
sh spec/reuse-candidate-v1/check.sh
```

The gate validates all committed positive and negative vectors, runs twice in
fresh work directories, and also runs the AggregateLayout v1 gate. It does not
read, execute, or modify compiler/runtime code.
