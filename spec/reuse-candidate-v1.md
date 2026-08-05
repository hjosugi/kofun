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
- `observers`: the five observer classes that could witness partial mutation,
  each `absent`, `live`, or `unknown`;
- `write_ordering`: whether every fallible or effectful new-field expression is
  evaluated before the first destructive write;
- `backend_support`: the backend the state is claimed against, that backend's
  committed reuse support, and what the backend does with a guarantee;
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

The gap is precise, and naming it is the point. `E2S146` fires under a
deliberately narrow rule — an immutable local of managed type, named directly,
asserted in its own scope, no later use, no lambda capture, every earlier read
alias-free. Its conclusion is *this binding is not read again*. A reuse
candidate needs *this storage has no other reference*, which is a statement
about the heap object rather than about one name for it. Two bindings can each
be at their last use while both denote the same storage. Citing
`compiler.ensure_move` as `evidence_id` under
`compile-time-ownership-escape-last-use` is therefore refused outright
(`RCV111`), rather than being silently accepted as the stronger proposition.

## Observers and write ordering

A guarantee is unsound if anything can watch the storage change. `observers`
records the five classes #576 names, each `absent` (proved that none exists),
`live` (one exists), or `unknown` (not analysed):

- `borrowed_view`;
- `closure_capture`;
- `weak_reference`;
- `foreign_pointer`;
- `alias`.

`unknown` is not a weaker `absent`. It says the producer did not look, and it
disqualifies a guarantee exactly as `live` does.

`write_ordering` is `evaluate-all-then-write`, `interleaved`, or `unknown`.
`evaluate-all-then-write` means every fallible or effectful new-field
expression is evaluated before the first destructive write, which is what makes
a panic between two writes impossible to observe.

`reuse_statically_guaranteed` requires every observer `absent` **and**
`write_ordering: evaluate-all-then-write`. Together these mean that
**no observer can witness a partially rewritten node** — no half-updated
storage, no torn value, and no alias reading a node that is neither the old
value nor the new one. A record that fails either condition is refused
(`RCV117`, `RCV119`); it may still be a `reuse_candidate`, because a candidate
promises nothing.

The five observer classes and five disqualifying reasons correspond one to one.
A record whose reason is `borrowed-view`, `closure-capture`,
`weakly-referenced`, `ffi-exposed`, or `possible-alias` MUST NOT declare the
matching observer `absent`; a reason its own evidence denies is refused
(`RCV118`). The converse is not required: the single-reason rule stays
diagnostic provenance, so a live observer does not force the matching reason
when a structural conflict was reported first.

## Backend support is evidence, not an assumption

`backend_support` names the backend a state is claimed against.
`spec/reuse-candidate-v1/backends.json` is the committed table, keyed by the
backend ids registered in `tests/conformance/capabilities.tsv`, and it is
authoritative: `backend_id` MUST appear in it, its `data_layouts` MUST include
`layout_evidence.target_data_layout`, and the record's `reuse_support` MUST
equal the table's (`RCV112`, `RCV113`).

`guarantee_disposition` states what the named backend does with a static
guarantee:

| Disposition | Meaning |
| --- | --- |
| `honour` | the backend implements the reuse. Requires `reuse_support: honours-guarantee`. |
| `reject-program` | the backend cannot implement it and refuses to compile the program. |
| `weaken-to-allocation` | never valid. See below. |
| `not-applicable` | the state is not a guarantee, so no backend obligation exists. |

Only `reuse_statically_guaranteed` carries a disposition; every other state
requires `not-applicable`, and a guarantee requires something other than
`not-applicable` (`RCV116`).

A backend MUST NOT silently weaken a guarantee to allocation. That is the
distinction #576 asks the three states to preserve: a missed candidate is
inspectable, whereas a guarantee quietly turned into an allocation is a proof
that was never checked. `weaken-to-allocation` is therefore rejected on sight
(`RCV115`), for every state. A producer that wants the allocation path emits
`ordinary_allocation` with the `backend-limitation` reason, which is a recorded
decision rather than a silent one, and which contradicts a backend the table
says honours guarantees (`RCV113`).

