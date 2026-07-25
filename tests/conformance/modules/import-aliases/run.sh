#!/usr/bin/env sh
set -eu

LC_ALL=C
export LC_ALL

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)
CASES="$ROOT/tests/conformance/modules/import-aliases"
FIXTURES="$CASES/fixtures"
EXPECTED="$CASES/expected"
CC=${CC:-cc}
WORK=${KOFUN_IMPORT_ALIASES_WORK:-"$ROOT/build/import-aliases"}
TOOL="$WORK/imports-qualified"
SELECTIVE_TOOL="$WORK/imports-selective"
PACKAGE_ID=1111111111111111111111111111111111111111111111111111111111111111
MAIN_MODULE=2222222222222222222222222222222222222222222222222222222222222222
TARGET_MODULE=3333333333333333333333333333333333333333333333333333333333333333
MAIN_FILE=4444444444444444444444444444444444444444444444444444444444444444
TARGET_FILE=5555555555555555555555555555555555555555555555555555555555555555
OTHER_MODULE=6666666666666666666666666666666666666666666666666666666666666666
OTHER_FILE=7777777777777777777777777777777777777777777777777777777777777777

rm -rf "$WORK"
mkdir -p "$WORK"

"$CC" -std=c11 -Wall -Wextra -Werror -pedantic \
    "$ROOT/bootstrap/stage2/imports_qualified.c" \
    "$ROOT/bootstrap/stage2/visibility_access.c" \
    "$ROOT/bootstrap/stage2/sha256.c" \
    -o "$TOOL"
"$CC" -std=c11 -Wall -Wextra -Werror -pedantic \
    "$ROOT/bootstrap/stage2/imports_selective.c" \
    "$ROOT/bootstrap/stage2/visibility_access.c" \
    "$ROOT/bootstrap/stage2/sha256.c" \
    -o "$SELECTIVE_TOOL"
"$CC" -std=c11 -Wall -Wextra -Werror -pedantic \
    -I"$ROOT/bootstrap/stage2" \
    "$CASES/alias_identity_reference.c" \
    "$ROOT/bootstrap/stage2/sha256.c" \
    -o "$WORK/alias-identity-reference"

write_inventory() {
    main_source=$1
    output=$2
    {
        printf '%s|%s|%s|app.main|app/main.kofun|%s\n' \
            "$PACKAGE_ID" "$MAIN_MODULE" "$MAIN_FILE" "$main_source"
        printf '%s|%s|%s|data.formats.delimited.reader|data/formats/delimited/reader.kofun|%s\n' \
            "$PACKAGE_ID" "$TARGET_MODULE" "$TARGET_FILE" "$FIXTURES/reader.kofun"
    } >"$output"
}

write_collision_inventory() {
    main_source=$1
    output=$2
    {
        printf '%s|%s|%s|app.main|app/main.kofun|%s\n' \
            "$PACKAGE_ID" "$MAIN_MODULE" "$MAIN_FILE" "$main_source"
        printf '%s|%s|%s|data.formats.delimited.reader|data/formats/delimited/reader.kofun|%s\n' \
            "$PACKAGE_ID" "$TARGET_MODULE" "$TARGET_FILE" "$FIXTURES/reader.kofun"
        printf '%s|%s|%s|other.reader|other/reader.kofun|%s\n' \
            "$PACKAGE_ID" "$OTHER_MODULE" "$OTHER_FILE" "$FIXTURES/other_reader.kofun"
    } >"$output"
}

run_program() {
    source=$1
    stem=$2
    write_inventory "$source" "$WORK/$stem.inventory"
    "$TOOL" "$WORK/$stem.inventory" "$WORK/$stem.hir" "$WORK/$stem.c"
    "$CC" -std=c11 -Wall -Wextra -Werror -pedantic \
        "$WORK/$stem.c" -o "$WORK/$stem-program"
    set +e
    "$WORK/$stem-program"
    status=$?
    set -e
    test "$status" -eq 42
}

