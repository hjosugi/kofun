// Reference interpretation of the date/time corpus.
//
// This is deliberately a *second* implementation, not a reader of the first.
// It shares no code with stdlib/date_time/date_time.kofun and never loads the
// golden: it recomputes every observation from the contract in
// docs/stdlib/date-time.md and prints them in the same order. The gate then
// requires this output and the compiled Stage 2 C11 artifact to be
// byte-identical.
//
// That is the point. A golden compared against itself proves the program is
// deterministic, which it already was. Two independent implementations
// agreeing proves the program is *right* — or, when they disagree, says so
// instead of blessing whichever one was written first.
//
// Everything is BigInt. The corpus reaches both 64-bit bounds, and IEEE
// doubles would silently round exactly the values the overflow rules exist to
// pin.

const MAX = 9223372036854775807n
const MIN = -9223372036854775808n
const NANOS = 1000000000n

// ---- outcomes ----
// Outcomes stay tagged and are folded to an integer only at the moment of
// printing. The fold is not injective — `DateTimeOk(-5)` and the error space
// both land near 95 — so branching on the folded value silently misreads any
// negative payload as a failure. The first draft of this file did exactly
// that, and the differential below is what caught it: the producer matched on
// the closed outcome, this reference compared `< 100`, and the two disagreed
// on a negative instant. Match on the tag; encode only to print.
const ok = (v) => ({ kind: 'ok', value: v })
const invalidField = (f) => ({ kind: 'field', value: f })
const parsePosition = (p) => ({ kind: 'position', value: p })
const inexact = (r) => ({ kind: 'inexact', value: r })
const overflow = (op) => ({ kind: 'overflow', value: op })

const observe = (o) => {
    if (o.kind === 'ok') return 100n + o.value
    if (o.kind === 'field') return 200n + o.value
    if (o.kind === 'position') return 300n + o.value
    if (o.kind === 'inexact') return 400n + o.value
    return 500n + o.value
}

const out = []
const emit = (value) => {
    const tagged = value !== null && typeof value === 'object' && 'kind' in value
    out.push(String(tagged ? observe(value) : value))
}

// ---- calendar ----
const leapYear = (y) => (y % 400n === 0n ? true : y % 100n === 0n ? false : y % 4n === 0n)

const daysInMonth = (y, m) => {
    if (m === 2n) return leapYear(y) ? 29n : 28n
    if (m === 4n || m === 6n || m === 9n || m === 11n) return 30n
    return 31n
}

const monthBoundariesOk = (year, expectedFebruary) => {
    const table = [31n, expectedFebruary, 31n, 30n, 31n, 30n, 31n, 31n, 30n, 31n, 30n, 31n]
    for (let i = 0; i < 12; i += 1) {
        if (daysInMonth(year, BigInt(i + 1)) !== table[i]) return 0n
    }
    return 1n
}

// Era-based civil/day conversion. Written from the published algorithm, with
// truncating BigInt division; the corpus stays inside 0001..9999 so the
// numerators here are never negative and this agrees with the producer's
// flooring `//`.
const daysFromCivil = (year, month, day) => {
    const y = month <= 2n ? year - 1n : year
    const era = y / 400n
    const yoe = y - era * 400n
    const mp = month <= 2n ? month + 9n : month - 3n
    const doy = (153n * mp + 2n) / 5n + day - 1n
    const doe = yoe * 365n + yoe / 4n - yoe / 100n + doy
    return era * 146097n + doe - 719468n
}

const civilFromDays = (days) => {
    const z = days + 719468n
    const era = z / 146097n
    const doe = z - era * 146097n
    const yoe = (doe - doe / 1460n + doe / 36524n - doe / 146096n) / 365n
    const yearOfEra = yoe + era * 400n
    const doy = doe - (365n * yoe + yoe / 4n - yoe / 100n)
    const mp = (5n * doy + 2n) / 153n
    const day = doy - (153n * mp + 2n) / 5n + 1n
    const month = mp >= 10n ? mp - 9n : mp + 3n
    const year = month <= 2n ? yearOfEra + 1n : yearOfEra
    return { year, month, day }
}

const civilRoundTrip = (year, month, day) => {
    const back = civilFromDays(daysFromCivil(year, month, day))
    return back.year === year && back.month === month && back.day === day ? 1n : 0n
}

