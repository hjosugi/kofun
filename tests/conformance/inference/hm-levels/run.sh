#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -P -- "$(dirname -- "$0")/../../../.." && pwd)
CASES="$ROOT/tests/conformance/inference/hm-levels"
CC=${CC:-cc}
ANALYZER_CC=${ANALYZER_CC:-gcc}
WORK=${KOFUN_HM_LEVELS_WORK:-"$ROOT/build/hm-levels"}

fail() {
    printf '%s\n' "FAIL: $*" >&2
    exit 1
}

validate_work_path() {
    case $WORK in
        "$ROOT/build/hm-levels") return ;;
        "$ROOT/build/hm-levels."*)
            suffix=${WORK#"$ROOT/build/hm-levels."}
            case $suffix in
                ''|*[!A-Za-z0-9_-]*) ;;
                *) return ;;
            esac
            ;;
    esac
    fail "work directory must be $ROOT/build/hm-levels or use a safe suffix: $WORK"
}

command -v "$CC" >/dev/null 2>&1 || fail 'a C11 compiler is required'
command -v "$ANALYZER_CC" >/dev/null 2>&1 ||
    fail 'GCC is required for the static analyzer gate'
validate_work_path
if test "${KOFUN_HM_LEVELS_GUARD_PROBE:-0}" = 1; then
    exit 0
fi

GUARD_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/kofun-hm-levels-guard.XXXXXX")
GUARD_WORK="$GUARD_ROOT/hm-levels"
mkdir "$GUARD_WORK"
printf '%s\n' 'do-not-delete' >"$GUARD_WORK/sentinel"
set +e
KOFUN_HM_LEVELS_WORK="$GUARD_WORK" KOFUN_HM_LEVELS_GUARD_PROBE=1 \
    sh "$0" >"$GUARD_ROOT/probe.stdout" 2>"$GUARD_ROOT/probe.stderr"
guard_status=$?
set -e
test "$guard_status" -eq 1 || fail 'out-of-tree work path was accepted'
test "$(cat "$GUARD_WORK/sentinel")" = 'do-not-delete' ||
    fail 'out-of-tree work-path refusal changed its sentinel'
rm "$GUARD_ROOT/probe.stdout" "$GUARD_ROOT/probe.stderr" \
    "$GUARD_WORK/sentinel"
rmdir "$GUARD_WORK" "$GUARD_ROOT"

rm -rf "$WORK"
mkdir -p "$WORK/remapped"

SOURCE="$ROOT/bootstrap/stage2/hm_levels_frontend.c"
FRONTEND="$WORK/kofun-hm-levels-frontend"
SANITIZED="$WORK/kofun-hm-levels-sanitize"

"$CC" -std=c11 -O2 -Wall -Wextra -Werror -pedantic \
    "$SOURCE" -o "$FRONTEND"
"$ANALYZER_CC" -std=c11 -O0 -g -Wall -Wextra -Werror -pedantic \
    -fanalyzer "$SOURCE" -o "$WORK/kofun-hm-levels-analyzer"
"$CC" -std=c11 -O1 -g -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$SOURCE" -o "$SANITIZED"

expect_unsafe_paths() {
    label=$1
    source=$2
    ir_output=$3
    token_output=$4
    set +e
    "$FRONTEND" "$source" "$ir_output" "$token_output" \
        >"$WORK/$label.stdout" 2>"$WORK/$label.stderr"
    status=$?
    set -e
    test "$status" -eq 2 || fail "$label exited $status instead of 2"
    test ! -s "$WORK/$label.stdout" || fail "$label wrote stdout"
    grep -Fx 'hm levels frontend: unsafe or oversized path arguments' \
        "$WORK/$label.stderr" >/dev/null ||
        fail "$label did not report unsafe path arguments"
}

ALIASES="$WORK/path-aliases"
mkdir "$ALIASES"
cp "$CASES/positive.kofun" "$ALIASES/source-output.kofun"
printf '%s\n' 'prior-token' >"$ALIASES/source-output.tokens"
expect_unsafe_paths source-is-output \
    "$ALIASES/source-output.kofun" "$ALIASES/source-output.kofun" \
    "$ALIASES/source-output.tokens"
cmp "$CASES/positive.kofun" "$ALIASES/source-output.kofun" ||
    fail 'source/output alias changed the source'
