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

canonical="$ROOT/stdlib/tzdb/tzdb.kofun"
assert_regular_file 'canonical tzdb surface' "$canonical"

for declaration in \
    'type ZoneId = {' \
    'type UtcOffset = {' \
    'type Instant = {' \
    'type LocalDateTime = {' \
    'type Transition = {' \
    'type Tzdb = {' \
    'type TzdbError =' \
    'type Resolution =' \
    'fn tzdb_read(input: Bytes)' \
    'fn tzdb_resolve_local(' \
    'fn tzdb_resolve_instant('
do
    assert_grep 'canonical tzdb surface lost a declaration' \
        -Fq -- "$declaration" "$canonical"
done

# The whole contract rests on this: the two cases that name more than one
# offset carry both of them, rather than handing back one as the answer.
assert_grep 'canonical Ambiguous no longer carries both offsets' -Fq -- \
    '| Ambiguous(earlier: UtcOffset, later: UtcOffset)' "$canonical"
assert_grep 'canonical Nonexistent no longer carries the offsets either side' \
    -Fq -- '| Nonexistent(before: UtcOffset, after: UtcOffset)' "$canonical"

# The canonical file is still ahead of the compiler. Pinning that keeps the
# corpus honest: the executable evidence is the producer, not this.
if "$ROOT/bin/kofun" check "$canonical" \
    >"$WORK/canonical.stdout" 2>"$WORK/canonical.stderr"
then
    fail "canonical source unexpectedly claimed executable codegen: $canonical"
fi
assert_grep 'canonical source did not stop at the documented compiler boundary' \
    -Fq -- 'error[E2S02]: expected top-level `fn` or `type`' \
    "$WORK/canonical.stderr"

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

# Independence from ambient time-zone and locale state, demonstrated rather
# than grepped for. The greps prove the symbols are not named; these runs prove
# the behaviour, which is the claim that matters to a caller. The empty
# environment is included because a program that needed TZ would more likely
# fall back than fail, and a fallback would not show up as a difference between
# two hostile settings.
TZ=Pacific/Kiritimati LC_ALL=C LANG=C "$WORK/tzdb" >"$WORK/hostile.stdout"
cmp "$WORK/backend.stdout" "$WORK/hostile.stdout" ||
    fail 'output changed under TZ=Pacific/Kiritimati'
TZ=America/Sao_Paulo LC_ALL=tr_TR.UTF-8 LANG=tr_TR.UTF-8 "$WORK/tzdb" \
    >"$WORK/hostile.locale.stdout"
cmp "$WORK/backend.stdout" "$WORK/hostile.locale.stdout" ||
    fail 'output changed under a different time zone and locale'
env -i "$WORK/tzdb" >"$WORK/bare.stdout"
cmp "$WORK/backend.stdout" "$WORK/bare.stdout" ||
    fail 'output changed with an empty environment'

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

# The edges of the gap and the fold. Nothing above reads one: the resolver gets
# them right — `local >= start && local < end` for both — but an off-by-one at
# an edge is the classic way an hour goes missing, and until now nothing pinned
# it. Half-open at the top means the first local second of an interval is
# inside it and the first second after is not.
assert_eq 'the gap includes its low edge' "$(field 36)" '3'
assert_eq 'the gap excludes its high edge' "$(field 37)" '1'
assert_eq 'the second past the gap maps through the new offset' \
    "$(field 38)" '1000'
assert_eq 'the fold includes its low edge' "$(field 39)" '2'
assert_eq 'the fold excludes its high edge' "$(field 40)" '1'
assert_eq 'the second past the fold maps through the new offset' \
    "$(field 41)" '2100'

# Provenance per result, not once per run. Fields 2 and 3 carry the version and
# digest for the whole run; a caller holding one answer holds neither, and two
# offsets computed under different rule versions are otherwise the same number.
assert_eq 'a normal resolution carries zone, version, and digest' \
    "$(field 42) $(field 43) $(field 44)" "7 1 $(field 3)"
assert_eq 'the normal resolution is still Unique at the same instant' \
    "$(field 45) $(field 46)" '1 1400'
assert_eq 'a gap resolution carries zone, version, and digest' \
    "$(field 47) $(field 48) $(field 49)" "7 1 $(field 3)"
assert_eq 'the gap resolution is still Nonexistent with the same payload' \
    "$(field 50) $(field 51)" '3 1000'
assert_eq 'a fold resolution carries zone, version, and digest' \
    "$(field 52) $(field 53) $(field 54)" "7 1 $(field 3)"
assert_eq 'the fold resolution still packs both instants' \
    "$(field 55) $(field 56)" '2 195002050'

# Drift, observed rather than described. The same local reading is asked of two
# rule sets differing only in when the first transition happens. Both answers
# are correct for their own rules; without the digest travelling with them they
# are indistinguishable.
assert_eq 'the second rule set is the same zone and format version' \
    "$(field 57) $(field 58)" '7 1'
assert_ne 'moving a transition changes the digest every result carries' \
    "$(field 59)" "$(field 3)"
assert_eq 'the same local reading is an ordinary one under the moved rules' \
    "$(field 60) $(field 61)" '1 1050'
assert_ne 'and it is a different answer from the one the shipped rules give' \
    "$(field 60)" "$(field 50)"

assert_num 'golden line count' "$(wc -l <"$expected" | tr -d ' ')" -eq 61

# A local reading and an instant are both one number. Keeping them apart is the
# whole reason the resolution sum has anywhere to live, so the toolchain must
# refuse to confuse them — at `check`, not only once the backend runs.

expect_rejected() {
    stem=$1
    reason=$2

    if "$ROOT/bin/kofun" check "$CASES/$stem.kofun" \
        >"$WORK/$stem.check.stdout" 2>"$WORK/$stem.check.stderr"
    then
        fail "$stem passed \`kofun check\`; the separate tzdb types did not stop it"
    fi
    assert_grep "$stem was rejected by check for the wrong reason" \
        -Fq -- "$reason" "$WORK/$stem.check.stderr"

    if "$ROOT/bin/kofun" build "$CASES/$stem.kofun" -o "$WORK/$stem" \
        >"$WORK/$stem.stdout" 2>"$WORK/$stem.stderr"
    then
        fail "$stem built after check refused it"
    fi
    assert_grep "$stem was rejected by build for the wrong reason" \
        -Fq -- "$reason" "$WORK/$stem.stderr"
    assert_absent "$stem emitted a binary despite being refused" "$WORK/$stem"

    printf 'tzdb: refused by check and build: %s\n' "$stem"
}

expect_rejected mixed_local_instant \
    'error[E2S32]: nominal record binding has the wrong type'
expect_rejected local_wall_seconds_on_instant \
    'error[E2S32]: unknown nominal record field read'

printf '%s\n' \
    'tzdb injected Bytes and versioned digest: PASS' \
    'tzdb normal, gap, and fold resolution: PASS' \
    'tzdb malformed, version, digest, zone, truncation, trailing, overflow, size, and limit errors: PASS' \
    'tzdb gap and fold edges are half-open at the top: PASS' \
    'tzdb every serialized resolution carries its zone, version, and digest: PASS' \
    'tzdb two rule sets give different answers, told apart by the digest: PASS' \
    'tzdb local readings and instants cannot be confused: PASS' \
    'tzdb bytes do not move under a hostile TZ, locale, or an empty environment: PASS' \
    'tzdb reference and C11 backend observations: PASS'