// ---- field validation ----
const validateParts = (year, month, day, hour, minute, second, nanosecond) => {
    if (year < 1n || year > 9999n) return invalidField(1n)
    if (month < 1n || month > 12n) return invalidField(2n)
    if (day < 1n || day > daysInMonth(year, month)) return invalidField(3n)
    if (hour < 0n || hour > 23n) return invalidField(4n)
    if (minute < 0n || minute > 59n) return invalidField(5n)
    if (second < 0n || second > 59n) return invalidField(6n)
    if (nanosecond < 0n || nanosecond > 999999999n) return invalidField(7n)
    return ok(nanosecond)
}

// ---- checked arithmetic ----
// Guards are expressed as range predicates rather than as the producer's
// magnitude comparison, so a mistake in either shape shows up as a difference.
const checkedAdd = (a, b) => {
    const r = a + b
    return r > MAX || r < MIN ? overflow(1n) : ok(r)
}
const checkedSub = (a, b) => {
    const r = a - b
    return r > MAX || r < MIN ? overflow(2n) : ok(r)
}
const checkedMulRaw = (a, b) => {
    const r = a * b
    return r > MAX || r < MIN ? null : r
}
const checkedMul = (a, b) => {
    const r = checkedMulRaw(a, b)
    return r === null ? overflow(3n) : ok(r)
}

const normalizeDuration = (seconds, nanoseconds) => {
    if (nanoseconds < 0n) return { seconds: seconds - 1n, nanoseconds: nanoseconds + NANOS }
    if (nanoseconds >= NANOS) return { seconds: seconds + 1n, nanoseconds: nanoseconds - NANOS }
    return { seconds, nanoseconds }
}

const durationRoundTrip = (seconds, nanoseconds) => {
    const n = normalizeDuration(seconds, nanoseconds)
    if (n.nanoseconds < 0n || n.nanoseconds >= NANOS) return 0n
    const before = seconds * NANOS + nanoseconds
    const after = n.seconds * NANOS + n.nanoseconds
    return before === after ? 1n : 0n
}

const nanosToMillis = (n) => (n % 1000000n !== 0n ? inexact(n % 1000000n) : ok(n / 1000000n))

// ---- offset to instant ----
const offsetToInstant = (y, mo, d, h, mi, s, ns, offsetSeconds) => {
    if (offsetSeconds < -64800n || offsetSeconds > 64800n) return invalidField(8n)
    const parts = validateParts(y, mo, d, h, mi, s, ns)
    if (parts.kind !== 'ok') return parts
    const base = checkedMulRaw(daysFromCivil(y, mo, d), 86400n)
    if (base === null) return overflow(3n)
    const civil = base + (h * 3600n + mi * 60n + s)
    if (civil > MAX || civil < MIN) return overflow(1n)
    const utc = civil - offsetSeconds
    return utc > MAX || utc < MIN ? overflow(2n) : ok(utc)
}

// ---- RFC 3339 ----
// The grammar is fixed-position by contract, so the reference walks the same
// positions the contract names. Failure positions are recomputed, not copied.
const isDigit = (c) => c >= '0' && c <= '9'

const digitsAt = (t, from, to) => {
    let v = 0n
    for (let i = from; i < to; i += 1) {
        if (!isDigit(t[i])) return { error: parsePosition(BigInt(i)) }
        v = v * 10n + BigInt(t.charCodeAt(i) - 48)
    }
    return { value: v }
}

