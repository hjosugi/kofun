#!/usr/bin/env sh
set -eu

LC_ALL=C
export LC_ALL

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)
CASES="$ROOT/tests/conformance/modules/re-exports"
CC=${CC:-cc}
WORK=${KOFUN_RE_EXPORTS_WORK:-"$ROOT/build/re-exports"}
TOOL="$WORK/re-exports"
KIF_TOOL="$WORK/kofun-kif-v1"
PACKAGE_ID=1111111111111111111111111111111111111111111111111111111111111111
COLLECTIONS_MODULE=2222222222222222222222222222222222222222222222222222222222222222
COLLECTIONS_FILE=2323232323232323232323232323232323232323232323232323232323232323
FACADE_MODULE=3333333333333333333333333333333333333333333333333333333333333333
FACADE_FILE=3434343434343434343434343434343434343434343434343434343434343434
SECOND_MODULE=4444444444444444444444444444444444444444444444444444444444444444
SECOND_FILE=4545454545454545454545454545454545454545454545454545454545454545
ALTERNATE_MODULE=5555555555555555555555555555555555555555555555555555555555555555
ALTERNATE_FILE=5656565656565656565656565656565656565656565656565656565656565656

fail() {
    printf '%s\n' "FAIL: $*" >&2
    exit 1
}

case $WORK in
    */re-exports|*/re-exports.*) ;;
    *) fail "work directory must end in re-exports[.suffix]: $WORK" ;;
esac

rm -rf "$WORK"
mkdir -p "$WORK"

"$CC" -std=c11 -O2 -Wall -Wextra -Werror -pedantic \
    -DKOFUN_TEST_DIAGNOSTIC_FAULTS \
    -I"$ROOT/bootstrap/stage2" \
    "$ROOT/bootstrap/stage2/re_exports.c" \
    "$ROOT/bootstrap/stage2/kif_v1.c" \
    "$ROOT/bootstrap/stage2/visibility_access.c" \
    "$ROOT/bootstrap/stage2/sha256.c" \
    -o "$TOOL"

"$CC" -std=c11 -O2 -Wall -Wextra -Werror -pedantic \
    -I"$ROOT/bootstrap/stage2" \
    "$ROOT/bootstrap/stage2/kif_v1_tool.c" \
    "$ROOT/bootstrap/stage2/kif_v1.c" \
    "$ROOT/bootstrap/stage2/sha256.c" \
    -o "$KIF_TOOL"

write_inventory() {
    facade_source=$1
    output=$2
    {
        printf '%s|%s|%s|lib.collections|lib/collections.kofun|%s\n' \
            "$PACKAGE_ID" "$COLLECTIONS_MODULE" "$COLLECTIONS_FILE" \
            "$CASES/fixtures/collections.kofun"
        printf '%s|%s|%s|api.collections|api/collections.kofun|%s\n' \
            "$PACKAGE_ID" "$FACADE_MODULE" "$FACADE_FILE" "$facade_source"
    } >"$output"
}

expect_failure() {
    code=$1
    name=$2
    facade=$3
    inventory=$4
    hir="$WORK/$name.hir"
    kif="$WORK/$name.kif"
    tooling="$WORK/$name.tooling"
    log="$WORK/$name.log"
    printf '%s\n' stale >"$hir"
    printf '%s\n' stale >"$kif"
    printf '%s\n' stale >"$tooling"
    set +e
    "$TOOL" "$inventory" "$facade" "$hir" "$kif" "$tooling" \
        >"$log" 2>&1
    result=$?
    set -e
    test "$result" -eq 1 || fail "$name exited $result instead of 1"
    grep -F "error[$code]:" "$log" >/dev/null ||
        fail "$name did not emit $code"
    test ! -e "$hir" || fail "$name left HIR"
    test ! -e "$kif" || fail "$name left KIF"
    test ! -e "$tooling" || fail "$name left tooling projection"
}

# Both accepted forms, same-spelled namespaces, and KIF/tooling publication.
write_inventory "$CASES/fixtures/facade.kofun" "$WORK/positive.inventory"
"$TOOL" "$WORK/positive.inventory" api.collections \
    "$WORK/positive.hir" "$WORK/positive.kif" "$WORK/positive.tooling"
