#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
WORK=${KOFUN_MATCH_GUARD_FUZZ_WORK:-"$ROOT/build/match-guard-fuzz"}
CASES=${KOFUN_MATCH_GUARD_FUZZ_CASES:-32}
CC=${CC:-cc}

case $CASES in
    ''|*[!0-9]*|0)
        printf '%s\n' "match-guard fuzz: case count must be positive" >&2
        exit 2
        ;;
esac

rm -rf "$WORK"
mkdir -p "$WORK"
"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$ROOT/bootstrap/stage2/compiler.c" \
    -o "$WORK/kofun-stage2"
"$CC" -std=c11 -O1 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$ROOT/bootstrap/stage2/compiler.c" \
    -o "$WORK/kofun-stage2-sanitized"

seed=610839776
next_random() {
    seed=$(((seed * 1103515245 + 12345) % 2147483648))
}

case_index=0
while test "$case_index" -lt "$CASES"; do
    next_random
    first_probe=$((seed % 1000 + 10))
    next_random
    second_probe=$((seed % 1000 + 1010))
    next_random
    selected=$((seed % 1000 + 2010))

    if test $((case_index % 2)) -eq 0; then
        scrutinee=true
        matching=true
        nonmatching=false
    else
        scrutinee=false
        matching=false
        nonmatching=true
    fi

    case_work="$WORK/case-$case_index"
    mkdir -p "$case_work"
    source="$case_work/program.kofun"
    {
        printf '%s\n' 'fn probe(value: Int) -> Int {'
        printf '%s\n' '    print(value)'
        printf '%s\n' '    return value'
        printf '%s\n' '}'
        printf '%s\n' ''
        printf '%s\n' 'fn main() -> Int {'
        printf '    match %s {\n' "$scrutinee"
        printf '        %s if 1 // 0 == 0 => {\n' "$nonmatching"
        printf '%s\n' '            print(9001)'
        printf '%s\n' '        },'
        printf '        %s if probe(%s) == %s => {\n' \
            "$matching" "$first_probe" "$((first_probe + 1))"
        printf '%s\n' '            print(9002)'
        printf '%s\n' '        },'
        printf '        %s if probe(%s) == %s => {\n' \
            "$matching" "$second_probe" "$second_probe"
        printf '            print(%s)\n' "$selected"
        printf '%s\n' '        },'
        printf '        %s if 1 // 0 == 0 => {\n' "$matching"
        printf '%s\n' '            print(9003)'
        printf '%s\n' '        },'
        printf '%s\n' '        true => {'
        printf '%s\n' '            print(9004)'
        printf '%s\n' '        },'
        printf '%s\n' '        false => {'
        printf '%s\n' '            print(9005)'
        printf '%s\n' '        },'
        printf '%s\n' '    }'
        printf '%s\n' '    return 0'
        printf '%s\n' '}'
    } >"$source"

    "$WORK/kofun-stage2" \
        "$source" \
        "$case_work/program.c" \
        "$case_work/program.ir" \
        "$case_work/program.tokens" >/dev/null
    ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
    UBSAN_OPTIONS=halt_on_error=1 \
        "$WORK/kofun-stage2-sanitized" \
        "$source" \
        "$case_work/program-sanitized.c" \
        "$case_work/program-sanitized.ir" \
        "$case_work/program-sanitized.tokens" >/dev/null
    cmp "$case_work/program.c" "$case_work/program-sanitized.c"
    cmp "$case_work/program.ir" "$case_work/program-sanitized.ir"
    cmp "$case_work/program.tokens" "$case_work/program-sanitized.tokens"

    "$CC" -std=c11 -O2 -Wall -Wextra -Werror \
        "$case_work/program.c" -o "$case_work/program"
    "$case_work/program" \
        >"$case_work/actual.stdout" \
        2>"$case_work/actual.stderr"
    {
        printf '%s\n' "$first_probe"
        printf '%s\n' "$second_probe"
        printf '%s\n' "$selected"
    } >"$case_work/expected.stdout"
    cmp "$case_work/expected.stdout" "$case_work/actual.stdout"
    test ! -s "$case_work/actual.stderr"

    case_index=$((case_index + 1))
done

printf '%s\n' \
    "PASS: match-guard fuzz preserved ordered selected-only evaluation for $CASES programs"
