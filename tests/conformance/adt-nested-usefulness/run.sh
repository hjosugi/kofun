#!/usr/bin/env sh
set -eu

LC_ALL=C
export LC_ALL

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CASES="$ROOT/tests/conformance/adt-nested-usefulness"
CC=${CC:-cc}
WORK=${KOFUN_ADT_NESTED_USEFULNESS_WORK:-"$ROOT/build/adt-nested-usefulness"}
TOOL="$WORK/adt-nested-usefulness"
ASSERT_CONTEXT='adt nested usefulness'
. "$ROOT/tests/assertions/assert.sh"

case "$WORK" in
    */adt-nested-usefulness|*/adt-nested-usefulness.*) ;;
    *) assert_fail "work directory must end in adt-nested-usefulness[.suffix]: $WORK" ;;
esac

rm -rf "$WORK"
mkdir -p "$WORK"

"$CC" -std=c11 -Wall -Wextra -Werror -pedantic \
    "$ROOT/bootstrap/stage2/adt_nested_usefulness.c" -o "$TOOL"

run_success() {
    stem=$1
    input=$2
    "$TOOL" "$input" "$WORK/$stem.result"
}

expect_failure() {
    stem=$1
    input=$2
    code=$3
    printf '%s\n' stale > "$WORK/$stem.result"
    if "$TOOL" "$input" "$WORK/$stem.result" > "$WORK/$stem.actual" 2>&1
    then
        assert_fail "expected $code failure for $stem"
    fi
    assert_grep "$stem.actual" -F "error[$code]:" "$WORK/$stem.actual"
    assert_absent "$stem.result" "$WORK/$stem.result"
}

run_success positive "$CASES/fixtures/positive.matrix"
assert_grep "positive.result" -Fx \
    'kofun-adt-nested-usefulness-result/v1' "$WORK/positive.result"
assert_grep "positive.result" -F \
    '|witness=Wrap(Left)|outer=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa|inner=cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc' \
    "$WORK/positive.result"
assert_grep "positive.result" -F \
    '|witness=Wrap(Right)|outer=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa|inner=dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd' \
    "$WORK/positive.result"
assert_grep "positive.result" -F '|witness=Empty|' "$WORK/positive.result"
assert_grep "positive.result" -F 'complete|rows=3|cells=3|' "$WORK/positive.result"

expect_failure missing "$CASES/fixtures/missing.matrix" E2S25
cmp "$CASES/fixtures/missing.stderr" "$WORK/missing.actual"
expect_failure redundant-nested "$CASES/fixtures/redundant_nested.matrix" E2S26
cmp "$CASES/fixtures/redundant_nested.stderr" "$WORK/redundant-nested.actual"
expect_failure redundant-whole "$CASES/fixtures/redundant_whole.matrix" E2S26
cmp "$CASES/fixtures/redundant_whole.stderr" "$WORK/redundant-whole.actual"

# The decoy constructor has the same display name as the resolved inner
# constructor but another owner identity, so it cannot cover the payload cell.
expect_failure wrong-owner "$CASES/fixtures/wrong_owner.matrix" E2S110
assert_grep "wrong-owner.actual" -F \
    'nested constructor is not owned by resolved payload ADT' "$WORK/wrong-owner.actual"
expect_failure too-deep "$CASES/fixtures/too_deep.matrix" E2S110
assert_grep "too-deep.actual" -F \
    'nested usefulness row exceeds depth 1' "$WORK/too-deep.actual"

# Missing witnesses have a fixed display cap and retain declaration order.
{
    outer_adt=4444444444444444444444444444444444444444444444444444444444444444
    printf '%s\n' 'kofun-adt-nested-usefulness/v1'
    printf 'adt|id=%s|name=Many\n' "$outer_adt"
    ordinal=0
    while test "$ordinal" -lt 10; do
        constructor=$(printf '9%063d' "$ordinal")
        printf 'constructor|id=%s|owner=%s|ordinal=%s|name=C%s|payload-owner=-\n' \
            "$constructor" "$outer_adt" "$ordinal" "$ordinal"
        ordinal=$((ordinal + 1))
    done
    printf 'target|adt=%s\n' "$outer_adt"
    printf 'row|outer=%s|inner=-|depth=0|span=10..12\n' "$(printf '9%063d' 0)"
} > "$WORK/display-cap.matrix"
expect_failure display-cap "$WORK/display-cap.matrix" E2S25
assert_grep "display-cap.actual order" -F \
    'missing `C1`, `C2`, `C3`, `C4`, `C5`, `C6`, `C7`, `C8`, and 1 more' \
    "$WORK/display-cap.actual"

