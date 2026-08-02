#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CASES="$ROOT/tests/conformance/text-results"
CC=${CC:-cc}
ASSERT_CONTEXT='text results'
. "$ROOT/tests/assertions/assert.sh"
. "$ROOT/bootstrap/stage2/build.sh"

WORK=$(mktemp -d "${TMPDIR:-/tmp}/kofun-text-results.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM

fail() {
    printf '%s\n' "FAIL: text results: $*" >&2
    exit 1
}

compare() {
    label=$1
    expected=$2
    actual=$3
    cmp "$expected" "$actual" || fail "$label differs"
}

command -v "$CC" >/dev/null 2>&1 || fail 'a C11 compiler is required'
mkdir -p "$WORK/remapped"
kofun_stage2_build "$ROOT" "$WORK/kofun-stage2"

compile_positive() {
    label=$1
    source=$2
    mkdir -p "$WORK/$label"
    (cd "$WORK/$label" &&
        "$WORK/kofun-stage2" "$source" \
            output.c output.ir output.tokens \
            >compiler.stdout 2>compiler.stderr) ||
        fail "$label did not compile"
    assert_eq "$label compiler stdout" \
        "$(cat "$WORK/$label/compiler.stdout")" output.c
    assert_file_empty "$label compiler stderr" \
        "$WORK/$label/compiler.stderr"
    assert_file_nonempty "$label C artifact" "$WORK/$label/output.c"
    assert_file_nonempty "$label structured IR" "$WORK/$label/output.ir"
    assert_file_nonempty "$label token tape" "$WORK/$label/output.tokens"
}

cp "$CASES/values.kofun" "$WORK/remapped/values.kofun"
compile_positive values.first "$CASES/values.kofun"
compile_positive values.second "$CASES/values.kofun"
compile_positive values.remapped "$WORK/remapped/values.kofun"

for artifact in output.c output.ir output.tokens compiler.stdout compiler.stderr; do
    compare "repeated $artifact" \
        "$WORK/values.first/$artifact" "$WORK/values.second/$artifact"
    compare "remapped $artifact" \
        "$WORK/values.first/$artifact" "$WORK/values.remapped/$artifact"
done

"$CC" -std=c11 -O2 -Wall -Wextra -Werror -pedantic \
    "$WORK/values.first/output.c" -o "$WORK/values" ||
    fail 'positive emitted C is not strict ISO C11'
"$WORK/values" >"$WORK/values.stdout" 2>"$WORK/values.stderr" ||
    fail 'positive executable exited non-zero'
compare 'positive stdout' "$CASES/values.stdout" "$WORK/values.stdout"
assert_file_empty 'positive runtime stderr' "$WORK/values.stderr"

"$WORK/values" >"$WORK/values.second.stdout" \
    2>"$WORK/values.second.stderr" ||
    fail 'repeated positive execution exited non-zero'
compare 'repeated runtime stdout' \
    "$WORK/values.stdout" "$WORK/values.second.stdout"
compare 'repeated runtime stderr' \
    "$WORK/values.stderr" "$WORK/values.second.stderr"

# Sanitizers are acceptance evidence, not an optional best-effort branch.
"$CC" -std=c11 -O1 -g -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$WORK/values.first/output.c" -o "$WORK/values.sanitize" ||
    fail 'the C compiler cannot build the sanitizer-backed positive'
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
    "$WORK/values.sanitize" >"$WORK/values.sanitize.stdout" \
    2>"$WORK/values.sanitize.stderr" ||
    fail 'sanitizer-backed positive execution failed'
compare 'sanitized stdout' \
    "$CASES/values.stdout" "$WORK/values.sanitize.stdout"
assert_file_empty 'ASan/UBSan positive report' \
    "$WORK/values.sanitize.stderr"

# The source deliberately contains all three helper spellings as user data.
# If the site counter scans inside C string literals, this accepted fixture
# drifts or spuriously reaches E2S156.
assert_grep 'string data containing helper names survived unchanged' \
    -Fq -- '"kofun_to_text(kofun_text_concat(kofun_text_slice("' \
    "$WORK/values.first/output.c"

# The arena is monotonic. No modulo ring and no per-site overwrite may return:
# the two calls to `render` in values.kofun share the same producer site and
# stay independently observable as 81 and 92.
assert_grep 'emitted bounded Text arena' \
    -Fq -- 'static char kofun_text_slots[KOFUN_TEXT_TEMPORARY_LIMIT][256];' \
    "$WORK/values.first/output.c"
assert_grep 'emitted explicit Text exhaustion guard' \
    -Fq -- 'if (kofun_text_next_slot >= KOFUN_TEXT_TEMPORARY_LIMIT)' \
    "$WORK/values.first/output.c"
assert_not_grep 'a modulo Text ring survived' \
    -E -- 'next_slot[^;]*%|%[[:space:]]*64u' "$WORK/values.first/output.c"
assert_not_grep 'a same-site Text slot survived' \
    -Fq -- 'kofun_text_slice_at(' "$WORK/values.first/output.c"
assert_grep 'Text concatenation uses its bounded helper' \
    -Fq -- 'kofun_text_concat(' "$WORK/values.first/output.c"
assert_grep 'Int addition remains checked integer addition' \
    -Fq -- 'kofun_add(INT64_C(20), INT64_C(22))' "$WORK/values.first/output.c"

compile_refusal() {
    stem=$1
    code=$2
    set +e
    "$WORK/kofun-stage2" "$CASES/$stem.kofun" \
        "$WORK/$stem.c" "$WORK/$stem.ir" "$WORK/$stem.tokens" \
        >"$WORK/$stem.actual" 2>"$WORK/$stem.internal.stderr"
    status=$?
    set -e
    assert_num "$stem exit status" "$status" -eq 1
    assert_file_empty "$stem internal stderr" \
        "$WORK/$stem.internal.stderr"
    compare "$stem diagnostic" "$CASES/$stem.stderr" "$WORK/$stem.actual"
    assert_grep "$stem stable code" -Fq -- "error[$code]:" \
        "$WORK/$stem.actual"
    assert_absent "$stem partial C artifact" "$WORK/$stem.c"
}

compile_refusal mixed_text_int E2S155
compile_refusal mixed_int_text E2S155
compile_refusal site_limit E2S156

run_runtime_refusal() {
    stem=$1
    code=$2
    compile_positive "$stem.compile" "$CASES/$stem.kofun"
    "$CC" -std=c11 -O2 -Wall -Wextra -Werror -pedantic \
        "$WORK/$stem.compile/output.c" -o "$WORK/$stem" ||
        fail "$stem emitted invalid C"
    set +e
    "$WORK/$stem" >"$WORK/$stem.stdout" 2>"$WORK/$stem.stderr"
    status=$?
    set -e
    assert_num "$stem runtime status" "$status" -eq 1
    compare "$stem runtime diagnostic" \
        "$CASES/$stem.stderr" "$WORK/$stem.stderr"
    assert_grep "$stem runtime code" -Fq -- "error[$code]:" \
        "$WORK/$stem.stderr"

    set +e
    "$WORK/$stem" >"$WORK/$stem.second.stdout" \
        2>"$WORK/$stem.second.stderr"
    second_status=$?
    set -e
    assert_num "$stem repeated runtime status" "$second_status" -eq 1
    compare "$stem repeated stdout" \
        "$WORK/$stem.stdout" "$WORK/$stem.second.stdout"
    compare "$stem repeated stderr" \
        "$WORK/$stem.stderr" "$WORK/$stem.second.stderr"
}

run_runtime_refusal slice_out_of_range R020
assert_file_empty 'slice refusal stdout' "$WORK/slice_out_of_range.stdout"
run_runtime_refusal concat_too_long R021
assert_file_empty 'concat refusal stdout' "$WORK/concat_too_long.stdout"
run_runtime_refusal temporary_exhaustion R022
assert_file_empty 'temporary exhaustion has no partial stdout' \
    "$WORK/temporary_exhaustion.stdout"

# The two source/transliteration surfaces must carry the same boundaries.
for source in \
    "$ROOT/bootstrap/stage2/compiler.kofun" \
    "$ROOT/bootstrap/stage2/compiler.c"
do
    assert_grep "E2S155 is present in $source" -Fq -- E2S155 "$source"
    assert_grep "E2S156 is present in $source" -Fq -- E2S156 "$source"
    assert_grep "R020 is present in $source" -Fq -- R020 "$source"
    assert_grep "R021 is present in $source" -Fq -- R021 "$source"
    assert_grep "R022 is present in $source" -Fq -- R022 "$source"
done

printf '%s\n' \
    'PASS: Text arguments/results survive nested and later temporary calls' \
    'PASS: to_text covers zero, signs, and both Int64 boundaries exactly' \
    'PASS: Text concatenation is bounded while Int addition stays checked' \
    'PASS: compile-time and runtime Text budgets fail deterministically' \
    'PASS: repeated and remapped output is byte-identical strict C11' \
    'PASS: emitted C executes cleanly under address/undefined sanitizers'
