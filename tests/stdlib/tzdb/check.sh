#!/bin/sh
set -eu

# Bounded tzdb producer gate for #878.
#
# Four things are checked, in this order:
#
#   1. the canonical surface in stdlib/tzdb/ still declares the contract, and
#      still stops at the documented compiler boundary rather than pretending
#      to be executable;
#   2. the executable producer runs identically on the reference interpreter
#      and the C11 backend, and specific decisions in its output are read
#      rather than accepted wholesale from a golden file;
#   3. the value shapes and both closed sums survive the typed-HIR projection,
#      so "checked source -> typed HIR -> executable backend" is observed;
#   4. the two mixing mistakes the separate local/instant types exist to
#      prevent are refused by the toolchain.
#
# Nothing here reads a host clock, a host zoneinfo file, TZ, or the network.
# That is asserted, not assumed: the emitted C is searched for the symbols that
# would reach them, and the producer is run twice and compared.

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CASES="$ROOT/tests/stdlib/tzdb"
ASSERT_CONTEXT="tzdb"
. "$ROOT/tests/assertions/assert.sh"

WORK=$(mktemp -d "${TMPDIR:-/tmp}/kofun-tzdb.XXXXXX")
trap 'rm -rf "$WORK"' 0 1 2 15

fail() {
    printf 'tzdb: FAIL: %s\n' "$*" >&2
    exit 1
}

require_line() {
    file=$1
    needle=$2
    label=$3
    assert_grep "$label" -Fq -- "$needle" "$file"
}

# ------------------------------------------------------- corpus hygiene

find "$CASES" -type f \( -name '*.py' -o -name '*.kf' \) >"$WORK/forbidden"
assert_file_empty 'forbidden Python or .kf source in the corpus' \
    "$WORK/forbidden"

# ---------------------------------------------------- canonical surface

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
    'type ResolvedLocal = {' \
    'fn tzdb_digest(' \
    'fn tzdb_read(' \
    'fn tzdb_zone(' \
    'fn tzdb_transitions(' \
    'fn tzdb_interval(' \
    'fn tzdb_resolve_local(' \
    'fn tzdb_resolve_instant(' \
    'fn tzdb_local_from_instant(' \
    'fn tzdb_serialize('
do
    require_line "$canonical" "$declaration" \
        'canonical tzdb surface lost a declaration'
done

for variant in MalformedHeader UnsupportedVersion DigestMismatch InvalidZone \
    TruncatedInput TrailingBytes TzdbArithmeticOverflow OversizedInput \
    LimitExhausted
do
    require_line "$canonical" "| $variant" \
        'canonical TzdbError lost a closed variant'
done

# The whole contract rests on this: a local reading resolves to one of three
# cases, and the two that name more than one offset carry both of them.
require_line "$canonical" '| Unique(offset: UtcOffset)' \
    'canonical Resolution lost its unique case'
require_line "$canonical" '| Ambiguous(earlier: UtcOffset, later: UtcOffset)' \
    'canonical Ambiguous no longer carries both offsets'
require_line "$canonical" '| Nonexistent(before: UtcOffset, after: UtcOffset)' \
    'canonical Nonexistent no longer carries the offsets either side'

# The reader takes bytes handed to it, and nothing else.
require_line "$canonical" 'fn tzdb_read(input: Bytes)' \
    'canonical reader no longer takes injected Bytes'

# Asserted against code, not against prose. These files explain at length what
# they do not reach for, and a grep over the whole text cannot tell the comment
# saying "does not consult TZ" from a line that consults it.
code_only() {
    sed 's/[[:space:]]*#.*$//' "$1"
}

code_only "$canonical" >"$WORK/canonical.code"
assert_not_grep 'the canonical tzdb surface reaches for ambient time-zone state' \
    -qE -- 'zoneinfo|/usr/share|getenv|"TZ"|tzset|localtime|clock_gettime' \
    "$WORK/canonical.code"