# Missing identities and unknown fields fail closed before analysis.
sed 's/target|adt=111111/target|adt=999999/' \
    "$CASES/fixtures/positive.matrix" > "$WORK/stale-identity.matrix"
expect_failure stale-identity "$WORK/stale-identity.matrix" E2S110
assert_grep "stale-identity.actual" -F 'target ADT identity is absent' \
    "$WORK/stale-identity.actual"
sed 's/row|outer=/row|unknown=/' \
    "$CASES/fixtures/positive.matrix" > "$WORK/malformed.matrix"
expect_failure malformed "$WORK/malformed.matrix" E2S110
assert_grep "malformed.actual" -F 'malformed row record' "$WORK/malformed.actual"

# Repeated runs and a remapped input path have byte-identical publications.
run_success repeated "$CASES/fixtures/positive.matrix"
cmp "$WORK/positive.result" "$WORK/repeated.result"
mkdir -p "$WORK/remapped"
cp "$CASES/fixtures/positive.matrix" "$WORK/remapped/model.matrix"
run_success remapped "$WORK/remapped/model.matrix"
cmp "$WORK/positive.result" "$WORK/remapped.result"

identity() {
    printf '%064d' "$1"
}

generate_adt_limit_model() {
    adt_count=$1
    output=$2
    {
        printf '%s\n' 'kofun-adt-nested-usefulness/v1'
        index=0
        while test "$index" -lt "$adt_count"; do
            adt=$(identity "$((1000 + index))")
            constructor=$(identity "$((2000 + index))")
            printf 'adt|id=%s|name=A%s\n' "$adt" "$index"
            printf 'constructor|id=%s|owner=%s|ordinal=0|name=Only|payload-owner=-\n' \
                "$constructor" "$adt"
            index=$((index + 1))
        done
        printf 'target|adt=%s\n' "$(identity 1000)"
        printf 'row|outer=%s|inner=-|depth=0|span=10..14\n' "$(identity 2000)"
    } > "$output"
}

generate_constructor_limit_model() {
    constructor_count=$1
    output=$2
    adt=$(identity 3000)
    {
        printf '%s\n' 'kofun-adt-nested-usefulness/v1'
        printf 'adt|id=%s|name=Wide\n' "$adt"
        ordinal=0
        while test "$ordinal" -lt "$constructor_count"; do
            constructor=$(identity "$((4000 + ordinal))")
            printf 'constructor|id=%s|owner=%s|ordinal=%s|name=C%s|payload-owner=-\n' \
                "$constructor" "$adt" "$ordinal" "$ordinal"
            ordinal=$((ordinal + 1))
        done
        printf 'target|adt=%s\n' "$adt"
        printf '%s\n' 'row|outer=*|inner=-|depth=0|span=10..11'
    } > "$output"
}

