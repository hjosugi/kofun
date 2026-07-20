#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SOURCE="$ROOT/bootstrap/stage1/compiler.kofun"
SEED="$ROOT/bootstrap/stage1/compiler.c"
FIXTURE="$ROOT/bootstrap/fixtures/answer.kofun"
WORK="${KOFUN_STAGE1_WORK:-$ROOT/build/bootstrap-stage1}"
CC="${CC:-cc}"

mkdir -p "$WORK"

(
    cd "$ROOT/bootstrap/stage1"
    sha256sum -c SHA256SUMS
)

"$CC" -std=c11 -O2 -Wall -Wextra -Werror "$SEED" -lm -o "$WORK/kofun-stage1"
"$WORK/kofun-stage1" "$FIXTURE" "$WORK/answer.c"
"$CC" -std=c11 -O2 -Wall -Wextra -Werror "$WORK/answer.c" -o "$WORK/answer"
answer=$("$WORK/answer")
test "$answer" = "42"

printf '%s\n' \
    "PASS: Python-free Kofun Stage 1 built with $CC" \
    "PASS: compiled fixture returned $answer"
