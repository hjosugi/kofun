# Validation accumulation v1

Status: proposed normative design for GitHub issue #742, which is
`needs-decision`. Merging this document records the proposal; the maintainer
records acceptance on the issue, and the open questions at the end are the
decisions still to be made. No library, compiler path, or backend implements
this contract, and its named gate does not exist yet.

The umbrella #428 is a planning tracker. Once this document is accepted, #428
links here as the semantic contract for validation accumulation and defines no
competing semantics of its own.

Decision owner: the repository maintainer. A later change to the accepted
states, operations, ordering, effect rule, ownership boundary, or complexity
bound requires a separately reviewed, versioned specification change.

The words **must**, **must not**, **should**, and **may** are normative.

## Decision

Kofun adds one accumulating validation type, distinct from `Result`:

```kofun
type Validated[T, E] =
    | Valid(T)
    | Disputed(T, Issues[E])
    | Invalid(Issues[E])
```

with three load-bearing properties:

1. a `Validated` is an ordinary eager value: every check that was written runs
   when its expression is evaluated, in source order;
2. **independent** combination (`map2`, `map3`, `all`) collects every issue
   from every branch, in deterministic left-to-right order; and
3. **dependent** sequencing (`and_then`) never invokes its continuation when
   the input has no value, so a check that needs a parsed value cannot run
   against a value that was never produced.

A representative program:

```kofun
fn parse_port(text: Text) -> Validated[Int, ConfigIssue]
fn parse_host(text: Text) -> Validated[Text, ConfigIssue]

fn parse_endpoint(read raw: RawConfig) -> Validated[Endpoint, ConfigIssue] {
    let host = parse_host(raw.host)
    let port = parse_port(raw.port)
        .and_then(fn(p) => check_port_range(p))
    return Validated.map2(host, port,
        fn(h, p) => Endpoint(host: h, port: p))
}
```

If both `raw.host` and `raw.port` are malformed, both issues are reported. If
`raw.port` fails to parse, `check_port_range` is not invoked, because there is
no port value for it to check. `parse_endpoint` makes that dependency graph
visible: what is written with `map2` is independent, what is written with
`and_then` is dependent, and nothing else decides which checks run.

`Result` is unchanged. #487 owns ordinary fail-fast `Result`, and its bind
does not accumulate; this document defines the separate accumulating
abstraction and the lossless conversions between the two.

## Alternatives and proposed boundary

| Alternative | V1 decision | Reason |
| --- | --- | --- |
| Make `Result`'s bind accumulate independent errors | rejected | #487 owns fail-fast `Result`; one type cannot have both behaviors without the Applicative/Monad ambiguity this document exists to avoid |
| Success/failure only, no value-plus-issues state | rejected | normalizing validators (`dispute`) need to report an issue while still producing a usable value; without `Disputed` they must either lie or fail |
| Suspended validator functions or a transformer (`ValidateT` port) | rejected for v1 | eager values keep evaluation order identical to the host language's, need no effect-stack ordering rules, and cannot re-run branches; monad-validate documents that transformer ordering can silently prevent accumulation |
| A typeclass hierarchy relating Applicative and Monad instances | rejected | the two composition forms agree only on success/failure while retaining different issue contents; claiming a lawful relationship over the full observation would be false, so the two forms get distinct named operations instead |
| Implicit parallel or reordered branch execution | rejected | branch independence is about issue accumulation, not scheduling freedom; §Effects gives the observable counterexample |
| Eager three-state value with closure-based `and_then` | **selected** | smallest API that makes accumulation, non-execution, ordering, effects, and ownership all follow from existing language rules |

## Prior art: monad-validate

