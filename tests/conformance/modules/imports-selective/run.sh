#!/usr/bin/env sh
set -eu

LC_ALL=C
export LC_ALL

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)
CASES="$ROOT/tests/conformance/modules/imports-selective"
CC=${CC:-cc}
WORK=${KOFUN_IMPORTS_SELECTIVE_WORK:-"$ROOT/build/imports-selective"}
TOOL="$WORK/imports-selective"
PACKAGE_ID=1111111111111111111111111111111111111111111111111111111111111111
MAIN_MODULE=2222222222222222222222222222222222222222222222222222222222222222
MATH_MODULE=3333333333333333333333333333333333333333333333333333333333333333
MAIN_FILE=4444444444444444444444444444444444444444444444444444444444444444
MATH_FILE=5555555555555555555555555555555555555555555555555555555555555555

rm -rf "$WORK"
mkdir -p "$WORK"

"$CC" -std=c11 -Wall -Wextra -Werror -pedantic \
    -DKOFUN_TEST_DIAGNOSTIC_FAULTS \
    "$ROOT/bootstrap/stage2/imports_selective.c" \
    "$ROOT/bootstrap/stage2/visibility_access.c" \
    "$ROOT/bootstrap/stage2/sha256.c" \
    -o "$TOOL"

write_inventory() {
    main_path=$1
    math_path=$2
    output=$3
    {
        printf '%s|%s|%s|app.main|app/main.kofun|%s\n' \
            "$PACKAGE_ID" "$MAIN_MODULE" "$MAIN_FILE" "$main_path"
        printf '%s|%s|%s|lib.math|lib/math.kofun|%s\n' \
            "$PACKAGE_ID" "$MATH_MODULE" "$MATH_FILE" "$math_path"
    } > "$output"
}

expect_failure() {
    code=$1
    source=$2
    name=$3
    inventory="$WORK/$name.inventory"
    hir="$WORK/$name.hir"
    backend="$WORK/$name.c"
    log="$WORK/$name.log"
    write_inventory "$source" "$CASES/fixtures/math.kofun" "$inventory"
    printf '%s\n' stale > "$hir"
    printf '%s\n' stale > "$backend"
    if "$TOOL" "$inventory" "$hir" "$backend" > "$log" 2>&1; then
        printf '%s\n' "expected $code failure for $name" >&2
        exit 1
    fi
    grep -F "error[$code]:" "$log" >/dev/null
    test ! -e "$hir"
    test ! -e "$backend"
}

write_inventory "$CASES/fixtures/main.kofun" "$CASES/fixtures/math.kofun" \
    "$WORK/positive.inventory"
"$TOOL" "$WORK/positive.inventory" "$WORK/positive.hir"

grep -Fx 'kofun-imports-selective/v1' "$WORK/positive.hir" >/dev/null
grep -F '|local=identity|' "$WORK/positive.hir" >/dev/null
grep -F '|local=answer|' "$WORK/positive.hir" >/dev/null
test "$(grep -c '|local=Value|' "$WORK/positive.hir")" -eq 2
grep -F '|namespace-name=value|local=Value|' "$WORK/positive.hir" >/dev/null
grep -F '|namespace-name=type|local=Value|' "$WORK/positive.hir" >/dev/null
grep -F 'qualified-import|' "$WORK/positive.hir" >/dev/null
grep -F 'selective-call|' "$WORK/positive.hir" >/dev/null
grep -F 'qualified-call|' "$WORK/positive.hir" >/dev/null
grep -F 'type-reference|' "$WORK/positive.hir" >/dev/null
grep -F '|reexport=false' "$WORK/positive.hir" >/dev/null
grep -F '|interface=no|' "$WORK/positive.hir" >/dev/null

write_inventory "$CASES/fixtures/runtime.kofun" "$CASES/fixtures/math.kofun" \
    "$WORK/runtime.inventory"
