#!/bin/sh
set -eu

date_time_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
repo_dir=$(CDPATH= cd -- "$date_time_dir/../.." && pwd)
work=${TMPDIR:-/tmp}/kofun-date-time-verify.$$
cc=${CC:-cc}
. "$repo_dir/bootstrap/stage2/build.sh"

mkdir -p "$work"
trap 'rm -rf "$work"' EXIT HUP INT TERM

fail() {
    printf 'date/time producer: FAIL: %s\n' "$*" >&2
    exit 1
}

require_line() {
    file=$1
    needle=$2
    label=$3
    grep -Fq "$needle" "$file" || fail "$label is missing: $needle"
}

# Every observation names the input it came from and prints what was seen, so a
# regression reports the defect instead of "the goldens differ" (#814, #838).
# The exact-golden `cmp` still runs below as the backstop.
expect_line() {
    expect_at=$1
    expect_want=$2
    expect_label=$3
    expect_got=$(sed -n "${expect_at}p" "$work/date_time.stdout")
    [ "$expect_got" = "$expect_want" ] || fail \
        "$expect_label (stdout line $expect_at: wanted $expect_want, observed ${expect_got:-<no line>})"
}

# `observe` folds `ParsePosition(p)` to `300 + p`, so a rejection assertion
# states the byte it demands and lets the helper do that arithmetic. Naming the
# byte is the point of the matrix: "rejected" alone would pass even if the
# parser blamed the wrong character.
expect_reject_at() {
    reject_at=$1
    reject_byte=$2
    reject_label=$3
    expect_line "$reject_at" "$((300 + reject_byte))" \
        "$reject_label was not refused at byte $reject_byte"
}

command -v "$cc" >/dev/null 2>&1 || fail 'a C11 compiler is required'
kofun_stage2_build "$repo_dir" "$work/kofun-stage2"

"$work/kofun-stage2" \
    "$date_time_dir/date_time.kofun" \
    "$work/date_time.c" \
    "$work/date_time.ir" \
    "$work/date_time.tokens" >/dev/null

"$cc" -std=c11 -O2 -Wall -Wextra -Werror \
    "$work/date_time.c" -o "$work/date_time"
"$work/date_time" \
    >"$work/date_time.stdout" \
    2>"$work/date_time.stderr"

# Checked first: an unguarded Text read aborts with R020 and would truncate
# stdout, which would otherwise surface as a confusing per-line mismatch.
cmp "$date_time_dir/tests/date_time.stderr" "$work/date_time.stderr" ||
    fail 'Stage 2 stderr differs from the exact date/time golden; R010/R020 must never escape'

# Value, arithmetic, and canonical-UTC evidence carried over from #846.
expect_line 8 100 \
    'the canonical UTC form 2024-02-29T12:34:56Z did not parse to DateTimeOk(0)'
expect_line 9 203 \
    'February 29 of the common year 2023 did not report InvalidField(day=3)'
expect_line 10 -1 \
    'a negative duration did not normalize its seconds to -1'
expect_line 11 999999999 \
    'a negative duration did not normalize its nanoseconds to 999999999'
expect_line 12 501 \
    'maximum-plus-one did not report ArithmeticOverflow(addition=1)'
expect_reject_at 13 17 'a non-digit inside time-second'
expect_line 14 401 \
    'an inexact nanosecond-to-millisecond conversion did not carry remainder 1'
expect_line 15 123456889 \
    'nine fractional digits did not accumulate to 123456789 nanoseconds (observe folds DateTimeOk(v) to 100 + v)'

# Accepted time-offset forms (#645). Each string is observed twice: once through
# the nanosecond projection to show the parse succeeded, once through the offset
# projection to show which offset it decoded to. "Did not fail" alone would not
# distinguish +09:00 from -05:30.
expect_line 119 100 \
    'the offset form 2024-02-29T12:34:56+09:00 was not accepted'
expect_line 120 32500 \
    '+09:00 did not decode to 32400 offset seconds (observe folds DateTimeOk(v) to 100 + v)'
expect_line 121 -19700 \
    '-05:30 did not decode to -19800 offset seconds'
expect_line 122 100 \
    '+00:00 did not decode to 0 offset seconds; RFC 3339 4.3 makes it UTC, unlike -00:00'
expect_line 123 100 \
    'the Z terminator did not decode to 0 offset seconds'
