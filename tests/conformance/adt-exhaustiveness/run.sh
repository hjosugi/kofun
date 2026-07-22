#!/usr/bin/env sh
set -eu

LC_ALL=C
export LC_ALL

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CASES="$ROOT/tests/conformance/adt-exhaustiveness"
CC=${CC:-cc}
WORK=${KOFUN_ADT_EXHAUSTIVENESS_WORK:-"$ROOT/build/adt-exhaustiveness"}
STAGE2="$WORK/kofun-stage2"
SYMBOLS="$WORK/module-symbols"
TOOL="$WORK/adt-exhaustiveness"
PACKAGE_ID=1111111111111111111111111111111111111111111111111111111111111111
TARGET_MODULE=2222222222222222222222222222222222222222222222222222222222222222
TARGET_FILE=3333333333333333333333333333333333333333333333333333333333333333
OTHER_MODULE=4444444444444444444444444444444444444444444444444444444444444444
OTHER_FILE=5555555555555555555555555555555555555555555555555555555555555555
LOGICAL=demo/matching.kofun

rm -rf "$WORK"
mkdir -p "$WORK"

"$CC" -std=c11 -Wall -Wextra -Werror -pedantic \
    "$ROOT/bootstrap/stage2/compiler.c" -o "$STAGE2"
"$CC" -std=c11 -Wall -Wextra -Werror -pedantic \
    "$ROOT/bootstrap/stage2/module_symbols.c" \
    "$ROOT/bootstrap/stage2/sha256.c" -o "$SYMBOLS"
"$CC" -std=c11 -Wall -Wextra -Werror -pedantic \
    "$ROOT/bootstrap/stage2/adt_exhaustiveness.c" -o "$TOOL"

write_inventory() {
    source=$1
    output=$2
    logical=${3:-$LOGICAL}
    printf '%s|%s|%s|%s|%s\n' \
        "$PACKAGE_ID" "$TARGET_MODULE" "$TARGET_FILE" "$logical" "$source" \
        > "$output"
}

prepare() {
    stem=$1
    source=$2
    write_inventory "$source" "$WORK/$stem.inventory"
    "$SYMBOLS" "$WORK/$stem.inventory" "$WORK/$stem.symbols"
    "$STAGE2" --parse-patterns "$source" "$WORK/$stem.patterns"
    "$STAGE2" --emit-scope-hir "$source" "$WORK/$stem.scopes"
}

run_success() {
    stem=$1
    source=$2
    prepare "$stem" "$source"
    "$TOOL" "$source" "$LOGICAL" "$WORK/$stem.symbols" \
        "$WORK/$stem.patterns" "$WORK/$stem.scopes" "$WORK/$stem.typed"
}

expect_failure() {
    stem=$1
    source=$2
    code=$3
    prepare "$stem" "$source"
    printf '%s\n' stale > "$WORK/$stem.typed"
    if "$TOOL" "$source" "$LOGICAL" "$WORK/$stem.symbols" \
        "$WORK/$stem.patterns" "$WORK/$stem.scopes" "$WORK/$stem.typed" \
        > "$WORK/$stem.actual" 2>&1
    then
        printf '%s\n' "expected $code failure for $stem" >&2
        exit 1
    fi
    grep -F "error[$code]:" "$WORK/$stem.actual" >/dev/null
    test ! -e "$WORK/$stem.typed"
}

run_success exhaustive "$CASES/fixtures/exhaustive.kofun"
grep -Fx 'kofun-typed-adt-match/v1' "$WORK/exhaustive.typed" >/dev/null
grep -F '|adt=Result|adt-symbol=' "$WORK/exhaustive.typed" >/dev/null
grep -F '|constructor=Ok|constructor-symbol=' "$WORK/exhaustive.typed" >/dev/null
grep -F '|constructor=Err|constructor-symbol=' "$WORK/exhaustive.typed" >/dev/null
grep -F '|name=item|role=payload|' "$WORK/exhaustive.typed" >/dev/null
grep -F '|binding=2|name=item|role=read' "$WORK/exhaustive.typed" >/dev/null

run_success wildcard "$CASES/fixtures/wildcard.kofun"
grep -F '|role=wildcard|' "$WORK/wildcard.typed" >/dev/null
run_success binding "$CASES/fixtures/binding.kofun"
grep -F '|role=binding|' "$WORK/binding.typed" >/dev/null
grep -F '|name=anything|role=catchall|' "$WORK/binding.typed" >/dev/null

expect_failure missing-payload "$CASES/fixtures/missing_payload.kofun" E2S25
cmp "$CASES/fixtures/missing_payload.stderr" "$WORK/missing-payload.actual"
expect_failure missing-multiple "$CASES/fixtures/missing_multiple.kofun" E2S25
cmp "$CASES/fixtures/missing_multiple.stderr" "$WORK/missing-multiple.actual"
expect_failure guarded-only "$CASES/fixtures/guarded_only.kofun" E2S25
cmp "$CASES/fixtures/guarded_only.stderr" "$WORK/guarded-only.actual"
expect_failure duplicate-constructor "$CASES/fixtures/duplicate_constructor.kofun" E2S26
cmp "$CASES/fixtures/duplicate_constructor.stderr" "$WORK/duplicate-constructor.actual"
expect_failure after-catchall "$CASES/fixtures/after_catchall.kofun" E2S26
cmp "$CASES/fixtures/after_catchall.stderr" "$WORK/after-catchall.actual"
expect_failure redundant-catchall "$CASES/fixtures/redundant_catchall.kofun" E2S26
cmp "$CASES/fixtures/redundant_catchall.stderr" "$WORK/redundant-catchall.actual"
expect_failure nested-payload "$CASES/fixtures/nested_payload.kofun" E2S79
grep -F 'nested payload usefulness is unsupported' "$WORK/nested-payload.actual" >/dev/null