# The canonical file is still ahead of the compiler. Pinning that keeps the
# corpus honest: the executable evidence is the producer below, not this.
if "$ROOT/bin/kofun" check "$canonical" \
    >"$WORK/canonical.stdout" 2>"$WORK/canonical.stderr"
then
    fail "canonical source unexpectedly claimed executable codegen: $canonical"
fi
require_line "$WORK/canonical.stderr" \
    'error[E2S02]: expected top-level `fn` or `type`' \
    'canonical source did not stop at the documented compiler boundary'

# ------------------------------------------------------------- producer

producer="$CASES/tzdb.kofun"
expected="$CASES/tzdb.stdout"
assert_regular_file 'producer source' "$producer"
assert_regular_file 'producer golden' "$expected"

# The producer is self-contained on purpose: it cannot read rules it cannot
# name, and the only rules it names are the bytes written into it.
assert_not_grep 'the bounded producer imports a module and is no longer sealed' \
    -q -- '^import ' "$producer"
code_only "$producer" >"$WORK/producer.code"
assert_not_grep 'the bounded producer names ambient time or time-zone state' \
    -qE -- 'clock_gettime|gettimeofday|localtime|tzset|zoneinfo|getenv|fopen|__linux_syscall' \
    "$WORK/producer.code"

"$ROOT/bin/kofun" check "$producer" \
    >"$WORK/check.stdout" 2>"$WORK/check.stderr" ||
    fail "producer did not check: $(cat "$WORK/check.stderr")"

"$ROOT/bin/kofun" build "$producer" -o "$WORK/tzdb" \
    --emit-c "$WORK/tzdb.c" >"$WORK/build.stdout" 2>"$WORK/build.stderr" ||
    fail "producer did not build: $(cat "$WORK/build.stderr")"

"$WORK/tzdb" >"$WORK/backend.stdout"
cmp "$expected" "$WORK/backend.stdout" ||
    fail 'C11 backend output differs from the recorded tzdb decisions'

# The reference executor must agree with the backend on the whole artifact.
"$ROOT/bin/kofun" run "$producer" >"$WORK/reference.stdout" 2>"$WORK/run.stderr" ||
    fail "producer did not run on the reference executor: $(cat "$WORK/run.stderr")"
cmp "$expected" "$WORK/reference.stdout" ||
    fail 'reference executor and C11 backend disagree on the tzdb rules'

# Deterministic means deterministic: same binary, same bytes, twice. Run from
# a directory that holds nothing, so a run cannot pick anything up from here.
(cd "$WORK" && "$WORK/tzdb" >"$WORK/backend.second")
cmp "$WORK/backend.stdout" "$WORK/backend.second" ||
    fail 'repeated lookup and serialization over the same bytes differ'

# Independence from ambient time-zone and locale state, demonstrated rather
# than grepped for. The greps above prove the symbols are not named; these runs
# prove the behaviour, which is the claim that actually matters to a caller.
# An empty environment is included because a program that needed TZ would more
# likely fall back than fail, and a fallback is exactly what would not show up
# as a difference between two hostile settings.
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

# No ambient reach, checked against the code that actually runs.
assert_not_grep 'the emitted C reaches for host time or time-zone state' -qE -- \
    'time\.h|clock_gettime|gettimeofday|localtime|gmtime|tzset|tzname|getenv|fopen|/usr/share' \
    "$WORK/tzdb.c"

# ---------------------------------------------------------- typed HIR
#
# The Stage 2 semantic sidecar projects a bounded program and the producer is
# larger than that bound. typed_hir.kofun carries the same value shapes and the
# same two closed sums through the projection, so the source -> typed HIR ->
# backend path is observed on a source that names the tzdb types.

witness="$CASES/typed_hir.kofun"
witness_expected="$CASES/typed_hir.stdout"
assert_regular_file 'typed HIR witness' "$witness"
assert_regular_file 'typed HIR witness golden' "$witness_expected"