grep -Fx 'kofun-re-exports/v1' "$WORK/positive.hir" >/dev/null
test "$(grep -c '^export|' "$WORK/positive.hir")" -eq 5
grep -F '|ns=2:module:' "$WORK/positive.hir" | grep -F '|name=collections|' >/dev/null
test "$(grep -F '|name=Map|' "$WORK/positive.hir" | wc -l | tr -d ' ')" -eq 2
grep -F '|ns=0:value:' "$WORK/positive.hir" | grep -F '|name=Map|' >/dev/null
grep -F '|ns=1:type:' "$WORK/positive.hir" | grep -F '|name=Map|' >/dev/null
grep -F '|name=Set|' "$WORK/positive.hir" >/dev/null
grep -F '|name=Present|' "$WORK/positive.hir" >/dev/null
grep -F '|proof=non-widening-public-v1' "$WORK/positive.hir" >/dev/null
grep -F 'doc|facade=api.collections.Map|canonical=lib.collections.Map|' \
    "$WORK/positive.tooling" >/dev/null
grep -F '|linker-forwarding=false|runtime-forwarding=false' \
    "$WORK/positive.tooling" >/dev/null

"$KIF_TOOL" read "$WORK/positive.kif" "$WORK/positive.json"
test "$(grep -c '"kind": "export"' "$WORK/positive.json")" -eq 5
grep -F '"target_kind": "function", "chain_count": 1' \
    "$WORK/positive.json" >/dev/null
"$TOOL" --resolve-kif "$WORK/positive.kif" Map value \
    "$WORK/consumer-value.hir"
"$TOOL" --resolve-kif "$WORK/positive.kif" Map type \
    "$WORK/consumer-type.hir"
"$TOOL" --resolve-kif "$WORK/positive.kif" collections module \
    "$WORK/consumer-module.hir"
"$TOOL" --resolve-kif "$WORK/positive.kif" Present value \
    "$WORK/consumer-constructor.hir"
grep -F '|namespace=value|' "$WORK/consumer-value.hir" >/dev/null
grep -F '|namespace=type|' "$WORK/consumer-type.hir" >/dev/null
grep -F '|namespace=module|' "$WORK/consumer-module.hir" >/dev/null
grep -F '|namespace=value|' "$WORK/consumer-constructor.hir" >/dev/null

# Source/inventory/path remapping cannot change authoritative interface bytes.
write_inventory "$CASES/fixtures/facade_reordered.kofun" \
    "$WORK/reordered.inventory"
"$TOOL" "$WORK/reordered.inventory" api.collections \
    "$WORK/reordered.hir" "$WORK/reordered.kif" "$WORK/reordered.tooling"
cmp "$WORK/positive.kif" "$WORK/reordered.kif"
mkdir -p "$WORK/remapped/a" "$WORK/remapped/b"
cp "$CASES/fixtures/collections.kofun" "$WORK/remapped/a/collections.kofun"
cp "$CASES/fixtures/facade.kofun" "$WORK/remapped/b/facade.kofun"
{
    printf '%s|%s|%s|api.collections|moved/facade.kofun|%s\n' \
        "$PACKAGE_ID" "$FACADE_MODULE" "$FACADE_FILE" \
        "$WORK/remapped/b/facade.kofun"
    printf '%s|%s|%s|lib.collections|moved/collections.kofun|%s\n' \
        "$PACKAGE_ID" "$COLLECTIONS_MODULE" "$COLLECTIONS_FILE" \
        "$WORK/remapped/a/collections.kofun"
} >"$WORK/remapped.inventory"
"$TOOL" "$WORK/remapped.inventory" api.collections \
    "$WORK/remapped.hir" "$WORK/remapped.kif" "$WORK/remapped.tooling"
cmp "$WORK/positive.kif" "$WORK/remapped.kif"

# A private target body edit does not perturb the facade digest; adding an
# edge does.
sed '/private fn hidden/,/^}/s/return value/return 42/' \
    "$CASES/fixtures/collections.kofun" \
    >"$WORK/collections-body-edit.kofun"
{
    printf '%s|%s|%s|lib.collections|lib/collections.kofun|%s\n' \
        "$PACKAGE_ID" "$COLLECTIONS_MODULE" "$COLLECTIONS_FILE" \
        "$WORK/collections-body-edit.kofun"
    printf '%s|%s|%s|api.collections|api/collections.kofun|%s\n' \
        "$PACKAGE_ID" "$FACADE_MODULE" "$FACADE_FILE" \
        "$CASES/fixtures/facade.kofun"
} >"$WORK/body-edit.inventory"
"$TOOL" "$WORK/body-edit.inventory" api.collections \
    "$WORK/body-edit.hir" "$WORK/body-edit.kif" "$WORK/body-edit.tooling"
