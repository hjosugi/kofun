#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CASES="$ROOT/tests/conformance/optional"
CC=${CC:-cc}
ANALYZER_CC=${ANALYZER_CC:-gcc}
WORK=${KOFUN_OPTIONAL_FRONTEND_WORK:-"$ROOT/build/optional-frontend"}

fail() {
    printf '%s\n' "FAIL: $*" >&2
    exit 1
}

command -v "$CC" >/dev/null 2>&1 || fail 'a C11 compiler is required'
command -v "$ANALYZER_CC" >/dev/null 2>&1 ||
    fail 'GCC is required for the static analyzer gate'
case $WORK in
    */optional-frontend|*/optional-frontend.*) ;;
    *) fail "work directory must end in optional-frontend[.suffix]: $WORK" ;;
esac
rm -rf "$WORK"
mkdir -p "$WORK/remapped"

"$CC" -std=c11 -O2 -Wall -Wextra -Werror -pedantic \
    "$ROOT/bootstrap/stage2/optional_frontend.c" \
    -o "$WORK/kofun-optional-frontend"
"$ANALYZER_CC" -std=c11 -O0 -g -Wall -Wextra -Werror -pedantic \
    -fanalyzer "$ROOT/bootstrap/stage2/optional_frontend.c" \
    -o "$WORK/kofun-optional-analyzer"
"$CC" -std=c11 -O1 -g -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$ROOT/bootstrap/stage2/optional_frontend.c" \
    -o "$WORK/kofun-optional-sanitize"

cp "$CASES/positive.kofun" "$WORK/remapped/positive.kofun"
for suffix in first second remapped; do
    source="$CASES/positive.kofun"
    test "$suffix" = remapped && source="$WORK/remapped/positive.kofun"
    "$WORK/kofun-optional-frontend" "$source" \
        "$WORK/positive.$suffix.ir" "$WORK/positive.$suffix.tokens" \
        >"$WORK/positive.$suffix.stdout" \
        2>"$WORK/positive.$suffix.stderr"
    test ! -s "$WORK/positive.$suffix.stdout" ||
        fail "$suffix positive run wrote stdout"
    test ! -s "$WORK/positive.$suffix.stderr" ||
        fail "$suffix positive run wrote stderr"
done
cmp "$WORK/positive.first.ir" "$WORK/positive.second.ir" ||
    fail 'repeated Optional IR differs'
cmp "$WORK/positive.first.tokens" "$WORK/positive.second.tokens" ||
    fail 'repeated Optional token tape differs'
cmp "$WORK/positive.first.ir" "$WORK/positive.remapped.ir" ||
    fail 'Optional IR depends on the host source path'
cmp "$WORK/positive.first.tokens" "$WORK/positive.remapped.tokens" ||
    fail 'Optional token tape depends on the host source path'
cmp "$CASES/positive.ir" "$WORK/positive.first.ir" ||
    fail 'positive Optional typed IR differs from its golden'

# `null` is classified as its own token kind, not an identifier.
grep -q '^null|' "$WORK/positive.first.tokens" ||
    fail 'null was not classified as a keyword token'

# The suffix binds to the complete primary type before it, so these two are
# structurally distinct rather than two spellings of one type.
grep -F 'type=Optional(List(builtin:Int))' "$WORK/positive.first.ir" \
    >/dev/null || fail 'List[Int]? is not Optional(List(Int))'
grep -F 'type=List(Optional(builtin:Int))' "$WORK/positive.first.ir" \
    >/dev/null || fail 'List[Int?] is not List(Optional(Int))'

# Spans are recorded for the suffix as well as the underlying type, and a
# non-optional type carries no suffix span.
grep -F 'type=Optional(builtin:Int)|type-span=382..386|suffix-span=385..386' \
    "$WORK/positive.first.ir" >/dev/null ||
    fail 'the suffix span was not recorded'
grep -F 'name=concrete|type=builtin:Int|type-span=438..441|suffix-span=0..0' \
    "$WORK/positive.first.ir" >/dev/null ||
    fail 'a non-optional type carried a suffix span'

# `null` reaches typed IR as the expected optional, and the injection rule
# admits a concrete T under an expected Optional(T).
grep -F 'type=Optional(builtin:Int)|written=Optional(builtin:Int)|injected=no|null=yes' \
    "$WORK/positive.first.ir" >/dev/null ||
    fail 'contextually typed null is missing from typed IR'
