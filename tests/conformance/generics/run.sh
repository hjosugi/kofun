#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CASES="$ROOT/tests/conformance/generics"
CC=${CC:-cc}
ANALYZER_CC=${ANALYZER_CC:-gcc}
WORK=${KOFUN_GENERICS_FRONTEND_WORK:-"$ROOT/build/generics-frontend"}

fail() {
    printf '%s\n' "FAIL: $*" >&2
    exit 1
}

command -v "$CC" >/dev/null 2>&1 || fail 'a C11 compiler is required'
command -v "$ANALYZER_CC" >/dev/null 2>&1 ||
    fail 'GCC is required for the static analyzer gate'
case $WORK in
    */generics-frontend|*/generics-frontend.*) ;;
    *) fail "work directory must end in generics-frontend[.suffix]: $WORK" ;;
esac
rm -rf "$WORK"
mkdir -p "$WORK/remapped"

"$CC" -std=c11 -O2 -Wall -Wextra -Werror -pedantic \
    "$ROOT/bootstrap/stage2/generics_frontend.c" \
    -o "$WORK/kofun-generics-frontend"
"$ANALYZER_CC" -std=c11 -O0 -g -Wall -Wextra -Werror -pedantic \
    -fanalyzer "$ROOT/bootstrap/stage2/generics_frontend.c" \
    -o "$WORK/kofun-generics-analyzer"
"$CC" -std=c11 -O1 -g -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$ROOT/bootstrap/stage2/generics_frontend.c" \
    -o "$WORK/kofun-generics-sanitize"

cp "$CASES/positive.kofun" "$WORK/remapped/positive.kofun"
for suffix in first second remapped; do
    source="$CASES/positive.kofun"
    test "$suffix" = remapped && source="$WORK/remapped/positive.kofun"
    "$WORK/kofun-generics-frontend" "$source" \
        "$WORK/positive.$suffix.ir" "$WORK/positive.$suffix.tokens" \
        >"$WORK/positive.$suffix.stdout" \
        2>"$WORK/positive.$suffix.stderr"
    test ! -s "$WORK/positive.$suffix.stdout" ||
        fail "$suffix positive run wrote stdout"
    test ! -s "$WORK/positive.$suffix.stderr" ||
        fail "$suffix positive run wrote stderr"
done
cmp "$WORK/positive.first.ir" "$WORK/positive.second.ir" ||
    fail 'repeated generic IR differs'
cmp "$WORK/positive.first.tokens" "$WORK/positive.second.tokens" ||
    fail 'repeated generic token tape differs'
cmp "$WORK/positive.first.ir" "$WORK/positive.remapped.ir" ||
    fail 'generic IR depends on the host source path'
cmp "$WORK/positive.first.tokens" "$WORK/positive.remapped.tokens" ||
    fail 'generic token tape depends on the host source path'
cmp "$CASES/positive.ir" "$WORK/positive.first.ir" ||
    fail 'positive generic typed IR differs from its golden'

grep -F \
    'type-parameter-id=type-parameter:function:identity:0' \
    "$WORK/positive.first.ir" >/dev/null ||
    fail 'identity TypeParameterId is missing'
grep -F \
    'type-parameter-id=type-parameter:function:later:0' \
    "$WORK/positive.first.ir" >/dev/null ||
    fail 'same-spelled T was captured across declarations'
grep -F \
    'callee=function:identity|type-arguments=builtin:Int' \
    "$WORK/positive.first.ir" >/dev/null ||
    fail 'identity[Int] substitution is missing'
grep -F \
    'callee=function:identity|type-arguments=builtin:Bool' \
    "$WORK/positive.first.ir" >/dev/null ||
    fail 'identity[Bool] substitution is missing'
grep -F \
    'callee=function:later|type-arguments=builtin:Text' \
    "$WORK/positive.first.ir" >/dev/null ||
    fail 'forward generic call did not resolve'
grep -F \
    'callee=function:first|type-arguments=builtin:Int,builtin:Bool' \
    "$WORK/positive.first.ir" >/dev/null ||
    fail 'two-parameter substitution is missing'