cmp "$WORK/positive.kif" "$WORK/body-edit.kif"

write_inventory "$CASES/fixtures/facade_map_only.kofun" \
    "$WORK/map-only.inventory"
"$TOOL" "$WORK/map-only.inventory" api.collections \
    "$WORK/map-only.hir" "$WORK/map-only.kif" "$WORK/map-only.tooling"
if cmp -s "$WORK/positive.kif" "$WORK/map-only.kif"; then
    fail 'adding the Set edge did not change facade KIF'
fi
positive_digest=$(sed -n \
    's/.*"public_semantic_digest": "\([0-9a-f]*\)".*/\1/p' \
    "$WORK/positive.json")
"$KIF_TOOL" read "$WORK/map-only.kif" "$WORK/map-only.json"
map_only_digest=$(sed -n \
    's/.*"public_semantic_digest": "\([0-9a-f]*\)".*/\1/p' \
    "$WORK/map-only.json")
test "$positive_digest" != "$map_only_digest"

# A two-level chain keeps original target identities. Two different facades
# consumed from source-free KIF resolve to one declaration SymbolId.
{
    printf '%s|%s|%s|lib.collections|lib/collections.kofun|%s\n' \
        "$PACKAGE_ID" "$COLLECTIONS_MODULE" "$COLLECTIONS_FILE" \
        "$CASES/fixtures/collections.kofun"
    printf '%s|%s|%s|api.collections|api/collections.kofun|%s\n' \
        "$PACKAGE_ID" "$FACADE_MODULE" "$FACADE_FILE" \
        "$CASES/fixtures/facade.kofun"
    printf '%s|%s|%s|api.v2|api/v2.kofun|%s\n' \
        "$PACKAGE_ID" "$SECOND_MODULE" "$SECOND_FILE" \
        "$CASES/fixtures/facade_second.kofun"
} >"$WORK/two-level.inventory"
"$TOOL" "$WORK/two-level.inventory" api.v2 \
    "$WORK/two-level.hir" "$WORK/two-level.kif" "$WORK/two-level.tooling"
test "$(grep -F 'export|module=api.v2|' "$WORK/two-level.hir" |
    grep -F '|name=Map|' | grep -c '|chain=')" -eq 2
grep -F 'doc|facade=api.v2.Map|canonical=lib.collections.Map|' \
    "$WORK/two-level.tooling" | grep -F '|chain=2|' >/dev/null
"$TOOL" --resolve-kif "$WORK/two-level.kif" Map value \
    "$WORK/v2-consumer.hir"
{
    printf '%s|%s|%s|lib.collections|lib/collections.kofun|%s\n' \
        "$PACKAGE_ID" "$COLLECTIONS_MODULE" "$COLLECTIONS_FILE" \
        "$CASES/fixtures/collections.kofun"
    printf '%s|%s|%s|api.alternate|api/alternate.kofun|%s\n' \
        "$PACKAGE_ID" "$ALTERNATE_MODULE" "$ALTERNATE_FILE" \
        "$CASES/fixtures/facade_alternate.kofun"
} >"$WORK/alternate.inventory"
"$TOOL" "$WORK/alternate.inventory" api.alternate \
    "$WORK/alternate.hir" "$WORK/alternate.kif" "$WORK/alternate.tooling"
"$TOOL" --resolve-kif "$WORK/alternate.kif" Map value \
    "$WORK/alternate-consumer.hir"
v2_target=$(sed -n 's/.*|target-symbol=\([0-9a-f]*\)|.*/\1/p' \
    "$WORK/v2-consumer.hir")
alternate_target=$(sed -n 's/.*|target-symbol=\([0-9a-f]*\)|.*/\1/p' \
    "$WORK/alternate-consumer.hir")
test "${#v2_target}" -eq 64
test "$v2_target" = "$alternate_target"

# Every rejected source spelling is explicit and transactional.
sed 's/pub import lib.collections/pub import lib.collections as c/' \
    "$CASES/fixtures/facade.kofun" >"$WORK/qualified-alias.kofun"
write_inventory "$WORK/qualified-alias.kofun" "$WORK/qualified-alias.inventory"
expect_failure E2S85 qualified-alias api.collections \
    "$WORK/qualified-alias.inventory"

