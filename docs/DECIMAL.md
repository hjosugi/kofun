# Decimal design

Status: accepted language design for issue #545. This document specifies the
target semantics; it does not claim that the compiler implements Decimal
literals or the representation described here.

The executable module under [`stdlib/decimal`](../stdlib/decimal/) is a bounded
checkpoint. It is useful migration evidence, but its signed 64-bit significand
and scale range `0 .. 18` are not the final language representation.

## Goals

`Decimal` is Kofun's exact base-10 value type for money, ledgers, measurements,
and ordinary fractional arithmetic. It must make these statements true without
passing through binary floating point:

```kofun
0.1 + 0.2 == 0.3
1.20 == 1.2
```

The design has five non-negotiable properties.

- Unsuffixed fractional literals denote `Decimal`, not `Float`.
- Decimal arithmetic never rounds implicitly.
- No operation implicitly promotes between `Int`, `Decimal`, and `Float`.
- All backends observe the same value, result, diagnostic, and explicitly
  requested textual rendering.
- Resource limits may reject an operation explicitly, but may not change its
  mathematical result.

`Float` remains the opt-in binary64 type for numerical computing and native
library interoperability. Decimal is not a replacement for BLAS, LAPACK, FFT,
NaN, infinities, or signed zero.

## Abstract value and canonical form

A Decimal value is a pair `(significand, scale)` denoting

```text
significand × 10^(-scale)
```

The significand is a signed arbitrary-precision integer stored in binary.
Semantically, scale is an unbounded signed integer; a conformance profile may
choose a bounded physical encoding and reject values outside it explicitly. A
negative scale therefore represents positive powers of ten without
materializing zero digits:

```text
(12, 1)  = 1.2
(12, 0)  = 12
(12, -2) = 1200
```

Values have one canonical form.

- Zero is `(0, 0)`.
- A nonzero significand has no factor of ten.
- Removing one trailing decimal zero from the significand decrements scale by
  one and does not change the value.

Consequently `(120, 2)` normalizes to `(12, 1)`. `Decimal` is opaque: literal
construction, public constructors, deserialization, FFI adapters, and backend
boundaries must all establish this invariant. Equality, ordering, and hashing
use the mathematical value and therefore agree with canonical representation.
Decimal has no NaN, infinity, signed zero, or payload representation.

### Why a variable-length binary significand

The language representation uses a variable-length significand rather than
BCD or a fixed `i128`.

- BCD makes decimal digit shifts and digit inspection cheap, but wastes space
  and receives little help from general-purpose CPUs.
- A fixed `i128` makes the validity of ordinary exact expressions depend on an
  arbitrary magnitude boundary. It would also make associativity fail through
  overflow unless every operator returned a checked result.
- A binary big integer gives exact integer arithmetic, compact storage, and a
  straightforward implementation on all target backends.

An implementation may keep small values inline in `i64` or `i128` and promote
on overflow. This is an unobservable optimization: the threshold must not
alter equality, rounding, diagnostics, or backend agreement. Falling back to
`Float` is never allowed.

A conformance profile may impose declared limits on digit count, scale
magnitude, allocation, or operation cost. Every backend registered for that
profile must use the same observable limits and diagnostics. Exceeding a limit
produces a stable resource error before an unbounded allocation. It never
clamps, wraps, rounds, or silently changes representation.

### Profile v1

The thresholds this document previously deferred are now introduced, and are
versioned as one unit — a limit cannot move without the version moving with it.

| Limit | Value |
|---|---|
| Profile version | 1 |
| Significand digits | 4096 |
| Scale | `-6144 .. 6144` |

| Code | Condition |
|---|---|
| `D001` | significand exceeds the digit limit |
| `D002` | scale outside the range, before or after canonicalization |
| `D003` | not a literal this grammar accepts |
| `D004` | allocation refused |

Both boundaries and their one-over cases are gated, so exceeding a limit is
observably a `D00x` code and never a clamped value. Leading zeros do not
consume the digit budget, and trailing zeros are canonicalized away before the
scale is checked, so `1000.000` costs one digit and not seven.

