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
expect_line 16 100 \
    'the offset form 2024-02-29T12:34:56+09:00 was not accepted'
expect_line 17 32500 \
    '+09:00 did not decode to 32400 offset seconds (observe folds DateTimeOk(v) to 100 + v)'
expect_line 18 -19700 \
    '-05:30 did not decode to -19800 offset seconds'
expect_line 19 100 \
    '+00:00 did not decode to 0 offset seconds; RFC 3339 4.3 makes it UTC, unlike -00:00'
expect_line 20 100 \
    'the Z terminator did not decode to 0 offset seconds'
expect_line 21 86440 \
    'the maximal RFC 3339 offset +23:59 did not decode to 86340 offset seconds'
expect_line 22 500000100 \
    'one fractional digit before an offset did not scale to 500000000 nanoseconds'
expect_line 23 -19700 \
    'an offset following a fraction did not decode to -19800 seconds'
expect_line 24 123456889 \
    'nine fractional digits before an offset did not accumulate to 123456789 nanoseconds'
expect_line 25 32500 \
    '+09:00 following nine fractional digits did not decode to 32400 seconds'

# The rejection matrix required by #645: each form must fail at one exact byte.
expect_reject_at 26 0 'leading whitespace'
expect_reject_at 27 10 'a space in place of the T separator'
expect_reject_at 28 10 'a lowercase t separator'
expect_reject_at 29 19 'a lowercase z terminator'
expect_reject_at 30 16 'a missing time-second field'
expect_reject_at 31 19 'a comma in place of the fraction point'
expect_reject_at 32 29 'a tenth fractional digit'
expect_reject_at 33 11 'the hour 24'
expect_reject_at 34 17 'the second 60'
expect_reject_at 35 22 'a non-colon offset separator'
expect_reject_at 36 19 'the unknown-offset form -00:00'
expect_reject_at 37 20 'trailing whitespace'

# Neighbouring defects that share those code paths, kept so that a fix to one
# matrix row cannot quietly move another row's byte.
expect_reject_at 38 20 'a fraction point with no digit after it'
expect_reject_at 39 20 'the offset hour 24'
expect_reject_at 40 23 'the offset minute 60'
expect_reject_at 41 24 'a truncated offset minute'
expect_reject_at 42 25 'a trailing byte after a complete offset'
expect_reject_at 43 19 'a missing time-offset'
expect_reject_at 44 4 'input that ends inside the date'

# The constructed-value path keeps the field-ordinal vocabulary: Ints have no
# byte to blame, so hour 24 is InvalidField(4) here and byte 11 in text.
expect_line 45 889 \
    'a valid civil tuple did not validate to DateTimeOk(789 nanoseconds)'
expect_line 46 204 \
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

printf '%s\n' \
    'date/time seven nominal value shapes: PASS' \
    'date/time closed typed outcomes: PASS' \
    'date/time checked arithmetic without R010: PASS' \
    'date/time strict RFC 3339 recursive parser: PASS' \
    'date/time leap, invalid, negative, overflow, and position fixture: PASS' \
    'date/time Z and +/-HH:MM offsets decode to exact offset seconds: PASS' \
    'date/time nineteen-form rejection matrix fails at exact bytes: PASS'
