#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
WORK=${KOFUN_TUI_WORK:-"$ROOT/build/tui-framework"}
CC=${CC:-cc}

rm -rf "$WORK"
mkdir -p "$WORK"

"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    -I"$ROOT/framework/tui/include" \
    "$ROOT/framework/tui/src/kofun_tui.c" \
    "$ROOT/framework/tui/tests/test_tui.c" \
    -lm -o "$WORK/test-tui"
"$WORK/test-tui"

"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    -I"$ROOT/framework/tui/include" \
    "$ROOT/framework/tui/src/kofun_tui.c" \
    "$ROOT/framework/tui/tests/benchmark.c" \
    -o "$WORK/benchmark-tui"
"$WORK/benchmark-tui" | tee "$WORK/benchmark.txt"
grep -Fq '2,000,000 ns frame budget' "$WORK/benchmark.txt"

"$ROOT/framework/tui/build.sh" \
    "$ROOT/examples/tui_dashboard.kofun" \
    "$WORK/tui-dashboard" >/dev/null
TERM=xterm-256color COLORTERM=truecolor \
    "$WORK/tui-dashboard" >"$WORK/dashboard.stdout"
test "$(wc -l <"$WORK/dashboard.stdout")" -eq 6
grep -Fq 'compile 東京' "$WORK/dashboard.stdout"
grep -Fq 'linux-x86_64' "$WORK/dashboard.stdout"
grep -Fq 'artifact' "$WORK/dashboard.stdout"
grep -Fq 'build complete' "$WORK/dashboard.stdout"
if LC_ALL=C grep "$(printf '\033')" "$WORK/dashboard.stdout" >/dev/null; then
    printf '%s\n' 'FAIL: append-only Kofun output contained an escape sequence' >&2
    exit 1
fi

printf '%s\n' \
    'PASS: Kofun program consumed framework.tui through the public C ABI' \
    'PASS: redirected/disabled sessions are append-only and escape-free'