sed 's/pub from lib.collections import Map, Set/pub from lib.collections import */' \
    "$CASES/fixtures/facade.kofun" >"$WORK/wildcard.kofun"
write_inventory "$WORK/wildcard.kofun" "$WORK/wildcard.inventory"
expect_failure E2S85 wildcard api.collections "$WORK/wildcard.inventory"

sed 's/pub from lib.collections import Map, Set/pub from lib.collections import Map as M/' \
    "$CASES/fixtures/facade.kofun" >"$WORK/per-name-alias.kofun"
write_inventory "$WORK/per-name-alias.kofun" "$WORK/per-name-alias.inventory"
expect_failure E2S85 per-name-alias api.collections \
    "$WORK/per-name-alias.inventory"

sed 's/Map, Set, Present/Map,,Set/' \
    "$CASES/fixtures/facade.kofun" >"$WORK/malformed-list.kofun"
write_inventory "$WORK/malformed-list.kofun" \
    "$WORK/malformed-list.inventory"
expect_failure E2S85 malformed-list api.collections \
    "$WORK/malformed-list.inventory"

sed 's/pub from lib.collections import Map, Set/pub from lib.collections import/' \
    "$CASES/fixtures/facade.kofun" >"$WORK/empty.kofun"
write_inventory "$WORK/empty.kofun" "$WORK/empty.inventory"
expect_failure E2S85 empty api.collections "$WORK/empty.inventory"

sed 's/pub from lib.collections import Map, Set/pub from ext:collections import Map/' \
    "$CASES/fixtures/facade.kofun" >"$WORK/external.kofun"
write_inventory "$WORK/external.kofun" "$WORK/external.inventory"
expect_failure E2S85 external api.collections "$WORK/external.inventory"

sed 's/pub import lib.collections/internal import lib.collections/' \
    "$CASES/fixtures/facade.kofun" >"$WORK/internal-form.kofun"
write_inventory "$WORK/internal-form.kofun" "$WORK/internal-form.inventory"
expect_failure E2S85 internal-form api.collections \
    "$WORK/internal-form.inventory"

sed 's/pub import lib.collections/private import lib.collections/' \
    "$CASES/fixtures/facade.kofun" >"$WORK/private-form.kofun"
write_inventory "$WORK/private-form.kofun" "$WORK/private-form.inventory"
expect_failure E2S85 private-form api.collections \
    "$WORK/private-form.inventory"

sed 's/pub import lib.collections/export import lib.collections/' \
    "$CASES/fixtures/facade.kofun" >"$WORK/export-form.kofun"
write_inventory "$WORK/export-form.kofun" "$WORK/export-form.inventory"
expect_failure E2S85 export-form api.collections \
    "$WORK/export-form.inventory"

{
    printf '%s\n' 'module api.collections'
    printf '%s\n' 'fn local() -> Int {' '    return 0' '}'
    printf '%s\n' 'pub from lib.collections import Map'
} >"$WORK/after-declaration.kofun"
write_inventory "$WORK/after-declaration.kofun" \
    "$WORK/after-declaration.inventory"
expect_failure E2S85 after-declaration api.collections \
    "$WORK/after-declaration.inventory"

sed 's/lib.collections import Map, Set/missing.collections import Map/' \
    "$CASES/fixtures/facade.kofun" >"$WORK/missing.kofun"
write_inventory "$WORK/missing.kofun" "$WORK/missing.inventory"
expect_failure E2S86 missing api.collections "$WORK/missing.inventory"

sed 's/Map, Set/hidden/' "$CASES/fixtures/facade.kofun" \
    >"$WORK/private-target.kofun"
write_inventory "$WORK/private-target.kofun" "$WORK/private-target.inventory"
expect_failure E2S87 private-target api.collections \
    "$WORK/private-target.inventory"
grep -F 'requested=pub effective=private' "$WORK/private-target.log" >/dev/null

sed 's/Map, Set/Concealed/' "$CASES/fixtures/facade.kofun" \
    >"$WORK/private-enclosing-type.kofun"
write_inventory "$WORK/private-enclosing-type.kofun" \
    "$WORK/private-enclosing-type.inventory"
expect_failure E2S87 private-enclosing-type api.collections \
    "$WORK/private-enclosing-type.inventory"

sed 's/Map, Set/Leaky/' "$CASES/fixtures/facade.kofun" \
    >"$WORK/hidden-signature.kofun"
