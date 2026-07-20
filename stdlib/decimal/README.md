# Exact base-10 Decimal checkpoint

`Decimal { significand: s, scale: e }` denotes exactly `s / 10^e`.  It uses a
binary `Int` significand, not binary floating point and not BCD.  Consequently
`Decimal(1, 1) + Decimal(2, 1) == Decimal(3, 1)` is exact: this checkpoint never
passes those values through a `Float`.

The canonical API is [`decimal.kofun`](decimal.kofun).  It provides addition,
subtraction, multiplication, equality, explicit rounding, exact division,
rounded division, and a scale-carrying `Fixed` form.  Decimal values normalize
trailing zeroes; Fixed values retain them because the assignment scale is part
of their contract.

## Rounding and division

No API supplies a default rounding mode.  Both `decimal_round` and rounded
`decimal_divide` require one of:

| mode | behavior |
|---|---|
| `HalfUp` | nearest; a tie goes away from zero |
| `HalfEven` | nearest; a tie goes to the even retained digit |
| `TowardZero` | discard the fractional part |
| `Floor` | toward negative infinity |
| `Ceiling` | toward positive infinity |

`decimal_divide_exact(a, b)` has three defined outcomes: `DivisionByZero` when
`b` is zero, `DecimalValue` when long division terminates exactly within the
supported scale, and `InexactDivision` otherwise.  It never rounds.
`decimal_divide(a, b, scale, mode)` is the explicit alternative for an inexact
quotient.  The destination scale and mode are mandatory, including when a
particular input happens to divide exactly.

The current representation is deliberately bounded.  `scale` is `0 .. 18`
and the significand and intermediate operations are signed 64-bit `Int`.
Overflow retains the compiler's existing `R010` checked-Int failure; arbitrary
precision and IEEE decimal64/decimal128 interchange are not claimed here.

## Fixed point

`Fixed { significand, scale }` carries the destination scale, and
`fixed_assign(decimal, scale, mode)` performs rounding at the store boundary.
For example, assigning `1.999` to scale 2 with `HalfUp` produces significand
`200` and retains scale 2, i.e. `2.00`.

Kofun does not yet have const-generic integer parameters, so this checkpoint
value-carries the scale instead of pretending that `Fixed[2]` can already be
checked statically.  `fixed_add` rejects a runtime scale mismatch.  Moving the
field into a future const-generic type is an API evolution point, not an
implemented compiler feature.

## Law evidence and compiler boundary

[`tests/checkpoint.kofun`](tests/checkpoint.kofun) executes exact Decimal
addition over the Cartesian cube of this finite model:

```text
[-1.2, -0.05, 0, 0.1, 0.25]
```

All `5^3 = 125` associativity cases pass.  The same gate executes a binary64
counterexample:

```text
(10000000000000000 + -10000000000000000) + 1 = 1
10000000000000000 + (-10000000000000000 + 1) = 0
```

The deterministic result is recorded in
[`tests/law-evidence.json`](tests/law-evidence.json).  Its assurance is
`bounded-exhaustive`, not a universal proof.  The current CLI exposes no
general algebraic-law checker, and Stage 2 cannot code-generate records or
algebraic data types.  Therefore the gate honestly uses an
executable Int-Core projection plus a binary64 runtime probe; it does **not**
claim that the compiler accepted a Decimal law declaration.  General law
declarations remain a compiler boundary.

## Worked ledger and tax reference

[`examples/ledger_tax.kofun`](examples/ledger_tax.kofun) stores three line
amounts as two-place minor units and represents 8.25% exactly as `825 / 10^4`.
Tax is rounded per line using the explicitly selected `HalfUp` mode.

| line | amount | exact tax | scale-2 tax |
|---|---:|---:|---:|
| 1 | 19.99 | 1.649175 | 1.65 |
| 2 | 5.75 | 0.474375 | 0.47 |
| 3 | 123.40 | 10.180500 | 10.18 |
| total | 149.14 | 12.304050 | 12.30 |

The ledger closes at `149.14 + 12.30 = 161.44`, digit for digit with the
decimal calculation.  The executable output uses integer minor units:
`14914`, `1230`, and `16144`.

Run the Python-free gate with:

```sh
sh stdlib/decimal/tests/verify.sh
```