"$TOOL" "$WORK/runtime.inventory" "$WORK/runtime.hir" "$WORK/runtime.c"
cc -std=c11 -Wall -Wextra -Werror -pedantic "$WORK/runtime.c" -o "$WORK/runtime"
if "$WORK/runtime"; then
    runtime_status=0
else
    runtime_status=$?
fi
test "$runtime_status" -eq 42

# Declaration order and host path remapping cannot perturb semantic output.
write_inventory "$CASES/fixtures/main.kofun" "$CASES/fixtures/math_reordered.kofun" \
    "$WORK/reordered.inventory"
"$TOOL" "$WORK/reordered.inventory" "$WORK/reordered.hir"
cmp "$WORK/positive.hir" "$WORK/reordered.hir"
mkdir -p "$WORK/remapped/a" "$WORK/remapped/b"
cp "$CASES/fixtures/main.kofun" "$WORK/remapped/a/main.kofun"
cp "$CASES/fixtures/math.kofun" "$WORK/remapped/b/math.kofun"
write_inventory "$WORK/remapped/a/main.kofun" "$WORK/remapped/b/math.kofun" \
    "$WORK/remapped.inventory"
"$TOOL" "$WORK/remapped.inventory" "$WORK/remapped.hir"
cmp "$WORK/positive.hir" "$WORK/remapped.hir"

expect_failure E2S69 "$CASES/fixtures/empty.kofun" empty
expect_failure E2S70 "$CASES/fixtures/duplicate.kofun" duplicate
expect_failure E2S71 "$CASES/fixtures/missing.kofun" missing
expect_failure E2S72 "$CASES/fixtures/inaccessible.kofun" inaccessible
expect_failure E2S73 "$CASES/fixtures/local_collision.kofun" local-collision
expect_failure E2S73 "$CASES/fixtures/import_collision.kofun" import-collision
expect_failure E2S69 "$CASES/fixtures/wildcard.kofun" wildcard
expect_failure E2S69 "$CASES/fixtures/alias.kofun" alias
expect_failure E2S69 "$CASES/fixtures/malformed_commas.kofun" malformed-commas
expect_failure E2S69 "$CASES/fixtures/after_declaration.kofun" after-declaration
expect_failure E2S74 "$CASES/fixtures/unlisted.kofun" unlisted
expect_failure E2S74 "$CASES/fixtures/wrong_namespace.kofun" wrong-namespace

expect_exact_forbidden() {
    code=$1
    name=$2
    expected=$3
    shift 3
    hir="$WORK/$name.hir"
    backend="$WORK/$name.c"
    stdout="$WORK/$name.stdout"
    stderr="$WORK/$name.stderr"
    printf '%s\n' stale >"$hir"
    printf '%s\n' stale >"$backend"
    set +e
    "$@" >"$stdout" 2>"$stderr"
    status=$?
    set -e
    test "$status" -eq 1 || {
        printf '%s\n' "$name exited $status instead of 1" >&2
        exit 1
    }
    test ! -s "$stderr"
    test ! -e "$hir"
    test ! -e "$backend"
    printf '%s\n' "$expected" >"$WORK/$name.expected"
    cmp "$WORK/$name.expected" "$stdout"
    grep -F "error[$code]:" "$stdout" >/dev/null
}

{
    printf '%s\n' 'module app.main'
    index=0
    while test "$index" -lt 257; do
        printf '%s\n' 'from lib.math import identity'
        index=$((index + 1))
    done
    printf '%s\n' 'fn main() -> Int {' '    return 0' '}'
} >"$WORK/selective-declarations-over.kofun"
write_inventory "$WORK/selective-declarations-over.kofun" \
    "$CASES/fixtures/math.kofun" "$WORK/selective-declarations-over.inventory"
expect_exact_forbidden E2S75 selective-declarations-over \
    'error[E2S75]: module `app/main.kofun` exceeds 256 combined qualified/selective imports; hint: combine or remove imports' \
    "$TOOL" "$WORK/selective-declarations-over.inventory" \
    "$WORK/selective-declarations-over.hir" "$WORK/selective-declarations-over.c"

