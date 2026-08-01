#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CASES="$ROOT/tests/stdlib/clock-adapters"
ASSERT_CONTEXT='clock Linux x86-64 integration'
. "$ROOT/tests/assertions/assert.sh"

WORK=$(mktemp -d "${TMPDIR:-/tmp}/kofun-clock-linux-x86-64.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM

assert_eq 'host operating system' "$(uname -s)" 'Linux'
assert_eq 'host architecture' "$(uname -m)" 'x86_64'

adapter="$ROOT/stdlib/clock/adapters_linux_x86_64.kofun"
wrapper="$ROOT/stdlib/linux_x86_64/time.kofun"
assert_grep 'clock wrapper converts raw negative returns into SysError' \
    -Fq -- 'syscall_result("clock_gettime", raw)' "$wrapper"
assert_grep 'sleep wrapper converts raw negative returns into SysError' \
    -Fq -- 'return match syscall_result(' "$wrapper"
assert_grep 'adapter preserves platform errno in the public clock error' \
    -Fq -- 'return PlatformReadFailed(error.errno)' "$adapter"
assert_grep 'adapter preserves seconds from the Linux timespec' \
    -Fq -- 'timestamp.seconds,' "$adapter"
assert_grep 'adapter preserves nanoseconds from the Linux timespec' \
    -Fq -- 'timestamp.nanoseconds,' "$adapter"

cc=${CC:-cc}
"$cc" -std=c11 -O2 -Wall -Wextra -Werror \
    "$CASES/linux_x86_64.c" -o "$WORK/clock-linux-x86-64"
"$WORK/clock-linux-x86-64" >"$WORK/platform.stdout"

cat >"$WORK/platform.expected" <<'EXPECTED'
clock_gettime monotonic: seconds plus nanoseconds in 0..999999999
clock_gettime realtime: seconds plus nanoseconds in 0..999999999
clock_gettime invalid ID: raw -EINVAL
nanosleep invalid nanoseconds: raw -EINVAL
nanosleep zero duration: success
EXPECTED

if ! cmp "$WORK/platform.expected" "$WORK/platform.stdout"
then
    assert_fail 'Linux clock and sleep decisions differ from the five expected outcomes'
fi

printf 'clock Linux x86-64 integration: units, raw errors, and zero sleep: PASS\n'