The representation is `bootstrap/stage2/decimal_v1.c`; `make decimal` is its
gate. A significand is a base-2^32 magnitude with no width ceiling, and the
small-value path is proven unobservable by constructing the last inline value
and the first promoted one and comparing every public observation.

## Literal syntax and lexing

The target literal grammar is:

```text
digits          = digit, { [ "_" ], digit }
exponent        = ( "e" | "E" ), [ "+" | "-" ], digits
decimal         = digits, ".", digits, [ exponent ]
                | digits, exponent
float64         = digits, "f64"
                | digits, ".", digits, [ exponent ], "f64"
                | digits, exponent, "f64"
```

Examples:

```kofun
42          # Int
0.1         # Decimal: exactly one tenth
6.02e23     # Decimal
1e-9        # Decimal
0.1f64      # Float: explicitly binary64
42f64       # Float
```

There is no Decimal suffix because Decimal is the default fractional type.
`f64` is part of the numeric token, not an identifier or an implicit
conversion. A future additional binary-float width requires a distinct suffix.

The lexer must use maximal munch with the range exception:

```text
1.0   -> one Decimal token
1e3   -> one Decimal token
1f64  -> one Float token
1..2  -> Int(1), range(..), Int(2)
```

`1.`, `.5`, malformed exponents, and underscores outside positions between two
digits are lexical errors. Stage 2 reports all of them as **`E2S98`**, before a
token tape exists and with no artifact written. The token retains the original
digit sequence and the positions of the decimal point, exponent, and suffix.
The lexer must not convert through a host `double`; semantic construction
removes underscores, builds the integer significand, applies the exponent to
scale, and normalizes.

`_1` is not in that list. It is a well-formed identifier under the identifier
grammar rather than a numeric literal, and it reports as an unknown binding at
its own byte. The underscore rule constrains the numeric grammar; it does not
remove an identifier spelling.

The scanner stays permissive about `.` on purpose, and the malformed forms are
diagnosed from the token sequence instead. Deciding between the range operator
and a fraction inside the scanner needs a character of lookahead at exactly the
point where the range exception above says not to — so `1..2` and `1.` both
lex, and `E2S98` reads the result, where `..` and a lone `.` are already
distinct tokens.

A **well-formed** Decimal or Float literal that reaches lowering is a different
condition and gets its own code, **`E2S99`**, which names the slice that would
implement it. Slice 1 delivers the token contract only; the runtime
representation is slice 2. Reusing a generic "invalid expression" diagnostic
here would report the literal as wrong when what is true is that the compiler
is unfinished.

This deliberately revises older planning text that calls every unsuffixed
fractional or scientific literal a `Float`. That text is migration input, not
the final Decimal rule. Parser and conformance changes must update the
normative syntax documents atomically when literal support is implemented.

## Typing and conversions

There is no implicit numeric promotion.

```kofun
1 + 0.5          # type error: Int + Decimal
0.5 + 1.0f64    # type error: Decimal + Float
```

Each numeric operator resolves against one operand type. Conversions use named,
explicit operations:

```kofun
Decimal.from_int(count)
Float.from_decimal(value, rounding: ...)
Decimal.from_float(value, policy: ...)
```

The conversion APIs must state whether they preserve the exact source value,
round to a requested decimal scale, or reject an inexact result. A displayed
`Float` string is not silently treated as the exact binary value, and a binary
value is not silently treated as the decimal text a user originally typed.

Annotations are checked in both directions. `let x: Decimal = 1` is rejected
for the same reason `let x: Int = 1.5` is: a rule that rejected only the
narrowing direction would still be promoting, one way and quietly.

```kofun
let a: Decimal = 1.5     # ok
let b: Float = 1.5f64    # ok
let c: Int = 1.5         # type error: value is Decimal
let d: Decimal = 1       # type error: value is Int
let e: Decimal = 1.5f64  # type error: value is Float
```

An unannotated binding takes its literal's type, so `let f = 1.5` is a
`Decimal` binding and `let g = 1.5f64` is a `Float` one.