`monad-validate` (Unison's validation library) is the primary prior art. What
v1 adopts, and what it deliberately does not:

| monad-validate | Kofun v1 consequence |
| --- | --- |
| Applicative composition accumulates independent errors | `map2`/`map3`/`all` accumulate; they are the only independent forms |
| Monadic bind does not run the continuation after fatal failure | `and_then` does not invoke its continuation on `Invalid` |
| `dispute` records a non-fatal error while preserving a value | `dispute` constructs `Disputed(value, issues)` |
| `refute` fails the computation with no value | `refuse` constructs `Invalid(issues)`; the name matches this repository's refusal diction |
| `tolerate` converts fatal failure into an optional value, retaining errors | `tolerate` yields `Validated[T?, E]`, retaining every issue |
| Errors combine with an associative semigroup | `Issues[E]` is nonempty and concatenation is associative |
| Caution: `<*>` and `ap` agree only on success/failure | stated as a labeled success/failure-only law, never as full equality |
| Caution: transformer ordering can prevent accumulation | no transformer exists; `Validated` is a plain value |
| Caution: strict left-associated concatenation can be quadratic | §Complexity forbids O(N²) left-associated composition |

These are design evidence, not compatibility promises. The Haskell transformer
API, typeclass instances, and `MonadValidate` class are not adopted.

## States

`Validated[T, E]` has exactly three states:

| State | Value | Issues | Meaning |
| --- | --- | --- | --- |
| `Valid(value)` | present | none | every check passed |
| `Disputed(value, issues)` | present, usable | one or more | recoverable issues; a normalized or best-effort value exists |
| `Invalid(issues)` | absent | one or more | fatal; no value was produced |

`Issues[E]` is an ordered, **nonempty** collection of issues with associative
concatenation. Because it is nonempty by construction, `Disputed` and
`Invalid` cannot exist with zero issues, and a failure with no explanation is
unrepresentable. `E` is an ordinary type — typically a typed error or domain
issue type from the #421 work — and v1 imposes no structure on it beyond what
`Issues[E]` needs, which is none.

`Disputed` and `Invalid` are distinct constructors observed by `match`. No
operation in this document converts between them implicitly, and no operation
discards issues: every operation either preserves the incoming issues in the
result or does not exist. A `Validated` is a must-use value; dropping one
without observing its state is a diagnostic (§Diagnostics).

## Operations

Proposed signatures, in surface style only — no parser production is added by
this document:

```kofun
Validated.valid(value: T) -> Validated[T, E]
Validated.refuse(issue: E) -> Validated[T, E]
Validated.dispute(issue: E, value: T) -> Validated[T, E]

validated.map(transform: fn(T) -> U) -> Validated[U, E]
validated.map_issues(transform: fn(E) -> F) -> Validated[T, F]

Validated.map2(a: Validated[A, E], b: Validated[B, E],
               combine: fn(A, B) -> C) -> Validated[C, E]
Validated.map3(a, b, c, combine) -> Validated[D, E]
Validated.all(items: List[Validated[T, E]]) -> Validated[List[T], E]

validated.and_then(next: fn(T) -> Validated[U, E]) -> Validated[U, E]

validated.harden() -> Validated[T, E]
validated.tolerate() -> Validated[T?, E]

validated.issues() -> List[E]
validated.to_result() -> Result[T, Issues[E]]
Validated.from_result(result: Result[T, E]) -> Validated[T, E]
Validated.from_issues(result: Result[T, Issues[E]]) -> Validated[T, E]
```

### Independent combination

`map2(a, b, combine)` is the primitive independent form; `map3` and `all` are
its n-ary extensions and **must** behave as its iterated application:

| `a` | `b` | Result |
| --- | --- | --- |
| `Valid(x)` | `Valid(y)` | `Valid(combine(x, y))` |
| `Valid(x)` | `Disputed(y, is)` | `Disputed(combine(x, y), is)` |
| `Disputed(x, is1)` | `Disputed(y, is2)` | `Disputed(combine(x, y), is1 ++ is2)` |
| `Invalid(is1)` | any | `Invalid(is1 ++ issues(b))` |
| any | `Invalid(is2)` | `Invalid(issues(a) ++ is2)` |

`combine` is invoked exactly when both operands carry a value, and never
otherwise. Both operands were already evaluated before `map2` is entered —
`map2` combines values, it does not schedule checks — so a failed branch never
prevents a sibling from having run. If a caller needs a later check *not* to
run after an earlier failure, that is a dependency and **must** be written
with `and_then`; the risk in #742 — users reading "collect all errors" as "all
checks ran" — is resolved by making the two spellings distinct.

`all([])` is `Valid([])`. `all` preserves element order in both the produced
list and the accumulated issues.

### Dependent sequencing

`and_then(next)`:

| Receiver | Behavior |
| --- | --- |
| `Valid(v)` | returns `next(v)` |
| `Disputed(v, is)` | invokes `next(v)`; result `Valid(w)` becomes `Disputed(w, is)`, `Disputed(w, is2)` becomes `Disputed(w, is ++ is2)`, `Invalid(is2)` becomes `Invalid(is ++ is2)` |
| `Invalid(is)` | returns `Invalid(is)`; `next` is **not** invoked |

The continuation is invoked at most once, before `and_then` returns. Prior
issues are always retained and always precede issues the continuation adds.

### Promotion and demotion

- `harden()` promotes `Disputed(v, is)` to `Invalid(is)`; `Valid` and
  `Invalid` are unchanged. The value is dropped by ordinary drop rules; the
  issues are retained exactly.
- `tolerate()` demotes fatal failure to an explicit absence:
  `Valid(v)` becomes `Valid(v)` at `T?`, `Disputed(v, is)` becomes
  `Disputed(v, is)` at `T?`, and `Invalid(is)` becomes `Disputed(null, is)` —
  the value is the `T?` null, and every issue is retained.

A missing value can be tolerated only as a type-visible `T?` whose absence the
consumer must handle; no operation invents a `T` that was never produced, and
no operation drops issues while doing so. Promotion and demotion compose:
`x.tolerate().harden()` is not `x` — laws are stated per operation, not
assumed.

### Local issue mapping and context

`map_issues(transform)` applies `transform` to each issue, preserving count
and order: `issues(x.map_issues(g)) == issues(x).map(g)`. It is the hook for
adding local context — wrapping a field's issues with the field name before a
group is combined — and aligns with the standardized error-context direction
in `docs/TYPE_SYSTEM.md`. It **must not** merge, drop, or reorder issues; a
context system that aggregates differently is a separate future design.

## Evaluation and non-execution

Branch expressions are ordinary expressions. They evaluate exactly when the
host language evaluates them — eagerly, left to right in argument position —
and combinators neither defer, repeat, cancel, nor reorder them. There is no
cancellation in v1: "which effects execute after another branch fails" has the
answer *all of the effects the program itself sequenced, in program order, and
no others*.

The only conditional execution in the contract is the `and_then`
continuation, which is invoked exactly when its input carries a value. The
conformance corpus **must** prove non-execution observationally: the dependent
continuation of a failed prerequisite adds a sentinel issue if invoked, and
the golden output shows the sentinel absent; once the #556 impure marker
exists, an effect-counting fixture must additionally show the counter
unchanged.

## Deterministic issue order

The observation model for issues is the flattened left-to-right sequence:

- `issues(map2(a, b, f)) == issues(a) ++ issues(b)`, in every state;
- `x.and_then(f)` places `x`'s issues before any issue `f` adds;
- `all(items)` concatenates per-element issues in element order;
- nested and grouped composition flattens depth-first, left to right.

Concatenation is associative, so regrouping independent combination —
`map2(map2(a, b, pair), c, f)` against `map2(a, map2(b, c, pair), g)` —
changes neither the resulting state nor the flattened issue order. Issue
order is therefore a function of source order alone: the order in which the
branches appear in the program is the order in which their issues are
reported, and hash order, scheduling, memoization, and cache warmth **must
not** be observable in it.

## Effects

V1 requires the functions passed to `map`, `map_issues`, `map2`, `map3`,
`all`, and `and_then` to be **pure** in the #556 sense — where `pure` still
admits divergence and panics, which deciding termination makes irreducible. A
panic or divergence inside a branch or continuation propagates by the
ordinary rules; accumulation never catches it, and exception handling is out
of scope.

This rule is what makes the ordering claims above complete: with pure
branches, the only observations are the resulting state, value, and ordered
issues, and those are fully specified. The corpus **must** include the
counterexample showing why the rule exists: given an impure branch that
writes output, `map2(a, b, f)` and `map2(b, a, flip(f))` produce identical
states and reordered-only issues but *different* output interleavings — so
"independent" grants an implementation no license to reorder or parallelize
branch evaluation, and a wider effect rule must specify observable order and
cancellation explicitly before it can be accepted (§Open questions).

Because combinator closures are invoked at most once, before the combinator
returns, they are non-escaping in the `docs/MEMORY_MODEL.md` sense, and may
capture `read`/`edit` views under the ordinary non-escaping closure rules.
This contract stays compatible with #556's pure/impure split and with
#569-style explicit capabilities: a future impure validator would carry the
impure marker and any authority it needs as an explicit capability parameter;
nothing here grants ambient authority.

## Ownership

Validation accumulation adds **no** new ownership rules; the contract is that
the existing ones are sufficient and visible:

- a `Validated[T, E]` owns its value and its issues; if `T` is affine, the
  `Validated` is affine, exactly as a record with an affine field is;
- branch expressions evaluate under ordinary move checking, so two
  independent branches cannot both `take` the same binding — the second
  consume is the existing use-after-take refusal, and the corpus **must**
  carry the rejected double-`take` fixture;
- validators that share an input declare `read` parameters; sharing is
  type-visible at the call site, never inferred;
- `map2` consumes both operands; `combine` receives both values owned; when
  one side is `Invalid`, the sibling's produced value is dropped before the
  combinator returns, in reverse argument order, matching the record
  field-drop convention;
- `harden` drops the disputed value the same way; `issues()` and
  `to_result()` on a value-carrying state drop or move the value per the
  ordinary rules, never copy it implicitly.

## Conversions

`to_result()` has one policy: **any issue is failure**.

| Receiver | `to_result()` |
| --- | --- |
| `Valid(v)` | `Ok(v)` |
| `Disputed(v, is)` | `Err(is)` — the value is dropped, deliberately |
| `Invalid(is)` | `Err(is)` |

`Result` cannot carry a value and errors at once, so the conversion keeps the
errors: a `Disputed` that must keep its value stays a `Validated` or is
`tolerate`d/`match`ed first. The rejected alternative — `Disputed` mapping to
`Ok(v)` — silently discards issues and **must not** be offered under any
name.

The error side is `Issues[E]`: every issue, in order, nonempty by
construction. A caller that needs a single-error `Result[T, E]` projects
explicitly — `Issues.first()` is ordered and documented as lossy, and the
issue count remains observable before projecting. There is no implicit
first-error conversion.

Inbound, `from_result` maps `Ok(v)` to `Valid(v)` and `Err(e)` to
`Invalid(Issues.one(e))`; `from_issues` maps `Err(is)` to `Invalid(is)`
unchanged. Conversion is honest about loss: `from_result` of a projected
single-error `Result` yields exactly that one issue, and no round trip
reconstructs issues a projection dropped, nor the value `to_result` dropped
from a `Disputed`. `from_issues(x.to_result())` equals `x` exactly when `x`
is `Valid` or `Invalid`; that equation deliberately excludes `Disputed`.

## Representation and complexity

`Issues[E]` is representation-free with normative bounds:

- constructing any single state, and each `++` concatenation, is amortized
  O(1) in issue count, excluding user function cost;
- any composition of the operations above producing N total issues costs
  O(N) time and space in total, **including left-associated chains** — the
  strict-left-concatenation O(N²) failure mode monad-validate documents is a
  conformance failure here, not a quality-of-implementation note;
- `issues()` flattens to the observation order in O(N).

A chunked sequence, an append tree flattened once, or a difference-list style
builder all qualify; a naive cons-list with eager left-nested append does
not. The evidence is a #640-compatible benchmark at N = 1, N = 1,000, and
N = 100,000 issues for both a balanced and a fully left-associated
composition shape, with the linear budget stated in the gate.

## Laws

Equality for a law is stated against one of two labeled observation models,
and every law names which one it uses. **Full observation** compares state
constructor, value, and the exact ordered flattened issue sequence.
**Success/failure only** compares whether a value was produced, and is the
weaker model — a law stated there says nothing about issue contents.

Full-observation laws:

1. `valid(v).and_then(f) == f(v)`
2. `m.and_then(Validated.valid) == m`
3. `m.and_then(f).and_then(g) == m.and_then(fn(x) => f(x).and_then(g))`
4. `map2` regrouping: nesting order of independent combination changes
   neither state nor flattened issue order
5. `issues(map2(a, b, f)) == issues(a) ++ issues(b)`
6. `x.map(identity) == x`, and `map` composes without touching issues
7. `issues(x.map_issues(g)) == issues(x).map(g)`, count and order preserved
8. `x.tolerate()` retains issues exactly; `x.harden()` retains issues exactly

Success/failure-only law, labeled as such:

9. `map2(a, b, f)` and `a.and_then(fn(x) => b.map(fn(y) => f(x, y)))` agree
   on state and, when a value exists, on the value. They are **not** equal
   under full observation: when `a` is `Invalid`, the dependent encoding
   reports only `a`'s issues and loses `b`'s. This is the monad-validate
   Applicative/Monad caution stated as a boundary rather than hidden behind
   an instance, and the corpus **must** include the distinguishing case.

Law evidence, when produced, uses the full-observation model by default; any
artifact recording law 9 **must** carry the success/failure-only label.

## Diagnostics

Each condition below is a distinct diagnostic condition. Stable codes are
assigned when the bounded conformance gate registers them, following the
records-v1 precedent that a production frontend inherits the *conditions*,
not necessarily the code numbers.

| Condition | Requirement |
| --- | --- |
| independent combine over mismatched issue types (`Validated[_, E]` with `Validated[_, F]`) | names both issue types and both branch spans, and suggests `map_issues`; must be distinguishable from the condition below |
| using a `Validated[T, E]` where a `T` is required | states that the value may be absent and names `match`, `and_then`, or `to_result` as the settlements; this is the "unavailable dependent value" diagnostic and must not be merged with ordinary type mismatch on `T` |
| `and_then` continuation parameter not matching the produced value type | ordinary function-type mismatch, pointing at the continuation |
| a second `take` of a moved input in a sibling branch | the existing use-after-take condition, naming both consuming branch spans |
| a `Validated` result dropped without observing its state | must-use refusal; prevents silently discarding issues |

## Required corpus

The conformance corpus for the first implementation, from #742:

1. three independent fields, two invalid and one valid: both issues reported,
   in source order;
2. an invalid parse followed by a dependent range check: the check does not
   run, proven by the sentinel-issue fixture (and the effect counter once
   #556's marker exists);
3. a recoverable issue with a usable normalized value: `Disputed` observed
   with both;
4. nested independent groups: exact flattened order matches source order
   under both groupings of law 4;
5. local issue-context mapping via `map_issues`, count and order preserved;
6. conversion to and from single-error and multi-issue `Result`, including
   the documented losses;
7. pure branches over a `read`-borrowed input, plus the rejected
   double-`take` variant;
8. N = 1, 1,000, and 100,000 issue constructions, balanced and
   left-associated, within the linear budget;
9. the effectful reordering counterexample from §Effects.

## Validation

| Check | Artifact/command | Expected result |
| --- | --- | --- |
| Semantic model | `sh spec/effects/validation-accumulation/check.sh`, wired as `make validation-accumulation` | full positive/negative corpus passes |
| Non-execution | corpus item 2 | sentinel absent; counter unchanged |
| Ordering and laws | corpus items 4, 5, and the law fixtures | full-observation laws hold; law 9 carries its label |
| Ownership | corpus item 7 | borrowed forms pass; duplicated consume is refused |
| Complexity | corpus item 8 under a #640-compatible benchmark | linear budget met, including left-associated shapes |
| Repository | `make verify` | pass |

None of these artifacts exist yet. They are the acceptance bar for the first
implementation, and no capability claim about validation accumulation may
precede them.

## Non-goals for v1

Changing compiler diagnostic recovery or aggregation; exception handling;
parallel or reordered branch execution; making `Result`'s bind accumulate;
claiming a lawful Applicative/Monad relationship over full observation; a
transformer or typeclass hierarchy; effectful independent branches; partial
moves out of a `Validated`; and any issue-deduplication, sorting, or severity
system. A follow-up may add any of these only by revising this contract.

## Implementation status and compatibility

This document is a design contract. The active compiler and library define no
`Validated`, `Issues`, or any operation above; landing this specification
claims no executable support, adds no accepted syntax, and changes no
existing program, artifact, or gate. The change class is `additive`: every
name is new, and `Result`'s observable behavior is untouched. The first
executable slice requires a separate implementation change, the gate above,
and conformance evidence before any claim.

## Open questions

Decisions deliberately left to maintainer review, with a recommendation each:

1. **Naming.** `Validated` vs `Validation`; `refuse` vs `refute`; `Disputed`
   vs `Flagged`; `harden` vs `strict`. Recommended: the names as written —
   `refuse` matches this repository's refusal diction, and `Validated`
   reads as a value rather than a process. Pure bikeshed; any consistent
   choice preserves the semantics.
2. **Impure dependent continuations.** `and_then` runs its continuation at
   most once, in a fixed order, so admitting the #556 impure marker there is
   deterministic and safe in a way independent branches are not. Recommended:
   keep v1 fully pure for one rule instead of two, and lift the restriction
   for `and_then` only, later, with an explicit ordering statement.
3. **Heterogeneous n-ary sugar.** `map2`/`map3` cover the corpus; a
   record-building or applicative-builder surface depends on open tuple
   (#52) and metaprogramming decisions. Recommended: defer; adding sugar
   later is additive.
4. **Arity ceiling.** Whether `map4`+ are provided or nesting is the answer.
   Recommended: provide through `map3`, then nest; revisit with evidence
   from real validators.
5. **`Issues` visibility.** Whether `Issues[E]` is a public nonempty
   sequence type or an opaque carrier with `first()`, `count()`, and
   iteration. Recommended: opaque in v1 so the O(N) representation can
   change; expose exactly the ordered iteration, `first()`, and `count()`.
