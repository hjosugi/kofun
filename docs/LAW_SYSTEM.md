# Law system

## Status

This document is the accepted target design for issue
[#551](https://github.com/hjosugi/kofun/issues/551). The active compiler does
**not** parse, type-check, evaluate, or emit evidence for this design.
`./bin/kofun check` currently rejects the checked-in `law monad` examples with
`E2S02`; no counterexample is computed and no law result participates in a
normal build.

The old examples and
[`kofun.law-evidence/v1`](../spec/law-evidence.schema.json) schema are retained
as historical migration fixtures. They are not an active language contract,
compiler gate, optimizer input, or release claim. The accepted replacement is
the deliberately incompatible
[`kofun.law-evidence/v2`](../spec/law-evidence-v2.schema.json) design below.

## Purpose

A Kofun law is a named equation belonging to an algebraic structure. It is
narrower than an arbitrary property, is checked rather than assumed like an
axiom, and does not claim proof merely because a finite model passes.

`law` is therefore the standard user-facing term. It is a contextual
top-level construct, not a globally reserved identifier. `Monad`, `Monoid`,
`Functor`, `Applicative`, and `Semigroup` are ordinary library identifiers.
The parser and evaluator must not contain a branch for any one of those
families.

The v1 source model separates three identities that the historical
`law monad Name { ... }` form mixed together:

1. a **law family** declares typed operation requirements and named equations;
2. a **named implementation** supplies operations for one ground
   instantiation; and
3. a **named law check** supplies finite domains, equality, assurance policy,
   and an evaluation budget.

That separation makes the same finite-model engine usable for a library
`Monad`, a library `Monoid`, or a user-declared structure.

## Canonical concrete-first source model

The accepted source shape is:

```kofun
law Monad[A, MA] {
    operation pure(value: A) -> MA
    operation bind(value: MA, next: A -> MA) -> MA

    equation left_identity(
        value: A in values,
        next: A -> MA in functions,
    ) = bind(pure(value), next) == next(value)

    equation right_identity(
        value: MA in monads,
    ) = bind(value, pure) == value

    equation associativity(
        value: MA in monads,
        first: A -> MA in functions,
        second: A -> MA in functions,
    ) = bind(bind(value, first), second)
        == bind(value, fn(x: A) => bind(first(x), second))
}

impl OptionalBoolMonad: Monad[Bool, Bool?] {
    pure = optional_pure
    bind = optional_bind
}

check laws OptionalBoolMonadEvidence {
    instance = OptionalBoolMonad
    domain values: Bool = all
    domain monads: Bool? = all
    domain functions: Bool -> Bool? = all_functions(values, monads)
    equality Bool? = structural
    require assurance = proven_finite
    budget = standard
}
```

`law` and the two-word `check laws` form are recognized only in their
unambiguous top-level positions. `operation`, `equation`, the colon form of
`impl`, `domain`, `equality`, `require assurance`, and `budget` are contextual
within those constructs. They are not new globally reserved words. Callable
types use fixed-arity arrow notation: `A -> B`, `(A, B) -> C`, and `() -> R`.
There is no implicit currying.

`A` and `MA` above are first-order law-schema slots. Every executable first
version substitutes ground types, for example `Bool` and `Bool?`, or `Int` and
`List[Int]`, before checking an equation. V1 has no type-constructor
quantification, `M[_]`, generic `Monad` proof, or requirement for higher-kinded
types.

A named law implementation has a stable identity. If its operations are
provided by a trait implementation, evidence additionally binds that trait
implementation's stable `ImplementationId`. Concrete implementations do not
wait for traits, dictionaries, or higher-kinded types.

Sequencing syntax is independent. An equation may use ordinary calls whether
or not any `Result` propagation or bind sugar exists.

## Typing, quantification, and domains

Every operation binding is type-checked against the instantiated family
signature before evaluation. Every equation must type-check to `Bool`.
Equation parameters are universal quantifiers over their named, typed domains.

The following order is semantic and evidence-bound:

1. law-family equation declaration order;
2. equation parameter order;
3. domain declaration order; and
4. canonical order within each domain.

Cases are the Cartesian product in that order. Source discovery, hash-table
order, task completion order, locale, and host pointer order must not affect
case selection or evidence bytes.

A domain has one of these origins:

- an explicit finite list is a sample and yields at most
  `bounded-exhaustive`;
- `all` is accepted only when the compiler enumerates and certifies the
  complete finite carrier; and
- `all_functions(input, output)` is accepted only when both named domains are
  certified complete, and enumerates every total function from the input
  carrier to the output carrier.

A user assertion such as the historical `complete = true` is never trusted.
Duplicate, missing, ill-typed, non-canonical, or uncertifiable domain entries
are compile errors.

The first implementation is intentionally ground and concrete. Useful initial
checks include `Optional[Bool]`, a declared bounded `List[Int]` sample, an
`Int` monoid over a bounded model, and a deliberately broken user law.

## Typed observational equality

`==` in an equation means the typed observational equality selected by the
check for the equation result type. It is not an untyped serialization
comparison.

- Compiler-certified `structural` equality is permitted for a supported type.
- A custom equality function must have the exact type `(T, T) -> Bool`, be
  total for the evaluated domain, and have an empty effect set.
- `proven-finite` with custom equality additionally requires exhaustive
  certification that the function is an equivalence relation over the
  complete carrier.
- Function equality is extensional only over a compiler-certified complete
  input carrier. Otherwise any result that depends on function comparison is
  at most `bounded-exhaustive`.

Equality identity and implementation body/interface digests are part of the
evidence identity. Changing equality invalidates the evidence even when the
new function happens to return the same answers for one run.

## Deterministic search and shrinking

Evaluation visits cases in the canonical Cartesian order above. On failure,
the checker records the first failing case and then performs deterministic,
type-directed shrinking under the same evaluation budget.

Candidate shrinks are ordered by:

1. structural size;
2. canonical encoded byte length; and
3. canonical encoded bytes.

The minimal counterexample is the first failing fixed point under that order.
Parallel evaluation may compute speculative work, but it must commit the same
failure, shrink trace, diagnostics, and evidence as a single-threaded run.
Randomized generation or randomized shrinking is not part of v1.

## A second family, and a deliberate defect

The engine must contain no `Monad`-, `Monoid`-, or `Functor`-specific branch.
That claim is only checkable against a second family written in the same
constructs, so one is given here. `Monoid` introduces no new grammar: the same
`law`, `operation`, and `equation` forms, and a `check laws` block that differs
only in its domains.

```kofun
law Monoid[T] {
    operation empty() -> T
    operation combine(left: T, right: T) -> T

    equation left_identity(
        value: T in values,
    ) = combine(empty(), value) == value

    equation right_identity(
        value: T in values,
    ) = combine(value, empty()) == value

    equation associativity(
        first: T in values,
        second: T in values,
        third: T in values,
    ) = combine(combine(first, second), third)
        == combine(first, combine(second, third))
}

fn bool_and_empty() -> Bool {
    return true
}

fn bool_and_combine(left: Bool, right: Bool) -> Bool {
    return left && right
}

impl BoolAndMonoid: Monoid[Bool] {
    empty = bool_and_empty
    combine = bool_and_combine
}

check laws BoolAndMonoidEvidence {
    instance = BoolAndMonoid
    domain values: Bool = all
    equality Bool = structural
    require assurance = proven_finite
    budget = standard
}
```

`Bool` is a compiler-certified complete carrier, so `all` is accepted and the
run is exhaustive: two cases for each identity equation and eight for
associativity, twelve in total. The computed assurance is `proven-finite`.
Nothing above names a family the compiler knows; substituting `Monad` for
`Monoid` changes the declarations and not the engine.

### The same family with a wrong identity

`&&` is associative and has identity `true`. Declaring the identity as `false`
leaves associativity intact and breaks both identity laws, which is what makes
it a useful defect: the checker must report one specific failure rather than
whichever it happened to reach first.

```kofun
fn bool_wrong_empty() -> Bool {
    return false
}

impl BrokenBoolMonoid: Monoid[Bool] {
    empty = bool_wrong_empty
    combine = bool_and_combine
}

check laws BrokenBoolMonoidEvidence {
    instance = BrokenBoolMonoid
    domain values: Bool = all
    equality Bool = structural
    require assurance = proven_finite
    budget = standard
}
```

Equation declaration order and Cartesian enumeration order are semantic, so
the reported failure is determined rather than chosen. `left_identity` is
declared first and is evaluated first; over the canonical `Bool` carrier its
cases are `false` then `true`:

| Case | `combine(empty(), value)` | `value` | Result |
| --- | --- | --- | --- |
| `value = false` | `false && false` = `false` | `false` | holds |
| `value = true` | `false && true` = `false` | `true` | **fails** |

The first failing case is therefore `value = true`. Shrinking looks for a
smaller failing case under structural size, then canonical encoded byte
length, then canonical bytes. The only other inhabitant of the carrier is
`false`, and `false` holds — so `value = true` is already the minimal failing
fixed point, and the shrink trace terminates without replacing it.

The check must report, at minimum, the law and equation identity
(`Monoid.left_identity`), the implementation identity (`BrokenBoolMonoid`),
the bound parameter (`value = true`), and the two sides that disagree
(`combine(empty(), true)` evaluating to `false` against `true`). `E2S02` is
what the active compiler emits for this source today, because none of this is
parsed yet; the dedicated diagnostic codes are allocated with the parser in
step 1 of the implementation sequence, not reserved here.

A failed law records a canonical counterexample and **no** assurance —
`proven-finite` is not weakened to `bounded-exhaustive` by failure, it is
absent. `require assurance = proven_finite` then fails the build, which is the
gate doing its job rather than a separate reporting mode.

`right_identity` also fails at `value = true`. It is not reported as the
counterexample, because the first failure in declaration order is, but a
second run that reordered the two equations must report `right_identity`
instead — order is part of the evidence identity, so the two are different
checks and not two answers to the same one.

## Compile-time sandbox and `standard-v1`

Law evaluation executes untrusted user logic during compilation. Operations,
equations, custom equality, domain enumeration, and shrinking must all have an
empty effect set.

The versioned `kofun.law-eval/standard-v1` profile has these hard caps:

| Resource | Cap |
| --- | ---: |
| planned cases | 100,000 |
| evaluator steps | 10,000,000 |
| recursion depth | 256 |
| allocations | 1,000,000 |
| live heap | 64 MiB |
| one rendered or serialized value | 1 MiB |
| total diagnostic text | 64 KiB |

A source-level custom budget may only reduce these caps in v1. It cannot
increase or disable one.

The empty-effect rule denies print/debug output, clock and time, randomness,
environment and process arguments, file/network/process access, FFI, async
work, and global mutation. Capability possession does not override the rule.

Cancellation is checked at least every 1,024 evaluator steps. Cancellation
emits no reusable evidence. A wall-clock watchdog may abort compilation, but
wall time is not a semantic budget and an abort can never become passing or
cacheable evidence. Case planning, step, recursion, allocation/byte,
forbidden-effect, cancellation, and insufficient-assurance failures have
distinct stable diagnostics and fail normal `kofun check` and `kofun build`.
`kofun laws` may later provide filtering and reports; it is not a weaker
compile path.

## Assurance levels

Assurance is computed by the checker, never asserted by the implementation.
The ordered levels remain distinct:

### `bounded-exhaustive`

Every case in the declared finite sample was checked. This says nothing about
values or functions outside that sample and is not a universal proof.

### `proven-finite`

Every case over compiler-certified complete finite carriers and, where used,
the complete total-function space was checked. The result applies only to the
exact ground types, implementation, domains, equality, and evaluator version
named by the evidence. It is not a generic proof.

### `proven`

A future trusted proof kernel validated a proof certificate for the stated
proposition. No current compiler path or finite evaluator may emit `proven`.
Future proof search or SMT may produce a certificate, but is not itself the
root of trust.

Source spelling uses `bounded_exhaustive` and `proven_finite`; evidence uses
the stable strings `bounded-exhaustive` and `proven-finite`.
`require assurance` is a build gate. It does not upgrade computed assurance,
and it is not an input to the reusable evaluation result.

## Evidence v2 and cache identity

The target artifact is `kofun.law-evidence/v2`, defined by
[`spec/law-evidence-v2.schema.json`](../spec/law-evidence-v2.schema.json).
V2 is deliberately incompatible with historical v1.

Two purpose-separated SHA-256 digests are recorded:

- the **evaluation cache key** binds all semantic inputs; and
- the **evidence identity** binds that cache key plus cases checked, computed
  assurance, outcome, resource use, and the canonical counterexample.

The semantic input identity includes:

- compiler semantic version/digest and evaluator version;
- package, module, and law declaration identities;
- the ground type substitution and normalized typed-equation digest;
- named law implementation identity and its transitive interface/body digests;
- the trait `ImplementationId` when applicable;
- ordered domains, their origins and canonical value digest;
- equality identity and body/interface digest;
- budget profile/caps and enumeration algorithm version.

Absolute display paths, host paths, wall-clock time, and requested assurance
are metadata or policy, not semantic identity inputs. Requested assurance is
checked after retrieving or producing evidence.

Passing evidence requires `cases_checked == cases_planned`, a completed
evaluation, non-null computed assurance, and no counterexample. A failed law
records a canonical counterexample but has no assurance. Every cancellation,
resource, or forbidden-effect result has neither assurance nor reusable
authority.

Failed, cancelled, resource-exhausted, forbidden-effect, stale, weaker,
wrong-model, wrong-ground-type, or dependency-mismatched artifacts cannot
authorize an optimization or cache success. Consumers recompute both digests,
validate every dependency, and require the exact assurance appropriate to the
rewrite.

| Rewrite scope | Minimum evidence |
| --- | --- |
| diagnostic/test reporting | matching `bounded-exhaustive` |
| exact concrete finite specialization | matching `proven-finite` |
| generic semantic rewrite | matching `proven` |
| cross-package semantic rewrite | matching `proven` and compatible evidence ABI |

A generic rewrite such as `bind(pure(x), f) -> f(x)` must not use bounded or
ground finite evidence.

## Historical migration boundary

The following are historical only:

- `law monad Name { ... }`;
- compiler-known Monad left/right identity and associativity branches;
- `Fn[...]` callable types in the retained examples;
- user-provided `complete = true`;
- numeric `limit` as the only resource control; and
- `kofun.law-evidence/v1`.

The v1 schema remains in the repository so old static artifacts can be
identified and rejected or migrated explicitly. A v1 artifact must never be
silently interpreted as v2.

## Implementation sequence

The accepted design still requires separate implementation work:

1. contextual parser and typed HIR for families, implementations, and checks;
2. the empty-effect finite evaluator and `standard-v1` resource sandbox;
3. generic deterministic search and shrinking;
4. canonical v2 evidence encoding and validation;
5. library `Monoid` and concrete `Monad` declarations; and
6. normal check/build assurance gates with positive and deliberate-failure
   conformance cases.

Until those executable gates land, documents and examples describe a target
contract only.