test "$(cat "$ALIASES/source-output.tokens")" = 'prior-token' ||
    fail 'source/output alias changed the prior token artifact'

printf '%s\n' 'prior-shared-output' >"$ALIASES/shared.out"
expect_unsafe_paths outputs-are-equal "$CASES/positive.kofun" \
    "$ALIASES/shared.out" "$ALIASES/shared.out"
test "$(cat "$ALIASES/shared.out")" = 'prior-shared-output' ||
    fail 'equal outputs changed their prior artifact'

cp "$CASES/positive.kofun" "$ALIASES/source-is-temp.ir.hm-levels.tmp"
printf '%s\n' 'prior-ir' >"$ALIASES/source-is-temp.ir"
printf '%s\n' 'prior-token' >"$ALIASES/source-is-temp.tokens"
expect_unsafe_paths source-is-derived-temp \
    "$ALIASES/source-is-temp.ir.hm-levels.tmp" \
    "$ALIASES/source-is-temp.ir" "$ALIASES/source-is-temp.tokens"
cmp "$CASES/positive.kofun" "$ALIASES/source-is-temp.ir.hm-levels.tmp" ||
    fail 'derived-temp/source alias changed the source'
test "$(cat "$ALIASES/source-is-temp.ir")" = 'prior-ir' ||
    fail 'derived-temp/source alias changed the prior IR artifact'
test "$(cat "$ALIASES/source-is-temp.tokens")" = 'prior-token' ||
    fail 'derived-temp/source alias changed the prior token artifact'

printf '%s\n' 'prior-ir' >"$ALIASES/cross.ir"
printf '%s\n' 'prior-token' >"$ALIASES/cross.ir.hm-levels.tmp"
expect_unsafe_paths output-is-other-temp "$CASES/positive.kofun" \
    "$ALIASES/cross.ir" "$ALIASES/cross.ir.hm-levels.tmp"
test "$(cat "$ALIASES/cross.ir")" = 'prior-ir' ||
    fail 'cross-output/temp alias changed the prior IR artifact'
test "$(cat "$ALIASES/cross.ir.hm-levels.tmp")" = 'prior-token' ||
    fail 'cross-output/temp alias changed the prior token artifact'

cp "$CASES/positive.kofun" "$ALIASES/inode-source.kofun"
ln "$ALIASES/inode-source.kofun" "$ALIASES/inode-output.ir"
printf '%s\n' 'prior-token' >"$ALIASES/inode-output.tokens"
expect_unsafe_paths existing-inode-alias \
    "$ALIASES/inode-source.kofun" "$ALIASES/inode-output.ir" \
    "$ALIASES/inode-output.tokens"
cmp "$CASES/positive.kofun" "$ALIASES/inode-source.kofun" ||
    fail 'existing-file alias changed the source'
cmp "$CASES/positive.kofun" "$ALIASES/inode-output.ir" ||
    fail 'existing-file alias changed the prior IR artifact'
test "$(cat "$ALIASES/inode-output.tokens")" = 'prior-token' ||
    fail 'existing-file alias changed the prior token artifact'

cp "$CASES/positive.kofun" "$WORK/remapped/positive.kofun"
for suffix in first second remapped; do
    source="$CASES/positive.kofun"
    test "$suffix" = remapped && source="$WORK/remapped/positive.kofun"
    "$FRONTEND" "$source" \
        "$WORK/positive.$suffix.ir" "$WORK/positive.$suffix.tokens" \
        >"$WORK/positive.$suffix.stdout" \
        2>"$WORK/positive.$suffix.stderr"
    test ! -s "$WORK/positive.$suffix.stdout" ||
        fail "$suffix positive run wrote stdout"
    test ! -s "$WORK/positive.$suffix.stderr" ||
        fail "$suffix positive run wrote stderr"
done
cmp "$CASES/positive.ir" "$WORK/positive.first.ir" ||
    fail 'positive typed IR differs from its golden'
cmp "$WORK/positive.first.ir" "$WORK/positive.second.ir" ||
    fail 'repeated typed IR differs'
cmp "$WORK/positive.first.tokens" "$WORK/positive.second.tokens" ||
    fail 'repeated token tape differs'
cmp "$WORK/positive.first.ir" "$WORK/positive.remapped.ir" ||
    fail 'typed IR depends on the absolute checkout path'
