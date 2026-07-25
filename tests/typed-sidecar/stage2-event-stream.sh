#!/usr/bin/env sh
set -eu

LC_ALL=C
export LC_ALL

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CC=${CC:-cc}
WORK=${KOFUN_STAGE2_EVENT_STREAM_WORK:-"$ROOT/build/stage2-event-stream"}
FIXTURE="$ROOT/tests/typed-sidecar/fixtures/stage2_events.kofun"

fail() {
    printf '%s\n' "FAIL: $*" >&2
    exit 1
}

command -v "$CC" >/dev/null 2>&1 || fail 'a C11 compiler is required'
case $WORK in
    */stage2-event-stream|*/stage2-event-stream.*) ;;
    *) fail "work directory must end in stage2-event-stream[.suffix]: $WORK" ;;
esac
rm -rf "$WORK"
mkdir -p "$WORK"

"$CC" -std=c11 -O2 -g -Wall -Wextra -Werror -pedantic \
    -I"$ROOT/bootstrap/stage2" \
    "$ROOT/bootstrap/stage2/sha256.c" \
    "$ROOT/unicode/kofun_unicode.c" \
    "$ROOT/bootstrap/stage2/semantic_events.c" \
    "$ROOT/tests/typed-sidecar/stage2_events_test.c" \
    -o "$WORK/stage2-events-test"

"$WORK/stage2-events-test" stream "$WORK/output" "$FIXTURE"

printf '%s\n' \
    'PASS: internal kofun-stage2-semantic-events/v1 stream'
