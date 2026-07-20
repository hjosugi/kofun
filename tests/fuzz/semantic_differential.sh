#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
WORK=${KOFUN_SEMANTIC_FUZZ_WORK:-"$ROOT/build/semantic-fuzz"}
CASES=${KOFUN_SEMANTIC_FUZZ_CASES:-48}
KOFUN="$ROOT/bin/kofun"

case $CASES in
    ''|*[!0-9]*|0)
        printf '%s\n' "semantic fuzz: case count must be positive" >&2
        exit 2
        ;;
esac

rm -rf "$WORK"
mkdir -p "$WORK"

seed=195936478
next_random() {
    seed=$(((seed * 1103515245 + 12345) % 2147483648))
}

case_index=0
while test "$case_index" -lt "$CASES"; do
    next_random
    left=$((seed % 9 + 1))
    next_random
    right=$((seed % 9 + 1))
    next_random
    factor=$((seed % 4 + 1))
    next_random
    shape=$((seed % 3))

    case $shape in
        0)
            expression="$left + $right + 20"
            expected=$((left + right + 20))
            ;;
        1)
            expression="($left + $right) * $factor"
            expected=$(((left + right) * factor))
            ;;
        *)
            expression="$left * $right + 10"
            expected=$((left * right + 10))
            ;;
    esac
    if test "$expected" -lt 10 || test "$expected" -gt 99; then
        continue
    fi

    case_work="$WORK/case-$case_index"
    mkdir -p "$case_work"
    source="$case_work/program.kofun"
    {
        printf '%s\n' 'fn main() {'
        printf '    print(%s)\n' "$expression"
        printf '%s\n' '}'
    } >"$source"

    "$KOFUN" build "$source" \
        --backend c \
        --emit-c "$case_work/reference.c" \
        -o "$case_work/reference" >/dev/null
    "$KOFUN" build "$source" \
        --target x86_64-linux \
        -o "$case_work/native" >/dev/null

    "$case_work/reference" \
        >"$case_work/reference.stdout" \
        2>"$case_work/reference.stderr"
    "$case_work/native" \
        >"$case_work/native.stdout" \
        2>"$case_work/native.stderr"
    printf '%s\n' "$expected" >"$case_work/expected.stdout"

    cmp "$case_work/expected.stdout" "$case_work/reference.stdout"
    cmp "$case_work/reference.stdout" "$case_work/native.stdout"
    cmp "$case_work/reference.stderr" "$case_work/native.stderr"
    test ! -s "$case_work/reference.stderr"
    case_index=$((case_index + 1))
done

printf '%s\n' \
    "PASS: semantic fuzz matched expected, C11, and native output for $CASES programs"
