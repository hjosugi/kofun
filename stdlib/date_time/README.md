# Bounded date/time core

This directory is the executable Stage 2 Core implementation of the portable
half of [`docs/stdlib/date-time.md`](../../docs/stdlib/date-time.md). It reads
no host clock, calls no host calendar library, and substitutes no host parser;
the gate proves all three rather than asserting them.
This directory is the executable Stage 2 Core producer started by issue #846 and
extended by the strict RFC 3339 slice of issue #645. It proves the portable
value and failure shapes required by
[`docs/stdlib/date-time.md`](../../docs/stdlib/date-time.md) without reading the
host clock, calling a host calendar library, or substituting a host parser.

`date_time.kofun` contains seven nominal value shapes, a closed
`DateTimeOutcome`, checked addition/subtraction/multiplication, exact
conversion, duration normalization, proleptic Gregorian day arithmetic in both
directions, fixed-offset to instant mapping, and a recursive Text parser for
the canonical RFC 3339 UTC form:
`DateTimeOutcome`, preflight-checked addition, exact conversion, duration
normalization, Gregorian leap-day validation, and a recursive Text parser for
the strict RFC 3339 profile:

```text
YYYY-MM-DDTHH:MM:SS[.1..9 digits](Z | +HH:MM | -HH:MM)
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

- **Serialization.** Two identities are spelled in the text itself —
  `posix-v1:<seconds>.<nine digits>` and `gregorian-v1:YYYY-MM-DD` — and the
  grammar is canonical rather than merely accepted. `+5`, `05`, `-0`, an
  eight-digit fraction, and a ten-digit fraction are each refused instead of
  normalized, because two spellings of one value would defeat the point of
  carrying a version.
- **Independent agreement.** `tests/reference.mjs` is a second implementation
  of the whole corpus. It shares no code with the producer and never reads the
  golden; the gate requires its output and the compiled C11 artifact to be
  byte-identical.

## Why a reference interpretation, not just a golden

A golden compared against itself proves the program is deterministic, which was
never in question. Two independent implementations agreeing is what makes the
corpus evidence of correctness — and this one earned its place immediately: it
caught a real disagreement on a negative instant while it was being written.
The cause was worth keeping. `observe` folds a closed outcome into one integer
for printing, and that fold is not injective: `DateTimeOk(-5)` prints as `95`,
which a consumer branching on the number reads as an error. The producer
matches on the closed outcome and was right; the reference compared against
`100` and was wrong. Match on the outcome, encode only to print.

Both directions of the differential are load-bearing. Mutating the reference
turns the gate red, and so does mutating the producer.

## Not implemented here

Clock adapters (#647) and time-zone data (#648) stay owned by their own issues,
and no leap-second handling exists by contract. `Monotonic` has no record in
this producer at all: the contract says it has no constructor from numbers, so
the mistake of serializing one cannot be written here even to test it.
Because the closed outcome carries exactly one `Int`, the walk takes a
projection selector: `parse_rfc3339` reports the nanosecond of the second and
`parse_rfc3339_offset` reports the UTC offset in seconds over the identical
grammar. The selector never changes what is accepted.

## Failure vocabularies

The producer keeps two on purpose. Text parsed from RFC 3339 has bytes to blame,
so every rejection is a `ParsePosition` naming the exact zero-based byte. A
value built from `Int` fields has no bytes, so `validate_parts` reports a field
ordinal instead. Both share `validate_date` for the one rule that needs several
fields at once.

There is no minimum-length precheck. An earlier version refused anything under
20 bytes at `ParsePosition(length)`, which reported where the input ended rather
than what was wrong: `2024-02-29T12:34Z` blamed byte 17 when the missing `:` is
at byte 16. Every read is bounds-checked individually instead, so short input
fails at the first byte that violates the grammar.

## Rejection matrix

The gate pins each of these to one exact byte:

| Rejected form | Byte |
| --- | --- |
| leading whitespace | 0 |
| a space in place of `T` | 10 |
| a lowercase `t` | 10 |
| the hour `24` | 11 |
| a missing `time-second` field | 16 |
| the second `60` | 17 |
| a lowercase `z` | 19 |
| a comma in place of the fraction point | 19 |
| the unknown-offset form `-00:00` | 19 |
| a missing `time-offset` | 19 |
| trailing whitespace | 20 |
| a fraction point with no digit | 20 |
| the offset hour `24` | 20 |
| a non-colon offset separator | 22 |
| the offset minute `60` | 23 |
| a truncated offset minute | 24 |
| a trailing byte after a complete offset | 25 |
| a tenth fractional digit | 29 |
| input that ends inside the date | 4 |

`+00:00` stays accepted: RFC 3339 section 4.3 makes it UTC, and reserves
`-00:00` for "offset unknown", which this profile cannot represent.

## Budget

Measured on the current program: 3 Text temporary sites in the emitted function
bodies (one each in `digit_at`, `require_character`, and `parse_offset`) against
the compile-time limit of 256, and 718 runtime Text slots against the emitted
arena's non-wrapping limit of 4096. Every fixture parse consumes slots
permanently — the arena never resets — at roughly 36 slots per full parse, so
new fixtures cost runtime budget, not source budget.

## Running the gate

```sh
sh stdlib/date_time/tests/verify.sh
```

Every value assertion names the exact golden line it reads, so reordering
`main` fails the gate loudly instead of silently checking a different
observation.

The gate compiles the Kofun source through scope HIR and the Stage 2 C11
backend, compiles the generated C with warnings as errors, checks stderr is
empty so an `R010`/`R020` runtime abort can never pass as a typed rejection,
names every value and byte position it observes, and only then compares the
exact stdout golden.

## Still owned by the wider contract

Formatting, `OffsetDateTime`-to-`Instant` mapping, the `Offset` type's
±18h representable range, serialization identities, clock adapters, and time
zones remain open in #645, #647, and #648.