expect_line 124 86440 \
    'the maximal RFC 3339 offset +23:59 did not decode to 86340 offset seconds'
expect_line 125 500000100 \
    'one fractional digit before an offset did not scale to 500000000 nanoseconds'
expect_line 126 -19700 \
    'an offset following a fraction did not decode to -19800 seconds'
expect_line 127 123456889 \
    'nine fractional digits before an offset did not accumulate to 123456789 nanoseconds'
expect_line 128 32500 \
    '+09:00 following nine fractional digits did not decode to 32400 seconds'

# The rejection matrix required by #645: each form must fail at one exact byte.
expect_reject_at 129 0 'leading whitespace'
expect_reject_at 130 10 'a space in place of the T separator'
expect_reject_at 131 10 'a lowercase t separator'
expect_reject_at 132 19 'a lowercase z terminator'
expect_reject_at 133 16 'a missing time-second field'
expect_reject_at 134 19 'a comma in place of the fraction point'
expect_reject_at 135 29 'a tenth fractional digit'
expect_reject_at 136 11 'the hour 24'
expect_reject_at 137 17 'the second 60'
expect_reject_at 138 22 'a non-colon offset separator'
expect_reject_at 139 19 'the unknown-offset form -00:00'
expect_reject_at 140 20 'trailing whitespace'

# Neighbouring defects that share those code paths, kept so that a fix to one
# matrix row cannot quietly move another row's byte.
expect_reject_at 141 20 'a fraction point with no digit after it'
expect_reject_at 142 20 'the offset hour 24'
expect_reject_at 143 23 'the offset minute 60'
expect_reject_at 144 24 'a truncated offset minute'
expect_reject_at 145 25 'a trailing byte after a complete offset'
expect_reject_at 146 19 'a missing time-offset'
expect_reject_at 147 4 'input that ends inside the date'

# The constructed-value path keeps the field-ordinal vocabulary: Ints have no
# byte to blame, so hour 24 is InvalidField(4) here and byte 11 in text.
expect_line 148 889 \
    'a valid civil tuple did not validate to DateTimeOk(789 nanoseconds)'
expect_line 149 204 \
    'the constructed hour 24 did not report InvalidField(hour=4)'

cmp "$date_time_dir/tests/date_time.stdout" "$work/date_time.stdout" ||
    fail 'Stage 2 stdout differs from the exact date/time golden'

for record in Duration Instant Date TimeOfDay LocalDateTime UtcOffset OffsetDateTime
do
    require_line "$work/date_time.ir" "record|$record|" \
        "$record typed HIR declaration"
    require_line "$work/date_time.c" "KofunRecord_$record" \
        "$record executable C11 shape"
done

for constructor in \
    DateTimeOk InvalidField ParsePosition InexactConversion ArithmeticOverflow
do
    require_line "$work/date_time.ir" "constructor|$constructor|DateTimeOutcome|" \
        "$constructor closed outcome payload"
done

require_line "$work/date_time.ir" 'function|parse_rfc3339|1|' \
    'RFC 3339 parser typed HIR function'
require_line "$work/date_time.ir" 'function|days_from_civil|3|' \
    'Gregorian day-count typed HIR function'
require_line "$work/date_time.ir" 'function|civil_from_days|1|' \
    'Gregorian inverse typed HIR function'
require_line "$work/date_time.ir" 'function|offset_date_time_to_instant|1|' \
    'OffsetDateTime-to-Instant typed HIR function'
require_line "$work/date_time.ir" 'function|checked_sub|2|' \
    'checked subtraction typed HIR function'
require_line "$work/date_time.ir" 'function|checked_mul|2|' \
    'checked multiplication typed HIR function'
require_line "$work/date_time.ir" 'function|parse_rfc3339_offset|1|' \
    'RFC 3339 offset projection typed HIR function'
require_line "$work/date_time.ir" 'function|parse_digits|5|' \
    'recursive parser loop typed HIR function, bounds-checked by input length'
require_line "$work/date_time.ir" 'function|parse_offset|5|' \
    'time-offset dispatch typed HIR function'
require_line "$work/date_time.ir" 'function|parse_offset_hour|6|' \
    'offset hour typed HIR function'
require_line "$work/date_time.ir" 'function|parse_offset_minute|7|' \
    'offset minute typed HIR function'
require_line "$work/date_time.ir" 'function|parse_fraction|6|' \
    'digit-at-a-time secfrac scanner typed HIR function'
