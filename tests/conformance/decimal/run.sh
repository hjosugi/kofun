#!/usr/bin/env sh
set -eu

# Decimal slice 2 (#721): the runtime representation, its canonical form, and
# the versioned resource profile.
#
# What this gate is for, beyond "the code runs". Four of #710's frozen
# decisions are only checkable by observation, and each has a section below:
#
#   1. the significand is arbitrary precision, and small storage is an
#      *unobservable* optimization;
#   6. plain Decimal canonicalizes display scale away, so `1.0` and `1` are one
#      value;
#   8. resource limits are versioned, fail explicitly, and never clamp or
#      change representation;
#   and `docs/DECIMAL.md`'s rule that no conversion goes through a host
#   `double`.

ROOT=$(CDPATH= cd -P -- "$(dirname -- "$0")/../../.." && pwd)
CASES="$ROOT/tests/conformance/decimal"
CC=${CC:-cc}

command -v "$CC" >/dev/null 2>&1 || {
    printf '%s\n' "decimal: a C11 compiler is required" >&2
    exit 1
}

WORK=$(mktemp -d "${TMPDIR:-/tmp}/kofun-decimal.XXXXXX")
trap 'rm -rf "$WORK"' 0 1 2 15

fail() {
    printf '%s\n' "FAIL: $*" >&2
    exit 1
}

"$CC" -std=c11 -O2 -g -Wall -Wextra -Werror -pedantic \
    -I"$ROOT/bootstrap/stage2" \
    "$ROOT/bootstrap/stage2/decimal_v1.c" \
    "$CASES/decimal_v1_test.c" \
    -o "$WORK/decimal-test"

golden() {
    name=$1
    shift
    "$WORK/decimal-test" "$@" >"$WORK/$name.observed" 2>&1
    cmp "$CASES/$name.golden" "$WORK/$name.observed" ||
        fail "$name observation changed"
    printf '%s\n' "PASS: $name"
}

# Construction. The digits are preserved exactly, including significands well
# past Int64 — 2^127 and a 60-digit value are here so that "arbitrary
# precision" is measured rather than asserted.
golden construct construct \
    1.5 1.50 1.500 1 1.0 1.00 1000 1e3 1e+3 0.1 0.10 6.02e23 1e-9 \
    1_000.000_1 0 0.0 0.000 \
    9223372036854775807 9223372036854775808 \
    170141183460469231731687303715884105728 \
    123456789012345678901234567890.123456789012345678901234567890

# Canonicalization (frozen decision 6). Distinct spellings of one value must be
# *structurally* equal after construction, not merely compare equal — so the
# golden records both, and a canonicalizer that only fixed `compare` would fail
# on the `equal=` column.
golden canonical equal \
    1.0 1 \
    1.00 1.0 \
    1.000 1.00 \
    1000 1e3 \
    0.1e1 1 \
    10e-1 1 \
    0 0.0 \
    0.1 0.2 \
    2 10 \
    1e3 1e4 \
    0.000001 1e-6

# Small-storage invisibility (frozen decision 1). Each line carries the path
# taken *and* every public observation, and the pairs straddle the boundary:
# 18446744073709551615 is the last inline value and ...616 the first promoted
# one. If the threshold ever leaked into equality, scale or rendering, the
# adjacent pair is where it would show.
golden storage storage \
    1.5 1 0.1 \
    18446744073709551615 18446744073709551616 \
    99999999999999999999999999999999999999.5

# The versioned profile (frozen decision 8). `docs/DECIMAL.md` deferred the
# first concrete thresholds but required them to be "versioned together when
# introduced", so the version and every limit and message are one golden: a
# limit cannot move without the version moving in the same diff.
golden profile profile

golden malformed construct 1..2 1e 1.5.5 abc ''