**No backend in the committed table honours a reuse guarantee.** At
`00ce08f093da2706156f88f80d8c2fb366006d35` every registered backend is
`unsupported`, because no reuse rewrite exists anywhere in the tree: the
generated Stage 2 prelude includes no `<stdlib.h>`, Core enums are by-value
`KofunEnumValue {tag, payload}`, and the only `kofun_rt_alloc` lives in the
selfhost-C11 lane's prelude. Two consequences are normative rather than
incidental:

1. Every record that claims `reuse_statically_guaranteed` today MUST carry
   `guarantee_disposition: reject-program`. Claiming `honour` is refused
   (`RCV114`) — that is a guarantee no backend can keep.
2. `valid/backend-limitation-allocation.json` is the honest shape of a
   candidate at this commit: reusable layout, proved uniqueness, no live
   observer, safe ordering — and `ordinary_allocation`, because the backend
   cannot do it.

A backend row changes to `honours-guarantee` only in the change that implements
the rewrite, together with the tests that show it. Editing the table ahead of
that is how a guarantee gets claimed without a proof, and the gate refuses a
committed valid vector that claims an honouring backend.

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
| `RCV108 provenance-inconsistent` | Proof state and provenance disagree, including a runtime check spelled as proof. |
| `RCV110 layout-incompatible` | A reuse state is carried over source and target layouts that differ. |
| `RCV111 insufficient-evidence` | `compiler.ensure_move` alone is cited as constructor-storage proof. |
| `RCV112 backend-unknown` | The backend is unregistered, or has no committed layout descriptor for the target. |
| `RCV113 backend-support-contradiction` | Declared or implied backend support differs from the committed table. |
| `RCV114 guarantee-unhonourable` | A guarantee is claimed honoured by a backend that does not honour guarantees. |
| `RCV115 guarantee-weakened` | A backend silently weakens a reuse state to allocation. |
| `RCV116 disposition-inapplicable` | A disposition is carried by a non-guarantee, or omitted by a guarantee. |
| `RCV117 observer-live` | A guarantee is claimed while an observer is live or unanalysed. |
| `RCV118 observer-reason-contradiction` | A reason is contradicted by its own observer evidence. |
| `RCV119 write-ordering-unsafe` | A guarantee is claimed without evaluate-all-then-write ordering. |

Run the complete gate with:

```sh
sh spec/reuse-candidate-v1/check.sh
```

The gate validates all committed positive and negative vectors, runs twice in
fresh work directories, refuses a hand-edited and a reformatted golden, and also
runs the AggregateLayout v1 gate. It does not read, execute, or modify
compiler/runtime code.

## What the vectors do not cover

Stating the holes is part of the contract, because a rejection nobody exercises
is a claim rather than a check.

`incompatible-size` and `incompatible-alignment` are specified and enforced, but
no committed vector reaches them. Every constructor type in the committed
AggregateLayout v1 corpus — `Maybe`, `Shape`, `Optional[Int]`, on both targets —
is 16 bytes with 8-byte alignment, so no two of them disagree on size or
alignment, and a record that asserted such a disagreement would be refused by
`RCV106` for contradicting the descriptor before `RCV110` could apply. Both
branches are exercised in `check.mjs` against synthetic mutations instead. When
the corpus gains a constructor type of a different size, they become
vector-covered without a schema change.

Nothing here is produced by a compiler. There is no pass, no IR carrying this
record, no reuse rewrite, and no allocation to count. Every committed vector is
a hand-written document asserting what a future producer must be able to say.

## Compatibility of this strengthening

The observer, ordering, and backend-support groups were added to v1 rather than
to a v2. The schema name is unchanged because there is nothing to be compatible
with: no compiler stage, tool, or IR emits a `kofun.reuse-candidate/v1` record,
so the committed vectors in this directory are the complete population, and they
are updated in the same change. A v2 becomes the right instrument once a
producer exists.