require_line "$work/date_time.ir" 'function|validate_date|3|' \
    'shared Gregorian calendar rule typed HIR function'
require_line "$work/date_time.c" \
    'static KofunEnumValue kofun_fn_parse_rfc3339(const char *' \
    'Text parser executable C11 signature'
require_line "$work/date_time.c" \
    'static KofunEnumValue kofun_fn_parse_rfc3339_offset(const char *' \
    'Text offset projection executable C11 signature'
require_line "$work/date_time.c" 'strcmp(kofun_condition_left, kofun_condition_right)' \
    'Text comparison lowering'
require_line "$work/date_time.c" 'kofun_text_slice(' \
    'bounded Text slicing runtime'

[ "$(sed -n '8p' "$work/date_time.stdout")" -eq 100 ] ||
    fail 'leap-day success did not match DateTimeOk(0)'
[ "$(sed -n '9p' "$work/date_time.stdout")" -eq 203 ] ||
    fail 'invalid date did not match InvalidField(day=3)'
[ "$(sed -n '10p' "$work/date_time.stdout")" -eq -1 ] ||
    fail 'negative duration did not normalize seconds to -1'
[ "$(sed -n '11p' "$work/date_time.stdout")" -eq 999999999 ] ||
    fail 'negative duration did not normalize nanoseconds to 999999999'
[ "$(sed -n '12p' "$work/date_time.stdout")" -eq 501 ] ||
    fail 'maximum-plus-one did not match ArithmeticOverflow(addition=1)'
[ "$(sed -n '13p' "$work/date_time.stdout")" -eq 317 ] ||
    fail 'RFC 3339 invalid second did not report byte position 17'
[ "$(sed -n '14p' "$work/date_time.stdout")" -eq 401 ] ||
    fail 'inexact nanosecond conversion did not carry remainder 1'
[ "$(sed -n '15p' "$work/date_time.stdout")" -eq 123456889 ] ||
    fail 'nine fractional digits did not accumulate to 123456789 nanoseconds'

# Each assertion below names the exact line it reads, so a reordered `main`
# fails loudly here instead of silently checking a different observation.
require_value() {
    line=$1
    expected=$2
    label=$3
    actual=$(sed -n "${line}p" "$work/date_time.stdout")
    [ "$actual" = "$expected" ] ||
        fail "$label: line $line was $actual, expected $expected"
}

# Century rule: 1900 and 2100 are common, 2000 is leap, and every month length
# of each year is checked individually rather than summed.
require_value 16 1 '1900 month boundaries'
require_value 17 1 '2000 month boundaries'
require_value 18 1 '2100 month boundaries'
require_value 19 1 '2024 month boundaries'
require_value 20 1 '2023 month boundaries'

# Day-count vectors against the POSIX epoch, including both range ends.
require_value 21 0 '1970-01-01 is day zero'
require_value 22 19782 '2024-02-29 day count'
require_value 23 -25508 '1900-03-01 day count crosses a common century'
require_value 24 11017 '2000-03-01 day count crosses a leap century'
require_value 25 47541 '2100-03-01 day count crosses a common century'
require_value 26 -719162 '0001-01-01 lower range end'
require_value 27 2932896 '9999-12-31 upper range end'

for line in 28 29 30 31 32 33 34 35
do
    require_value "$line" 1 "civil round-trip at line $line"
done

for line in 36 37 38 39
do
    require_value "$line" 1 "duration normalization round-trip at line $line"
done

require_value 40 502 'subtracting past the negative bound is ArithmeticOverflow(2)'
require_value 41 -9223372036854775707 'representable subtraction is not rejected'
require_value 42 503 'multiplying past the positive bound is ArithmeticOverflow(3)'
require_value 43 503 'maximum-plus-one product is ArithmeticOverflow(3)'
require_value 44 1 'the representable negative-bound product is accepted'
require_value 45 1 'the representable negative-bound product is order-independent'
require_value 46 100 'multiplication by zero short-circuits'
require_value 47 121 'an in-range product is exact'