## Exact arithmetic

Addition and subtraction align scales with exact powers of ten. Multiplication
multiplies significands and adds scales. Results normalize to canonical form.
These operations do not accept a rounding mode because they never round.

Negation, absolute value, comparison, and equality are exact. `%` and `//`
must specify their same-type quotient/remainder relation and preserve the
identity

```text
left = (left // right) * right + (left % right)
```

for every nonzero right operand. Their final signed convention must be landed
with executable positive and negative examples before those operators become
available for Decimal.

### Division

Decimal division has two explicit forms.

1. Exact division either returns the unique terminating Decimal result or a
   checked `InexactDivision` / `DivisionByZero` result.
2. Rounded division requires both a destination scale and a rounding mode.

The `/` operator is the exact form. Its static result is a checked result; it
does not unwrap, trap, consult a process-wide context, or choose a rounding
mode:

```kofun
let quarter = 1.0 / 4.0  # DecimalResult containing 0.25
let third = 1.0 / 3.0    # DecimalResult containing InexactDivision
let bad = 1.0 / 0.0      # DecimalResult containing DivisionByZero
```

A quotient terminates exactly when, after reducing numerator and denominator,
the denominator has no prime factors other than two and five. Rounded division
uses a named API:

```kofun
Decimal.divide(
    amount,
    divisor,
    scale: 2,
    rounding: HalfEven,
)
```

Even if the particular operands happen to divide exactly, this API still
requires both arguments. There is no thread-local, module-local, build-profile,
or ambient default rounding context.

## Rounding

Every operation that can discard digits requires a destination scale and one
of these modes:

| Mode | Result |
| --- | --- |
| `HalfUp` | nearest; an exact tie goes away from zero |
| `HalfEven` | nearest; an exact tie makes the retained digit even |
| `TowardZero` | discard digits toward zero |
| `Floor` | toward negative infinity |
| `Ceiling` | toward positive infinity |

The names above define Kofun behavior for positive and negative values.
`HalfUp` therefore maps both `2.5` to `3` and `-2.5` to `-3`.

Rounding operates on the exact value. It must not first convert to `Float`, and
it must produce the same carry behavior at every magnitude:

```text
round(1.999, scale=2, HalfUp) = canonical Decimal 2
Fixed[2].from_decimal(1.999, HalfUp) = scale-preserving 2.00
round(2.5,   scale=0, HalfEven) = 2
round(3.5,   scale=0, HalfEven) = 4
```

A plain Decimal normalizes after rounding, so formatting scale is not part of
its identity. Code that must retain `2.00` uses `Fixed` or an explicit format
scale.

## Fixed point

`Fixed[scale]` is the target scale-carrying type. Its scale is a compile-time
integer parameter, and assignment or construction requires an explicit
rounding mode whenever digits may be discarded:

```kofun
let tax: Fixed[2] = Fixed.from_decimal(
    exact_tax,
    rounding: HalfUp,
)
```

Values with different scales are different types. Cross-scale conversion is
explicit. Addition and subtraction of the same `Fixed[S]` type retain `S` and
are exact. Exact multiplication has type `Fixed[A] * Fixed[B] -> Fixed[A+B]`.
Converting that product to a different target scale requires an explicit mode
when it discards digits. Other operations that can exceed a requested scale
likewise specify their result type and rounding boundary.

The current checkpoint stores scale as a runtime field because const-generic
integer parameters are not implemented. It must migrate to `Fixed[scale]`
without claiming that the runtime field already provides static scale safety.
The exact const-generic landing order is deferred to the type-system work that
implements integer value parameters.

## Laws

Arbitrary-precision exact Decimal addition forms a commutative monoid.
Multiplication is associative and distributes over addition. These statements
hold over mathematical Decimal values because ordinary operations neither
overflow nor round.

Resource exhaustion is an explicit operation failure, not a different Decimal
value. Law evidence must state whether it proves the value operation or a
bounded implementation profile.

