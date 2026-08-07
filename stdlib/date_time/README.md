# Bounded date/time producer

This directory is the executable Stage 2 Core producer started by issue #846 and
extended by the strict RFC 3339 slice of issue #645. It proves the portable
value and failure shapes required by
[`docs/stdlib/date-time.md`](../../docs/stdlib/date-time.md) without reading the
host clock, calling a host calendar library, or substituting a host parser.

`date_time.kofun` contains seven nominal value shapes, a closed
`DateTimeOutcome`, preflight-checked addition, exact conversion, duration
normalization, Gregorian leap-day validation, and a recursive Text parser for
the strict RFC 3339 profile:

```text
YYYY-MM-DDTHH:MM:SS[.1..9 digits](Z | +HH:MM | -HH:MM)
```

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

The gate compiles the Kofun source through scope HIR and the Stage 2 C11
backend, compiles the generated C with warnings as errors, checks stderr is
empty so an `R010`/`R020` runtime abort can never pass as a typed rejection,
names every value and byte position it observes, and only then compares the
exact stdout golden.

## Still owned by the wider contract

Formatting, `OffsetDateTime`-to-`Instant` mapping, the `Offset` type's
±18h representable range, serialization identities, clock adapters, and time
zones remain open in #645, #647, and #648.
