#!/usr/bin/env sh
set -eu

LC_ALL=C
export LC_ALL

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)
CASES="$ROOT/tests/conformance/modules/imports-qualified"
CC=${CC:-cc}
WORK=${KOFUN_IMPORTS_QUALIFIED_WORK:-"$ROOT/build/imports-qualified"}
PACKAGE_ID=1111111111111111111111111111111111111111111111111111111111111111

fail() {
    printf '%s\n' "FAIL: $*" >&2
    exit 1
}

command -v "$CC" >/dev/null 2>&1 || fail 'a C11 compiler is required'
command -v sha256sum >/dev/null 2>&1 || fail 'sha256sum is required'
command -v xxd >/dev/null 2>&1 || fail 'xxd is required'
case $WORK in
    */imports-qualified|*/imports-qualified.*) ;;
    *) fail "work directory must end in imports-qualified[.suffix]: $WORK" ;;
esac
rm -rf "$WORK"
mkdir -p "$WORK/tmp"
export TMPDIR="$WORK/tmp"

"$CC" -std=c11 -O2 -Wall -Wextra -Werror -pedantic \
    -I"$ROOT/bootstrap/stage2" \
    "$ROOT/bootstrap/stage2/sha256.c" \
    "$ROOT/bootstrap/stage2/visibility_access.c" \
    "$ROOT/bootstrap/stage2/module_symbols.c" \
    -o "$WORK/kofun-imports-qualified"

id_for() {
    printf '%s' "$1" | sha256sum | awk '{ print $1 }'
}

append_module() {
    inventory=$1
    module_path=$2
    source=$3
    logical=${4:-"src/$(printf '%s' "$module_path" | tr . /).kofun"}
    printf '%s|%s|%s|%s|%s\n' \
        "$PACKAGE_ID" "$(id_for "module:$module_path")" \
        "$(id_for "file:$logical")" "$logical" "$source" >>"$inventory"
}

new_inventory() {
    inventory=$1
    shift
    : >"$inventory"
    while test "$#" -gt 0; do
        append_module "$inventory" "$1" "$2"
        shift 2
    done
}

expect_failure() {
    stem=$1
    code=$2
    inventory=$3
    set +e
    "$WORK/kofun-imports-qualified" "$inventory" "$WORK/$stem.out" "$WORK/$stem.c" \
        >"$WORK/$stem.actual" 2>"$WORK/$stem.internal.stderr"
    status=$?
    set -e
    test "$status" -eq 1 || fail "$stem exited $status instead of 1"
    test ! -s "$WORK/$stem.internal.stderr" || fail "$stem wrote internal stderr"
    test ! -e "$WORK/$stem.out" || fail "$stem committed rejected HIR"
    test ! -e "$WORK/$stem.c" || fail "$stem committed rejected backend output"
    grep -F "error[$code]:" "$WORK/$stem.actual" >/dev/null ||
        fail "$stem expected $code"
    if test "${KOFUN_COLLECT_IMPORT_DIAGNOSTICS:-0}" != 1; then
        cmp "$CASES/$stem.stderr" "$WORK/$stem.actual" ||
            fail "$stem diagnostic differs"
    fi
    printf '%s\n' "PASS qualified-import diagnostic: $stem"
}

# One package exercises forward qualified calls, two importers sharing one
# target, internal/public reachability, private same-file calls, and a
# three-module transitive chain whose every edge is explicit.
new_inventory "$WORK/positive.inventory" \
    client.transitive "$CASES/transitive.kofun" \
    user.math "$CASES/math.kofun" \
    app.main "$CASES/main.kofun" \
    library.bridge "$CASES/bridge.kofun" \
    client.another "$CASES/another.kofun"

"$WORK/kofun-imports-qualified" \
    "$WORK/positive.inventory" "$WORK/positive.out" "$WORK/positive.c"
"$WORK/kofun-imports-qualified" \
    "$WORK/positive.inventory" "$WORK/positive.second.out" "$WORK/positive.second.c"
cmp "$WORK/positive.out" "$WORK/positive.second.out" ||
    fail 'repeated HIR output differs'
cmp "$WORK/positive.c" "$WORK/positive.second.c" ||
    fail 'repeated backend output differs'