# Each limit at its exact boundary and one past it. Exceeding a limit is a
# stable code, never a clamped value — so the `-at` line must show a
# constructed value and the `-over` line a bare code.
{
    "$WORK/decimal-test" limit digits-at
    "$WORK/decimal-test" limit digits-over
    "$WORK/decimal-test" limit scale-at
    "$WORK/decimal-test" limit scale-over
} >"$WORK/limits.observed" 2>&1
cmp "$CASES/limits.golden" "$WORK/limits.observed" ||
    fail "resource limit observations changed"
grep -q '^digits-over -> D001$' "$WORK/limits.observed" ||
    fail "one digit past the limit did not report D001"
grep -q '^scale-over -> D002$' "$WORK/limits.observed" ||
    fail "one step past the scale limit did not report D002"
printf '%s\n' "PASS: limits"

# Binary64, as raw bits rather than a decimal rendering, so no printf of the
# host's own can hide a difference. The list covers the subnormal cliff
# (5e-324, 2.47e-324), the smallest normal, and two exact ties
# (9007199254740993, 1e23).
golden float float \
    0.1 0.2 0.3 1.5 1 2 0.5 1e-9 6.02e23 1e308 1e-308 \
    5e-324 2.4703282292062328e-324 2.2250738585072014e-308 \
    9007199254740993 1e22 1e23 3.141592653589793 0 0.0

# `docs/DECIMAL.md` forbids converting a literal through a host `double`. That
# is a property of the source, so it is checked there: the module must not
# reach for the host's decimal parser or its floating-point math library.
# The pattern requires the opening parenthesis of a call. Matching the bare
# name instead flags the module's own comment explaining that it does not use
# `strtod`, which is how this check first failed.
for forbidden in strtod strtof strtold atof sscanf scanf; do
    if grep -nE "(^|[^_[:alnum:]])$forbidden[[:space:]]*\\(" \
        "$ROOT/bootstrap/stage2/decimal_v1.c" >/dev/null 2>&1
    then
        fail "decimal_v1.c calls $forbidden; conversion must stay exact"
    fi
done
if grep -n '#include <math.h>' "$ROOT/bootstrap/stage2/decimal_v1.c" \
    >/dev/null 2>&1
then
    fail "decimal_v1.c includes math.h; conversion must stay exact"
fi
printf '%s\n' "PASS: no host decimal parser on the conversion path"

# Sanitizers, matching what the other Stage 2 module gates do. An
# arbitrary-precision buffer that grows by doubling is exactly the shape where
# an off-by-one survives a golden comparison.
if "$CC" -std=c11 -x c -fsanitize=address,undefined \
        -o "$WORK/probe" - >/dev/null 2>&1 <<'EOF'
int main(void) { return 0; }
EOF
then
    "$CC" -std=c11 -O1 -g -Wall -Wextra -Werror -pedantic \
        -fno-omit-frame-pointer -fsanitize=address,undefined \
        -I"$ROOT/bootstrap/stage2" \
        "$ROOT/bootstrap/stage2/decimal_v1.c" \
        "$CASES/decimal_v1_test.c" \
        -o "$WORK/decimal-test-sanitized"
    ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
        "$WORK/decimal-test-sanitized" construct \
        1.5 1000 0.1 170141183460469231731687303715884105728 0 \
        >/dev/null
    ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
        "$WORK/decimal-test-sanitized" equal 1.0 1 1000 1e3 0.1 0.2 \
        >/dev/null
    ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
        "$WORK/decimal-test-sanitized" float 0.1 5e-324 1e308 1e23 \
        >/dev/null
    ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
        "$WORK/decimal-test-sanitized" limit digits-over >/dev/null
    printf '%s\n' "PASS: AddressSanitizer and UndefinedBehaviorSanitizer"
else
    printf '%s\n' "SKIP: sanitizers unavailable"
fi

printf '%s\n' \
    "PASS: Decimal slice 2 — arbitrary-precision canonical representation," \
    "  versioned resource profile v$( \
        "$WORK/decimal-test" profile | \
        sed -n 's/^profile-version=//p'), and exact binary64"