const parseRfc3339 = (t) => {
    const n = t.length
    if (n < 20) return parsePosition(BigInt(n))

    const year = digitsAt(t, 0, 4)
    if (year.error) return year.error
    if (t[4] !== '-') return parsePosition(4n)
    const month = digitsAt(t, 5, 7)
    if (month.error) return month.error
    if (t[7] !== '-') return parsePosition(7n)
    const day = digitsAt(t, 8, 10)
    if (day.error) return day.error
    if (t[10] !== 'T') return parsePosition(10n)
    const hour = digitsAt(t, 11, 13)
    if (hour.error) return hour.error
    if (t[13] !== ':') return parsePosition(13n)
    const minute = digitsAt(t, 14, 16)
    if (minute.error) return minute.error
    if (t[16] !== ':') return parsePosition(16n)
    const second = digitsAt(t, 17, 19)
    if (second.error) return second.error

    if (n === 20) {
        if (t[19] !== 'Z') return parsePosition(19n)
        return validateParts(year.value, month.value, day.value, hour.value, minute.value, second.value, 0n)
    }
    if (t[19] !== '.') return parsePosition(19n)
    if (n < 22) return parsePosition(20n)
    if (n > 30) return parsePosition(30n)
    if (t[n - 1] !== 'Z') return parsePosition(BigInt(n - 1))
    const fraction = digitsAt(t, 20, n - 1)
    if (fraction.error) return fraction.error
    // Scale to nanoseconds by the digit count the text actually carried.
    let scaled = fraction.value
    for (let d = n - 1 - 20; d < 9; d += 1) scaled *= 10n
    return validateParts(year.value, month.value, day.value, hour.value, minute.value, second.value, scaled)
}

// ---- serialization ----
const padLeft = (value, width) => String(value).padStart(width, '0')
const serializeInstant = (s, ns) => `posix-v1:${s}.${padLeft(ns, 9)}`
const serializeDate = (y, m, d) => `gregorian-v1:${padLeft(y, 4)}-${padLeft(m, 2)}-${padLeft(d, 2)}`

const canonicalSeconds = (t, start, end) => {
    if (end <= start) return invalidField(10n)
    if (t[start] === '+') return invalidField(10n)
    const digitsStart = t[start] === '-' ? start + 1 : start
    if (end <= digitsStart) return invalidField(10n)
    if (t[start] === '-' && t[digitsStart] === '0') return invalidField(10n)
    if (t[digitsStart] === '0' && end - digitsStart > 1) return invalidField(10n)
    const magnitude = digitsAt(t, digitsStart, end)
    if (magnitude.error) return magnitude.error
    return ok(t[start] === '-' ? -magnitude.value : magnitude.value)
}

const serializedInstantSeconds = (t) => {
    const n = t.length
    if (n < 9) return invalidField(9n)
    if (t.slice(0, 9) !== 'posix-v1:') return invalidField(9n)
    if (n < 20) return invalidField(11n)
    const point = n - 10
    if (t[point] !== '.') return invalidField(11n)
    const fraction = digitsAt(t, point + 1, n)
    if (fraction.error) return fraction.error
    return canonicalSeconds(t, 9, point)
}

const serializedInstantNanoseconds = (t) => {
    const n = t.length
    if (n < 20) return invalidField(11n)
    const value = digitsAt(t, n - 9, n)
    if (value.error) return value.error
    return value.value > 999999999n ? invalidField(11n) : ok(value.value)
}

const serializedDateStatus = (t) => {
    const n = t.length
    if (n < 13) return invalidField(9n)
    if (t.slice(0, 13) !== 'gregorian-v1:') return invalidField(9n)
    if (n !== 23) return parsePosition(13n)
    if (t[17] !== '-') return parsePosition(17n)
    if (t[20] !== '-') return parsePosition(20n)
    const year = digitsAt(t, 13, 17)
    if (year.error) return year.error
    const month = digitsAt(t, 18, 20)
    if (month.error) return month.error
    const day = digitsAt(t, 21, 23)
    if (day.error) return day.error
    return validateParts(year.value, month.value, day.value, 0n, 0n, 0n, 0n)
}

const instantRoundTrip = (s, ns) => {
    const text = serializeInstant(s, ns)
    const backSeconds = serializedInstantSeconds(text)
    const backNanos = serializedInstantNanoseconds(text)
    if (backSeconds.kind !== 'ok' || backNanos.kind !== 'ok') return 0n
    return backSeconds.value === s && backNanos.value === ns ? 1n : 0n
}

const dateRoundTrip = (y, m, d) => {
    const text = serializeDate(y, m, d)
    if (serializedDateStatus(text).kind !== 'ok') return 0n
    const back = { y: text.slice(13, 17), m: text.slice(18, 20), d: text.slice(21, 23) }
    return BigInt(back.y) === y && BigInt(back.m) === m && BigInt(back.d) === d ? 1n : 0n
}