grep '^kofun-imports-qualified/v1$' "$WORK/positive.out" >/dev/null ||
    fail 'qualified import projection header is missing'
test "$(grep -c '|declared=user.math$' "$WORK/positive.out")" -eq 1 ||
    fail 'shared target module was not loaded exactly once'
test "$(grep -c '^import|' "$WORK/positive.out")" -eq 4 ||
    fail 'expected four explicit import bindings'
test "$(grep -c '^qualified-call|' "$WORK/positive.out")" -eq 4 ||
    fail 'expected four qualified HIR calls'
grep '|access=Allowed|reason=Allowed|' "$WORK/positive.out" >/dev/null ||
    fail 'qualified HIR omitted the access decision'
grep '|local=math|' "$WORK/positive.out" >/dev/null ||
    fail 'final path component did not become the qualifier'

"$CC" -std=c11 -O2 -Wall -Wextra -Werror -pedantic \
    "$WORK/positive.c" -o "$WORK/positive-program"
set +e
"$WORK/positive-program" >"$WORK/positive.stdout" 2>"$WORK/positive.stderr"
program_status=$?
set -e
test "$program_status" -eq 42 || fail "qualified backend exited $program_status instead of 42"
test ! -s "$WORK/positive.stdout" || fail 'qualified backend wrote stdout'
test ! -s "$WORK/positive.stderr" || fail 'qualified backend wrote stderr'

# Package inventory order and host checkout paths are non-semantic.
sed '1!G;h;$!d' "$WORK/positive.inventory" >"$WORK/reverse.inventory"
"$WORK/kofun-imports-qualified" \
    "$WORK/reverse.inventory" "$WORK/reverse.out" "$WORK/reverse.c"
cmp "$WORK/positive.out" "$WORK/reverse.out" ||
    fail 'source-list permutation changed qualified HIR'
cmp "$WORK/positive.c" "$WORK/reverse.c" ||
    fail 'source-list permutation changed backend output'

mkdir -p "$WORK/remap-a" "$WORK/remap-b"
for source in math main another bridge transitive; do
    cp "$CASES/$source.kofun" "$WORK/remap-a/$source.kofun"
    cp "$CASES/$source.kofun" "$WORK/remap-b/$source.kofun"
done
new_inventory "$WORK/remap-a.inventory" \
    user.math "$WORK/remap-a/math.kofun" \
    app.main "$WORK/remap-a/main.kofun" \
    client.another "$WORK/remap-a/another.kofun" \
    library.bridge "$WORK/remap-a/bridge.kofun" \
    client.transitive "$WORK/remap-a/transitive.kofun"
new_inventory "$WORK/remap-b.inventory" \
    user.math "$WORK/remap-b/math.kofun" \
    app.main "$WORK/remap-b/main.kofun" \
    client.another "$WORK/remap-b/another.kofun" \
    library.bridge "$WORK/remap-b/bridge.kofun" \
    client.transitive "$WORK/remap-b/transitive.kofun"
"$WORK/kofun-imports-qualified" \
    "$WORK/remap-a.inventory" "$WORK/remap-a.out" "$WORK/remap-a.c"
"$WORK/kofun-imports-qualified" \
    "$WORK/remap-b.inventory" "$WORK/remap-b.out" "$WORK/remap-b.c"
cmp "$WORK/remap-a.out" "$WORK/remap-b.out" ||
    fail 'absolute checkout remap changed qualified HIR'
cmp "$WORK/remap-a.c" "$WORK/remap-b.c" ||
    fail 'absolute checkout remap changed backend output'

new_inventory "$WORK/import-order-a.inventory" \
    order.consumer "$CASES/import_order_a.kofun" \
    user.math "$CASES/math.kofun" library.bridge "$CASES/bridge.kofun"
new_inventory "$WORK/import-order-b.inventory" \
    order.consumer "$CASES/import_order_b.kofun" \
    user.math "$CASES/math.kofun" library.bridge "$CASES/bridge.kofun"
"$WORK/kofun-imports-qualified" \
    "$WORK/import-order-a.inventory" "$WORK/import-order-a.out" "$WORK/import-order-a.c"