cmp "$WORK/positive.first.tokens" "$WORK/positive.remapped.tokens" ||
    fail 'token tape depends on the absolute checkout path'

run_positive() {
    stem=$1
    "$FRONTEND" "$CASES/$stem.kofun" \
        "$WORK/$stem.ir" "$WORK/$stem.tokens" \
        >"$WORK/$stem.stdout" 2>"$WORK/$stem.stderr"
    test ! -s "$WORK/$stem.stdout" || fail "$stem wrote stdout"
    test ! -s "$WORK/$stem.stderr" || fail "$stem wrote stderr"
}

run_positive annotations
grep -F \
    'binding-id=binding:checked:0|name=checked|role=let|scheme=(Int -> Int)' \
    "$WORK/annotations.ir" >/dev/null ||
    fail 'function annotation did not constrain the lambda'
grep -F 'result|type=Int|' "$WORK/annotations.ir" >/dev/null ||
    fail 'annotated fixture did not infer Int'

run_positive capture
grep -F \
    "binding-id=binding:ignore:0|name=ignore|role=let|scheme=forall 'a. ('a -> _0)" \
    "$WORK/capture.ir" >/dev/null ||
    fail 'captured outer metavariable was quantified by the inner binding'
grep -F 'binding-id=binding:ignore:0|name=ignore|type=(Int -> _0)' \
    "$WORK/capture.ir" >/dev/null ||
    fail 'first capture instantiation is missing'
grep -F 'binding-id=binding:ignore:0|name=ignore|type=(Bool -> _0)' \
    "$WORK/capture.ir" >/dev/null ||
    fail 'second capture instantiation is missing'

run_positive shadowing
grep -F 'use|binding-id=binding:value:0|name=value|type=Int|' \
    "$WORK/shadowing.ir" >/dev/null ||
    fail 'first shadowed use lost its BindingId'
grep -F 'use|binding-id=binding:value:1|name=value|type=Bool|' \
    "$WORK/shadowing.ir" >/dev/null ||
    fail 'second shadowed use lost its BindingId'

run_positive order-first
run_positive order-second
for suffix in first second; do
    grep '^binding|.*|role=let|' "$WORK/order-$suffix.ir" \
        | sed 's/|span=[0-9][0-9]*\.\.[0-9][0-9]*$//' \
        | sort >"$WORK/order-$suffix.schemes"
done
cmp "$WORK/order-first.schemes" "$WORK/order-second.schemes" ||
    fail 'unrelated declaration order changed canonical let schemes'

run_positive alpha-first
run_positive alpha-second
for suffix in first second; do
    sed \
        -e 's/binding-id=[^|]*/binding-id=<id>/g' \
        -e 's/name=[^|]*/name=<name>/g' \
        -e 's/span=[0-9][0-9]*\.\.[0-9][0-9]*/span=<span>/g' \
        "$WORK/alpha-$suffix.ir" >"$WORK/alpha-$suffix.normalized.ir"
done
cmp "$WORK/alpha-first.normalized.ir" "$WORK/alpha-second.normalized.ir" ||
    fail 'alpha-renaming changed canonical schemes, use types, or result type'

failures='syntax occurs-check value-restriction level-escape unknown-binding recursion traits rows match effects ownership named-function mutable'
for stem in $failures; do
    set +e
    "$FRONTEND" "$CASES/$stem.kofun" \
        "$WORK/$stem.ir" "$WORK/$stem.tokens" \
        >"$WORK/$stem.actual" 2>"$WORK/$stem.internal.stderr"
    status=$?
    set -e
    test "$status" -eq 1 || fail "$stem exited $status instead of 1"
    cmp "$CASES/$stem.stdout" "$WORK/$stem.actual" ||
        fail "$stem diagnostic differs"
    test ! -s "$WORK/$stem.internal.stderr" ||
        fail "$stem wrote internal stderr"
    test ! -e "$WORK/$stem.ir" || fail "$stem emitted rejected typed IR"
    test ! -e "$WORK/$stem.tokens" || fail "$stem emitted rejected tokens"
done