"$ROOT/bin/kofun" check "$witness" \
    --emit-typed-sidecar "$WORK/typed_hir-semantic.json" --generation 1 \
    >"$WORK/witness.stdout" 2>"$WORK/witness.stderr" ||
    fail "typed HIR witness did not check: $(cat "$WORK/witness.stderr")"
assert_file_nonempty 'typed sidecar' "$WORK/typed_hir-semantic.json"
require_line "$WORK/typed_hir-semantic.json" '"stage2-semantic-v1"' \
    'typed sidecar is not the Stage 2 semantic artifact'
require_line "$WORK/typed_hir-semantic.json" '"completeness": "complete"' \
    'typed sidecar is a partial projection'
for named in ZoneId UtcOffset Instant LocalDateTime Transition Tzdb \
    TzdbError ResolutionKind
do
    require_line "$WORK/typed_hir-semantic.json" "\"$named\"" \
        'typed HIR does not carry the tzdb type'
done

"$ROOT/bin/kofun" build "$witness" -o "$WORK/typed_hir" \
    >"$WORK/witness.build.stdout" 2>"$WORK/witness.build.stderr" ||
    fail "typed HIR witness did not build: $(cat "$WORK/witness.build.stderr")"
"$WORK/typed_hir" >"$WORK/witness.run.stdout"
cmp "$witness_expected" "$WORK/witness.run.stdout" ||
    fail 'typed HIR witness output differs from the recorded decisions'

# ------------------------------------------- recorded tzdb decisions
#
# Each block names the decision it reads, so a failure says which rule changed
# rather than only that a golden moved.
#
# The producer prints eleven header lines, then seven lines per resolution
# (zone, subject, kind, primary offset, secondary offset, version, digest),
# then two lines for the offset shape, then two lines per typed error.

field() {
    sed -n "$1,$2p" "$expected" | tr '\n' ' '
}

assert_eq 'the fixture reads, and its provenance is the bytes that were written' \
    "$(field 1 5)" '0 7 1 101 2 '
assert_eq 'transition 0 springs forward one hour at 10000 seconds' \
    "$(field 6 8)" '10000 0 3600 '
assert_eq 'transition 1 falls back one hour at 20000 seconds' \
    "$(field 9 11)" '20000 3600 0 '

# Kind 1 is Unique, 2 is Ambiguous, 3 is Nonexistent. Every line below carries
# the version and digest, so an answer cannot be read without its provenance.
assert_eq 'a local reading before any transition is unique at +00:00' \
    "$(field 12 18)" '7 5000 1 0 0 1 101 '
assert_eq 'a local reading inside the gap names no instant' \
    "$(field 19 25)" '7 12000 3 0 3600 1 101 '
assert_eq 'a local reading between the transitions is unique at +01:00' \
    "$(field 26 32)" '7 16000 1 3600 3600 1 101 '
assert_eq 'a local reading inside the fold names two, earlier first' \
    "$(field 33 39)" '7 21000 2 3600 0 1 101 '
assert_eq 'a local reading after both transitions is unique at +00:00' \
    "$(field 40 46)" '7 30000 1 0 0 1 101 '

# Half-open at the top: the first instant of a gap or fold is inside it, the
# first instant after is not. An off-by-one here is the classic way an hour
# goes missing, so both ends of both intervals are read.
assert_eq 'the gap includes its low boundary' \
    "$(field 47 53)" '7 10000 3 0 3600 1 101 '
assert_eq 'the gap excludes its high boundary' \
    "$(field 54 60)" '7 13600 1 3600 3600 1 101 '
assert_eq 'the fold includes its low boundary' \
    "$(field 61 67)" '7 20000 2 3600 0 1 101 '
assert_eq 'the fold excludes its high boundary' \
    "$(field 68 74)" '7 23600 1 0 0 1 101 '

# An instant already names one point on the UTC time line, including at the
# instant a transition takes effect, where the new offset is the one in force.
assert_eq 'an instant before any transition is unique at +00:00' \
    "$(field 75 81)" '7 5000 1 0 0 1 101 '
assert_eq 'an instant between the transitions is unique at +01:00' \
    "$(field 82 88)" '7 16000 1 3600 3600 1 101 '