write_inventory "$WORK/hidden-signature.kofun" \
    "$WORK/hidden-signature.inventory"
expect_failure E2S87 hidden-signature api.collections \
    "$WORK/hidden-signature.inventory"

{
    printf '%s\n' 'module api.collections'
    printf '%s\n' 'pub from lib.collections import Map'
    printf '%s\n' 'pub from lib.collections import Map'
} >"$WORK/duplicate.kofun"
write_inventory "$WORK/duplicate.kofun" "$WORK/duplicate.inventory"
expect_failure E2S88 duplicate api.collections "$WORK/duplicate.inventory"
grep -F 'binding spans=' "$WORK/duplicate.log" >/dev/null

{
    printf '%s\n' 'module api.collections'
    printf '%s\n' 'pub from lib.collections import Map'
    printf '%s\n' 'pub fn Map(value: Int) -> Int {' '    return value' '}'
} >"$WORK/local-collision.kofun"
write_inventory "$WORK/local-collision.kofun" \
    "$WORK/local-collision.inventory"
expect_failure E2S88 local-collision api.collections \
    "$WORK/local-collision.inventory"

printf '%s\n' 'module other.collections' \
    'pub fn Other(value: Int) -> Int {' '    return value' '}' \
    >"$WORK/other-collections.kofun"
printf '%s\n' 'module api.collections' \
    'import other.collections' \
    'pub import lib.collections' >"$WORK/import-collision.kofun"
{
    printf '%s|%s|%s|lib.collections|lib/collections.kofun|%s\n' \
        "$PACKAGE_ID" "$COLLECTIONS_MODULE" "$COLLECTIONS_FILE" \
        "$CASES/fixtures/collections.kofun"
    printf '%s|%s|%s|other.collections|other/collections.kofun|%s\n' \
        "$PACKAGE_ID" 5757575757575757575757575757575757575757575757575757575757575757 \
        5858585858585858585858585858585858585858585858585858585858585858 \
        "$WORK/other-collections.kofun"
    printf '%s|%s|%s|api.collections|api/collections.kofun|%s\n' \
        "$PACKAGE_ID" "$FACADE_MODULE" "$FACADE_FILE" \
        "$WORK/import-collision.kofun"
} >"$WORK/import-collision.inventory"
expect_failure E2S88 import-collision api.collections \
    "$WORK/import-collision.inventory"

# Self, two-node, three-node, and competing cycles are rejected. Reversing the
# inventory preserves the canonical shortest diagnostic.
printf '%s\n' 'module cycle.self' \
    'pub from cycle.self import Loop' >"$WORK/self.kofun"
printf '%s|%s|%s|cycle.self|cycle/self.kofun|%s\n' \
    "$PACKAGE_ID" 6161616161616161616161616161616161616161616161616161616161616161 \
    6262626262626262626262626262626262626262626262626262626262626262 \
    "$WORK/self.kofun" >"$WORK/self.inventory"
expect_failure E2S89 self-cycle cycle.self "$WORK/self.inventory"

printf '%s\n' 'module cycle.a' \
    'pub from cycle.b import Loop' >"$WORK/cycle-a.kofun"
printf '%s\n' 'module cycle.b' \
    'pub from cycle.a import Loop' >"$WORK/cycle-b.kofun"
{
    printf '%s|%s|%s|cycle.a|cycle/a.kofun|%s\n' \
        "$PACKAGE_ID" 6363636363636363636363636363636363636363636363636363636363636363 \
        6464646464646464646464646464646464646464646464646464646464646464 \
        "$WORK/cycle-a.kofun"
    printf '%s|%s|%s|cycle.b|cycle/b.kofun|%s\n' \
        "$PACKAGE_ID" 6565656565656565656565656565656565656565656565656565656565656565 \
        6666666666666666666666666666666666666666666666666666666666666666 \
        "$WORK/cycle-b.kofun"
} >"$WORK/two-cycle.inventory"
expect_failure E2S89 two-cycle cycle.a "$WORK/two-cycle.inventory"
grep -F 'canonical re-export cycle:' "$WORK/two-cycle.log" >/dev/null
sed '1!G;h;$!d' "$WORK/two-cycle.inventory" \
    >"$WORK/two-cycle-reversed.inventory"
expect_failure E2S89 two-cycle-reversed cycle.a \
    "$WORK/two-cycle-reversed.inventory"
cmp "$WORK/two-cycle.log" "$WORK/two-cycle-reversed.log"