sed 's/identity(42)/identity()/' "$CASES/fixtures/main.kofun" \
    >"$WORK/selective-wrong-arity.kofun"
write_inventory "$WORK/selective-wrong-arity.kofun" "$CASES/fixtures/math.kofun" \
    "$WORK/selective-wrong-arity.inventory"
expect_exact_forbidden E2S76 selective-wrong-arity \
    'error[E2S76]: selective call `identity` expects 1 arguments but got 0 in `app/main.kofun`; hint: pass exactly 1 arguments' \
    "$TOOL" "$WORK/selective-wrong-arity.inventory" \
    "$WORK/selective-wrong-arity.hir" "$WORK/selective-wrong-arity.c"

mkdir -p "$WORK/selective-transaction/preserved.hir"
printf '%s\n' sentinel >"$WORK/selective-transaction/preserved.hir/marker"
write_inventory "$CASES/fixtures/runtime.kofun" "$CASES/fixtures/math.kofun" \
    "$WORK/selective-transaction.inventory"
set +e
(
    cd "$WORK/selective-transaction"
    "$TOOL" "$WORK/selective-transaction.inventory" preserved.hir \
        >e2s77.stdout 2>e2s77.stderr
)
transaction_status=$?
set -e
test "$transaction_status" -eq 1
test ! -s "$WORK/selective-transaction/e2s77.stderr"
test -d "$WORK/selective-transaction/preserved.hir"
grep -Fx sentinel "$WORK/selective-transaction/preserved.hir/marker" >/dev/null
printf '%s\n' \
    'error[E2S77]: cannot clear requested output `preserved.hir` before the transaction' \
    >"$WORK/selective-transaction/e2s77.expected"
cmp "$WORK/selective-transaction/e2s77.expected" \
    "$WORK/selective-transaction/e2s77.stdout"

expect_exact_forbidden E2S78 selective-internal \
    'error[E2S78]: type-resolution signature token invariant failed' \
    env KOFUN_DIAGNOSTIC_FAULT=selective-type-resolution-token \
    "$TOOL" "$WORK/positive.inventory" "$WORK/selective-internal.hir" \
    "$WORK/selective-internal.c"

# The qualified-import helper remains independently buildable and passing.
sh "$ROOT/tests/conformance/modules/imports-qualified/run.sh"

if command -v clang >/dev/null 2>&1; then
    clang -std=c11 -Wall -Wextra -Werror -pedantic \
        "$ROOT/bootstrap/stage2/imports_selective.c" \
        "$ROOT/bootstrap/stage2/visibility_access.c" \
        "$ROOT/bootstrap/stage2/sha256.c" \
        -o "$WORK/imports-selective-clang"
    "$WORK/imports-selective-clang" "$WORK/positive.inventory" "$WORK/clang.hir"
    cmp "$WORK/positive.hir" "$WORK/clang.hir"
fi

"$CC" -std=c11 -O1 -g -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$ROOT/bootstrap/stage2/imports_selective.c" \
    "$ROOT/bootstrap/stage2/visibility_access.c" \
    "$ROOT/bootstrap/stage2/sha256.c" \
    -o "$WORK/imports-selective-sanitized"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
    "$WORK/imports-selective-sanitized" \
    "$WORK/positive.inventory" "$WORK/sanitized.hir"
cmp "$WORK/positive.hir" "$WORK/sanitized.hir"

if "$CC" -std=c11 -O0 -Wall -Wextra -Werror -pedantic -fanalyzer \
    "$ROOT/bootstrap/stage2/imports_selective.c" \
    "$ROOT/bootstrap/stage2/visibility_access.c" \
    "$ROOT/bootstrap/stage2/sha256.c" \
    -o "$WORK/imports-selective-analyzed" >/dev/null 2>&1
then
    printf '%s\n' 'PASS: GCC analyzer accepts the selective-import resolver'
fi

printf '%s\n' 'PASS: selective imports preserve namespaces, identities, explicit edges, and transactional failures'
