#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -P -- "$(dirname -- "$0")/../../../.." && pwd)
WORK=${KOFUN_DENSE_ENUM_WORK:-"$ROOT/build/dense-enum-dispatch"}
CC=${CC:-cc}
. "$ROOT/bootstrap/stage2/build.sh"

rm -rf "$WORK"
mkdir -p "$WORK"
kofun_stage2_build "$ROOT" "$WORK/kofun-stage2"

fail() {
    printf '%s\n' "FAIL dense enum dispatch: $*" >&2
    exit 1
}

case_value() {
    index=$1
    printf '%s\n' "$(((index * index * 17 + index * 13 + 97) % 100003))"
}

assert_assembly_shape() {
    size=$1
    c_source=$2
    assembly="$WORK/dense-$size.s"
    function_assembly="$WORK/dense-$size-classify.s"
    extra_flags=
    compiler_version=$($CC --version 2>/dev/null | sed -n '1p')
    case $compiler_version in
        *GCC*|*gcc*|*Free\ Software\ Foundation*)
            extra_flags=-fno-ipa-cp
            ;;
    esac
    # Keep the enum parameter opaque at the dispatch observation point. This
    # prevents whole-program constant propagation from erasing a valid switch.
    # It does not direct the compiler's switch/jump-table decision.
    # shellcheck disable=SC2086
    "$CC" -std=c11 -O2 -fno-inline $extra_flags -S \
        "$c_source" -o "$assembly"
    awk '
        /^kofun_fn_classify[^:]*:/ { copying = 1 }
        copying { print }
        copying && /^[[:space:]]*\.size[[:space:]]+kofun_fn_classify[^,]*,/ {
            exit
        }
    ' "$assembly" >"$function_assembly"
    test -s "$function_assembly" ||
        fail "$size-case assembly omitted kofun_fn_classify"

    conditional_branches=$(grep -Ec \
        '^[[:space:]]+j(a|ae|b|be|c|e|g|ge|l|le|na|nae|nb|nbe|nc|ne|ng|nge|nl|nle|no|np|ns|nz|o|p|pe|po|s|z)[[:space:]]' \
        "$function_assembly" || true)
    test "$conditional_branches" -le 8 ||
        fail "$size-case assembly has $conditional_branches conditional branches"
    grep -Eq \
        'jmp[a-z]*[[:space:]]+\*|\([^)]*,%[[:alnum:]]+,[1248]\)' \
        "$function_assembly" ||
        fail "$size-case assembly has no indexed dispatch or lookup"
}