printf '%s\n' 'module cycle.c' \
    'pub from cycle.a import Loop' >"$WORK/cycle-c.kofun"
sed 's/from cycle.a/from cycle.c/' "$WORK/cycle-b.kofun" \
    >"$WORK/cycle-b-three.kofun"
{
    sed -n '1p' "$WORK/two-cycle.inventory"
    printf '%s|%s|%s|cycle.b|cycle/b.kofun|%s\n' \
        "$PACKAGE_ID" 6565656565656565656565656565656565656565656565656565656565656565 \
        6666666666666666666666666666666666666666666666666666666666666666 \
        "$WORK/cycle-b-three.kofun"
    printf '%s|%s|%s|cycle.c|cycle/c.kofun|%s\n' \
        "$PACKAGE_ID" 6767676767676767676767676767676767676767676767676767676767676767 \
        6868686868686868686868686868686868686868686868686868686868686868 \
        "$WORK/cycle-c.kofun"
} >"$WORK/three-cycle.inventory"
expect_failure E2S89 three-cycle cycle.a "$WORK/three-cycle.inventory"

printf '%s\n' 'module cycle.x' \
    'pub from cycle.y import Loop' >"$WORK/cycle-x.kofun"
printf '%s\n' 'module cycle.y' \
    'pub from cycle.z import Loop' >"$WORK/cycle-y.kofun"
printf '%s\n' 'module cycle.z' \
    'pub from cycle.x import Loop' >"$WORK/cycle-z.kofun"
{
    cat "$WORK/two-cycle.inventory"
    printf '%s|%s|%s|cycle.x|cycle/x.kofun|%s\n' \
        "$PACKAGE_ID" 7171717171717171717171717171717171717171717171717171717171717171 \
        7272727272727272727272727272727272727272727272727272727272727272 \
        "$WORK/cycle-x.kofun"
    printf '%s|%s|%s|cycle.y|cycle/y.kofun|%s\n' \
        "$PACKAGE_ID" 7373737373737373737373737373737373737373737373737373737373737373 \
        7474747474747474747474747474747474747474747474747474747474747474 \
        "$WORK/cycle-y.kofun"
    printf '%s|%s|%s|cycle.z|cycle/z.kofun|%s\n' \
        "$PACKAGE_ID" 7575757575757575757575757575757575757575757575757575757575757575 \
        7676767676767676767676767676767676767676767676767676767676767676 \
        "$WORK/cycle-z.kofun"
} >"$WORK/competing-cycle.inventory"
expect_failure E2S89 competing-cycle cycle.a \
    "$WORK/competing-cycle.inventory"
cmp "$WORK/two-cycle.log" "$WORK/competing-cycle.log"

# Exact 64-edge forwarding succeeds; one more edge fails transactionally.
: >"$WORK/chain.inventory"
printf '%s\n' 'module chain.base' \
    'pub fn Item(value: Int) -> Int {' '    return value' '}' \
    >"$WORK/chain-base.kofun"
printf '%s|%064d|%064d|chain.base|chain/base.kofun|%s\n' \
    "$PACKAGE_ID" 7001 8001 "$WORK/chain-base.kofun" \
    >>"$WORK/chain.inventory"
chain_index=0
while test "$chain_index" -lt 65; do
    current=$(printf 'chain.e%02d' "$chain_index")
    current_path=$(printf 'chain/e%02d.kofun' "$chain_index")
    current_source=$(printf '%s/chain-e%02d.kofun' "$WORK" "$chain_index")
    if test "$chain_index" -eq 0; then
        previous=chain.base
    else
        previous=$(printf 'chain.e%02d' $((chain_index - 1)))
    fi
    printf '%s\n' "module $current" \
        "pub from $previous import Item" >"$current_source"
    module_id=$(printf '%064d' $((7100 + chain_index)))
    file_id=$(printf '%064d' $((8100 + chain_index)))
    printf '%s|%s|%s|%s|%s|%s\n' \
        "$PACKAGE_ID" "$module_id" "$file_id" "$current" \
        "$current_path" "$current_source" >>"$WORK/chain.inventory"
    chain_index=$((chain_index + 1))
done
head -n 65 "$WORK/chain.inventory" >"$WORK/chain64.inventory"
"$TOOL" "$WORK/chain64.inventory" chain.e63 \
    "$WORK/chain64.hir" "$WORK/chain64.kif" "$WORK/chain64.tooling"