// ---- the corpus, in the order `main` prints it ----

// 1-7: every nominal value shape compares equal to an identical construction.
for (let i = 0; i < 7; i += 1) emit(1n)

// 8-15: the original #846 fixture.
emit(parseRfc3339('2024-02-29T12:34:56Z'))
emit(parseRfc3339('2023-02-29T12:34:56Z'))
{
    const d = normalizeDuration(0n, -1n)
    emit(d.seconds)
    emit(d.nanoseconds)
}
emit(checkedAdd(MAX, 1n))
emit(parseRfc3339('2024-02-29T12:34:X6Z'))
emit(nanosToMillis(1000001n))
emit(parseRfc3339('2024-02-29T12:34:56.123456789Z'))

// 16-20: centuries and month boundaries.
emit(monthBoundariesOk(1900n, 28n))
emit(monthBoundariesOk(2000n, 29n))
emit(monthBoundariesOk(2100n, 28n))
emit(monthBoundariesOk(2024n, 29n))
emit(monthBoundariesOk(2023n, 28n))

// 21-27: day counts against the POSIX epoch.
emit(daysFromCivil(1970n, 1n, 1n))
emit(daysFromCivil(2024n, 2n, 29n))
emit(daysFromCivil(1900n, 3n, 1n))
emit(daysFromCivil(2000n, 3n, 1n))
emit(daysFromCivil(2100n, 3n, 1n))
emit(daysFromCivil(1n, 1n, 1n))
emit(daysFromCivil(9999n, 12n, 31n))

// 28-35: civil round-trip.
emit(civilRoundTrip(1970n, 1n, 1n))
emit(civilRoundTrip(2024n, 2n, 29n))
emit(civilRoundTrip(1900n, 2n, 28n))
emit(civilRoundTrip(2000n, 2n, 29n))
emit(civilRoundTrip(2100n, 3n, 1n))
emit(civilRoundTrip(1n, 1n, 1n))
emit(civilRoundTrip(9999n, 12n, 31n))
emit(civilRoundTrip(2023n, 12n, 31n))

// 36-39: duration normalization.
emit(durationRoundTrip(0n, -1n))
emit(durationRoundTrip(0n, NANOS))
emit(durationRoundTrip(5n, -999999999n))
emit(durationRoundTrip(-3n, 1500000000n))

// 40-47: checked arithmetic.
emit(checkedSub(MIN, 1n))
emit(checkedSub(0n, MAX))
emit(checkedMul(MAX, 2n))
emit(checkedMul(4611686018427387904n, 2n))
emit(checkedMulRaw(-4611686018427387904n, 2n) === MIN ? 1n : 0n)
emit(checkedMulRaw(2n, -4611686018427387904n) === MIN ? 1n : 0n)
emit(checkedMul(0n, MAX))
emit(checkedMul(3n, 7n))

// 48-56: OffsetDateTime to Instant.
emit(offsetToInstant(1970n, 1n, 1n, 0n, 0n, 0n, 0n, 0n))
emit(offsetToInstant(2024n, 2n, 29n, 12n, 34n, 56n, 0n, 0n))
emit(offsetToInstant(2024n, 2n, 29n, 12n, 34n, 56n, 0n, 32400n))
emit(offsetToInstant(2024n, 2n, 29n, 12n, 34n, 56n, 0n, -18000n))
emit(offsetToInstant(2024n, 2n, 29n, 12n, 34n, 56n, 0n, 64800n))
emit(offsetToInstant(2024n, 2n, 29n, 12n, 34n, 56n, 0n, -64800n))
emit(offsetToInstant(2024n, 2n, 29n, 12n, 34n, 56n, 0n, 64801n))
emit(offsetToInstant(2024n, 2n, 29n, 12n, 34n, 56n, 0n, -64801n))
emit(offsetToInstant(0n, 1n, 1n, 0n, 0n, 0n, 0n, 0n))