"$WORK/kofun-imports-qualified" \
    "$WORK/import-order-b.inventory" "$WORK/import-order-b.out" "$WORK/import-order-b.c"
grep '^import|' "$WORK/import-order-a.out" | sed 's/|span=.*//' >"$WORK/import-order-a.ids"
grep '^import|' "$WORK/import-order-b.out" | sed 's/|span=.*//' >"$WORK/import-order-b.ids"
cmp "$WORK/import-order-a.ids" "$WORK/import-order-b.ids" ||
    fail 'import declaration order changed ImportBindingIds or lookup targets'
cmp "$WORK/import-order-a.c" "$WORK/import-order-b.c" ||
    fail 'import declaration order changed the backend artifact'

# Independently reconstruct one ImportBindingId from the normative framing.
u16be() {
    number=$1
    printf "\\$(printf '%03o' $((number / 256)))"
    printf "\\$(printf '%03o' $((number % 256)))"
}

u32be() {
    number=$1
    printf "\\$(printf '%03o' $(((number / 16777216) % 256)))"
    printf "\\$(printf '%03o' $(((number / 65536) % 256)))"
    printf "\\$(printf '%03o' $(((number / 256) % 256)))"
    printf "\\$(printf '%03o' $((number % 256)))"
}

hash_field_file() {
    tag=$1
    value=$2
    length=$3
    u16be "$tag"
    u32be "$length"
    if test "$4" = hex; then printf '%s' "$value" | xxd -r -p; else printf '%s' "$value"; fi
}

printf '%s\n' 'kofun.namespace-id/v1' 'tag=2' 'name=module' \
    >"$WORK/module.namespace.payload"
{
    printf 'KOFUN\000'
    u16be 21
    printf '%s' kofun.id.namespace/v1
    u32be "$(wc -c <"$WORK/module.namespace.payload" | tr -d '[:space:]')"
    dd if="$WORK/module.namespace.payload" bs=4096 2>/dev/null
} >"$WORK/module.namespace.preimage"
module_namespace=$(sha256sum "$WORK/module.namespace.preimage" | awk '{ print $1 }')
main_module=$(id_for 'module:app.main')
main_file=$(id_for 'file:src/app/main.kofun')
math_module=$(id_for 'module:user.math')
{
    hash_field_file 32769 "$main_module" 32 hex
    hash_field_file 32770 "$main_file" 32 hex
    hash_field_file 32771 "$module_namespace" 32 hex
    hash_field_file 32772 math 4 text
    hash_field_file 32773 "$math_module" 32 hex
    hash_field_file 32774 qualified-module-v1 19 text
} >"$WORK/import.payload"
{
    printf 'KOFUN\000'
    u16be 26
    printf '%s' kofun.id.import-binding/v1
    u32be "$(wc -c <"$WORK/import.payload" | tr -d '[:space:]')"
    dd if="$WORK/import.payload" bs=4096 2>/dev/null
} >"$WORK/import.preimage"
expected_import=$(sha256sum "$WORK/import.preimage" | awk '{ print $1 }')
grep -F "caller-module=$main_module" "$WORK/positive.out" |
    grep -F "binding=$expected_import" >/dev/null ||
    fail 'ImportBindingId does not match normative framed SHA-256'

# Exact negative diagnostics and transactional artifact absence.
new_inventory "$WORK/missing.inventory" bad.missing "$CASES/missing.kofun"
new_inventory "$WORK/invalid_path.inventory" bad.invalid_path "$CASES/invalid_path.kofun"
new_inventory "$WORK/import_after_declaration.inventory" \
    bad.after "$CASES/import_after_declaration.kofun"
new_inventory "$WORK/duplicate.inventory" \
    bad.duplicate "$CASES/duplicate.kofun" user.math "$CASES/math.kofun"
new_inventory "$WORK/collision.inventory" \
    bad.collision "$CASES/collision.kofun" \
    user.math "$CASES/math.kofun" utility.math "$CASES/utility_math.kofun"
new_inventory "$WORK/self_import.inventory" bad.self_import "$CASES/self_import.kofun"
new_inventory "$WORK/cycle_two.inventory" \
    cycle.two.a "$CASES/cycle_two_a.kofun" cycle.two.b "$CASES/cycle_two_b.kofun"