grep -F 'doc|facade=chain.e63.Item|canonical=chain.base.Item|' \
    "$WORK/chain64.tooling" | grep -F '|chain=64|' >/dev/null
expect_failure E2S90 chain65 chain.e64 "$WORK/chain.inventory"

# The expanded-binding exact boundary succeeds; one-over is rejected.
printf '%s\n' 'module big.target' >"$WORK/big-target.kofun"
big_index=0
while test "$big_index" -lt 513; do
    big_name=$(printf 'N%03d' "$big_index")
    printf '%s\n' \
        "pub fn $big_name(value: Int) -> Int {" \
        '    return value' \
        '}' \
        "pub type $big_name =" \
        "    | ${big_name}None" \
        "    | ${big_name}Some(value: Int)" \
        >>"$WORK/big-target.kofun"
    big_index=$((big_index + 1))
done
printf '%s\n' 'module big.facade' >"$WORK/big-facade.kofun"
for group in 0 1; do
    printf '%s' 'pub from big.target import ' >>"$WORK/big-facade.kofun"
    item=0
    while test "$item" -lt 256; do
        value=$((group * 256 + item))
        name=$(printf 'N%03d' "$value")
        if test "$item" -ne 0; then printf '%s' ', ' >>"$WORK/big-facade.kofun"; fi
        printf '%s' "$name" >>"$WORK/big-facade.kofun"
        item=$((item + 1))
    done
    printf '\n' >>"$WORK/big-facade.kofun"
done
{
    printf '%s|%064d|%064d|big.target|big/target.kofun|%s\n' \
        "$PACKAGE_ID" 9101 9201 "$WORK/big-target.kofun"
    printf '%s|%064d|%064d|big.facade|big/facade.kofun|%s\n' \
        "$PACKAGE_ID" 9102 9202 "$WORK/big-facade.kofun"
} >"$WORK/big.inventory"
"$TOOL" "$WORK/big.inventory" big.facade \
    "$WORK/big.hir" "$WORK/big.kif" "$WORK/big.tooling"
test "$(grep -c '^export|' "$WORK/big.hir")" -eq 1024
printf '%s\n' 'pub from big.target import N512' \
    >>"$WORK/big-facade.kofun"
expect_failure E2S90 expanded-over big.facade "$WORK/big.inventory"

# Exactly 256 source re-export declarations succeed; the 257th is rejected.
printf '%s\n' 'module declarations.facade' \
    >"$WORK/declarations-facade.kofun"
declaration_index=0
while test "$declaration_index" -lt 256; do
    declaration_name=$(printf 'N%03d' "$declaration_index")
    printf '%s\n' "pub from big.target import $declaration_name" \
        >>"$WORK/declarations-facade.kofun"
    declaration_index=$((declaration_index + 1))
done
{
    printf '%s|%064d|%064d|big.target|big/target.kofun|%s\n' \
        "$PACKAGE_ID" 9101 9201 "$WORK/big-target.kofun"
    printf '%s|%064d|%064d|declarations.facade|declarations/facade.kofun|%s\n' \
        "$PACKAGE_ID" 9301 9302 "$WORK/declarations-facade.kofun"
} >"$WORK/declarations.inventory"
"$TOOL" "$WORK/declarations.inventory" declarations.facade \
    "$WORK/declarations.hir" "$WORK/declarations.kif" \
    "$WORK/declarations.tooling"
test "$(grep -c '^export|' "$WORK/declarations.hir")" -eq 512
printf '%s\n' 'pub from big.target import N256' \
    >>"$WORK/declarations-facade.kofun"
expect_failure E2S90 declarations-over declarations.facade \
    "$WORK/declarations.inventory"

# Defensive KIF consumption and output/internal failures have stable focused
# categories and never publish a partial success artifact.
cp "$WORK/positive.kif" "$WORK/corrupt.kif"
printf '\001' | dd of="$WORK/corrupt.kif" bs=1 seek=0 \
    conv=notrunc status=none
set +e
"$TOOL" --resolve-kif "$WORK/corrupt.kif" Map value \
    "$WORK/corrupt-consumer.hir" >"$WORK/corrupt-kif.log" 2>&1
corrupt_status=$?
set -e
test "$corrupt_status" -eq 1
grep -F 'error[E2S91]:' "$WORK/corrupt-kif.log" >/dev/null
test ! -e "$WORK/corrupt-consumer.hir"