# One OffsetDateTime denotes exactly one Instant; the offset is subtracted.
require_value 48 100 'the epoch maps to Instant zero'
require_value 49 1709210196 'UTC OffsetDateTime maps to its exact Instant'
require_value 50 1709177796 'a +09:00 offset subtracts 32400 seconds'
require_value 51 1709228196 'a -05:00 offset adds 18000 seconds'
require_value 52 1709145396 'the +18:00 extreme is representable'
require_value 53 1709274996 'the -18:00 extreme is representable'
require_value 54 208 'one second past +18:00 is InvalidField(8)'
require_value 55 208 'one second past -18:00 is InvalidField(8)'
require_value 56 201 'year 0 is outside the proleptic range as InvalidField(1)'

# Zero through nine fractional digits each scale to canonical nanoseconds.
require_value 57 100 'zero fractional digits'
require_value 58 100000100 'one fractional digit scales to 1e8 nanoseconds'
require_value 59 120000100 'two fractional digits'
require_value 60 123000100 'three fractional digits'
require_value 61 123400100 'four fractional digits'
require_value 62 123450100 'five fractional digits'
require_value 63 123456100 'six fractional digits'
require_value 64 123456800 'seven fractional digits'
require_value 65 123456880 'eight fractional digits'

# Every rejected form fails at an exact byte position or an exact field id.
require_value 66 310 'lowercase date/time separator fails at byte 10'
require_value 67 319 'lowercase zulu fails at byte 19'
require_value 68 310 'a space separator fails at byte 10'
require_value 69 316 'a missing seconds field fails at byte 16, where the colon is missing'
require_value 70 319 'a comma fraction separator fails at byte 19'
require_value 71 329 'more than nine fractional digits fails at the tenth digit, byte 29'
require_value 72 320 'an empty fraction fails at byte 20'
require_value 73 319 'a -00:00 offset is not the accepted UTC spelling'
require_value 74 322 'a non-colon offset separator fails at byte 22, where the separator is'
require_value 75 300 'leading whitespace fails at byte 0'
require_value 76 311 'hour 24 in parsed text fails at byte 11'
require_value 77 314 'minute 60 in parsed text fails at byte 14'
require_value 78 317 'second 60 in parsed text fails at byte 17'
require_value 79 202 'month 13 is InvalidField(2)'
require_value 80 203 'day 0 is InvalidField(3)'
require_value 81 203 '1900-02-29 is rejected because 1900 is common'
require_value 82 203 '2100-02-29 is rejected because 2100 is common'
require_value 83 100 '2000-02-29 is accepted because 2000 is leap'

# Serialization carries its identity in the text.
require_value 84 'posix-v1:0.000000000' 'the epoch serializes canonically'
require_value 85 'posix-v1:1709210096.123456789' 'subsecond serialization'
require_value 86 'posix-v1:-5.000000003' 'negative seconds serialize canonically'
require_value 87 'gregorian-v1:2024-02-29' 'a civil date carries the Gregorian identity'
require_value 88 'gregorian-v1:0001-01-01' 'the lower year end pads to four digits'
require_value 89 'gregorian-v1:9999-12-31' 'the upper year end'

for line in 90 91 92 93 94 95 96 97
do
    require_value "$line" 1 "serialization round-trip at line $line"
done

# Malformed serialized text names the field that is actually wrong.
require_value 98 211 'a missing fraction is InvalidField(11), not an identity fault'
require_value 99 209 'a wrong identity version is InvalidField(9)'
require_value 100 209 'an upper-case identity is InvalidField(9)'
require_value 101 209 'a missing identity is InvalidField(9)'
require_value 102 211 'a comma fraction separator is InvalidField(11)'
require_value 103 309 'a non-digit seconds field fails at byte 9'

# Noncanonical spellings are refused, not normalized: one value, one text.
require_value 104 210 'an explicit plus sign is InvalidField(10)'
require_value 105 210 'a leading zero is InvalidField(10)'
require_value 106 210 'negative zero is InvalidField(10)'
require_value 107 210 'a zero-padded zero is InvalidField(10)'
require_value 108 211 'eight fractional digits is InvalidField(11)'
require_value 109 211 'ten fractional digits is InvalidField(11)'
require_value 110 100 'canonical zero survives the leading-zero rule'
require_value 111 95 'canonical negative seconds survive the negative-zero rule'

require_value 112 100 'a canonical Gregorian identity is accepted'
require_value 113 313 'an unpadded month is a shape fault at byte 13'
require_value 114 209 'a wrong Gregorian identity version is InvalidField(9)'
require_value 115 317 'slash separators fail at byte 17'
require_value 116 203 '1900-02-29 is refused by the calendar, not by the shape'
require_value 117 201 'year 0000 is InvalidField(1)'
require_value 118 209 'a bare date carries no identity'