generate_wide_model() {
    outer_count=$1
    rows=$2
    output=$3
    outer_adt=$(identity 1)
    inner_adt=$(identity 2)
    {
        printf '%s\n' 'kofun-adt-nested-usefulness/v1'
        printf 'adt|id=%s|name=WideOuter\n' "$outer_adt"
        printf 'adt|id=%s|name=WideInner\n' "$inner_adt"
        ordinal=0
        while test "$ordinal" -lt "$outer_count"; do
            constructor=$(identity "$((100 + ordinal))")
            printf 'constructor|id=%s|owner=%s|ordinal=%s|name=O%s|payload-owner=%s\n' \
                "$constructor" "$outer_adt" "$ordinal" "$ordinal" "$inner_adt"
            ordinal=$((ordinal + 1))
        done
        ordinal=0
        while test "$ordinal" -lt 32; do
            constructor=$(identity "$((200 + ordinal))")
            printf 'constructor|id=%s|owner=%s|ordinal=%s|name=I%s|payload-owner=-\n' \
                "$constructor" "$inner_adt" "$ordinal" "$ordinal"
            ordinal=$((ordinal + 1))
        done
        printf 'target|adt=%s\n' "$outer_adt"
        row=0
        while test "$row" -lt "$rows"; do
            outer=$(identity "$((100 + row))")
            inner=$(identity 200)
            printf 'row|outer=%s|inner=%s|depth=1|span=%s..%s\n' \
                "$outer" "$inner" "$((10 + row * 10))" "$((15 + row * 10))"
            row=$((row + 1))
        done
    } > "$output"
}

# Singleton ADTs are valid finite domains, and all explicit parser/matrix
# maxima are exercised at their accepted edge before the first rejected row.
generate_adt_limit_model 1 "$WORK/singleton.matrix"
run_success singleton "$WORK/singleton.matrix"
assert_grep "singleton.result" -F 'complete|rows=1|cells=1|' \
    "$WORK/singleton.result"
generate_adt_limit_model 16 "$WORK/adts-boundary.matrix"
run_success adts-boundary "$WORK/adts-boundary.matrix"
generate_adt_limit_model 17 "$WORK/adts-over.matrix"
expect_failure adts-over "$WORK/adts-over.matrix" E2S110
assert_grep "adts-over.actual" -F 'exceeds 16 ADTs' "$WORK/adts-over.actual"

generate_constructor_limit_model 64 "$WORK/constructors-boundary.matrix"
run_success constructors-boundary "$WORK/constructors-boundary.matrix"
assert_grep "constructors-boundary.result" -F 'complete|rows=1|cells=64|' \
    "$WORK/constructors-boundary.result"
generate_constructor_limit_model 65 "$WORK/constructors-over.matrix"
expect_failure constructors-over "$WORK/constructors-over.matrix" E2S110
assert_grep "constructors-over.actual" -F 'exceeds 64 constructors' \
    "$WORK/constructors-over.actual"

# The 64-constructor global bound derives the 1024-cell maximum: 32 outer
# constructors each specialize the same 32-constructor payload ADT. A 1025th
# cell would require a 65th constructor and is rejected by the gate above.
generate_wide_model 32 0 "$WORK/cells-boundary.matrix"
printf '%s\n' 'row|outer=*|inner=-|depth=0|span=10..11' \
    >> "$WORK/cells-boundary.matrix"
run_success cells-boundary "$WORK/cells-boundary.matrix"
assert_grep "cells-boundary.result" -F \
    'complete|rows=1|cells=1024|operations=3072' \
    "$WORK/cells-boundary.result"

# Three novel rows over 512 cells consume 3584 checked row/cell and missing-set
# visits. A fourth reaches 4096 before the missing scan and fails on its next
# checked visit, so neither the coverage application nor witness scan is free.
generate_wide_model 16 3 "$WORK/budget-boundary.matrix"
expect_failure budget-boundary "$WORK/budget-boundary.matrix" E2S25
if grep -F 'exceeds 4096 operations' "$WORK/budget-boundary.actual" >/dev/null
then
    assert_fail 'budget boundary stopped before the ordinary usefulness result'
fi
generate_wide_model 16 4 "$WORK/budget-over.matrix"
expect_failure budget-over "$WORK/budget-over.matrix" E2S110
assert_grep "budget-over.actual" -F 'exceeds 4096 operations' "$WORK/budget-over.actual"

# The declared row limit is checked while reading, before usefulness analysis.
generate_rows_model() {
    row_count=$1
    output=$2
    {
    sed -n '/^row|/!p' "$CASES/fixtures/positive.matrix"
    row=0
    while test "$row" -lt "$row_count"; do
        printf 'row|outer=*|inner=-|depth=0|span=%s..%s\n' \
            "$((1000 + row * 2))" "$((1001 + row * 2))"
        row=$((row + 1))
    done
    } > "$output"
}
generate_rows_model 128 "$WORK/rows-boundary.matrix"
expect_failure rows-boundary "$WORK/rows-boundary.matrix" E2S26
if grep -F 'exceeds 128 rows' "$WORK/rows-boundary.actual" >/dev/null
then
    assert_fail 'the declared 128-row boundary was rejected while reading'
