#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SOURCE="$ROOT/bootstrap/stage1/compiler.kofun"
SEED="$ROOT/bootstrap/stage1/compiler.c"
FIXTURE="$ROOT/bootstrap/fixtures/answer.kofun"
BOOL_FIXTURE="$ROOT/bootstrap/selfhost/driver/corpus_bool.kofun"
BOOL_C="$ROOT/bootstrap/selfhost/driver/corpus_bool.c"
BOOL_STDOUT="$ROOT/bootstrap/selfhost/driver/corpus_bool.stdout"
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

"$WORK/kofun-stage1" "$BOOL_FIXTURE" "$WORK/bool.c"
cmp "$BOOL_C" "$WORK/bool.c"
"$CC" -std=c11 -O2 -Wall -Wextra -Werror "$WORK/bool.c" -o "$WORK/bool"
"$WORK/bool" >"$WORK/bool.stdout"
cmp "$BOOL_STDOUT" "$WORK/bool.stdout"

for fixture in \
    "$ROOT/bootstrap/selfhost/driver/corpus_reject_bool_arithmetic.kofun" \
    "$ROOT/bootstrap/selfhost/driver/corpus_reject_bool_print.kofun" \
    "$ROOT/bootstrap/selfhost/driver/corpus_reject_bool_annotation.kofun" \
    "$ROOT/bootstrap/selfhost/driver/corpus_reject_bool_infer_annotation.kofun" \
    "$ROOT/bootstrap/selfhost/driver/corpus_reject_bool_keyword_binding.kofun" \
    "$ROOT/bootstrap/selfhost/driver/corpus_reject_bool_order.kofun" \
    "$ROOT/bootstrap/selfhost/driver/corpus_reject_logical_int.kofun" \
    "$ROOT/bootstrap/selfhost/driver/corpus_reject_not_int.kofun" \
    "$ROOT/bootstrap/selfhost/driver/corpus_reject_single_pipe.kofun"
do
    output="$WORK/$(basename "$fixture" .kofun).c"
    rm -f "$output"
    set +e
    "$WORK/kofun-stage1" "$fixture" "$output" >"$output.stdout"
    status=$?
    set -e
    test "$status" -ne 0
    test ! -e "$output"
    cmp "$ROOT/bootstrap/selfhost/driver/corpus_reject.stdout" \
        "$output.stdout"
done

printf '%s\n' \
    "PASS: Python-free Kofun Stage 1 built with $CC" \
    "PASS: compiled fixture returned $answer" \
    "PASS: Int/Bool Core accepts comparisons and refuses typed boundary crossings"
