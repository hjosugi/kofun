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

cmp "$date_time_dir/tests/date_time.stdout" "$work/date_time.stdout" ||
    fail 'Stage 2 stdout differs from the exact date/time golden'
cmp "$date_time_dir/tests/date_time.stderr" "$work/date_time.stderr" ||
    fail 'Stage 2 stderr differs from the exact date/time golden; R010 must never escape'

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
require_line "$work/date_time.ir" 'function|parse_digits|4|' \
    'recursive parser loop typed HIR function'
require_line "$work/date_time.c" \
    'static KofunEnumValue kofun_fn_parse_rfc3339(const char *' \
    'Text parser executable C11 signature'
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

printf '%s\n' \
    'date/time seven nominal value shapes: PASS' \
    'date/time closed typed outcomes: PASS' \
    'date/time checked arithmetic without R010: PASS' \
    'date/time strict RFC 3339 recursive parser: PASS' \
    'date/time leap, invalid, negative, overflow, and position fixture: PASS'
