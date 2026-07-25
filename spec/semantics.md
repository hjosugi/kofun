# Stage 0 semantic contract

Status: normative for the bootstrap implementation.

## Values

Stage 0 has `Int`, `Float`, `Bool`, `Text`, `Null`, `List[T]`, `Tuple[...]`, functions, and an opaque `Resource` value. `T?` is represented by `Null` or a `T` value.

## Bindings

`let` creates an immutable binding. `let mut` creates a mutable binding. Assignment to an immutable binding is a compile-time error. `let own` marks an affine resource binding. After `take`, any later read is a compile-time error when detected by the local analysis and a runtime error otherwise.

## Numeric operators

`Int` is a signed 64-bit value in the inclusive range
`-9223372036854775808 .. 9223372036854775807`.

Integer `+`, `-`, `*`, and unary `-` use checked arithmetic. A result outside
the `Int` range is runtime error `R010`; it writes one canonical diagnostic
line naming the operator to stderr and exits with status 1. Implementations
must not wrap, saturate, or vary this behavior between debug and release
builds. `INT64_MIN // -1` is the same overflow error. A future explicit
wrapping API does not change ordinary arithmetic.

`/` is not defined on `Int`. Kofun performs no implicit numeric conversion, so
`/` cannot produce a fractional value from two `Int` operands and has nothing to
mean on them; `Int / Int` is a compile error. The integer quotient is `//`,
which computes the mathematical **floor** of the quotient — note that `//`
truncates in some other languages, so the rounding is normative here rather
than assumed. `%` is paired with that quotient:

```text
left == (left // right) * right + (left % right)
```

For a non-zero remainder, `%` has the divisor's sign. A zero divisor for `//`
or `%` is runtime error `R010` with status 1, including when the zero is known
only at runtime. Every backend must produce the same value, diagnostic, and
exit status or reject the construct as unsupported before execution.

The executable boundary cases and failure observations are defined by
`tests/conformance/numeric/` under the
`kofun.backend-differential/v1` contract. That corpus also carries the refusal
of `/` as `reject_slash_operator.kofun`, so a backend that gives the operator a
meaning fails the same gate that pins the values of `//` and `%`.

## Text

`Text` stores valid UTF-8. Its default sequence unit is the Unicode 17 extended
grapheme cluster:

- `len(text)` counts grapheme clusters;
- `text[index]` returns one complete grapheme cluster;
- negative indexes count grapheme clusters from the end; and
- `chars(text)` returns `List[Text]` of grapheme clusters.

Concatenation re-segments the joined bytes because a cluster can cross the
join. The lower-level views are explicit: `bytes(text)` exposes a `List[Int]`
of UTF-8 bytes, while `codepoints(text)` exposes a `List[Text]` of Unicode
scalars. Neither view changes default `Text` indexing.

Text literals may contain canonically decomposed content and preserve their
original UTF-8 bytes. Identifiers are stricter: they must already be NFC.

## Control flow

`if` is an expression when both branches produce values. In statement position it produces `Void`. `for` iterates a `List`; `start .. end` is an end-exclusive integer range.

## Functions

Top-level function headers are collected before bodies are checked, enabling recursion and forward calls. Parameters default to value mode. `read`, `edit`, and `take` are ownership modes; Stage 0 enforces a local affine approximation rather than the full planned borrow model.

## Law declarations

This section is a normative target, not an implemented Stage 0 capability. The
active compiler rejects the retained historical `law monad` syntax with
`E2S02` and emits no law evidence.

`law` contextually introduces a generic law family. A family contains typed
operation requirements and named equations. `Monad`, `Monoid`, and every other
family name is an ordinary library identifier; no family selects a
compiler-specific evaluator branch.

One named `impl Name: Family[GroundTypes]` supplies the operations for a ground
instantiation. One named `check laws Name` supplies typed finite domains,
typed observational equality, a required assurance, and an evaluation budget.
The first executable profile substitutes only ground types. It does not
quantify over type constructors or require higher-kinded types.

Equation parameters universally quantify over their named domains. Law-family
equation order, equation parameter order, domain declaration order, and
canonical value order determine Cartesian case enumeration. The first failing
case is shrunk deterministically by structural size, canonical encoded length,
then canonical encoded bytes. Search and shrinking consume the same budget.

An explicit finite list is a sample and yields at most
`bounded-exhaustive`. `all` is accepted only for a compiler-certified complete
finite carrier. `all_functions(input, output)` is accepted only for two such
domains and enumerates every total function. `proven-finite` applies only to
the exact ground model and also requires certified typed equality. Function
equality is extensional only over a certified complete input domain.
`proven` is reserved for a future trusted proof kernel.

Operations, equations, custom equality, enumeration, and shrinking require an
empty effect set. The versioned `kofun.law-eval/standard-v1` profile limits a
check to 100,000 planned cases, 10,000,000 evaluator steps, recursion depth
256, 1,000,000 allocations, 64 MiB live heap, 1 MiB per rendered/serialized
value, and 64 KiB total diagnostic text. A source budget may only reduce these
caps. Cancellation is checked at least every 1,024 steps and emits no reusable
evidence. Wall time may abort compilation but is not a semantic input.

The target artifact is `kofun.law-evidence/v2`. Its evaluation cache key binds
compiler/evaluator semantics, law and ground type identities, normalized typed
equations, implementation and transitive dependency digests, ordered domains,
equality, budget, and enumeration algorithm. Its evidence identity additionally
binds cases checked, computed assurance, outcome, resource use, and canonical
counterexample. Requested assurance is a gate rather than an evaluation
identity input.

Passing evidence requires every planned case to have been checked, a non-null
computed assurance, and no counterexample. A failed law may record its
canonical counterexample but has no assurance or reusable authority.

A consumer must reject failed, cancelled, resource-exhausted,
forbidden-effect, stale, weaker, wrong-model, wrong-ground-type, or
dependency-mismatched evidence. Historical `kofun.law-evidence/v1` artifacts
remain identifiable migration material and are never accepted as v2.

## Backends

The active Stage 1 C11 backend accepts a deliberately small integer Core and
rejects unsupported constructs before execution. The native checkpoint does
not yet lower general Kofun programs and therefore is not a registered
semantic backend. As more backends become executable, each must satisfy the
differential contract for every construct it accepts. No backend may silently
reinterpret a construct with different semantics.