assert_eq 'an instant at the fold transition takes the new offset' \
    "$(field 89 95)" '7 20000 1 0 0 1 101 '

assert_eq 'the fold hands back both offsets, not one of them' \
    "$(field 96 97)" '3600 0 '

# Every vector below is the good fixture with exactly one byte or one length
# changed, so the error is attributable to that one change. Distinct codes are
# the point: a reader that collapsed these into one "bad input" would leave a
# caller unable to tell a version it could upgrade from bytes it must refuse.
assert_eq 'a wrong magic byte reports its position' \
    "$(field 98 99)" '-1 0 '
assert_eq 'a non-zero reserved byte reports its position' \
    "$(field 100 101)" '-1 7 '
assert_eq 'an unsupported version reports the version found' \
    "$(field 102 103)" '-2 2 '
assert_eq 'an unknown zone reports the code, and never falls back to UTC' \
    "$(field 104 105)" '-4 8 '
assert_eq 'more transitions than the bound is limit exhaustion' \
    "$(field 106 107)" '-9 3 '
assert_eq 'a digest mismatch reports what was computed' \
    "$(field 108 109)" '-3 101 '
assert_eq 'an input that stops early is truncated, not padded' \
    "$(field 110 111)" '-5 16 '
assert_eq 'an input that runs long is trailing bytes, not ignored' \
    "$(field 112 113)" '-6 18 '
assert_eq 'an input past the reader capacity is refused before any slot is read' \
    "$(field 114 115)" '-8 25 '
assert_eq 'an addition that would leave the Int range is refused, not wrapped' \
    "$(field 116 117)" '-7 9223372036854775807 '

lines=$(wc -l <"$expected" | tr -d ' ')
assert_num 'recorded decisions cover the whole golden' "$lines" -eq 117

# Nine distinct codes, one per closed error constructor. Counted rather than
# eyeballed, so a collapsed pair cannot pass by looking plausible above.
sed -n '98,117p' "$expected" | sed -n '1~2p' | sort -u >"$WORK/error.codes"
assert_num 'every closed error constructor has its own code' \
    "$(wc -l <"$WORK/error.codes" | tr -d ' ')" -eq 9

# --------------------------------------------------------- mixing is a bug
#
# A local reading and an instant are both one number. Keeping them apart is the
# whole reason the resolution sum has anywhere to live, so the toolchain must
# refuse to confuse them.

expect_rejected() {
    stem=$1
    reason=$2

    if "$ROOT/bin/kofun" check "$CASES/$stem.kofun" \
        >"$WORK/$stem.check.stdout" 2>"$WORK/$stem.check.stderr"
    then
        fail "$stem passed \`kofun check\`; the separate tzdb types did not stop it"
    fi
    require_line "$WORK/$stem.check.stderr" "$reason" \
        "$stem was rejected by check for the wrong reason"

    if "$ROOT/bin/kofun" build "$CASES/$stem.kofun" -o "$WORK/$stem" \
        >"$WORK/$stem.stdout" 2>"$WORK/$stem.stderr"
    then
        fail "$stem built after check refused it"
    fi
    require_line "$WORK/$stem.stderr" "$reason" \
        "$stem was rejected by build for the wrong reason"
    assert_absent "$stem emitted a binary despite being refused" "$WORK/$stem"

    printf 'tzdb: refused by check and build: %s\n' "$stem"
}

expect_rejected mixed_local_instant \
    'error[E2S32]: nominal record binding has the wrong type'
expect_rejected local_epoch_field \
    'error[E2S32]: unknown nominal record field read'

printf 'bounded reader over injected bytes, with typed failure for every way they can be wrong: PASS\n'
printf 'bounded transition lookup: normal, gap, and fold, with both offsets carried: PASS\n'
printf 'every result binds zone, value, offsets, version, and digest: PASS\n'
printf 'reference executor and C11 backend agree, and the bytes do not move under a hostile TZ, locale, or an empty environment: PASS\n'