set +e
"$TOOL" --resolve-kif "$WORK/positive.kif" Missing value \
    "$WORK/missing-export.hir" >"$WORK/missing-export.log" 2>&1
missing_export_status=$?
set -e
test "$missing_export_status" -eq 1
grep -F 'error[E2S93]:' "$WORK/missing-export.log" >/dev/null
test ! -e "$WORK/missing-export.hir"

set +e
"$TOOL" "$WORK/positive.inventory" api.collections \
    "$WORK/absent/out.hir" "$WORK/io.kif" "$WORK/io.tooling" \
    >"$WORK/io.log" 2>&1
io_status=$?
set -e
test "$io_status" -eq 1
grep -F 'error[E2S92]:' "$WORK/io.log" >/dev/null
test ! -e "$WORK/absent/out.hir"
test ! -e "$WORK/io.kif"
test ! -e "$WORK/io.tooling"

printf '%s\n' stale >"$WORK/internal.hir"
printf '%s\n' stale >"$WORK/internal.kif"
printf '%s\n' stale >"$WORK/internal.tooling"
set +e
KOFUN_DIAGNOSTIC_FAULT=re-export-chain \
    "$TOOL" "$WORK/positive.inventory" api.collections \
    "$WORK/internal.hir" "$WORK/internal.kif" "$WORK/internal.tooling" \
    >"$WORK/internal.log" 2>&1
internal_status=$?
set -e
test "$internal_status" -eq 1
grep -F 'error[E2S94]:' "$WORK/internal.log" >/dev/null
test ! -e "$WORK/internal.hir"
test ! -e "$WORK/internal.kif"
test ! -e "$WORK/internal.tooling"

# Existing prerequisite gates stay independently executable.
sh "$ROOT/tests/conformance/modules/imports-qualified/run.sh"
sh "$ROOT/tests/conformance/modules/imports-selective/run.sh"
sh "$ROOT/tests/conformance/modules/kif-v1/run.sh"
sh "$ROOT/spec/re-exports/check.sh"

if command -v clang >/dev/null 2>&1; then
    clang -std=c11 -O2 -Wall -Wextra -Werror -pedantic \
        -I"$ROOT/bootstrap/stage2" \
        "$ROOT/bootstrap/stage2/re_exports.c" \
        "$ROOT/bootstrap/stage2/kif_v1.c" \
        "$ROOT/bootstrap/stage2/visibility_access.c" \
        "$ROOT/bootstrap/stage2/sha256.c" \
        -o "$WORK/re-exports-clang"
    "$WORK/re-exports-clang" "$WORK/positive.inventory" api.collections \
        "$WORK/clang.hir" "$WORK/clang.kif" "$WORK/clang.tooling"
    cmp "$WORK/positive.kif" "$WORK/clang.kif"
fi

"$CC" -std=c11 -O1 -g -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$ROOT/bootstrap/stage2" \
    "$ROOT/bootstrap/stage2/re_exports.c" \
    "$ROOT/bootstrap/stage2/kif_v1.c" \
    "$ROOT/bootstrap/stage2/visibility_access.c" \
    "$ROOT/bootstrap/stage2/sha256.c" \
    -o "$WORK/re-exports-sanitized"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
    "$WORK/re-exports-sanitized" "$WORK/positive.inventory" api.collections \
    "$WORK/sanitized.hir" "$WORK/sanitized.kif" "$WORK/sanitized.tooling"
cmp "$WORK/positive.kif" "$WORK/sanitized.kif"

if "$CC" -std=c11 -O0 -Wall -Wextra -Werror -pedantic -fanalyzer \
    -I"$ROOT/bootstrap/stage2" \
    "$ROOT/bootstrap/stage2/re_exports.c" \
    "$ROOT/bootstrap/stage2/kif_v1.c" \
    "$ROOT/bootstrap/stage2/visibility_access.c" \
    "$ROOT/bootstrap/stage2/sha256.c" \
    -o "$WORK/re-exports-analyzed" >/dev/null 2>&1
then
    printf '%s\n' 'PASS: GCC analyzer accepts the re-export resolver and KIF projection'
fi

printf '%s\n' \
    'PASS: explicit public re-exports preserve target identities and non-widening visibility' \
    'PASS: canonical chains/cycles and 64/65 boundaries are executable' \
    'PASS: public export KIF facts and source-free facade consumption are transactional' \
    'PASS: facade/canonical tooling paths do not claim linker, FFI, or runtime forwarding'