expect_failure() {
    expected_code=$1
    source=$2
    stem=$3
    expected_output=$4
    inventory="$WORK/$stem.inventory"
    if test "$stem" = qualifier-collision; then
        write_collision_inventory "$source" "$inventory"
    else
        write_inventory "$source" "$inventory"
    fi
    printf '%s\n' stale >"$WORK/$stem.hir"
    printf '%s\n' stale >"$WORK/$stem.c"
    if "$TOOL" "$inventory" "$WORK/$stem.hir" "$WORK/$stem.c" \
        >"$WORK/$stem.stdout" 2>"$WORK/$stem.stderr"; then
        printf '%s\n' "expected $expected_code failure for $stem" >&2
        exit 1
    fi
    test ! -s "$WORK/$stem.stderr"
    grep -F "error[$expected_code]:" "$WORK/$stem.stdout" >/dev/null
    cmp "$expected_output" "$WORK/$stem.stdout"
    test ! -e "$WORK/$stem.hir"
    test ! -e "$WORK/$stem.c"
}

run_program "$FIXTURES/main_csv.kofun" csv
cmp "$CASES/expected.hir" "$WORK/csv.hir"
grep -F "|local=csv|target=$TARGET_MODULE|form=qualified-module-v1|" \
    "$WORK/csv.hir" >/dev/null
grep -F '|alias-binding=' "$WORK/csv.hir" >/dev/null
grep -F '|alias-span=' "$WORK/csv.hir" >/dev/null
grep -F '|reexport=false' "$WORK/csv.hir" >/dev/null
if grep -F '|local=reader|' "$WORK/csv.hir" >/dev/null; then
    printf '%s\n' 'default final-component qualifier was bound beside the alias' >&2
    exit 1
fi

ALIAS_BINDING=$(sed -n 's/^import|.*|alias-binding=\([0-9a-f]*\)|.*/\1/p' \
    "$WORK/csv.hir")
ALIAS_SPAN=$(sed -n 's/^import|.*|alias-span=\([0-9]*\.\.[0-9]*\)|.*/\1/p' \
    "$WORK/csv.hir")