failures='
annotated_result_mismatch:E2S82
bounds_unsupported:E2S83
duplicate_type_parameter:E2S80
missing_type_arguments:E2S81
recursive_unsupported:E2S83
substituted_argument_mismatch:E2S82
too_few_type_arguments:E2S81
too_many_type_arguments:E2S81
type_parameter_as_value:E2S80
type_parameter_limit:E2S84
unconstrained_type_parameter:E2S80
unknown_type_parameter:E2S80
'

previous_ifs=$IFS
IFS='
'
for entry in $failures; do
    test -n "$entry" || continue
    stem=${entry%%:*}
    code=${entry#*:}
    set +e
    "$WORK/kofun-generics-frontend" "$CASES/$stem.kofun" \
        "$WORK/$stem.ir" "$WORK/$stem.tokens" \
        >"$WORK/$stem.actual" 2>"$WORK/$stem.internal.stderr"
    status=$?
    set -e
    test "$status" -eq 1 ||
        fail "$stem exited $status instead of 1"
    cmp "$CASES/$stem.stderr" "$WORK/$stem.actual" ||
        fail "$stem diagnostic differs"
    grep -F "error[$code]:" "$WORK/$stem.actual" >/dev/null ||
        fail "$stem expected $code"
    test ! -s "$WORK/$stem.internal.stderr" ||
        fail "$stem wrote internal stderr"
    test ! -e "$WORK/$stem.ir" ||
        fail "$stem emitted rejected typed IR"
    test ! -e "$WORK/$stem.tokens" ||
        fail "$stem emitted rejected tokens"
done
IFS=$previous_ifs

ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
    "$WORK/kofun-generics-sanitize" "$CASES/positive.kofun" \
    "$WORK/sanitize.ir" "$WORK/sanitize.tokens" \
    >"$WORK/sanitize.stdout" 2>"$WORK/sanitize.stderr"
test ! -s "$WORK/sanitize.stdout" ||
    fail 'sanitized positive run wrote stdout'
test ! -s "$WORK/sanitize.stderr" ||
    fail 'ASan/UBSan reported a positive-path finding'
cmp "$CASES/positive.ir" "$WORK/sanitize.ir" ||
    fail 'sanitized generic IR differs'

IFS='
'
for entry in $failures; do
    test -n "$entry" || continue
    stem=${entry%%:*}
    set +e
    ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
        "$WORK/kofun-generics-sanitize" "$CASES/$stem.kofun" \
        "$WORK/$stem.sanitize.ir" "$WORK/$stem.sanitize.tokens" \
        >"$WORK/$stem.sanitize.actual" \
        2>"$WORK/$stem.sanitize.internal.stderr"
    status=$?
    set -e
    test "$status" -eq 1 ||
        fail "sanitized $stem exited $status instead of 1"
    cmp "$CASES/$stem.stderr" "$WORK/$stem.sanitize.actual" ||
        fail "sanitized $stem diagnostic differs"
    test ! -s "$WORK/$stem.sanitize.internal.stderr" ||
        fail "ASan/UBSan reported a finding for $stem"
    test ! -e "$WORK/$stem.sanitize.ir" ||
        fail "sanitized $stem emitted rejected typed IR"
    test ! -e "$WORK/$stem.sanitize.tokens" ||
        fail "sanitized $stem emitted rejected tokens"
done
IFS=$previous_ifs

test -z "$(find "$WORK" -type f \
    \( -name '*.generated.c' -o -name '*.o' -o -name '*.wasm' \
       -o -name '*.elf' -o -name '*.native' \) -print)" ||
    fail 'generic frontend emitted a backend/runtime artifact'

printf '%s\n' \
    'PASS: explicit generic declarations and calls produce typed IR' \
    'PASS: declaration-scoped TypeParameterIds and substitutions are stable' \
    'PASS: type arity precedes value checking across exact diagnostics' \
    'PASS: typed-only boundaries, GCC analyzer, and ASan/UBSan remain clean'
