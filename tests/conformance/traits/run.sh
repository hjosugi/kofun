#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CASES="$ROOT/tests/conformance/traits"
CC=${CC:-cc}
ANALYZER_CC=${ANALYZER_CC:-gcc}
WORK=${KOFUN_TRAITS_FRONTEND_WORK:-"$ROOT/build/traits-frontend"}

fail() {
    printf '%s\n' "FAIL: $*" >&2
    exit 1
}

command -v "$CC" >/dev/null 2>&1 || fail 'a C11 compiler is required'
command -v "$ANALYZER_CC" >/dev/null 2>&1 ||
    fail 'GCC is required for the static analyzer gate'
case $WORK in
    */traits-frontend|*/traits-frontend.*) ;;
    *) fail "work directory must end in traits-frontend[.suffix]: $WORK" ;;
esac
rm -rf "$WORK"
mkdir -p "$WORK/remapped"

"$CC" -std=c11 -O2 -Wall -Wextra -Werror -pedantic \
    "$ROOT/bootstrap/stage2/traits_frontend.c" \
    -o "$WORK/kofun-traits-frontend"
"$ANALYZER_CC" -std=c11 -O0 -g -Wall -Wextra -Werror -pedantic \
    -fanalyzer "$ROOT/bootstrap/stage2/traits_frontend.c" \
    -o "$WORK/kofun-traits-analyzer"
"$CC" -std=c11 -O1 -g -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$ROOT/bootstrap/stage2/traits_frontend.c" \
    -o "$WORK/kofun-traits-sanitize"

cp "$CASES/positive.kofun" "$WORK/remapped/positive.kofun"
for suffix in first second remapped; do
    source="$CASES/positive.kofun"
    test "$suffix" = remapped && source="$WORK/remapped/positive.kofun"
    "$WORK/kofun-traits-frontend" "$source" \
        "$WORK/positive.$suffix.ir" "$WORK/positive.$suffix.tokens" \
        >"$WORK/positive.$suffix.stdout" \
        2>"$WORK/positive.$suffix.stderr"
    test ! -s "$WORK/positive.$suffix.stdout" ||
        fail "$suffix positive run wrote stdout"
    test ! -s "$WORK/positive.$suffix.stderr" ||
        fail "$suffix positive run wrote stderr"
done
cmp "$WORK/positive.first.ir" "$WORK/positive.second.ir" ||
    fail 'repeated trait IR differs'
cmp "$WORK/positive.first.tokens" "$WORK/positive.second.tokens" ||
    fail 'repeated trait token tape differs'
cmp "$WORK/positive.first.ir" "$WORK/positive.remapped.ir" ||
    fail 'trait IR depends on the host source path'
cmp "$WORK/positive.first.tokens" "$WORK/positive.remapped.tokens" ||
    fail 'trait token tape depends on the host source path'
cmp "$CASES/positive.ir" "$WORK/positive.first.ir" ||
    fail 'positive trait typed IR differs from its golden'

# Stable identities: TraitId carries provenance, MethodId carries the
# declaration-order slot, and ImplementationId carries the ABI schema version,
# the package, the trait, the normalized concrete arguments, the outer nominal
# self-type, and the implementation declaration.
grep -F 'trait-id=trait:local:Equal' "$WORK/positive.first.ir" >/dev/null ||
    fail 'local TraitId is missing'
grep -F 'trait-id=trait:foreign:Display' "$WORK/positive.first.ir" >/dev/null ||
    fail 'foreign TraitId is missing'
grep -F 'method-id=method:trait:local:Equal:0' \
    "$WORK/positive.first.ir" >/dev/null ||
    fail 'MethodId slot is missing'
grep -F 'implementation-id=impl:abi1/package:local/trait:local:Equal/args=builtin:Int/self=builtin:Int/decl=0' \
    "$WORK/positive.first.ir" >/dev/null ||
    fail 'ImplementationId does not carry every identity component'

# The #403 orphan rule admits a foreign trait for a local type and a local
# trait for a foreign type; only both-foreign is refused.
grep -F 'implementation-id=impl:abi1/package:local/trait:foreign:Display/args=nominal:local:Money/self=nominal:local:Money' \
    "$WORK/positive.first.ir" >/dev/null ||
    fail 'foreign trait over a local type was not admitted'
grep -F 'implementation-id=impl:abi1/package:local/trait:local:Equal/args=nominal:foreign:Duration/self=nominal:foreign:Duration' \
    "$WORK/positive.first.ir" >/dev/null ||
    fail 'local trait over a foreign type was not admitted'

# The bound is recorded, the method call resolves through it, and each explicit
# call selects exactly one implementation.
grep -F 'bound|owner=function:same|type-parameter=type-parameter:function:same:0|trait=trait:local:Equal' \
    "$WORK/positive.first.ir" >/dev/null ||
    fail 'the declared bound is missing'
grep -F 'method-call|caller=function:same|method=method:trait:local:Equal:0|via-bound=type-parameter:function:same:0' \
    "$WORK/positive.first.ir" >/dev/null ||
    fail 'the method call did not resolve through the bound'