run_dense_case() {
    size=$1
    middle=$((size / 2))
    last=$((size - 1))
    source="$WORK/dense-$size.kofun"
    c_source="$WORK/dense-$size.c"
    expected="$WORK/dense-$size.expected"
    actual="$WORK/dense-$size.actual"

    {
        printf 'type Dense%s =' "$size"
        index=0
        while test "$index" -lt "$size"; do
            printf ' | C%s' "$index"
            index=$((index + 1))
        done
        printf '\n\nfn classify(value: Dense%s) -> Int {\n' "$size"
        printf '%s\n' '    let mut result = 0' '    match value {'
        index=0
        while test "$index" -lt "$size"; do
            printf '        C%s => { result = %s },\n' \
                "$index" "$(case_value "$index")"
            index=$((index + 1))
        done
        printf '%s\n' '    }' '    return result' '}' '' 'fn main() {'
        printf '    let first: Dense%s = C0\n' "$size"
        printf '    let middle: Dense%s = C%s\n' "$size" "$middle"
        printf '    let last: Dense%s = C%s\n' "$size" "$last"
        printf '%s\n' \
            '    print(classify(first))' \
            '    print(classify(middle))' \
            '    print(classify(last))'
        printf '%s\n' '}'
    } >"$source"
    {
        case_value 0
        case_value "$middle"
        case_value "$last"
    } >"$expected"

    "$WORK/kofun-stage2" "$source" "$c_source" \
        "$WORK/dense-$size.ir" "$WORK/dense-$size.tokens" >/dev/null
    "$CC" -std=c11 -O2 -Wall -Wextra -Werror \
        "$c_source" -o "$WORK/dense-$size"
    "$WORK/dense-$size" >"$actual"
    cmp "$expected" "$actual" ||
        fail "$size-case first/middle/last semantic output differs"

    switch_count=$(grep -c 'switch (kofun_match_value.tag)' "$c_source")
    test "$switch_count" -eq 1 ||
        fail "$size-case emitted $switch_count switches instead of one"
    case_count=$(grep -c 'case INT64_C(' "$c_source")
    test "$case_count" -eq "$size" ||
        fail "$size-case emitted $case_count cases"
    if grep -q 'if (!kofun_match_selected && kofun_match_value.tag' \
        "$c_source"
    then
        fail "$size-case retained the ordered per-arm comparison chain"
    fi
    if grep -q 'bool kofun_match_selected = false;' "$c_source"; then
        fail "$size-case retained selected-state dispatch"
    fi

    if test "$size" -ge 32; then
        target=$($CC -dumpmachine 2>/dev/null || true)
        case $target in
            x86_64*) assert_assembly_shape "$size" "$c_source" ;;
            *) printf '%s\n' "SKIP dense enum $size assembly shape on $target" ;;
        esac
    fi
    printf '%s\n' \
        "PASS dense enum $size: first/middle/last, one switch, non-linear dispatch"
}

run_ineligible_case() {
    source="$WORK/ineligible.kofun"
    c_source="$WORK/ineligible.c"
    printf '%s\n' \
        'type Signal = | Red | Green | Blue' \
        'type Reply = | Ready(value: Int) | Pending' \
        '' \
        'fn probe(value: Int) -> Int {' \
        '    print(value)' \
        '    return value' \
        '}' \
        '' \
        'fn main() {' \
        '    let guarded: Signal = Red' \
        '    match guarded {' \
        '        Blue if 1 // 0 == 0 => { print(9001) },' \
        '        Red if probe(4) == 5 => { print(9002) },' \
        '        Red if probe(5) == 5 => { print(42) },' \
        '        _ => { print(9003) },' \
        '    }' \
        '    let wildcard: Signal = Green' \
        '    match wildcard {' \
        '        Red => { print(9004) },' \
        '        _ => { print(7) },' \
        '    }' \
        '    let reply: Reply = Ready(42)' \
        '    match reply {' \
        '        Ready(value) => { print(value) },' \
        '        Pending => { print(9005) },' \
        '    }' \
        '}' >"$source"
    printf '%s\n' 4 5 42 7 42 >"$WORK/ineligible.expected"

    "$WORK/kofun-stage2" "$source" "$c_source" \
        "$WORK/ineligible.ir" "$WORK/ineligible.tokens" >/dev/null
    "$CC" -std=c11 -O2 -Wall -Wextra -Werror \
        "$c_source" -o "$WORK/ineligible"
    "$WORK/ineligible" >"$WORK/ineligible.actual"
    cmp "$WORK/ineligible.expected" "$WORK/ineligible.actual" ||
        fail 'ineligible ordered semantics changed'
    if grep -q 'switch (kofun_match_value.tag)' "$c_source"; then
        fail 'guarded, wildcard, or payload match emitted a switch'
    fi
    ordered_count=$(grep -c \
        'if (!kofun_match_selected && kofun_match_value.tag' "$c_source")
    test "$ordered_count" -ge 4 ||
        fail 'ineligible matches did not retain ordered lowering'
    printf '%s\n' \
        'PASS ineligible enum matches retain guarded/wildcard/payload ordering'
}

run_dense_case 8
run_dense_case 32
run_dense_case 64
run_ineligible_case
printf '%s\n' 'PASS dense enum dispatch gate'
