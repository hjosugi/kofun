# Date and time contract

## Status

This document is the accepted contract for GitHub issue
[#639](https://github.com/hjosugi/kofun/issues/639). Nothing here is
implemented as a portable library: `stdlib/linux_x86_64/time.kofun` and
`stdlib/clock` are the syscall-level seed, and `clock_gettime` is not
evidence of calendar or time-zone support. Implementation is split into core
calendar arithmetic and RFC 3339 (#645), explicit clock adapters (#647), and
versioned time-zone data (#648).

Under the standard-library charter: civil types belong to the **portable
standard library**, clock capabilities are **platform adapters**, and
time-zone data is an **independently versioned official module**.

The words **must**, **must not**, and **may** are normative.

## Decision summary

Kofun separates *what time it is* from *what a clock says* from *what a
calendar calls it*. Six distinct types make the classic confusions
unrepresentable; every conversion that can fail or be ambiguous returns a
typed `Result`; and no API reads the host clock or host zone implicitly.

## Types

| Type | Meaning | Representation |
|---|---|---|
| `Duration` | signed physical span | `Int` nanoseconds, 64-bit, checked arithmetic |
| `Monotonic` | reading of a monotonic clock | opaque; subtraction yields `Duration`; **not serializable** |
| `Instant` | point on the POSIX time line (UTC) | seconds `Int` + nanoseconds `0..999_999_999` |
| `Date`, `TimeOfDay`, `DateTime` | civil calendar values, no zone | proleptic Gregorian |
| `Offset` | fixed UTC offset | seconds, `-18h..+18h` |
| `Zoned` | `DateTime` + zone id + resolved `Offset` + tz-db version | conversion product |

- `Monotonic` values cannot be serialized, compared with `Instant`, or mixed
  into civil arithmetic; the type has no constructor from numbers, so the
  mistake is a type error rather than a lint.
- Arithmetic is checked: overflow and out-of-range results are typed errors,
  never wraparound.
- Leap seconds are not representable: `Instant` is POSIX time and the
  limitation is documented, not smoothed over. (Alternative — TAI or leap
  smearing: merits are physical honesty; demerits are that every interchange
  format and OS clock is POSIX-shaped and the conversion table becomes
  mutable data v1 does not need. Rejected for v1, revisitable behind the
  same types.)

## Time zones

- Local-to-zoned conversion takes an explicit disambiguation rule; there is
  no default: `Resolve.earlier`, `Resolve.later`, or `Resolve.reject`
  decide DST folds, and nonexistent times (gaps) are an error under
  `reject` or shift under `earlier`/`later` with the choice visible at the
  call site.
- Zone lookups go through a `TimeZones` capability backed by IANA tzdata
  packaged as an independently versioned module artifact. Builds pin the
  tz-db version; every
  `Zoned` carries the version it was resolved against; updating tzdata
  never requires a compiler release.
- An unknown zone id, missing tz data, or version mismatch is a typed
  error.

## Parsing and formatting

- RFC 3339 is the first profile: exact accepted grammar, exact canonical
  output (uppercase `T`/`Z`, no fractional-second padding beyond
  precision). A strict ISO 8601 subset may extend it later; locale-aware
  formatting is a non-goal at every tier.
- Serialization keeps the distinction: an `Instant` serializes with `Z`, a
  `DateTime` has no offset and cannot be read back as an `Instant` without
  a zone or offset supplied by the caller.

## Clocks and testing

- `Clock` (wall) and `MonotonicClock` are platform-adapter capabilities passed
  explicitly; there is no global `now()`.
- Tests inject fake clocks and fixed zone data; no conformance fixture may
  depend on host time or host zone. Golden corpora cover leap years, month
  boundaries, RFC 3339 accept/reject, and DST gaps/folds for at least two
  real zones plus a synthetic zone with pathological transitions.

## Alternatives considered

**One `DateTime` type with optional zone (JavaScript `Date`, pre-JSR-310
Java).** Merits: one type to learn. Demerits: decades of evidence that
instant/civil confusion is the dominant date bug class; JSR-310 and
`chrono`/Temporal all moved to split types. Rejected.

**Default disambiguation for DST folds (most libraries pick `earlier` or
`later` silently).** Merits: shorter call sites. Demerits: a silently
chosen answer to an ambiguous question is exactly the bug the contract
exists to prevent; the explicit rule costs one argument. Rejected.

**Bundle tzdata into the compiler release.** Merits: zero configuration.
Demerits: tz rules change on political timescales, so stale compilers give
wrong answers; independently versioned data with per-build pinning keeps builds
reproducible *and* updateable. Rejected.

**128-bit nanosecond `Duration`.** Merits: astronomical range. Demerits:
64-bit nanoseconds already spans ±292 years, matches Go's proven shape,
and fits register arithmetic on every native target. Rejected.

## Non-goals

Non-Gregorian calendars, implicit global local zone, locale formatting,
scheduling/cron, network time synchronization, and leap-second arithmetic.

## Validation

| Check | Artifact | Expected result |
|---|---|---|
| Contract review | this document | every type, range, conversion, failure explicit |
| Golden corpus | #645 fixtures | deterministic results, host-independent |
| Existing seed | `sh stdlib/tests/verify.sh` | current clock contract remains truthful |
| Charter matrix | `sh stdlib/check-capabilities.sh` | date/time rows cite this contract |
