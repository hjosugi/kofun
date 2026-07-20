# Kofun runtime semantics

The normative rules live in `spec/semantics.md`. This document records
the numeric decision that backend and standard-library implementers must not
inherit accidentally from their host language.

## `Int` and overflow

`Int` is always a signed 64-bit value with the range
`-9223372036854775808 .. 9223372036854775807`.

Kofun uses checked arithmetic. Overflow in integer `+`, `-`, `*`, unary `-`,
or `INT64_MIN // -1` is runtime error `R010` and exit status 1. It never wraps
or saturates, and debug and release builds have identical behavior. The
diagnostic names the operator.

Checked arithmetic was selected because it makes mistakes observable without
making `Int` heap allocated. A future explicit wrapping API may be added, but
ordinary arithmetic will remain checked.

## Integer division

`//` floors toward negative infinity. `%` is defined with the paired floor
quotient, so a non-zero remainder has the divisor's sign.

```text
-7 // 2 == -4
-7 % 2 == 1
7 // -2 == -4
7 % -2 == -1
```

A zero divisor in `//` or `%` is `R010`, writes one canonical diagnostic line
to stderr, and exits with status 1. Backends must check runtime values, not
only zero literals.

## Conformance

Every registered backend executes the same `.kofun` corpus. The active
`c11-stage1` backend passes all nine numeric cases. The runner compares stdout,
stderr, and exit status exactly. Unsupported compilation is an explicit,
reported skip and reduces coverage; it never counts as a silent pass.

See `spec/backend-differential-contract.md` and
`tests/conformance/numeric/README.md` for the runner contract and corpus.