awk 'BEGIN {
    printf "fn main() {\n    "
    for (i = 0; i < 140; i++) printf "("
    printf "1"
    for (i = 0; i < 140; i++) printf ")"
    printf "\n}\n"
}' >"$WORK/nesting-limit.kofun"
set +e
"$FRONTEND" "$WORK/nesting-limit.kofun" \
    "$WORK/nesting-limit.ir" "$WORK/nesting-limit.tokens" \
    >"$WORK/nesting-limit.stdout" 2>"$WORK/nesting-limit.stderr"
status=$?
set -e
test "$status" -eq 1 || fail "nesting limit exited $status instead of 1"
grep '^error\[HML007\]: expression nesting limit is 128 ' \
    "$WORK/nesting-limit.stdout" >/dev/null ||
    fail 'nesting limit did not produce its bounded diagnostic'
test ! -s "$WORK/nesting-limit.stderr" || fail 'nesting limit wrote internal stderr'
test ! -e "$WORK/nesting-limit.ir" || fail 'nesting limit emitted typed IR'
test ! -e "$WORK/nesting-limit.tokens" || fail 'nesting limit emitted tokens'

awk 'BEGIN {
    printf "fn main() {\n    1\n}\n"
    for (i = 0; i < 65537; i++) printf " "
}' >"$WORK/source-limit.kofun"
set +e
"$FRONTEND" "$WORK/source-limit.kofun" \
    "$WORK/source-limit.ir" "$WORK/source-limit.tokens" \
    >"$WORK/source-limit.stdout" 2>"$WORK/source-limit.stderr"
status=$?
set -e
test "$status" -eq 1 || fail "source limit exited $status instead of 1"
grep '^error\[HML007\]: source limit is 65536 bytes ' \
    "$WORK/source-limit.stdout" >/dev/null ||
    fail 'source limit did not produce its bounded diagnostic'
test ! -s "$WORK/source-limit.stderr" || fail 'source limit wrote internal stderr'
test ! -e "$WORK/source-limit.ir" || fail 'source limit emitted typed IR'
test ! -e "$WORK/source-limit.tokens" || fail 'source limit emitted tokens'

ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
    "$SANITIZED" "$CASES/positive.kofun" \
    "$WORK/sanitize.ir" "$WORK/sanitize.tokens" \
    >"$WORK/sanitize.stdout" 2>"$WORK/sanitize.stderr"
test ! -s "$WORK/sanitize.stdout" ||
    fail 'sanitized positive run wrote stdout'
test ! -s "$WORK/sanitize.stderr" ||
    fail 'ASan/UBSan reported a positive-path finding'
cmp "$CASES/positive.ir" "$WORK/sanitize.ir" ||
    fail 'sanitized positive typed IR differs'

for stem in $failures; do
    set +e
    ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
        "$SANITIZED" "$CASES/$stem.kofun" \
        "$WORK/$stem.sanitize.ir" "$WORK/$stem.sanitize.tokens" \
        >"$WORK/$stem.sanitize.actual" \
        2>"$WORK/$stem.sanitize.internal.stderr"
    status=$?
    set -e
    test "$status" -eq 1 ||
        fail "sanitized $stem exited $status instead of 1"
    cmp "$CASES/$stem.stdout" "$WORK/$stem.sanitize.actual" ||
        fail "sanitized $stem diagnostic differs"
    test ! -s "$WORK/$stem.sanitize.internal.stderr" ||
        fail "ASan/UBSan reported a finding for $stem"
    test ! -e "$WORK/$stem.sanitize.ir" ||
        fail "sanitized $stem emitted rejected typed IR"
    test ! -e "$WORK/$stem.sanitize.tokens" ||
        fail "sanitized $stem emitted rejected tokens"
done

test -z "$(find "$WORK" -type f \
    \( -name '*.generated.c' -o -name '*.o' -o -name '*.wasm' \
       -o -name '*.elf' -o -name '*.native' \) -print)" ||
    fail 'HM frontend emitted a backend/runtime artifact'

printf '%s\n' \
    'PASS: local lambda let-polymorphism instantiates at Int, Bool, and Text' \
    'PASS: captures, level lowering, shadow identities, and value restriction are exact' \
    'PASS: schemes and typed IR are deterministic across alpha-renaming, order, path, and repetition' \
    'PASS: unsafe work/output aliases preserve sources and prior artifacts' \
    'PASS: unsupported forms refuse without artifacts; analyzer and sanitizers are clean'
