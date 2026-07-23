# Date and time contract

Status: accepted v1 design.

Issue: [#639](https://github.com/hjosugi/kofun/issues/639).

Tiers:

- portable values and arithmetic: Tier 1;
- system and monotonic clocks: Tier 2;
- IANA time-zone data: Tier 3 independently versioned data module.

## Outcome

Kofun distinguishes elapsed time, monotonic readings, POSIX-like instants,
civil dates/times, UTC offsets, and time-zone rules. Values from one domain do
not convert implicitly into another.

The current Linux `Timestamp` returned by `clock_gettime` is a platform seed,
not the general public type model. This contract replaces the catch-all name
with explicit types while preserving the truthful syscall boundary.

## Time model

V1 uses POSIX-style instants: seconds since
`1970-01-01T00:00:00Z`, excluding leap seconds, plus a nanosecond fraction.
This matches common operating-system clocks and the default IANA tzdb model.

RFC 3339 permits a `:60` leap-second spelling, but Kofun v1 rejects it with
`UnsupportedLeapSecond`. It must not silently normalize it to the following
minute. A later UTC-with-leap-seconds type would be a distinct scale and API.

The Gregorian calendar is proleptic for arithmetic. The first implementation
supports civil years `0001..9999`. The type contract reserves checked expansion
to signed years, but RFC 3339 encoding remains exactly four decimal year
digits.

## Core types

### Duration

```text
Duration {
    seconds: Int,
    nanoseconds: Int,
}
```

Canonical form has `0 <= nanoseconds < 1_000_000_000`; `seconds` is the floor
of the signed duration. Therefore negative half a second is:

```text
Duration { seconds: -1, nanoseconds: 500_000_000 }
```

Construction, addition, subtraction, multiplication by an integer, division,
absolute value, and conversion to a requested unit use checked arithmetic.
Overflow and division by zero are typed failures. Unit conversion states
whether it requires exact divisibility or an explicit rounding mode.

`Duration` has no calendar meaning. Adding one day means exactly 86,400
seconds, not "the same local time tomorrow".

### MonotonicInstant

```text
MonotonicInstant {
    clock_identity: ClockIdentity,
    opaque_ticks: internal,
}
```

Only comparison and subtraction between values from the same
`clock_identity` are allowed. A monotonic value:

- cannot be serialized;
- cannot be converted to a wall-clock instant;
- cannot be added to a civil date/time;
- remains meaningful only during the documented lifetime of its clock.

Cross-clock comparison returns `ClockMismatch`.

### Instant

```text
Instant {
    unix_seconds: Int,
    nanoseconds: Int,
}
```

`nanoseconds` has the same canonical range as `Duration`. `Instant` arithmetic
accepts an exact `Duration` and uses checked arithmetic. Subtracting two
instants returns `Duration`.

An `Instant` has no time-zone or calendar presentation until converted with a
fixed offset or zone database.

### Date

```text
Date {
    year: Int,
    month: Int,
    day: Int,
}
```

Construction validates the proleptic Gregorian calendar, including the
divisible-by-4, except-divisible-by-100, unless-divisible-by-400 leap-year
rule. Month/day arithmetic uses an explicit overflow policy:

```text
RejectInvalid
ClampToLastDay
```

There is no implicit clamp.

### TimeOfDay

```text
TimeOfDay {
    hour: Int,
    minute: Int,
    second: Int,
    nanosecond: Int,
}
```

V1 ranges are hour `0..23`, minute `0..59`, second `0..59`, nanosecond
`0..999_999_999`. `24:00` and leap second `:60` are rejected.

### LocalDateTime

```text
LocalDateTime {
    date: Date,
    time: TimeOfDay,
}
```

A local date/time is not an instant and has no implicit system time zone.

### UtcOffset

```text
UtcOffset {
    total_seconds: Int,
}
```

Offsets are checked in `-86_399..86_399`. This accommodates historical IANA
second-resolution offsets. RFC 3339 formatting requires minute alignment and
the RFC profile range; otherwise it returns `UnrepresentableOffset`.

### OffsetDateTime

```text
OffsetDateTime {
    local: LocalDateTime,
    offset: UtcOffset,
}
```

It maps to exactly one `Instant` using checked arithmetic. A numeric offset is
not a time zone and carries no daylight-saving rule.

### ZoneId and Tzdb

```text
ZoneId(Text)

Tzdb {
    version: TzdbVersion,
    digest: Digest,
    zones: internal,
}
```

Zone identifiers use the IANA names contained in the selected database.
Aliases remain aliases from that database; application code must not infer
country or political boundaries from a zone name.

The build/lockfile pins the tzdb release and content digest. A system-zone
adapter may be selected explicitly, but it records the resolved version/digest
or returns `UnversionedZoneDatabase`.

### ZonedDateTime

```text
ZonedDateTime {
    instant: Instant,
    zone: ZoneId,
    tzdb_version: TzdbVersion,
}
```

The local presentation and offset are derived facts from the pinned database.
They are not stored as independent authoritative fields.

## Local time resolution

Mapping a local date/time into a zone can produce zero, one, or two instants:

```text
LocalResolution =
    Unique(Instant)
  | Ambiguous(earlier: Instant, later: Instant)
  | Nonexistent(before: Instant, after: Instant)
```

The base resolver returns this value. It never guesses.

Convenience selection requires an explicit policy:

```text
Reject
Earlier
Later
ShiftForward
ShiftBackward
```

- `Earlier`/`Later` apply only to an ambiguous fold.
- `ShiftForward`/`ShiftBackward` apply only to a nonexistent gap.
- using a non-applicable policy is an error, not a fallback.

Fixtures pin both zone and tzdb version so future political rule changes do not
rewrite old expected results silently.

## Calendar arithmetic

Exact elapsed arithmetic uses `Duration` and `Instant`.

Civil arithmetic uses:

```text
Period {
    years: Int,
    months: Int,
    days: Int,
}
```

`Period` is not convertible to `Duration` without a starting civil value and,
for zoned values, a zone database plus gap/fold policy.

Applying a period proceeds in order: years, months, days. Each step uses the
selected invalid-date policy. Converting the resulting local value through a
zone then uses the selected local-resolution policy.

## Clocks

```text
trait MonotonicClock {
    fn now(read self) -> Result[MonotonicInstant, ClockError]
}

trait SystemClock {
    fn now(read self) -> Result[Instant, ClockError]
}

trait Sleeper {
    fn sleep(read self, duration: Duration, cancel: CancelToken) ->
        Result[Void, SleepError]
}
```

Clock and sleep capabilities are passed explicitly. Libraries do not read the
wall clock merely to format, parse, compare, or resolve a time zone.

A `FakeClock` owns a deterministic monotonic and system timeline. Tests advance
it explicitly and can schedule cancellation. Production system/monotonic clock
adapters wrap target APIs such as `clock_gettime` and record their clock
identity/resolution.

Backward or forward wall-clock adjustment affects later `SystemClock` readings
but never monotonic elapsed measurements.

## RFC 3339 profile

```text
parse_rfc3339(text: Text) -> Result[OffsetDateTime, DateTimeError]
format_rfc3339(value: OffsetDateTime) -> Result[Text, DateTimeError]
```

Accepted v1 grammar:

- exactly `YYYY-MM-DDTHH:MM:SS`;
- uppercase `T`;
- optional fractional seconds with `1..9` decimal digits;
- uppercase `Z` or signed `HH:MM` numeric offset;
- valid Gregorian date and ranges defined above.

Rejected:

- a space instead of `T`;
- lowercase `t`/`z`;
- missing seconds;
- comma fractions;
- more than nine fractional digits;
- `24:00:00`;
- leap second `:60`;
- offset without colon;
- unknown-local-offset `-00:00`;
- surrounding whitespace;
- locale names or zone abbreviations.

Canonical output:

- UTC uses `Z`;
- other offsets use `+HH:MM`/`-HH:MM`;
- the fraction is omitted for zero nanoseconds;
- otherwise it has the shortest exact decimal representation after removing
  trailing zeros;
- the output always includes seconds.

`-00:00` is rejected because it denotes an unknown local offset and cannot
construct an exact instant.

## Normative parsing and zone examples

RFC 3339 examples:

| input | result |
|---|---|
| `1985-04-12T23:20:50.52Z` | accepted; canonical output is identical |
| `1937-01-01T12:00:27.870000+00:20` | accepted; canonical output ends in `.87+00:20` |
| `2024-02-29T00:00:00Z` | accepted |
| `2023-02-29T00:00:00Z` | `InvalidDate` |
| `1990-12-31T23:59:60Z` | `UnsupportedLeapSecond` |
| `2024-01-01 00:00:00Z` | `Parse` at the separator |
| `2024-01-01T00:00:00-00:00` | `Parse` for unknown local offset |
| `2024-01-01T24:00:00Z` | `InvalidComponent(hour, 24)` |

The first tiny tzdb fixture pins the source version and digest and contains the
`America/New_York` 2024 transitions. Against that exact fixture:

| local input | resolution |
|---|---|
| `2024-03-10T01:30:00` | `Unique` at `-05:00` |
| `2024-03-10T02:30:00` | `Nonexistent`, bounded by the `-05:00` to `-04:00` transition |
| `2024-03-10T03:30:00` | `Unique` at `-04:00` |
| `2024-11-03T01:30:00` | `Ambiguous`; earlier is `-04:00`, later is `-05:00` |
| `2024-11-03T02:30:00` | `Unique` at `-05:00` |

Calling `resolve` without a policy returns the result variant and never chooses
between ambiguous candidates. `Earlier`/`Later` and
`ShiftForward`/`ShiftBackward` are accepted only for their matching fold/gap
variant.

## Serialization

Versioned portable representations are:

```text
Duration: { seconds, nanoseconds }
Instant: { unix_seconds, nanoseconds, scale: "posix-v1" }
Date: { year, month, day, calendar: "gregorian" }
LocalDateTime: { date, time, calendar: "gregorian" }
OffsetDateTime: { local, offset_seconds }
ZonedDateTime: { instant, zone_id, tzdb_version, tzdb_digest }
```

`MonotonicInstant` is not serializable.

Decoding a zoned value requires the exact pinned database. If unavailable, it
returns `MissingZoneDatabaseVersion`. Upgrading to another tzdb release is an
explicit operation that preserves the instant and may change local
presentation.

## Errors

Stable error categories include:

```text
InvalidComponent(component, value)
InvalidDate
Overflow(operation)
DivisionByZero
InexactUnitConversion
ClockMismatch
ClockUnavailable
UnsupportedTarget
UnsupportedLeapSecond
UnrepresentableOffset
UnknownZone
AmbiguousLocalTime
NonexistentLocalTime
MissingZoneDatabaseVersion
UnversionedZoneDatabase
Parse(position, expected)
Cancelled
```

Diagnostics use stable component/position data and do not depend on locale.

## Required golden corpus

- Gregorian common/leap centuries: 1900, 2000, 2100;
- month ends and explicit clamp/reject behavior;
- negative and subsecond durations in canonical form;
- checked boundaries for duration and instant arithmetic;
- RFC 3339 UTC/offset/fraction round trips and every rejection class;
- two fixed-offset conversions;
- one IANA fold and one gap, pinned to a committed tiny tzdb fixture;
- unknown zone and missing database version;
- monotonic cross-clock rejection;
- fake-clock sleep, cancellation, and wall-clock adjustment;
- serialization and explicit tzdb upgrade behavior.

Tests set no host `TZ`, locale, or current time. The full tzdb is not required
for the small golden contract; fixtures carry a source version and digest.

## Compatibility and updates

Portable type/serialization changes follow Tier 1 compatibility rules.
Clock adapters follow target/toolchain patch updates. Tzdb is a Tier 3 data
dependency resolved through the lockfile and can update independently of the
compiler.

An update may change future and historic local presentation for an instant.
It cannot change the stored instant. Applications that require reproducible
presentation retain the pinned version/digest.

## Implementation slices

1. Pure Gregorian `Date`, `TimeOfDay`, `Duration`, `Instant`, and RFC 3339.
2. Explicit monotonic/system clock interfaces and Linux adapter migration.
3. Versioned tiny tzdb reader plus gap/fold resolution.
4. Independently packaged full IANA tzdb data and update flow.

## Sources

- [RFC 3339: Date and Time on the Internet](https://www.rfc-editor.org/rfc/rfc3339)
- [IANA Time Zone Database](https://www.iana.org/time-zones)
- [IANA tzdb theory and scope](https://www.iana.org/time-zones/theory)
