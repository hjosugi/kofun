#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CASES="$ROOT/tests/stdlib/tzdb"
ASSERT_CONTEXT="tzdb producer"
. "$ROOT/tests/assertions/assert.sh"
. "$ROOT/bootstrap/stage2/build.sh"

WORK=$(mktemp -d "${TMPDIR:-/tmp}/kofun-tzdb.XXXXXX")
trap 'rm -rf "$WORK"' 0 1 2 15
cc=${CC:-cc}

fail() {
    printf 'tzdb producer: FAIL: %s\n' "$*" >&2
    exit 1
}

producer="$CASES/tzdb.kofun"
expected="$CASES/tzdb.stdout"
readme="$CASES/README.md"

assert_regular_file 'Kofun tzdb producer' "$producer"
assert_regular_file 'tzdb exact golden' "$expected"
assert_regular_file 'tzdb boundary documentation' "$readme"

find "$CASES" -type f \( -name '*.py' -o -name '*.kf' \) >"$WORK/forbidden"
assert_file_empty 'forbidden Python or .kf source in the corpus' "$WORK/forbidden"
assert_not_grep 'producer imports an ambient dependency' -q -- '^import ' "$producer"
assert_not_grep 'producer names host time, file, zoneinfo, locale, network, or randomness' \
    -qE -- 'clock_gettime|nanosleep|fopen|open\(|zoneinfo|localtime|setlocale|socket\(|connect\(|random|rand\(' \
    "$producer"

command -v "$cc" >/dev/null 2>&1 || fail 'a C11 compiler is required'
kofun_stage2_build "$ROOT" "$WORK/kofun-stage2"

"$WORK/kofun-stage2" "$producer" "$WORK/tzdb.c" \
    "$WORK/tzdb.ir" "$WORK/tzdb.tokens" \
    >"$WORK/stage2.stdout" 2>"$WORK/stage2.stderr" ||
    fail "Stage 2 producer failed: $(cat "$WORK/stage2.stderr")"
assert_file_nonempty 'typed tzdb HIR' "$WORK/tzdb.ir"
assert_file_nonempty 'emitted tzdb C11 source' "$WORK/tzdb.c"

for type_name in Bytes20 ZoneId Transition TzdbMetadata Instant \
    LocalDateTime UtcOffset Tzdb LoadResult
do
    assert_grep "typed HIR carries $type_name" \
        -Fq -- "record|$type_name|" "$WORK/tzdb.ir"
done

for constructor in Unique Ambiguous Nonexistent ResolutionFailed \
    MalformedInput UnsupportedVersion DigestMismatch InvalidZone \
    TruncatedBytes TrailingBytes ArithmeticOverflow OversizedInput LimitExhausted
do
    assert_grep "typed HIR carries constructor $constructor" \
        -Fq -- "constructor|$constructor|" "$WORK/tzdb.ir"
done
assert_grep 'typed HIR carries the recursive byte digest' \
    -Fq -- 'function|digest_bytes|3|' "$WORK/tzdb.ir"
assert_grep 'typed HIR carries local-time resolution' \
    -Fq -- 'function|resolve_local|2|' "$WORK/tzdb.ir"

"$cc" -std=c11 -O2 -Wall -Wextra -Werror \
    "$WORK/tzdb.c" -o "$WORK/tzdb" ||
    fail 'emitted tzdb C11 source did not compile with warnings as errors'
assert_executable 'emitted tzdb C11 program' "$WORK/tzdb"

"$WORK/tzdb" >"$WORK/backend.stdout" 2>"$WORK/backend.stderr" ||
    fail "C11 backend failed: $(cat "$WORK/backend.stderr")"
cmp "$expected" "$WORK/backend.stdout" ||
    fail 'C11 backend output differs from the exact tzdb golden'

"$ROOT/bin/kofun" run "$producer" \
    >"$WORK/reference.stdout" 2>"$WORK/reference.stderr" ||
    fail "reference executor failed: $(cat "$WORK/reference.stderr")"
cmp "$expected" "$WORK/reference.stdout" ||
    fail 'reference executor output differs from the exact tzdb golden'

