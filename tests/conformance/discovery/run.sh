#!/usr/bin/env sh
set -eu

# Developer discovery v1 contract layer (#637), against
# docs/DEVELOPER_DISCOVERY.md.
#
# What this gate is for. The discovery contract is almost entirely a set of
# rules about which shapes are *refused*, and those are the rules an
# implementation drifts away from silently: a parser that tolerates a reordered
# key, an emitter that fills in a reason the status forbids, or an offset check
# that accepts the middle of a code point. Each section below observes one of
# those refusals, so a regression shows up as a changed observation rather than
# as a result that merely looks plausible.

ROOT=$(CDPATH= cd -P -- "$(dirname -- "$0")/../../.." && pwd)
CASES="$ROOT/tests/conformance/discovery"
CC=${CC:-cc}

command -v "$CC" >/dev/null 2>&1 || {
    printf '%s\n' "discovery: a C11 compiler is required" >&2
    exit 1
}

WORK=$(mktemp -d "${TMPDIR:-/tmp}/kofun-discovery.XXXXXX")
trap 'rm -rf "$WORK"' 0 1 2 15

fail() {
    printf '%s\n' "FAIL: $*" >&2
    exit 1
}

"$CC" -std=c11 -O2 -g -Wall -Wextra -Werror -pedantic \
    -I"$ROOT/bootstrap/stage2" \
    "$ROOT/bootstrap/stage2/discovery_v1.c" \
    "$CASES/discovery_v1_test.c" \
    -o "$WORK/discovery-test"

golden() {
    name=$1
    shift
    "$WORK/discovery-test" "$@" >"$WORK/$name.observed" 2>&1
    cmp "$CASES/$name.golden" "$WORK/$name.observed" ||
        fail "$name observation changed"
    printf '%s\n' "PASS: $name"
}

# Request admissibility: canonical bytes are accepted, and every documented
# deviation is refused with the reason the contract names for it.
golden parse parse

# Result shape: the statuses that carry no facts emit canonical bytes, and the
# combinations the contract forbids are refused rather than emitted.
golden emit emit

# UTF-8 code-point boundaries, which the position rule requires of all three
# offsets.
golden boundaries boundaries

# Every rejection path above walks the parser over deliberately malformed
# bytes, which is exactly where an off-by-one reads past the end. Run the same
# cases under the sanitizers so a refusal that is "correct" but reads out of
# bounds still fails.
if printf 'int main(void){return 0;}\n' >"$WORK/probe.c" &&
    "$CC" -std=c11 -fsanitize=address,undefined "$WORK/probe.c" \
        -o "$WORK/probe" 2>/dev/null
then
    "$CC" -std=c11 -O1 -g -Wall -Wextra -Werror -pedantic \
        -fno-omit-frame-pointer -fsanitize=address,undefined \
        -I"$ROOT/bootstrap/stage2" \
        "$ROOT/bootstrap/stage2/discovery_v1.c" \
        "$CASES/discovery_v1_test.c" \
        -o "$WORK/discovery-test-sanitized"
    for mode in parse emit boundaries; do
        ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
        UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
            "$WORK/discovery-test-sanitized" "$mode" >/dev/null
    done
    printf '%s\n' "PASS: AddressSanitizer and UndefinedBehaviorSanitizer"
else
    printf '%s\n' "SKIP: sanitizers unavailable"
fi

# The emitted bytes are pinned byte-for-byte by emit.golden above, which is
# what makes the canonical-encoding rules checkable at all: key order,
# two-space indentation, the single space after ':', and the closing LF are all
# visible in the golden rather than asserted about. A reviewer reads the
# canonical form directly, and any drift shows up as a diff.
#
# Deliberately no second validator here. Re-checking these bytes with a JSON
# library would mean adding an interpreter this repository does not otherwise
# use in its gates, and `make repository-check` exists to keep that out.

printf '%s\n' "discovery: OK"