new_inventory "$WORK/cycle_three.inventory" \
    cycle.three.a "$CASES/cycle_three_a.kofun" \
    cycle.three.b "$CASES/cycle_three_b.kofun" \
    cycle.three.c "$CASES/cycle_three_c.kofun"
new_inventory "$WORK/cycle_canonical.inventory" \
    cycle.three.c "$CASES/cycle_three_c.kofun" \
    cycle.two.b "$CASES/cycle_two_b.kofun" \
    cycle.three.a "$CASES/cycle_three_a.kofun" \
    cycle.two.a "$CASES/cycle_two_a.kofun" \
    cycle.three.b "$CASES/cycle_three_b.kofun"
new_inventory "$WORK/unqualified.inventory" \
    bad.unqualified "$CASES/unqualified.kofun" user.math "$CASES/math.kofun"
new_inventory "$WORK/invisible.inventory" \
    bad.invisible "$CASES/invisible.kofun" user.math "$CASES/math.kofun"
new_inventory "$WORK/transitive_leak.inventory" \
    bad.transitive_leak "$CASES/transitive_leak.kofun" \
    library.bridge "$CASES/bridge.kofun" user.math "$CASES/math.kofun"
new_inventory "$WORK/alias.inventory" bad.alias "$CASES/alias.kofun"
new_inventory "$WORK/wildcard.inventory" bad.wildcard "$CASES/wildcard.kofun"
new_inventory "$WORK/selective.inventory" bad.selective "$CASES/selective.kofun"
new_inventory "$WORK/arity.inventory" \
    bad.arity "$CASES/arity.kofun" user.math "$CASES/math.kofun"
new_inventory "$WORK/backend_unsupported.inventory" \
    bad.backend_unsupported "$CASES/backend_unsupported.kofun" \
    user.math "$CASES/math.kofun"

printf '%s|%s|%s|%s|%s\n' \
    "$PACKAGE_ID" "$(id_for 'module:bad.escape')" "$(id_for 'file:bad.escape')" \
    '../escape.kofun' "$CASES/missing.kofun" >"$WORK/path_escape.inventory"

expect_failure missing E2S59 "$WORK/missing.inventory"
expect_failure invalid_path E2S57 "$WORK/invalid_path.inventory"
expect_failure import_after_declaration E2S58 "$WORK/import_after_declaration.inventory"
expect_failure duplicate E2S60 "$WORK/duplicate.inventory"
expect_failure collision E2S60 "$WORK/collision.inventory"
expect_failure self_import E2S59 "$WORK/self_import.inventory"
expect_failure cycle_two E2S61 "$WORK/cycle_two.inventory"
expect_failure cycle_three E2S61 "$WORK/cycle_three.inventory"
expect_failure cycle_canonical E2S61 "$WORK/cycle_canonical.inventory"
expect_failure unqualified E2S62 "$WORK/unqualified.inventory"
expect_failure invisible E2S63 "$WORK/invisible.inventory"
expect_failure transitive_leak E2S62 "$WORK/transitive_leak.inventory"
expect_failure alias E2S57 "$WORK/alias.inventory"
expect_failure wildcard E2S57 "$WORK/wildcard.inventory"
expect_failure selective E2S57 "$WORK/selective.inventory"
expect_failure arity E2S64 "$WORK/arity.inventory"
expect_failure backend_unsupported E2S65 "$WORK/backend_unsupported.inventory"
expect_failure path_escape E2S48 "$WORK/path_escape.inventory"

# Path and qualified-use resource boundaries are generated, not hand-waved.
path64=$(awk 'BEGIN { for (i = 1; i <= 64; i++) printf "%sc%d", i == 1 ? "" : ".", i }')
path65="$path64.c65"
printf 'module %s\npub fn answer() -> Int { return 42 }\n' "$path64" >"$WORK/path64-target.kofun"
printf 'module limits.path64\nimport %s\nfn main() -> Int { return c64.answer() }\n' "$path64" \
    >"$WORK/path64-main.kofun"
new_inventory "$WORK/path64.inventory" \
    "$path64" "$WORK/path64-target.kofun" limits.path64 "$WORK/path64-main.kofun"