# Reference interpretation and the executable backend must agree on the whole
# corpus. tests/reference.mjs is a second implementation, not a reader of the
# golden: it recomputes every observation from the contract and shares no code
# with the producer. A golden compared against itself proves determinism, which
# was never in doubt; two implementations agreeing is what makes the corpus
# evidence of correctness. This already earned its place — it caught a real
# disagreement on a negative instant while it was being written.
if command -v node >/dev/null 2>&1
then
    node "$date_time_dir/tests/reference.mjs" >"$work/reference.stdout" ||
        fail 'the reference interpretation did not run'
    cmp "$work/reference.stdout" "$work/date_time.stdout" ||
        fail 'the reference interpretation and the Stage 2 C11 backend disagree'
    differential='date/time reference and C11 backend agree on the full corpus: PASS'

    # The comparison must be able to fail, or agreement proves nothing.
    sed '1s/.*/999999/' "$work/reference.stdout" >"$work/reference.mutated"
    if cmp -s "$work/reference.mutated" "$work/date_time.stdout"
    then
        fail 'the differential cannot detect a disagreeing reference'
    fi
else
    differential='date/time reference differential: SKIPPED (node is not installed)'
fi

# The contract's separated types are enforced by the compiler, not by a lint:
# passing a Duration where an Instant is required must not compile.
mix=$work/mixed_types.kofun
cat >"$mix" <<'MIXED'
type Duration = { seconds: Int, nanoseconds: Int }
type Instant = { seconds: Int, nanoseconds: Int }

fn take_instant(value: Instant) -> Int { return value.seconds }

fn main() -> Int {
    let span: Duration = Duration(seconds: 1, nanoseconds: 0)
    print(take_instant(span))
    return 0
}
MIXED
if "$work/kofun-stage2" "$mix" "$work/mixed.c" "$work/mixed.ir" \
    "$work/mixed.tokens" >"$work/mixed.diagnostic" 2>&1
then
    fail 'a Duration was accepted where an Instant is required'
fi
grep -q 'E2S32' "$work/mixed.diagnostic" ||
    fail 'mixing Duration and Instant did not fail with E2S32'

# No ambient clock, zone, or locale reaches the generated artifact. The leading
# alternation is a portable word boundary: `\b` is a GNU extension, and without
# one `time` would also match `clock_gettime`.
for symbol in clock_gettime time gettimeofday localtime gmtime mktime tzset \
    getenv setlocale
do
    grep -Eq "(^|[^a-zA-Z0-9_])$symbol *\(" "$work/date_time.c" &&
        fail "the generated C11 calls the ambient host routine $symbol"
done
grep -Fq '#include <time.h>' "$work/date_time.c" &&
    fail 'the generated C11 includes <time.h>'

# The check above must be able to fail, or it proves nothing.
probe=$work/ambient_probe.c
printf '#include <time.h>\nint main(void){struct timespec t;clock_gettime(0,&t);return 0;}\n' \
    >"$probe"
grep -Eq "(^|[^a-zA-Z0-9_])clock_gettime *\(" "$probe" ||
    fail 'the ambient-access check cannot detect a host clock call'

printf '%s\n' \
    'date/time seven nominal value shapes: PASS' \
    'date/time closed typed outcomes: PASS' \
    'date/time checked arithmetic without R010: PASS' \
    'date/time strict RFC 3339 recursive parser: PASS' \
    'date/time leap, invalid, negative, overflow, and position fixture: PASS' \
    'date/time 1900/2000/2100 centuries and every month boundary: PASS' \
    'date/time Gregorian day count and civil round-trip at both range ends: PASS' \
    'date/time checked addition, subtraction, and multiplication bounds: PASS' \
    'date/time OffsetDateTime maps to one exact Instant: PASS' \
    'date/time RFC 3339 zero-to-nine fractional digits: PASS' \
    'date/time RFC 3339 rejection matrix at exact positions: PASS' \
    'date/time Duration and Instant cannot be mixed implicitly: PASS' \
    'date/time reads no ambient clock, zone, or locale: PASS' \
    'date/time versioned serialization round-trips and refuses noncanonical text: PASS' \
    'date/time Z and +/-HH:MM offsets decode to exact offset seconds: PASS' \
    'date/time nineteen-form rejection matrix fails at exact bytes: PASS' \
    "$differential"
