# Bounded date/time core

This directory is the executable Stage 2 Core implementation of the portable
half of [`docs/stdlib/date-time.md`](../../docs/stdlib/date-time.md). It reads
no host clock, calls no host calendar library, and substitutes no host parser;
the gate proves all three rather than asserting them.

`date_time.kofun` contains seven nominal value shapes, a closed
`DateTimeOutcome`, checked addition/subtraction/multiplication, exact
conversion, duration normalization, proleptic Gregorian day arithmetic in both
directions, fixed-offset to instant mapping, and a recursive Text parser for
the canonical RFC 3339 UTC form:

```text
YYYY-MM-DDTHH:MM:SS[.1-9 digits]Z
```

The parser reports a zero-based byte position and accumulates fractional
seconds as nanoseconds.

## What this core proves

- **Calendar.** Years are bounded to 0001..9999. Every month length of 1900,
  2000, 2100, 2024, and 2023 is checked individually, so the century rule is
  pinned rather than inferred from a total. `days_from_civil` and
  `civil_from_days` round-trip at both range ends.
- **Arithmetic.** Addition, subtraction, and multiplication each reject
  overflow as a closed `ArithmeticOverflow` value instead of reaching the
  backend's R010 failure. The multiplication guard compares magnitudes, because
  Kofun's `//` floors — a negative numerator would round away from zero and
  reject representable products — and it still accepts the one product past
  `maximum` that is representable, the negative bound itself.
- **Instants.** An `OffsetDateTime` maps to exactly one `Instant`; the offset is
  subtracted, never added, and offsets outside -18h..+18h are `InvalidField(8)`.
- **Parsing.** Zero through nine fractional digits each scale to canonical
  nanoseconds, and a rejection matrix pins lowercase separators, a space
  separator, missing seconds, comma fractions, over-long fractions, empty
  fractions, offset spellings, leading whitespace, and out-of-range fields to
  exact byte positions or exact field ids.
- **Separated types.** The gate compiles a fixture that passes a `Duration`
  where an `Instant` is required and requires Stage 2 to refuse it with
  `E2S32`, so the contract's central distinction is enforced by the compiler
  rather than by review.

## Not yet implemented here

Versioned portable serialization and the independent reference-oracle
differential corpus remain open: both need Text formatting that the current
Stage 2 surface does not express. Clock adapters (#647) and time-zone data
(#648) stay owned by their own issues, and no leap-second handling exists by
contract.

Run the focused gate with:

```sh
sh stdlib/date_time/tests/verify.sh
```

Every value assertion names the exact golden line it reads, so reordering
`main` fails the gate loudly instead of silently checking a different
observation.

The gate compiles the Kofun source through scope HIR and the Stage 2 C11
backend, compiles the generated C with warnings as errors, executes the exact
stdout/stderr goldens, and names every structural observation it checks.