"$WORK/kofun-imports-qualified" \
    "$WORK/path64.inventory" "$WORK/path64.out" "$WORK/path64.c"
printf 'module limits.path65\nimport %s\nfn main() -> Int { return 0 }\n' "$path65" \
    >"$WORK/path65.kofun"
new_inventory "$WORK/path65.inventory" limits.path65 "$WORK/path65.kofun"
expect_failure path65 E2S55 "$WORK/path65.inventory"

component255=$(awk 'BEGIN { for (i = 0; i < 255; i++) printf "a" }')
component256=$(awk 'BEGIN { for (i = 0; i < 256; i++) printf "b" }')
path4096=$(awk -v a="$component255" -v b="$component256" 'BEGIN {
    for (i = 0; i < 15; i++) printf "%s%s", i == 0 ? "" : ".", a
    printf ".%s", b
}')
path4097=$(awk -v a="$component255" -v b="$component256" 'BEGIN {
    for (i = 0; i < 14; i++) printf "%s%s", i == 0 ? "" : ".", a
    printf ".%s.%s", b, b
}')
test "$(printf '%s' "$path4096" | wc -c | tr -d '[:space:]')" -eq 4096 ||
    fail 'generated exact module-path byte boundary is wrong'
test "$(printf '%s' "$path4097" | wc -c | tr -d '[:space:]')" -eq 4097 ||
    fail 'generated module-path byte overflow is wrong'
printf 'module %s\npub fn answer() -> Int { return 42 }\n' "$path4096" \
    >"$WORK/path4096-target.kofun"
printf 'module limits.path4096\nimport %s\nfn main() -> Int { return %s.answer() }\n' \
    "$path4096" "$component256" >"$WORK/path4096-main.kofun"
: >"$WORK/path4096.inventory"
append_module "$WORK/path4096.inventory" "$path4096" \
    "$WORK/path4096-target.kofun" 'src/path4096-target.kofun'
append_module "$WORK/path4096.inventory" limits.path4096 \
    "$WORK/path4096-main.kofun" 'src/path4096-main.kofun'
"$WORK/kofun-imports-qualified" \
    "$WORK/path4096.inventory" "$WORK/path4096.out"
printf 'module limits.path4097\nimport %s\nfn main() -> Int { return 0 }\n' "$path4097" \
    >"$WORK/path4097.kofun"
new_inventory "$WORK/path_bytes_over.inventory" limits.path4097 "$WORK/path4097.kofun"
expect_failure path_bytes_over E2S55 "$WORK/path_bytes_over.inventory"

# The package cap makes 255 distinct non-self targets the largest successful
# outgoing set. A 256th import is still syntactically accepted, while 257 is
# rejected by the declared per-module guard before any lookup starts.
printf '%s\n' 'module limits.imports255' >"$WORK/imports255-root.kofun"
i=1
while test "$i" -le 255; do
    suffix=$(printf 'm%03d' "$i")
    printf 'import imports.%s\n' "$suffix" >>"$WORK/imports255-root.kofun"
    printf 'module imports.%s\npub fn value() -> Int { return %d }\n' \
        "$suffix" "$i" >"$WORK/imports255-$suffix.kofun"
    i=$((i + 1))
done
printf '%s\n' 'fn main() -> Int { return 0 }' >>"$WORK/imports255-root.kofun"
: >"$WORK/imports255.inventory"
append_module "$WORK/imports255.inventory" limits.imports255 "$WORK/imports255-root.kofun"
i=1
while test "$i" -le 255; do
    suffix=$(printf 'm%03d' "$i")
    append_module "$WORK/imports255.inventory" "imports.$suffix" "$WORK/imports255-$suffix.kofun"
    i=$((i + 1))
done
"$WORK/kofun-imports-qualified" \
    "$WORK/imports255.inventory" "$WORK/imports255.out"
test "$(grep -c '^import|' "$WORK/imports255.out")" -eq 255 ||
    fail '255 distinct outgoing imports did not commit'

printf '%s\n' 'module limits.imports256' >"$WORK/imports256.kofun"
i=1
while test "$i" -le 256; do
    printf 'import missing.m%03d\n' "$i" >>"$WORK/imports256.kofun"
    i=$((i + 1))
