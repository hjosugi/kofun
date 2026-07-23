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

`/` returns `Float`. `//` computes the mathematical floor of the quotient.
`%` is paired with that quotient:

```text
left == (left // right) * right + (left % right)
```

For a non-zero remainder, `%` has the divisor's sign. A zero divisor for `//`
or `%` is runtime error `R010` with status 1, including when the zero is known
only at runtime. Every backend must produce the same value, diagnostic, and
exit status or reject the construct as unsupported before execution.

The executable boundary cases and failure observations are defined by
`tests/conformance/numeric/` under the
`kofun.backend-differential/v1` contract.

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

`law monad` is compile-time-only. It cannot execute ordinary I/O. The compiler checks left identity, right identity, and associativity over the declared finite model. `bounded-exhaustive` is not a universal proof. `proven-finite` is emitted only for compiler-certified complete finite carriers and complete total-function spaces.

Law evidence may be serialized as `kofun.law-evidence/v1`. The artifact binds results to the source SHA-256 and compiler version. A caller may require a minimum assurance; a declared law below that level is rejected with `L200`.

## Backends

The active Stage 1 C11 backend accepts a deliberately small integer Core and
rejects unsupported constructs before execution. The native checkpoint does
not yet lower general Kofun programs and therefore is not a registered
semantic backend. As more backends become executable, each must satisfy the
differential contract for every construct it accepts. No backend may silently
reinterpret a construct with different semantics.