grep -F 'type=Optional(builtin:Int)|written=builtin:Int|injected=yes|null=no' \
    "$WORK/positive.first.ir" >/dev/null ||
    fail 'Int was not injected into an expected Optional(Int) result'
# The injection is recorded, not assumed: a non-optional result never injects.
! grep -E 'type=builtin:Int\|written=[^|]*\|injected=yes' \
    "$WORK/positive.first.ir" >/dev/null ||
    fail 'an injection was recorded for a non-optional result'
grep -F 'name=present|type=Optional(builtin:Int)' \
    "$WORK/positive.first.ir" >/dev/null ||
    fail 'let present: Int? = 7 did not inject'

# Runtime representation is deferred, so nothing may name a tag or a layout.
! grep -Eq 'tag|niche|layout|discriminant' "$WORK/positive.first.ir" ||
    fail 'the frontend implied a runtime representation'

failures='
nested_optional:E2S137
nil_is_an_identifier:E2S139
null_condition:E2S140
null_under_concrete:E2S135
optional_argument:E2S136
optional_arithmetic:E2S136
optional_ownership_mode:E2S138
optional_void:E2S137
prefix_optional:E2S138
recovery_after_suffix:E2S137
unconstrained_null:E2S134
unknown_type:E2S141
expected_type:E2S141
list_missing_bracket:E2S141
list_unclosed:E2S141
unterminated_text:E2S141
unsupported_byte:E2S141
'

previous_ifs=$IFS
IFS='
'
for entry in $failures; do
    test -n "$entry" || continue
    stem=${entry%%:*}
    code=${entry#*:}
    set +e
    "$WORK/kofun-optional-frontend" "$CASES/$stem.kofun" \
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

# Recovery is bounded and does not fabricate: a malformed suffix reports, then
# parsing continues far enough to report the independent error after it, and
# no Optional node survives from the broken input.
recovered=$(grep -c '^error' "$CASES/recovery_after_suffix.stderr")
test "$recovered" -eq 2 ||
    fail "recovery reported $recovered diagnostics instead of 2"
grep -F 'error[E2S137]' "$CASES/recovery_after_suffix.stderr" >/dev/null ||
    fail 'recovery lost the malformed suffix diagnostic'
grep -F 'error[E2S135]' "$CASES/recovery_after_suffix.stderr" >/dev/null ||
    fail 'recovery did not reach the later independent declaration'
test ! -e "$WORK/recovery_after_suffix.ir" ||
    fail 'recovery fabricated typed IR from broken input'

# The refusal corpus is globbed rather than listed twice, so a fixture added
# without a gate entry stops the build (DD-022).
declared=$(printf '%s' "$failures" | grep -c ':')
present=$(find "$CASES" -name '*.stderr' -type f | wc -l | tr -d ' ')
test "$declared" -eq "$present" ||
    fail "gate lists $declared refusals but $present fixtures exist"

ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
    "$WORK/kofun-optional-sanitize" "$CASES/positive.kofun" \
    "$WORK/sanitize.ir" "$WORK/sanitize.tokens" \
    >"$WORK/sanitize.stdout" 2>"$WORK/sanitize.stderr"
test ! -s "$WORK/sanitize.stdout" ||
    fail 'sanitized positive run wrote stdout'
test ! -s "$WORK/sanitize.stderr" ||
    fail 'ASan/UBSan reported a positive-path finding'
cmp "$CASES/positive.ir" "$WORK/sanitize.ir" ||
    fail 'sanitized Optional IR differs'

IFS='
'
for entry in $failures; do
    test -n "$entry" || continue
    stem=${entry%%:*}
    set +e
    ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
        "$WORK/kofun-optional-sanitize" "$CASES/$stem.kofun" \
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
done
IFS=$previous_ifs

test -z "$(find "$WORK" -type f \
    \( -name '*.generated.c' -o -name '*.o' -o -name '*.wasm' \
       -o -name '*.elf' -o -name '*.native' \) -print)" ||
    fail 'Optional frontend emitted a backend/runtime artifact'

printf '%s\n' \
    'PASS: null and one postfix ? reach typed IR as Optional(TypeId)' \
    'PASS: List[Int]? and List[Int?] are structurally distinct' \
    'PASS: null is contextual, and T injects only into an expected Optional(T)' \
    'PASS: malformed suffix recovery is bounded and fabricates nothing' \
    'PASS: typed-only boundaries, GCC analyzer, and ASan/UBSan remain clean'