done
printf '%s\n' 'fn main() -> Int { return 0 }' >>"$WORK/imports256.kofun"
new_inventory "$WORK/imports256.inventory" limits.imports256 "$WORK/imports256.kofun"
set +e
"$WORK/kofun-imports-qualified" "$WORK/imports256.inventory" "$WORK/imports256.out" \
    >"$WORK/imports256.actual"
imports256_status=$?
set -e
test "$imports256_status" -eq 1 || fail '256 unresolved imports unexpectedly succeeded'
grep -F 'error[E2S59]:' "$WORK/imports256.actual" >/dev/null ||
    fail 'the exact 256-import syntax boundary was rejected as a resource overflow'
test ! -e "$WORK/imports256.out" || fail '256 unresolved imports committed output'

awk '{ print; if ($0 == "import missing.m256") print "import missing.m257" }' \
    "$WORK/imports256.kofun" >"$WORK/imports257.kofun"
new_inventory "$WORK/imports257.inventory" limits.imports257 "$WORK/imports257.kofun"
set +e
"$WORK/kofun-imports-qualified" "$WORK/imports257.inventory" "$WORK/imports257.out" \
    >"$WORK/imports257.actual"
imports257_status=$?
set -e
test "$imports257_status" -eq 1 || fail '257 imports were accepted'
grep -F 'error[E2S55]:' "$WORK/imports257.actual" >/dev/null ||
    fail '257 imports did not report the resource diagnostic'
test ! -e "$WORK/imports257.out" || fail '257 imports committed output'

{
    printf '%s\n' 'module limits.calls' 'fn target() -> Int { return 0 }' 'fn main() -> Int {'
    awk 'BEGIN { for (i = 0; i < 65536; i++) print "target()" }'
    printf '%s\n' 'return 0' '}'
} >"$WORK/calls-limit.kofun"
new_inventory "$WORK/calls_limit.inventory" limits.calls "$WORK/calls-limit.kofun"
"$WORK/kofun-imports-qualified" \
    "$WORK/calls_limit.inventory" "$WORK/calls-limit.out"
awk '{ if ($0 == "return 0") print "target()"; print }' \
    "$WORK/calls-limit.kofun" >"$WORK/calls-over.kofun"
new_inventory "$WORK/calls_over.inventory" limits.calls "$WORK/calls-over.kofun"
expect_failure calls_over E2S55 "$WORK/calls_over.inventory"

"$CC" -std=c11 -O1 -g -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$ROOT/bootstrap/stage2" \
    "$ROOT/bootstrap/stage2/sha256.c" \
    "$ROOT/bootstrap/stage2/visibility_access.c" \
    "$ROOT/bootstrap/stage2/module_symbols.c" \
    -o "$WORK/kofun-imports-qualified-sanitized"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
    "$WORK/kofun-imports-qualified-sanitized" \
    "$WORK/positive.inventory" "$WORK/sanitized.out" "$WORK/sanitized.c"
cmp "$WORK/positive.out" "$WORK/sanitized.out" ||
    fail 'sanitized build changed qualified HIR'
cmp "$WORK/positive.c" "$WORK/sanitized.c" ||
    fail 'sanitized build changed backend output'

if "$CC" -std=c11 -O0 -Wall -Wextra -Werror -pedantic -fanalyzer \
    -I"$ROOT/bootstrap/stage2" \
    "$ROOT/bootstrap/stage2/sha256.c" \
    "$ROOT/bootstrap/stage2/visibility_access.c" \
    "$ROOT/bootstrap/stage2/module_symbols.c" \
    -o "$WORK/kofun-imports-qualified-analyzed" >/dev/null 2>&1
then
    printf '%s\n' 'PASS: GCC analyzer accepts the qualified import resolver'
fi

printf '%s\n' \
    'PASS: qualified imports bind only the final module qualifier' \
    'PASS: import identities, target SymbolIds, spans, and access proofs survive in HIR' \
    'PASS: local-package graph resolution and canonical shortest cycles are deterministic' \
    'PASS: private/internal/public visibility is enforced before HIR commit' \
    'PASS: SymbolId-linked bounded C11 execution returns 42' \
    'PASS: failed imports leave no HIR or backend artifact'