selected=$(grep -c 'selected-implementation=impl:' "$WORK/positive.first.ir")
test "$selected" -eq 3 ||
    fail "expected 3 resolved calls, found $selected"
test "$(grep -c 'selected-implementation=none' "$WORK/positive.first.ir")" -eq 0 ||
    fail 'a bounded call left its implementation unresolved'

# No runtime lowering is claimed: the IR names no dictionary and no
# monomorphised instance.
! grep -Eq 'dictionary|monomorph|vtable' "$WORK/positive.first.ir" ||
    fail 'the frontend claimed a runtime lowering'

failures='
blanket_implementation:E2S132
default_method:E2S132
duplicate_trait:E2S127
method_arity_mismatch:E2S128
method_name_mismatch:E2S127
method_parameter_mismatch:E2S128
method_result_mismatch:E2S128
missing_implementation:E2S129
multiple_bounds:E2S132
orphan_alias_ownership:E2S131
orphan_both_foreign:E2S131
overlapping_implementation:E2S130
recursive_bound:E2S132
trait_arity_mismatch:E2S127
two_type_parameter_trait:E2S132
unbounded_method_call:E2S129
'

previous_ifs=$IFS
IFS='
'
for entry in $failures; do
    test -n "$entry" || continue
    stem=${entry%%:*}
    code=${entry#*:}
    set +e
    "$WORK/kofun-traits-frontend" "$CASES/$stem.kofun" \
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

# An alias never confers ownership: the refusal names the type the alias
# resolves to, not the alias.
grep -F 'nominal:foreign:Duration' \
    "$CASES/orphan_alias_ownership.stderr" >/dev/null ||
    fail 'the alias refusal did not resolve to the foreign type'

# Declaration order must not select between candidates. `order_independence`
# is the positive program with every implementation declared in the opposite
# order; each call must reach the same trait and self-type, so only the
# declaration ordinal each identity carries may move.
"$WORK/kofun-traits-frontend" "$CASES/order_independence.kofun" \
    "$WORK/order.ir" "$WORK/order.tokens" \
    >"$WORK/order.stdout" 2>"$WORK/order.stderr"
test ! -s "$WORK/order.stdout" || fail 'reordered run wrote stdout'
test ! -s "$WORK/order.stderr" || fail 'reordered run wrote stderr'
selection() {
    grep '^call|' "$1" |
        sed -e 's/.*callee=\(function:[a-z_]*\)|.*/\1/' >"$2.callee"
    grep '^call|' "$1" |
        sed -e 's/.*selected-implementation=impl:[^/]*\/package:[^/]*\///' \
            -e 's/\/decl=[0-9]*|.*//' >"$2.selection"
}
selection "$WORK/positive.first.ir" "$WORK/declared"
selection "$WORK/order.ir" "$WORK/reordered"
cmp "$WORK/declared.callee" "$WORK/reordered.callee" ||
    fail 'reordering the implementations changed which function is called'
cmp "$WORK/declared.selection" "$WORK/reordered.selection" ||
    fail 'declaration order selected between implementation candidates'
test -s "$WORK/declared.selection" ||
    fail 'no selections were compared for order independence'

ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
    "$WORK/kofun-traits-sanitize" "$CASES/positive.kofun" \
    "$WORK/sanitize.ir" "$WORK/sanitize.tokens" \
    >"$WORK/sanitize.stdout" 2>"$WORK/sanitize.stderr"
test ! -s "$WORK/sanitize.stdout" ||
    fail 'sanitized positive run wrote stdout'
test ! -s "$WORK/sanitize.stderr" ||
    fail 'ASan/UBSan reported a positive-path finding'
cmp "$CASES/positive.ir" "$WORK/sanitize.ir" ||
    fail 'sanitized trait IR differs'

IFS='
'
for entry in $failures; do
    test -n "$entry" || continue
    stem=${entry%%:*}
    set +e
    ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
        "$WORK/kofun-traits-sanitize" "$CASES/$stem.kofun" \
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

# The refusal corpus is globbed rather than listed twice, so a fixture added
# without a gate entry stops the build (DD-022).
declared=$(printf '%s' "$failures" | grep -c ':')
present=$(find "$CASES" -name '*.stderr' -type f | wc -l | tr -d ' ')
test "$declared" -eq "$present" ||
    fail "gate lists $declared refusals but $present fixtures exist"

test -z "$(find "$WORK" -type f \
    \( -name '*.generated.c' -o -name '*.o' -o -name '*.wasm' \
       -o -name '*.elf' -o -name '*.native' \) -print)" ||
    fail 'trait frontend emitted a backend/runtime artifact'

printf '%s\n' \
    'PASS: traits, implementations, and bounded calls produce typed IR' \
    'PASS: TraitId, MethodId, and ImplementationId identities are stable' \
    'PASS: the #403 orphan rule admits and refuses exactly its cases' \
    'PASS: bound resolution yields one implementation or a stable diagnostic' \
    'PASS: typed-only boundaries, GCC analyzer, and ASan/UBSan remain clean'