"$WORK/tzdb" >"$WORK/backend.second"
cmp "$WORK/backend.stdout" "$WORK/backend.second" ||
    fail 'two executions of the same tzdb binary differ'

assert_not_grep 'emitted C reaches ambient time, file, zoneinfo, locale, network, or randomness' \
    -qE -- 'time\.h|clock_gettime|gettimeofday|nanosleep|fopen|zoneinfo|localtime|setlocale|socket|connect|rand\(' \
    "$WORK/tzdb.c"
for record_name in Bytes20 ZoneId Transition TzdbMetadata Instant \
    LocalDateTime UtcOffset Tzdb LoadResult
do
    assert_grep "emitted C contains $record_name" \
        -Fq -- "KofunRecord_$record_name" "$WORK/tzdb.c"
done
assert_grep 'emitted C contains the closed enum runtime value' \
    -Fq -- 'KofunEnumValue' "$WORK/tzdb.c"
assert_grep 'emitted C computes the fixture digest in producer code' \
    -Fq -- 'kofun_fn_digest_bytes' "$WORK/tzdb.c"
assert_grep 'emitted C resolves local time in producer code' \
    -Fq -- 'kofun_fn_resolve_local' "$WORK/tzdb.c"

field() {
    sed -n "$1p" "$expected"
}

assert_eq 'load succeeded' "$(field 1)" '0'
assert_eq 'schema version is bound' "$(field 2)" '1'
assert_num 'content digest is nonzero' "$(field 3)" -gt 0
assert_eq 'ZoneId Test/GapFold code is preserved' "$(field 4)" '7'
assert_eq 'first transition UTC instant' "$(field 5)" '1000'
assert_eq 'first transition before offset' "$(field 6)" '0'
assert_eq 'first transition after offset' "$(field 7)" '100'
assert_eq 'second transition UTC instant' "$(field 8)" '2000'
assert_eq 'second transition before offset' "$(field 9)" '100'
assert_eq 'second transition after offset' "$(field 10)" '0'
assert_eq 'Instant pass/return/read observation' "$(field 11)" '1400'
assert_eq 'normal local result is Unique' "$(field 12)" '1'
assert_eq 'local 1500 maps to UTC 1400' "$(field 13)" '1400'
assert_eq 'gap result is Nonexistent' "$(field 14)" '3'
assert_eq 'gap next valid UTC instant' "$(field 15)" '1000'
assert_eq 'fold result is Ambiguous' "$(field 16)" '2'
assert_eq 'fold preserves earlier and later UTC instants' "$(field 17)" '195002050'

assert_eq 'malformed magic error code' "$(field 18)" '-1'
assert_eq 'malformed magic byte position' "$(field 19)" '0'
assert_eq 'unsupported version error code' "$(field 20)" '-2'
assert_eq 'unsupported version detail' "$(field 21)" '2'
assert_eq 'digest mismatch error code' "$(field 22)" '-3'
assert_eq 'digest mismatch reports observed digest' "$(field 23)" "$(field 3)"
assert_eq 'invalid zone error code' "$(field 24)" '-4'
assert_eq 'invalid zone detail' "$(field 25)" '8'
assert_eq 'truncated bytes error code' "$(field 26)" '-5'
assert_eq 'truncated bytes length' "$(field 27)" '17'
assert_eq 'trailing bytes error code' "$(field 28)" '-6'
assert_eq 'trailing byte count' "$(field 29)" '1'
assert_eq 'oversized input error code' "$(field 30)" '-8'
assert_eq 'oversized input length' "$(field 31)" '21'
assert_eq 'transition limit error code' "$(field 32)" '-9'
assert_eq 'transition limit detail' "$(field 33)" '3'
assert_eq 'arithmetic overflow error code' "$(field 34)" '-7'
assert_eq 'arithmetic overflow payload' "$(field 35)" '-7'

assert_num 'golden line count' "$(wc -l <"$expected" | tr -d ' ')" -eq 35

printf '%s\n' \
    'tzdb injected Bytes and versioned digest: PASS' \
    'tzdb normal, gap, and fold resolution: PASS' \
    'tzdb malformed, version, digest, zone, truncation, trailing, overflow, size, and limit errors: PASS' \
    'tzdb reference and C11 backend observations: PASS'
