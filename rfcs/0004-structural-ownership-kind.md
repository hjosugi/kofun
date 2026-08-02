# RFC-0004: Ownership kind is computed structurally from contents

- Shepherd: hjosugi
- Opened: 2026-08-02
- Status: proposed

Proposal for [#570](https://github.com/hjosugi/kofun/issues/570), filed by
[#907](https://github.com/hjosugi/kofun/issues/907). Review is scheduled to
close on 2026-08-16, the ledger's 14-day minimum.

## Summary

A composite type's ownership kind is computed from what it contains, by one
join over a three-point classification order. `spec/records-v1.md` already
decides this for records — "a record containing an affine owned field is itself
affine" — and this RFC extends exactly that rule to tuples, every ADT shape,
closure captures, `Optional[T]`, `List[T]`, and generic substitution, without
changing the record answer.

`Copy` is decided to be an **orthogonal auto-property**, not a subtype of
managed. The `Copy < Managed < Owned` order exists only as a join for
classifying composites; it carries no subtyping, coercion, or substitutability.

`share` ([`docs/MEMORY_MODEL.md`](../docs/MEMORY_MODEL.md) §9) is the only
transformation from the owned domain to the managed domain. Placing an owned
value into a managed aggregate is not one, and is refused.

Nothing here is implemented. `docs/MVP_IMPLEMENTED.md` records
`general ownership and law checking | open | no active general pass`, and this
RFC does not change that.

## Motivation

#570 stays `needs-detail` because one question has no owner: when a composite
or generic type contains an owned value, what is the composite? Part of the
answer landed while the question sat open, and the split is the reason this
child exists.

**Already decided on `main`.** `spec/records-v1.md` §Ownership, mutation, and
drop states that a record containing an affine owned field is itself affine,
that dropping a record drops its owned fields in reverse declaration order,
and that partial moves such as `take record.field` are rejected in v1 so no
partially dropped record state exists. `docs/MEMORY_MODEL.md` §7 states that an
escaping closure may capture only managed values or taken owned values.

**Still unowned.** Tuples. Every ADT shape. `Optional[T]`. `List[T]`. Nested
generics. Generic substitution. And the lattice relation itself.

The gap is not academic. Three defects follow directly from leaving it open:

- **Ownership laundering.** If a managed aggregate may hold an owned value, an
  affine resource acquires managed aliasing and GC-timed release, and the
  deterministic cleanup §2.3 promises is silently lost. §10 already forbids
  leaving a transaction commit, a lock release, or a file flush to a finalizer;
  an unclassified `List[File]` is exactly that design arriving by accident.
- **Duplicate destruction.** If an ADT is classified by the variant currently
  inhabited rather than by every variant it may inhabit, the same storage is
  reachable through two paths with different drop obligations.
- **Silent generic escape.** If an owned type argument does not propagate
  through a generic constructor, `Box[File]` is managed while `File` is owned,
  and the affinity is gone at the instantiation boundary.

Each is a soundness hole that no diagnostic currently names, because no rule
currently classifies the shape.

## Detailed design

### The classification order

Every type has exactly one **ownership kind**:

| Kind | Meaning | Domain |
|---|---|---|
| `Copy` | duplicated by value; no allocation required | `docs/MEMORY_MODEL.md` §2.1 |
| `Managed` | reclaimed by the GC; aliases freely | §2.2 |
| `Owned` | affine; consumed zero or one times; deterministic drop | §2.3 |

Classification uses a join `⊔` over the order `Copy < Managed < Owned`:

```text
Copy    ⊔ Copy    = Copy
Copy    ⊔ Managed = Managed
Managed ⊔ Managed = Managed
K       ⊔ Owned   = Owned      for every K
```

**The order is a join for classification and nothing else.** It is not
subtyping: a `Copy` value is not substitutable where a managed value is
expected, no coercion exists between kinds, and no variance rule reads this
order. This is the RFC's answer to #570's lattice question, stated in the
form the issue asked for:

> **Decision.** `Copy` is an orthogonal auto-property. `Copy ≤ Managed` is
> **rejected** as the formal relation.

The rejected alternative and its cost are stated in [Alternatives](#alternatives).

### Composites are never `Copy` in v1

§2.1 fixes the Copy set as `Int`, `Float`, `Bool`, `Unit`, states it is closed
and not user-implementable, and states that "tuples and records remain non-Copy
until a later type-directed derivation decision."

That decision is not this RFC's to make, so the join is **clamped** at the
composite boundary:

```text
kind(composite) = max(Managed, ⨆ kind(component))
```

A tuple of two `Int`s is `Managed`, not `Copy`, even though every component is
`Copy`. When the type-directed derivation decision lands, the clamp is the one
line that changes; the rest of this RFC is unaffected.

### Classification table

Every shape in #907's scope, with the rule that produced its kind. `T`, `U` are
arbitrary; `O` denotes any type whose kind is `Owned`.

| Shape | Kind | Rule |
|---|---|---|
| `Int`, `Float`, `Bool`, `Unit` | `Copy` | §2.1 closed Copy set |
| `Text` | `Managed` | §2.2 |
| an `own` binding's type | `Owned` | §2.3 |
| record, no owned field | `Managed` | composite clamp |
| record with any owned field | `Owned` | `spec/records-v1.md`, unchanged |
| tuple, no owned component | `Managed` | composite clamp |
| tuple with any owned component | `Owned` | join |
| ADT, payload-free | `Managed` | composite clamp; no inhabitant carries a value |
| ADT, no variant carries owned | `Managed` | composite clamp |
| ADT, **any** variant carries owned | `Owned` | inhabitant rule, below |
| `Optional[T]`, `kind(T) ≤ Managed` | `Managed` | ADT rule; `Optional` is an ADT |
| `Optional[O]` | `Owned` | ADT rule — `Some(o)` is an owned-carrying inhabitant |
| `List[T]`, `kind(T) ≤ Managed` | `Managed` | element join, clamped |
| `List[O]` | `Owned` | element join; see [The `List[File]` boundary](#the-listfile-boundary) |
| non-escaping closure capturing `read`/`edit` | `Managed` | §7; the view cannot escape, so the capture adds no drop obligation |
| non-escaping closure capturing owned by reference | `Managed` | §7; the owned value is not consumed by the capture |
| escaping closure capturing a taken owned value | `Owned` | §7 permits the capture; the drop obligation moves with it |
| escaping closure capturing a `read`/`edit` view | — | refused by §7, `E362` |
| `G[T]` for a generic constructor `G` | `kind(T)`, clamped | substitution rule, below |
| `G[T, U]` | `max(Managed, kind(T) ⊔ kind(U))` | substitution rule |
| nested `G[H[O]]` | `Owned` | the join is computed structurally, to any depth |

### The inhabitant rule

> An ADT is `Owned` if **any** inhabitant can contain an owned value.

Classification is over the *type*, not over the variant currently inhabited. A
value's kind cannot depend on a runtime tag, because the drop obligation is
decided statically and one storage location must have one obligation.

```kofun
type Handle =
    | Closed
    | Open(own File)
```

`Handle` is `Owned`. `Closed` carries nothing, but a `Handle` may be `Open`,
and a `Handle` binding therefore owes deterministic drop. Classifying `Closed`
as managed would make the same type managed and owned depending on control
flow, which is the duplicate-destruction defect this rule exists to prevent.

### Generic substitution

> An owned type argument propagates through a generic constructor unless that
> constructor declares an explicit, verified ownership transformation. An
> unconstrained parameter is treated conservatively until its constraints prove
> a weaker behavior.

Concretely:

1. For a constructor `G[T]` with no declared transformation,
   `kind(G[T]) = max(Managed, kind(T))`.
2. An unconstrained `T` is classified as if `Owned` when checking `G`'s own
   body, so `G` is checked against the strongest obligation any instantiation
   can impose. A body that is sound for `Owned` is sound for every argument.
3. A constraint that *proves* a weaker kind relaxes (2). Constraint vocabulary
   is stated below; the constraints are not carried by any implemented feature
   today, and the RFC depends on none.
4. `share` is the only declared transformation this RFC admits
   (`share: Owned → Managed`). No other constructor may claim one until a
   later RFC defines how such a claim is verified.

### Constraint vocabulary

Four constraint forms are named, each with a case that needs it. DD-032 traits
are the eventual carrier; this RFC references them and **does not depend on
them**, so the vocabulary is stated as design, not as surface.

| Constraint | Admits | A case that needs it |
|---|---|---|
| *accept any* | any kind | a container that stores and returns `T` without duplicating or dropping it — `List[T]`'s own definition |
| *must be `Copy`* | `Copy` only | an API that reads `T` more than once without a view — an index that probes a key repeatedly |
| *must be managed* | `Managed` only | an API that aliases `T` — a cache that hands the same value to two callers |
| *must be owned* | `Owned` only | an API whose contract is release — a scope that guarantees deterministic drop of what it is handed |

*Accept any* is the default. The other three exist so an API can state the
obligation it actually needs instead of the strongest one, and so the
conservative rule in substitution step (2) has something to relax against.

### `share` is the only escape from the owned domain

§9 states the transformation:

```kofun
let own client = Client.connect(endpoint)
let shared = share(client)
```

After `share`, the original owned binding is taken and the result is managed.
That is the whole of the owned-to-managed boundary.

**Placing an owned value into a managed aggregate is not a transformation and
is refused.** There is no implicit `share`. If a caller wants a managed handle,
it writes `share` and accepts §9's consequence that deterministic close now
needs the `Shared[T]` protocol. A construction that would have laundered the
kind silently instead produces `E360`, naming the field, variant, element, or
type argument that carried the owned value.

### The `List[File]` boundary

`List[O]` is `Owned`: an owned collection with deterministic element cleanup.
Dropping the list drops its elements, in reverse index order, mirroring the
record rule's reverse declaration order.

The refusal this decides is the one #907 names: **never a silent
finalizer-dependent managed list.** §10 forbids leaving a file flush, a lock
release, or a transaction commit to a finalizer, and a managed `List[File]`
whose elements are reclaimed by the GC is exactly that. Two options were
available and one is chosen:

- **chosen:** `List[O]` is owned, with deterministic element drop;
- rejected: refuse `List[O]` outright. It is sound but it forbids the ordinary
  case of holding several resources, and it pushes users toward `share`, which
  is the finalizer-dependent design §10 forbids.

Partial moves out of an owned list are rejected in v1, as `take record.field`
is: an element cannot be removed and leave the list partially dropped.
`E363` names that refusal.

## Semantics

The rules above are stated normatively as:

1. `kind` is a total function on types; every type has exactly one kind.
2. `kind` is computed structurally and terminates; a recursive type's kind is
   the least fixed point of the join, which exists because the order is finite
   and the join is monotone.
3. `kind(composite) = max(Managed, ⨆ kind(component))` over every component the
   composite can contain, across every inhabitant.
4. No value's kind depends on control flow, a runtime tag, or an instantiation
   site's context.
5. `Owned` obligations compose: dropping a composite drops its owned components
   in reverse declaration order (records), reverse index order (lists), or the
   variant's declaration order (ADT payloads).
6. `share` is the sole `Owned → Managed` transformation. No implicit conversion
   between kinds exists in any position.

## Diagnostics

Band `E360`–`E369`. Verified unused: `tests/diagnostics/registry.tsv` carries
zero `E3xx` codes at `c165921`, and the reserved RFC bands are `E340`–`E344`
(RFC-0001) and `E350`–`E356` (RFC-0002). `E330` is a retired numbering that was
never registered, per `examples/check.sh`.

| Code | Refusal | The message names |
|---|---|---|
| `E360` | an owned value is placed into a managed aggregate | the field, variant, element position, or type argument that carried it, and that `share` is the only transformation |
| `E361` | an owned composite is used after any component was consumed | the component consumed and the site that consumed it |
| `E362` | an escaping closure captures a `read`/`edit` view | the captured binding and the capture site (§7) |
| `E363` | a partial move out of an owned composite | the component named, and that whole-value `take` is the supported form |
| `E364` | a generic constructor claims an ownership transformation it has not proved | the constructor and the parameter |
| `E365` | a constraint requires a kind the type argument does not have | the parameter, the required kind, and the argument's computed kind |

Every message states the computed kind and the component that produced it, so a
reader repairing one refusal is never told only that "the type is owned".

`E366`–`E369` stay unassigned within the band.

## Ownership and effects

This RFC classifies a type by its contents. It does not decide where the
storage lives, who may allocate it, or how long the backing store survives.

[RFC-0001](0001-allocator-capability.md) separates four facts — allocation
effect, allocator authority, memory ownership, and region lifetime — and this
RFC owns only the third. The region-escape rules and `E340`–`E343` are cited,
not restated, extended, or contradicted. An owned value allocated from an arena
is still affine, still dropped deterministically, and still may not outlive the
arena; that last clause is RFC-0001's, not this RFC's.

Environment authority is [RFC-0002](0002-environment-authority.md). Borrowed
*results* returned from functions are #571: this RFC classifies types by
contents, and return-position views are that issue's contract.

No `Send`- or `Share`-equivalent auto trait is proposed. §12 records that
neither is planned, so #570's acceptance bullet naming that integration is
superseded rather than inherited.

## Alternatives

**`Copy ≤ Managed` as a real subtyping relation.** Rejected. §2.1's Copy set is
closed and not user-implementable, and §2.2's managed domain is defined by GC
reclamation and free aliasing. Making every `Copy` value a managed value would
give `Int` a managed identity it does not have, contradict "a copy does not
require an explicit heap allocation", and force every optimization in §2.2's
list to reason about scalars. The three domains are disjoint; only the
classification join is ordered.

**Classify an ADT by its inhabited variant.** Rejected: it makes one storage
location carry two different drop obligations depending on control flow, which
is the duplicate-destruction defect in Motivation.

**Refuse composites containing owned values entirely.** Sound, and it is what
the compiler does today by refusing the syntax. Rejected because it forbids the
ordinary case — a record that owns a file handle — which `spec/records-v1.md`
has already decided to admit.

**Implicit `share` at the aggregate boundary.** Rejected. It is the ownership
laundering defect written as a feature: deterministic release would be lost at
a site with nothing in the source to mark it.

## Drawbacks

The conservative rule for unconstrained generic parameters means a container
generic over an unconstrained `T` is checked as if `T` were owned, so some
sound instantiations are refused until constraints exist to relax them. That
cost is accepted because the alternative — assume managed and correct later —
is unsound in exactly the direction that loses resources.

Classifying `Optional[O]` as owned means `Optional` gains a drop obligation
that its payload-free inhabitant does not need. That is the inhabitant rule
working as intended, and the cost is a drop check on `None`.

## Compatibility and migration

**Category: additive.**

Everything proposed is new refusal surface over shapes the compiler does not
accept today. No accepted program changes meaning, and no tracked Kofun source
constructs a composite containing an owned value.

Corpus query, run at `c165921`:

```sh
git ls-files '*.kofun' | wc -l
git grep -nE '\blet[[:space:]]+own\b' -- '*.kofun'
git grep -nE '\bshare\(' -- '*.kofun'
git grep -nE '^[[:space:]]*[a-z_]+:[[:space:]]*own\b' -- '*.kofun'
```

Result: 831 tracked `.kofun` sources. Five use `let own`, and all five bind a
scalar resource directly — `examples/ownership.kofun:10`,
`stdlib/tests/file_roundtrip.kofun:14`,
`tests/conformance/syntax/issues_35_47/structural_surface.kofun:16`,
`tests/conformance/syntax/issues_35_47/unsupported_owned_binding.kofun:2`,
`tests/kofun/ownership.kofun:7`. Zero sources call `share(`, and zero declare an
owned field, so no tracked source constructs any shape this RFC classifies as a
composite `Owned`. Every refusal introduced here has an empty blast radius on
the current corpus.

## Implementation plan

Not part of this RFC's acceptance. Stated so the ledger's `implementation`
record has a shape to point at later:

1. `kind` as a total function over the typed frontend's type representation,
   with the fixed-point computation and the composite clamp.
2. `E360` and `E363` first: they refuse the two laundering shapes and need no
   generics.
3. The inhabitant rule for ADTs, with `E361`.
4. Generic substitution and `E364`/`E365`, after DD-032 traits carry the
   constraint vocabulary.
5. `List[O]` drop order, with the reverse-index rule.

Each step needs its own issue, its own gate, and its own `release/claims.json`
row. None is claimed here.

## Validation

| Check | Command | Expected |
|---|---|---|
| Ledger schema and semantics | `sh tests/rfc/check-registry.sh` | passes; this proposal counted `proposed`, mutation fixtures still refused |
| Focused gate | `task rfc-registry` | green with the new row and its review dates |
| Diagnostics | `task diagnostics` | no unregistered or unowned code introduced; `E360`–`E369` stay unregistered until implemented |
| Claim boundary | `task release-claims` | unchanged; no capability gained or lost |
| Regression | `task verify` | no regression |

`docs/MVP_IMPLEMENTED.md` and `release/claims.json` are unchanged by this RFC,
and no shipped-capability statement is added anywhere.

## Unresolved questions

- **When do composites become `Copy`?** The clamp at `Managed` is a placeholder
  for §2.1's deferred type-directed derivation decision. Whoever makes that
  decision changes one line here.
- **How is a declared ownership transformation verified?** `share` is admitted
  by name. The general form — a constructor proving it consumes an owned
  argument and yields a managed result — needs its own RFC before `E364` can
  admit anything beyond `share`.
- **Recursive owned types.** The fixed point exists, but a recursive owned type
  implies unbounded drop depth. Whether that is bounded, refused, or made
  iterative is left to the implementation issue for step 5.
- **Pattern destructuring.** `spec/records-v1.md` defers ownership-aware
  destructuring to a separate design. The inhabitant rule makes that design
  harder, not easier, and it stays deferred here.