fi
generate_rows_model 129 "$WORK/rows-over.matrix"
expect_failure rows-over "$WORK/rows-over.matrix" E2S110
assert_grep "rows-over.actual" -F 'exceeds 128 rows' "$WORK/rows-over.actual"

# Output aliases and failed transactions never overwrite the input or leave a
# publication artifact.
cp "$CASES/fixtures/positive.matrix" "$WORK/alias.matrix"
if "$TOOL" "$WORK/alias.matrix" "$WORK/alias.matrix" > "$WORK/alias.actual" 2>&1
then
    assert_fail 'expected aliased path failure'
fi
cmp "$CASES/fixtures/positive.matrix" "$WORK/alias.matrix"
assert_grep "alias.actual" -F 'input and output paths must differ' "$WORK/alias.actual"
mkdir -p "$WORK/alias-dir"
cp "$CASES/fixtures/positive.matrix" "$WORK/alias-dir/model.matrix"
if "$TOOL" "$WORK/alias-dir/model.matrix" \
    "$WORK/alias-dir/./model.matrix" > "$WORK/dot-alias.actual" 2>&1
then
    assert_fail 'expected dot-segment aliased path failure'
fi
cmp "$CASES/fixtures/positive.matrix" "$WORK/alias-dir/model.matrix"
assert_grep "dot-alias.actual" -F 'input and output paths must differ' \
    "$WORK/dot-alias.actual"
cp "$CASES/fixtures/positive.matrix" "$WORK/transaction.result.tmp"
if "$TOOL" "$WORK/transaction.result.tmp" "$WORK/transaction.result" \
    > "$WORK/transaction-alias.actual" 2>&1
then
    assert_fail 'expected transaction-path input alias failure'
fi
cmp "$CASES/fixtures/positive.matrix" "$WORK/transaction.result.tmp"
assert_absent "transaction.result" "$WORK/transaction.result"
assert_grep "transaction-alias.actual" -F 'input and output paths must differ' \
    "$WORK/transaction-alias.actual"

if command -v clang >/dev/null 2>&1; then
    clang -std=c11 -Wall -Wextra -Werror -pedantic \
        "$ROOT/bootstrap/stage2/adt_nested_usefulness.c" \
        -o "$WORK/adt-nested-usefulness-clang"
    "$WORK/adt-nested-usefulness-clang" "$CASES/fixtures/positive.matrix" \
        "$WORK/clang.result"
    cmp "$WORK/positive.result" "$WORK/clang.result"
fi

"$CC" -std=c11 -O1 -g -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$ROOT/bootstrap/stage2/adt_nested_usefulness.c" \
    -o "$WORK/adt-nested-usefulness-sanitized"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
    "$WORK/adt-nested-usefulness-sanitized" "$CASES/fixtures/positive.matrix" \
    "$WORK/sanitized.result"
cmp "$WORK/positive.result" "$WORK/sanitized.result"

printf '%s\n' 'int main(void) { return 0; }' > "$WORK/analyzer-probe.c"
if "$CC" -std=c11 -O0 -Wall -Wextra -Werror -pedantic -fanalyzer \
    "$WORK/analyzer-probe.c" -o "$WORK/analyzer-probe" >/dev/null 2>&1
then
    "$CC" -std=c11 -O0 -Wall -Wextra -Werror -pedantic -fanalyzer \
        "$ROOT/bootstrap/stage2/adt_nested_usefulness.c" \
        -o "$WORK/adt-nested-usefulness-analyzed"
    printf '%s\n' 'PASS: GCC analyzer accepts nested ADT usefulness'
fi

printf '%s\n' \
    'PASS: one-level resolved nested ADT usefulness is identity-safe, bounded, and deterministic'
