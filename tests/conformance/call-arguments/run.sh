#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
cases="$root/tests/conformance/call-arguments"
diagnostics="$root/tests/diagnostics/stage2"
temporary=${TMPDIR:-/tmp}/kofun-call-arguments.$$
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary"

${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror \
    "$cases/observer_test.c" -o "$temporary/observer"

fail() {
    printf 'FAIL: %s\n' "$1" >&2
    exit 1
}

observations=$(
    "$temporary/observer" "$cases/typed_and_owned.kofun"
)
expected_observations='call-argument|choose|1|0|174|180|text|label|Text|copy
call-argument|choose|0|1|188|196|number|amount|Int|copy
call-argument|consume|0|0|218|224|into|file|Int|take'
actual_observations=$(printf '%s\n' "$observations" |
    grep '^call-argument|')
test "$actual_observations" = "$expected_observations" ||
    fail 'labelled arguments did not retain source order and fixed HIR slots'

scope_hir=$(
    "$temporary/observer" "$cases/typed_and_owned.kofun" scope
)
printf '%s\n' "$scope_hir" |
    grep -F '|amount|immutable|Int|copy|initialized|' >/dev/null ||
    fail 'the first internal parameter name did not bind in HIR'
printf '%s\n' "$scope_hir" |
    grep -F '|label|immutable|Text|copy|initialized|' >/dev/null ||
    fail 'the second internal parameter name did not bind in HIR'
printf '%s\n' "$scope_hir" |
    grep -F '|file|immutable|Int|take|initialized|' >/dev/null ||
    fail 'the take mode did not reach the bound HIR slot'
if printf '%s\n' "$scope_hir" |
    grep -E '\|(number|text|into)\|immutable\|' >/dev/null; then
    fail 'an external label became a lexical body binding'
fi

optional=$(
    "$temporary/observer" "$cases/reordered_optional.kofun" diagnostic
)
printf '%s\n' "$optional" |
    grep -F 'error[E2S158]: labelled-call ABI lowering is owned by #882' \
        >/dev/null ||
    fail 'a reordered Int? argument did not pass fixed-slot checking'
mismatch=$(
    "$temporary/observer" \
        "$cases/reordered_optional_mismatch.kofun" diagnostic
)
test "$mismatch" = \
    'error[E2S147]: `input` is `Int?`; narrow it with a `null` comparison before using it as `Int` at byte 167' ||
    fail 'the existing optional type check did not use the bound label slot'

duplicate=$(
    "$temporary/observer" "$cases/label_only_duplicate.kofun" diagnostic
)
printf '%s\n' "$duplicate" |
    grep -F 'error[E2S16]: duplicate Core function `identity`' >/dev/null ||
    fail 'external labels participated in callable selection'

"$temporary/observer" \
    "$diagnostics/e2s162_unknown_call_label.kofun" E2S162 - ||
    fail 'E2S162 structured primary span changed'
"$temporary/observer" \
    "$diagnostics/e2s163_duplicate_call_label.kofun" E2S163 in ||
    fail 'E2S163 declaration-related span changed'
"$temporary/observer" \
    "$diagnostics/e2s164_missing_call_argument.kofun" E2S164 from ||
    fail 'E2S164 declaration-related span changed'
"$temporary/observer" \
    "$diagnostics/e2s165_positional_after_label.kofun" E2S165 - ||
    fail 'E2S165 structured primary span changed'
"$temporary/observer" \
    "$diagnostics/e2s166_internal_name_as_label.kofun" E2S166 text ||
    fail 'E2S166 declaration-related span changed'

printf '%s\n' \
    'PASS: labelled calls bind once in source order to typed, owned declaration slots; #882 remains the ABI boundary'