ALIAS_START=${ALIAS_SPAN%%..*}
ALIAS_END=${ALIAS_SPAN##*..}
EXPECTED_ALIAS_BINDING=$(
    "$WORK/alias-identity-reference" "$MAIN_MODULE" "$MAIN_FILE" \
        "$ALIAS_START" "$ALIAS_END" csv "$TARGET_MODULE"
)
test "$ALIAS_BINDING" = "$EXPECTED_ALIAS_BINDING"
test "${#ALIAS_BINDING}" -eq 64
"$SELECTIVE_TOOL" "$WORK/csv.inventory" \
    "$WORK/csv-selective.hir" "$WORK/csv-selective.c"
grep -F "|local=csv|target=$TARGET_MODULE|" "$WORK/csv-selective.hir" >/dev/null
grep -F "|alias-binding=$ALIAS_BINDING|alias-span=$ALIAS_SPAN|reexport=false" \
    "$WORK/csv-selective.hir" >/dev/null
"$CC" -std=c11 -Wall -Wextra -Werror -pedantic \
    "$WORK/csv-selective.c" -o "$WORK/csv-selective-program"
set +e
"$WORK/csv-selective-program"
SELECTIVE_STATUS=$?
set -e
test "$SELECTIVE_STATUS" -eq 42

run_program "$FIXTURES/main_table.kofun" table
test "$(sed -n 's/^import|binding=\([0-9a-f]*\)|.*/\1/p' "$WORK/csv.hir")" != \
    "$(sed -n 's/^import|binding=\([0-9a-f]*\)|.*/\1/p' "$WORK/table.hir")"
test "$(sed -n 's/^import|.*|alias-binding=\([0-9a-f]*\)|.*/\1/p' "$WORK/csv.hir")" != \
    "$(sed -n 's/^import|.*|alias-binding=\([0-9a-f]*\)|.*/\1/p' "$WORK/table.hir")"
sed -n 's/^target|/target|/p; s/^qualified-call|.*|target-module=/target-module=/p' \
    "$WORK/csv.hir" |
    sed 's/|qualifier-span=.*//' >"$WORK/csv-targets"
sed -n 's/^target|/target|/p; s/^qualified-call|.*|target-module=/target-module=/p' \
    "$WORK/table.hir" |
    sed 's/|qualifier-span=.*//' >"$WORK/table-targets"
cmp "$WORK/csv-targets" "$WORK/table-targets"

mkdir -p "$WORK/remapped/source" "$WORK/remapped/dependency"
cp "$FIXTURES/main_csv.kofun" "$WORK/remapped/source/entry.kofun"
cp "$FIXTURES/reader.kofun" "$WORK/remapped/dependency/input.kofun"
{
    printf '%s|%s|%s|app.main|app/main.kofun|%s\n' \
        "$PACKAGE_ID" "$MAIN_MODULE" "$MAIN_FILE" "$WORK/remapped/source/entry.kofun"
    printf '%s|%s|%s|data.formats.delimited.reader|data/formats/delimited/reader.kofun|%s\n' \
        "$PACKAGE_ID" "$TARGET_MODULE" "$TARGET_FILE" \
        "$WORK/remapped/dependency/input.kofun"
} >"$WORK/remapped.inventory"
"$TOOL" "$WORK/remapped.inventory" "$WORK/remapped.hir" "$WORK/remapped.c"
cmp "$WORK/csv.hir" "$WORK/remapped.hir"
cmp "$WORK/csv.c" "$WORK/remapped.c"

expect_failure E2S59 "$FIXTURES/missing_alias.kofun" missing-alias \
    "$EXPECTED/missing_alias.txt"
expect_failure E2S59 "$FIXTURES/keyword_alias.kofun" keyword-alias \
    "$EXPECTED/keyword_alias.txt"
expect_failure E2S59 "$FIXTURES/invalid_alias.kofun" invalid-alias \
    "$EXPECTED/invalid_alias.txt"
expect_failure E2S63 "$FIXTURES/qualifier_collision.kofun" qualifier-collision \
    "$EXPECTED/qualifier_collision.txt"
expect_failure E2S62 "$FIXTURES/duplicate_target.kofun" duplicate-target \
    "$EXPECTED/duplicate_target.txt"
expect_failure E2S59 "$FIXTURES/public_alias.kofun" public-alias \
    "$EXPECTED/public_alias.txt"
expect_failure E2S59 "$FIXTURES/chained_alias.kofun" chained-alias \
    "$EXPECTED/chained_alias.txt"
expect_failure E2S59 "$FIXTURES/per_symbol_alias.kofun" per-symbol-alias \
    "$EXPECTED/per_symbol_alias.txt"
expect_failure E2S59 "$FIXTURES/external_package_alias.kofun" external-package-alias \
    "$EXPECTED/external_package_alias.txt"
expect_failure E2S59 "$FIXTURES/relative_alias.kofun" relative-alias \
    "$EXPECTED/relative_alias.txt"
expect_failure E2S59 "$FIXTURES/malformed_as.kofun" malformed-as \
    "$EXPECTED/malformed_as.txt"
expect_failure E2S65 "$FIXTURES/default_qualifier.kofun" default-qualifier \
    "$EXPECTED/default_qualifier.txt"

"$CC" -std=c11 -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$ROOT/bootstrap/stage2/imports_qualified.c" \
    "$ROOT/bootstrap/stage2/visibility_access.c" \
    "$ROOT/bootstrap/stage2/sha256.c" \
    -o "$WORK/imports-qualified-sanitized"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
    "$WORK/imports-qualified-sanitized" \
    "$WORK/csv.inventory" "$WORK/sanitized.hir"
cmp "$WORK/csv.hir" "$WORK/sanitized.hir"
set +e
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
    "$WORK/imports-qualified-sanitized" \
    "$WORK/missing-alias.inventory" "$WORK/sanitized-failure.hir" \
    >"$WORK/sanitized-failure.stdout"
SANITIZED_STATUS=$?
set -e
test "$SANITIZED_STATUS" -eq 1
cmp "$EXPECTED/missing_alias.txt" "$WORK/sanitized-failure.stdout"
test ! -e "$WORK/sanitized-failure.hir"

printf '%s\n' \
    'PASS: module aliases preserve target identities and reject malformed or widening forms'