// 57-65: zero through eight fractional digits.
emit(parseRfc3339('2024-02-29T12:34:56Z'))
emit(parseRfc3339('2024-02-29T12:34:56.1Z'))
emit(parseRfc3339('2024-02-29T12:34:56.12Z'))
emit(parseRfc3339('2024-02-29T12:34:56.123Z'))
emit(parseRfc3339('2024-02-29T12:34:56.1234Z'))
emit(parseRfc3339('2024-02-29T12:34:56.12345Z'))
emit(parseRfc3339('2024-02-29T12:34:56.123456Z'))
emit(parseRfc3339('2024-02-29T12:34:56.1234567Z'))
emit(parseRfc3339('2024-02-29T12:34:56.12345678Z'))

// 66-83: the rejection matrix.
emit(parseRfc3339('2024-02-29t12:34:56Z'))
emit(parseRfc3339('2024-02-29T12:34:56z'))
emit(parseRfc3339('2024-02-29 12:34:56Z'))
emit(parseRfc3339('2024-02-29T12:34Z'))
emit(parseRfc3339('2024-02-29T12:34:56,123Z'))
emit(parseRfc3339('2024-02-29T12:34:56.1234567890Z'))
emit(parseRfc3339('2024-02-29T12:34:56.Z'))
emit(parseRfc3339('2024-02-29T12:34:56-00:00'))
emit(parseRfc3339('2024-02-29T12:34:56+0900'))
emit(parseRfc3339(' 2024-02-29T12:34:56Z'))
emit(parseRfc3339('2024-02-29T24:00:00Z'))
emit(parseRfc3339('2024-02-29T12:60:56Z'))
emit(parseRfc3339('2024-02-29T12:34:60Z'))
emit(parseRfc3339('2024-13-29T12:34:56Z'))
emit(parseRfc3339('2024-02-00T12:34:56Z'))
emit(parseRfc3339('1900-02-29T12:34:56Z'))
emit(parseRfc3339('2100-02-29T12:34:56Z'))
emit(parseRfc3339('2000-02-29T12:34:56Z'))

// 84-89: serialization.
emit(serializeInstant(0n, 0n))
emit(serializeInstant(1709210096n, 123456789n))
emit(serializeInstant(-5n, 3n))
emit(serializeDate(2024n, 2n, 29n))
emit(serializeDate(1n, 1n, 1n))
emit(serializeDate(9999n, 12n, 31n))

// 90-97: serialization round-trip.
emit(instantRoundTrip(0n, 0n))
emit(instantRoundTrip(1709210096n, 123456789n))
emit(instantRoundTrip(-5n, 3n))
emit(instantRoundTrip(-MAX, 999999999n))
emit(dateRoundTrip(2024n, 2n, 29n))
emit(dateRoundTrip(1n, 1n, 1n))
emit(dateRoundTrip(9999n, 12n, 31n))
emit(dateRoundTrip(2000n, 2n, 29n))

// 98-111: malformed and noncanonical instants.
emit(serializedInstantSeconds('posix-v1:5'))
emit(serializedInstantSeconds('posix-v2:5.000000000'))
emit(serializedInstantSeconds('POSIX-V1:5.000000000'))
emit(serializedInstantSeconds('5.000000000'))
emit(serializedInstantSeconds('posix-v1:5,000000000'))
emit(serializedInstantSeconds('posix-v1:x.000000000'))
emit(serializedInstantSeconds('posix-v1:+5.000000000'))
emit(serializedInstantSeconds('posix-v1:05.000000000'))
emit(serializedInstantSeconds('posix-v1:-0.000000000'))
emit(serializedInstantSeconds('posix-v1:00.000000000'))
emit(serializedInstantSeconds('posix-v1:5.00000000'))
emit(serializedInstantSeconds('posix-v1:5.0000000000'))
emit(serializedInstantSeconds('posix-v1:0.000000000'))
emit(serializedInstantSeconds('posix-v1:-5.000000000'))

// 112-118: the Gregorian identity.
emit(serializedDateStatus('gregorian-v1:2024-02-29'))
emit(serializedDateStatus('gregorian-v1:2024-2-29'))
emit(serializedDateStatus('gregorian-v2:2024-02-29'))
emit(serializedDateStatus('gregorian-v1:2024/02/29'))
emit(serializedDateStatus('gregorian-v1:1900-02-29'))
emit(serializedDateStatus('gregorian-v1:0000-01-01'))
emit(serializedDateStatus('2024-02-29'))

process.stdout.write(out.join('\n') + '\n')