Exact same-scale Fixed addition is associative while its declared resource
profile succeeds. A computation that rounds after each store or cross-scale
conversion is not generally associative. Such law declarations must name the
scale and rounding boundary rather than inheriting Decimal laws.

`Float` is not forbidden in a law declaration at the type level. Instead, the
law checker evaluates the claim and reports a counterexample when binary64
addition violates associativity. Preventing the declaration would hide useful
evidence and would make the law system less general.

Initial executable evidence must include:

- bounded exhaustive Decimal addition associativity with no overflow shortcut;
- the known binary64 associativity counterexample;
- positive and negative tie cases for every rounding mode;
- distributivity and normalization cases across differing scales;
- explicit failures for division by zero, inexact exact-division, and resource
  limits.

Decimal has an infinite carrier, so execution over a finite set cannot produce
`proven-finite` evidence for Decimal as a whole. Bounded evidence must remain
labeled `bounded-exhaustive`; it is not a universal proof. A universal Decimal
law claim requires future proof-kernel evidence labeled `proven`.

## Relationship to IEEE decimal formats

The language value described here is arbitrary precision and is not IEEE
decimal64 or decimal128. Adapters for those interchange formats may be added,
but they must require an explicit precision, exponent range, rounding mode,
and overflow policy. IEEE NaN, infinity, signed zero, and payload behavior do
not enter the core Decimal value through an adapter implicitly.

This separation gives Kofun deterministic language arithmetic while retaining
a path to databases, financial protocols, and hardware or library decimal
formats.

## Deferred decisions

The following details are intentionally not invented by this design:

- the first cross-backend digit, scale, allocation, and operation-cost limits;
- stable diagnostic codes for those resource failures;
- the implementation schedule for integer const generics and `Fixed[scale]`;
- the canonical human-readable formatting API, exponent thresholds, locale
  layer, and preservation of requested display scale;
- the signed quotient/remainder convention for Decimal `//` and `%`;
- concrete IEEE decimal, database, and wire-format adapters.

These are separate design slices. They may not introduce implicit rounding,
backend-specific observable limits within one conformance profile, or a public
noncanonical Decimal representation.

## Migration from the checkpoint

The existing `stdlib/decimal` checkpoint already demonstrates:

- a binary significand;
- exact addition, subtraction, multiplication, and equality in a bounded
  range;
- explicit exact and rounded division;
- all five rounding modes;
- a runtime scale-carrying Fixed form;
- bounded law evidence and a ledger/tax example.

It does not establish arbitrary precision, Decimal literal parsing, `f64`
suffixes, `Fixed[scale]`, compiler law declarations, backend runtime layout, or
interchange formats.

Implementation should land in this order:

1. update the numeric token contract and parser with byte-exact literal tests;
2. add the arbitrary-precision runtime representation and canonicalization;
3. enforce same-type operators and explicit conversions in the type checker;
4. implement exact operations and checked exact division across every backend;
5. implement explicit rounding and formatting, then migrate the checkpoint;
6. add `Fixed[scale]` after integer const generics exist;
7. connect versioned law evidence and backend differential tests.

Each step must reject unsupported behavior explicitly. A backend that lacks the
new representation may not lower through `Float`, use a smaller fixed integer
silently, or disappear from declared conformance.

## Implementation acceptance

Language-level Decimal is not complete until all of the following hold.

- `0.1 + 0.2 == 0.3` is accepted and true on every declared backend.
- Literal tokens preserve exact digits and distinguish `1.0`, `1..2`, and
  `1.0f64`.
- The public representation is arbitrary precision; small-integer
  optimizations are observationally invisible.
- No `Int`/`Decimal`/`Float` expression receives an implicit promotion.
- Exact division, inexact division, division by zero, and every rounding mode
  have deterministic checked behavior.
- `Fixed[scale]` or an explicitly named interim profile carries storage scale
  without pretending runtime scale is static.
- Decimal laws pass at their stated assurance level and the Float
  associativity counterexample is reported.
- The ledger/tax example agrees digit for digit with its decimal reference.
- Resource failures and backend omissions fail conformance instead of changing
  results or weakening evidence.