# Same spelling in another module cannot steal the target constructor identity.
{
    printf '%s|%s|%s|target/main.kofun|%s\n' \
        "$PACKAGE_ID" "$TARGET_MODULE" "$TARGET_FILE" "$CASES/fixtures/same_target.kofun"
    printf '%s|%s|%s|other/main.kofun|%s\n' \
        "$PACKAGE_ID" "$OTHER_MODULE" "$OTHER_FILE" "$CASES/fixtures/same_other.kofun"
} > "$WORK/same.inventory"
"$SYMBOLS" "$WORK/same.inventory" "$WORK/same.symbols"
"$STAGE2" --parse-patterns "$CASES/fixtures/same_target.kofun" "$WORK/same.patterns"
"$STAGE2" --emit-scope-hir "$CASES/fixtures/same_target.kofun" "$WORK/same.scopes"
"$TOOL" "$CASES/fixtures/same_target.kofun" target/main.kofun \
    "$WORK/same.symbols" "$WORK/same.patterns" "$WORK/same.scopes" "$WORK/same.typed"
TARGET_SAME=$(sed -n '/module=0|.*kind=constructor|name=Same|/s/.*|symbol=\([^|]*\)|.*/\1/p' \
    "$WORK/same.symbols")
test "${#TARGET_SAME}" -eq 64
grep -F "|constructor=Same|constructor-symbol=$TARGET_SAME|" "$WORK/same.typed" >/dev/null

# Repeated runs and host-path remapping preserve the typed projection.
"$TOOL" "$CASES/fixtures/exhaustive.kofun" "$LOGICAL" "$WORK/exhaustive.symbols" \
    "$WORK/exhaustive.patterns" "$WORK/exhaustive.scopes" "$WORK/exhaustive.second.typed"
cmp "$WORK/exhaustive.typed" "$WORK/exhaustive.second.typed"
mkdir -p "$WORK/remapped"
cp "$CASES/fixtures/exhaustive.kofun" "$WORK/remapped/input.kofun"
run_success remapped "$WORK/remapped/input.kofun"
cmp "$WORK/exhaustive.typed" "$WORK/remapped.typed"

# Corrupt/stale artifacts are rejected transactionally.
sed 's/name=Result/name=StaleResult/' "$WORK/exhaustive.symbols" \
    > "$WORK/stale.symbols"
printf '%s\n' stale > "$WORK/stale.typed"
if "$TOOL" "$CASES/fixtures/exhaustive.kofun" "$LOGICAL" \
    "$WORK/stale.symbols" "$WORK/exhaustive.patterns" \
    "$WORK/exhaustive.scopes" "$WORK/stale.typed" > "$WORK/stale.actual" 2>&1
then
    printf '%s\n' 'expected stale artifact failure' >&2
    exit 1
fi
grep -F 'error[E2S79]:' "$WORK/stale.actual" >/dev/null
test ! -e "$WORK/stale.typed"

# Generate a 64-constructor ADT and prove the declared operation boundary.
generate_budget_source() {
    match_count=$1
    output=$2
    {
        printf '%s\n' 'module budget.main' '' 'type Wide ='
        constructor=0
        while test "$constructor" -lt 64; do
            printf '    | C%s\n' "$constructor"
            constructor=$((constructor + 1))
        done
        printf '%s\n' '' 'fn inspect(value: Wide) -> Int {'
        match=0
        while test "$match" -lt "$match_count"; do
            printf '%s\n' '    match value {' '        _ => { 0 }' '    }'
            match=$((match + 1))
        done
        printf '%s\n' '    return 0' '}'
    } > "$output"
}
generate_budget_source 61 "$WORK/budget-boundary.kofun"
run_success budget-boundary "$WORK/budget-boundary.kofun"
generate_budget_source 62 "$WORK/budget-over.kofun"
expect_failure budget-over "$WORK/budget-over.kofun" E2S79
grep -F 'exceeds 4096 operations' "$WORK/budget-over.actual" >/dev/null

if command -v clang >/dev/null 2>&1; then
    clang -std=c11 -Wall -Wextra -Werror -pedantic \
        "$ROOT/bootstrap/stage2/adt_exhaustiveness.c" -o "$WORK/adt-exhaustiveness-clang"
    "$WORK/adt-exhaustiveness-clang" "$CASES/fixtures/exhaustive.kofun" "$LOGICAL" \
        "$WORK/exhaustive.symbols" "$WORK/exhaustive.patterns" \
        "$WORK/exhaustive.scopes" "$WORK/clang.typed"
    cmp "$WORK/exhaustive.typed" "$WORK/clang.typed"
fi

"$CC" -std=c11 -O1 -g -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$ROOT/bootstrap/stage2/adt_exhaustiveness.c" -o "$WORK/adt-exhaustiveness-sanitized"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
    "$WORK/adt-exhaustiveness-sanitized" "$CASES/fixtures/exhaustive.kofun" "$LOGICAL" \
    "$WORK/exhaustive.symbols" "$WORK/exhaustive.patterns" \
    "$WORK/exhaustive.scopes" "$WORK/sanitized.typed"
cmp "$WORK/exhaustive.typed" "$WORK/sanitized.typed"

if "$CC" -std=c11 -O0 -Wall -Wextra -Werror -pedantic -fanalyzer \
    "$ROOT/bootstrap/stage2/adt_exhaustiveness.c" \
    -o "$WORK/adt-exhaustiveness-analyzed" >/dev/null 2>&1
then
    printf '%s\n' 'PASS: GCC analyzer accepts the ADT exhaustiveness adapter'
fi

printf '%s\n' 'PASS: resolved ADT identities drive exhaustive, redundant, guarded, and bounded match diagnostics'
