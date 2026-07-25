#!/usr/bin/env sh

set -eu

LC_ALL=C
export LC_ALL

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)
CASES="$ROOT/tests/conformance/modules/kif-v1"
CC=${CC:-cc}
WORK=${KOFUN_KIF_V1_WORK:-"$ROOT/build/kif-v1"}
TOOL="$WORK/kofun-kif-v1"
PACKAGE_ID=1111111111111111111111111111111111111111111111111111111111111111
EXTERNAL_PACKAGE=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
MODULE_ID=2222222222222222222222222222222222222222222222222222222222222222
FILE_ID=3333333333333333333333333333333333333333333333333333333333333333
FACADE_MODULE=4444444444444444444444444444444444444444444444444444444444444444
SECOND_EXPORT_EDGE=6666666666666666666666666666666666666666666666666666666666666666

fail() {
    printf '%s\n' "FAIL: $*" >&2
    exit 1
}

case $WORK in
    */kif-v1|*/kif-v1.*) ;;
    *) fail "work directory must end in kif-v1[.suffix]: $WORK" ;;
esac
command -v "$CC" >/dev/null 2>&1 || fail 'a C11 compiler is required'
rm -rf "$WORK"
mkdir -p "$WORK"

compile_tool() {
    compiler=$1
    output=$2
    shift 2
    "$compiler" -std=c11 -O2 -Wall -Wextra -Werror -pedantic \
        -DKOFUN_TEST_DIAGNOSTIC_FAULTS \
        -I"$ROOT/bootstrap/stage2" "$@" \
        "$ROOT/bootstrap/stage2/kif_v1_tool.c" \
        "$ROOT/bootstrap/stage2/kif_v1.c" \
        "$ROOT/bootstrap/stage2/sha256.c" \
        -o "$output"
}

compile_tool "$CC" "$TOOL"
"$CC" -std=c11 -O2 -Wall -Wextra -Werror -pedantic \
    -I"$ROOT/bootstrap/stage2" \
    "$CASES/codec_test.c" \
    "$ROOT/bootstrap/stage2/kif_v1.c" \
    "$ROOT/bootstrap/stage2/sha256.c" \
    -o "$WORK/codec-test"

write_inventory() {
    logical_path=$1
    source_path=$2
    output=$3
    printf '%s|%s|%s|%s|%s\n' \
        "$PACKAGE_ID" "$MODULE_ID" "$FILE_ID" "$logical_path" "$source_path" \
        >"$output"
}

write_inventory demo/api.kofun "$CASES/fixtures/interface.kofun" "$WORK/interface.inventory"
"$TOOL" write "$WORK/interface.inventory" "$MODULE_ID" edition-1 \
    "$WORK/interface.kif" "$WORK/interface.json"
"$TOOL" write "$WORK/interface.inventory" "$MODULE_ID" edition-1 \
    "$WORK/repeated.kif" "$WORK/repeated.json"
cmp "$WORK/interface.kif" "$WORK/repeated.kif" || fail 'repeated writer bytes differ'
cmp "$WORK/interface.json" "$WORK/repeated.json" || fail 'repeated dump differs'

write_inventory remapped/location.kofun "$CASES/fixtures/interface.kofun" \
    "$WORK/remapped.inventory"
"$TOOL" write "$WORK/remapped.inventory" "$MODULE_ID" edition-1 \
    "$WORK/remapped.kif" "$WORK/remapped.json"
cmp "$WORK/interface.kif" "$WORK/remapped.kif" || fail 'logical path remap changed KIF bytes'

write_inventory demo/api.kofun "$CASES/fixtures/interface_reordered.kofun" \
    "$WORK/reordered.inventory"
"$TOOL" write "$WORK/reordered.inventory" "$MODULE_ID" edition-1 \
    "$WORK/reordered.kif" "$WORK/reordered.json"
cmp "$WORK/interface.kif" "$WORK/reordered.kif" || fail 'declaration order changed KIF bytes'

"$TOOL" read "$WORK/interface.kif" "$WORK/readback.json"
cmp "$WORK/interface.json" "$WORK/readback.json" || fail 'writer/readback facts differ'
grep -F '"authoritative": false' "$WORK/interface.json" >/dev/null
grep -F '"name": "exported", "visibility": "pub", "parameter_count": 1' \
    "$WORK/interface.json" >/dev/null
grep -F '"name": "sibling", "visibility": "internal", "parameter_count": 1' \
    "$WORK/interface.json" >/dev/null
grep -F '"name": "Some", "visibility": "pub", "payload_count": 1' \
    "$WORK/interface.json" >/dev/null
grep -F '"name": "Right", "visibility": "internal", "payload_count": 1' \
    "$WORK/interface.json" >/dev/null
if grep -Eq '"name": "(hidden|implicit_private|HiddenChoice|Invisible|Secret)"' \
    "$WORK/interface.json"
then
    fail 'private fact leaked into KIF'
fi
public_digest=$(sed -n 's/.*"public_semantic_digest": "\([0-9a-f]*\)".*/\1/p' \
    "$WORK/interface.json")
internal_digest=$(sed -n 's/.*"package_internal_semantic_digest": "\([0-9a-f]*\)".*/\1/p' \
    "$WORK/interface.json")
test "${#public_digest}" -eq 64 || fail 'public digest is not 32 bytes'
test "${#internal_digest}" -eq 64 || fail 'internal digest is not 32 bytes'
test "$public_digest" != "$internal_digest" || fail 'public/internal digests unexpectedly match'

"$WORK/codec-test" "$WORK/interface.kif" "$WORK"
"$TOOL" read "$WORK/export-interface.kif" "$WORK/export-interface.json"
grep -F '"target_kind": "module"' "$WORK/export-interface.json" |
    grep -F '"target_module_path": "demo.api"' >/dev/null

# The dependency source is deliberately absent from the consumer invocation.
cp "$CASES/fixtures/consumer.kofun" "$WORK/consumer.kofun"
test ! -e "$WORK/dependency-source.kofun"
"$TOOL" resolve "$WORK/interface.kif" "$PACKAGE_ID" demo.api \
    "$WORK/consumer.kofun" "$WORK/source-free.hir"
grep -Fx 'kofun-imports-qualified/v1' "$WORK/source-free.hir" >/dev/null
grep -F '|path=demo.api|module=2222222222222222222222222222222222222222222222222222222222222222|view=package-internal|' \
    "$WORK/source-free.hir" >/dev/null
grep -F '|qualifier=api|name=exported|' "$WORK/source-free.hir" >/dev/null
grep -F '|arity=1|signature=fn(1:Int)->Int|' "$WORK/source-free.hir" >/dev/null

"$TOOL" resolve "$WORK/interface.kif" "$PACKAGE_ID" demo.api \
    "$CASES/fixtures/consumer_internal.kofun" "$WORK/internal.hir"
grep -F '|qualifier=api|name=sibling|' "$WORK/internal.hir" >/dev/null
grep -F '|view=package-internal|' "$WORK/internal.hir" >/dev/null

if "$TOOL" resolve "$WORK/interface.kif" "$EXTERNAL_PACKAGE" demo.api \
    "$CASES/fixtures/consumer_internal.kofun" "$WORK/external-internal.hir" \
    >"$WORK/external-internal.log" 2>&1
then
    fail 'external package consumed an internal KIF fact'
fi
grep -F 'error[E2S65]:' "$WORK/external-internal.log" >/dev/null
test ! -e "$WORK/external-internal.hir" || fail 'rejected resolver published HIR'

# A normal qualified import consumes a public facade edge, but keeps the
# ExportBindingId separate from the original function target and ordered chain.
export_line=$(
    grep -F '"kind": "export", "name": "exported"' \
        "$WORK/export-interface.json"
)
export_binding=$(
    printf '%s\n' "$export_line" |
        sed -n 's/.*"symbol_id": "\([0-9a-f]*\)".*/\1/p'
)
export_target=$(
    printf '%s\n' "$export_line" |
        sed -n 's/.*"target_symbol_id": "\([0-9a-f]*\)".*/\1/p'
)
test "${#export_binding}" -eq 64
test "${#export_target}" -eq 64
test ! -e "$WORK/facade-source.kofun"
"$TOOL" resolve "$WORK/export-interface.kif" "$PACKAGE_ID" facade.api \
    "$CASES/fixtures/consumer_export.kofun" "$WORK/export-same-package.hir"
"$TOOL" resolve "$WORK/export-interface.kif" "$EXTERNAL_PACKAGE" facade.api \
    "$CASES/fixtures/consumer_export.kofun" "$WORK/export-external.hir"
grep -F "|path=facade.api|module=$FACADE_MODULE|view=package-internal|" \
    "$WORK/export-same-package.hir" >/dev/null
grep -F "|path=facade.api|module=$FACADE_MODULE|view=public|" \
    "$WORK/export-external.hir" >/dev/null
for resolved in "$WORK/export-same-package.hir" "$WORK/export-external.hir"
do
    grep -F "|binding-module=$FACADE_MODULE|export-binding=$export_binding|" \
        "$resolved" |
        grep -F "|target-module=$MODULE_ID|target-symbol=$export_target|" |
        grep -F "|chain=2|chain-ids=$export_binding,$SECOND_EXPORT_EDGE" \
        >/dev/null
done

if "$TOOL" resolve "$WORK/export-interface.kif" "$PACKAGE_ID" facade.api \
    "$CASES/fixtures/consumer_export_wrong_arity.kofun" \
    "$WORK/export-wrong-arity.hir" >"$WORK/export-wrong-arity.log" 2>&1
then
    fail 'facade export accepted the wrong function arity'
fi
grep -F 'error[E2S65]:' "$WORK/export-wrong-arity.log" >/dev/null
test ! -e "$WORK/export-wrong-arity.hir"

printf '%s\n' prior-resolution >"$WORK/preserved-resolution.hir"
cp "$WORK/preserved-resolution.hir" "$WORK/preserved-resolution.expected"
if KOFUN_KIF_RESOLVE_FAULT=before-rename \
    "$TOOL" resolve "$WORK/export-interface.kif" "$PACKAGE_ID" facade.api \
        "$CASES/fixtures/consumer_export.kofun" \
        "$WORK/preserved-resolution.hir" \
        >"$WORK/interrupted-resolution.log" 2>&1
then
    fail 'injected pre-rename interruption committed KIF resolution'
fi
grep -F 'error[E2S68]:' "$WORK/interrupted-resolution.log" >/dev/null
cmp "$WORK/preserved-resolution.expected" "$WORK/preserved-resolution.hir"
if find "$WORK" -maxdepth 1 -name \
    'preserved-resolution.hir.kif-resolve-tmp.*' | grep . >/dev/null
then
    fail 'interrupted KIF resolution left a temporary artifact'
fi

if "$TOOL" resolve "$WORK/export-interface.kif" "$PACKAGE_ID" facade.api \
    "$CASES/fixtures/consumer_module_export_call.kofun" \
    "$WORK/module-export-call.hir" >"$WORK/module-export-call.log" 2>&1
then
    fail 'module export was accepted as a function'
fi
grep -F 'error[E2S65]:' "$WORK/module-export-call.log" >/dev/null
test ! -e "$WORK/module-export-call.hir"

# Resolver output paths are rejected before an exact or hardlink alias can
# truncate either compiled-interface bytes or consumer source.
cp "$WORK/export-interface.kif" "$WORK/alias-input.kif"
cp "$WORK/alias-input.kif" "$WORK/alias-input.expected"
if "$TOOL" resolve "$WORK/alias-input.kif" "$PACKAGE_ID" facade.api \
    "$CASES/fixtures/consumer_export.kofun" "$WORK/alias-input.kif" \
    >"$WORK/alias-input.log" 2>&1
then
    fail 'KIF resolver accepted its input path as output'
fi
grep -F 'error[E2S68]:' "$WORK/alias-input.log" >/dev/null
cmp "$WORK/alias-input.expected" "$WORK/alias-input.kif"

cp "$WORK/export-interface.kif" "$WORK/hardlink-input.kif"
ln "$WORK/hardlink-input.kif" "$WORK/hardlink-output.hir"
if "$TOOL" resolve "$WORK/hardlink-input.kif" "$PACKAGE_ID" facade.api \
    "$CASES/fixtures/consumer_export.kofun" "$WORK/hardlink-output.hir" \
    >"$WORK/hardlink-input.log" 2>&1
then
    fail 'KIF resolver accepted a hardlinked input/output pair'
fi
grep -F 'error[E2S68]:' "$WORK/hardlink-input.log" >/dev/null
cmp "$WORK/hardlink-input.kif" "$WORK/hardlink-output.hir"

cp "$CASES/fixtures/consumer_export.kofun" "$WORK/alias-consumer.kofun"
cp "$WORK/alias-consumer.kofun" "$WORK/alias-consumer.expected"
if "$TOOL" resolve "$WORK/export-interface.kif" "$PACKAGE_ID" facade.api \
    "$WORK/alias-consumer.kofun" "$WORK/alias-consumer.kofun" \
    >"$WORK/alias-consumer.log" 2>&1
then
    fail 'KIF resolver accepted its consumer source path as output'
fi
grep -F 'error[E2S68]:' "$WORK/alias-consumer.log" >/dev/null
cmp "$WORK/alias-consumer.expected" "$WORK/alias-consumer.kofun"

cp "$CASES/fixtures/consumer_export.kofun" "$WORK/hardlink-consumer.kofun"
ln "$WORK/hardlink-consumer.kofun" "$WORK/hardlink-consumer-output.hir"
if "$TOOL" resolve "$WORK/export-interface.kif" "$PACKAGE_ID" facade.api \
    "$WORK/hardlink-consumer.kofun" "$WORK/hardlink-consumer-output.hir" \
    >"$WORK/hardlink-consumer.log" 2>&1
then
    fail 'KIF resolver accepted a hardlinked source/output pair'
fi
grep -F 'error[E2S68]:' "$WORK/hardlink-consumer.log" >/dev/null
cmp "$WORK/hardlink-consumer.kofun" \
    "$WORK/hardlink-consumer-output.hir"

cp "$WORK/interface.kif" "$WORK/corrupt.kif"
printf '\001' | dd of="$WORK/corrupt.kif" bs=1 seek=0 conv=notrunc status=none
printf '%s\n' stale >"$WORK/corrupt.hir"
if "$TOOL" resolve "$WORK/corrupt.kif" "$PACKAGE_ID" demo.api \
    "$WORK/consumer.kofun" "$WORK/corrupt.hir" >"$WORK/corrupt.log" 2>&1
then
    fail 'corrupt dependency KIF resolved'
fi
grep -F 'error[KIF-corrupt]:' "$WORK/corrupt.log" >/dev/null
grep -Fx stale "$WORK/corrupt.hir" >/dev/null ||
    fail 'corrupt KIF replaced the prior atomic resolution'

printf '%s\n' stale >"$WORK/corrupt.json"
if "$TOOL" read "$WORK/corrupt.kif" "$WORK/corrupt.json" >"$WORK/read-corrupt.log" 2>&1
then
    fail 'corrupt KIF read succeeded'
fi
test ! -e "$WORK/corrupt.json" || fail 'failed reader left a diagnostic success artifact'

# Failed source projection preserves the previous atomic KIF replacement point.
cp "$WORK/interface.kif" "$WORK/preserved.kif"
sed 's/Right(value: Int)/Right(value: Bool)/' "$CASES/fixtures/interface.kofun" \
    >"$WORK/unsupported.kofun"
write_inventory demo/api.kofun "$WORK/unsupported.kofun" "$WORK/unsupported.inventory"
if "$TOOL" write "$WORK/unsupported.inventory" "$MODULE_ID" edition-1 \
    "$WORK/preserved.kif" >"$WORK/unsupported.log" 2>&1
then
    fail 'unsupported constructor payload was emitted'
fi
grep -F 'error[E2S50]:' "$WORK/unsupported.log" >/dev/null
cmp "$WORK/interface.kif" "$WORK/preserved.kif" || fail 'failed write replaced prior KIF'

if command -v clang >/dev/null 2>&1; then
    compile_tool clang "$WORK/kofun-kif-v1-clang"
    "$WORK/kofun-kif-v1-clang" read "$WORK/interface.kif" "$WORK/clang.json"
    cmp "$WORK/interface.json" "$WORK/clang.json" || fail 'Clang reader changed facts'
fi

"$CC" -std=c11 -O1 -g -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I"$ROOT/bootstrap/stage2" \
    "$CASES/codec_test.c" \
    "$ROOT/bootstrap/stage2/kif_v1.c" \
    "$ROOT/bootstrap/stage2/sha256.c" \
    -o "$WORK/codec-test-sanitized"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
    "$WORK/codec-test-sanitized" "$WORK/interface.kif" "$WORK"

if "$CC" -std=c11 -O0 -Wall -Wextra -Werror -pedantic -fanalyzer \
    -I"$ROOT/bootstrap/stage2" \
    "$ROOT/bootstrap/stage2/kif_v1_tool.c" \
    "$ROOT/bootstrap/stage2/kif_v1.c" \
    "$ROOT/bootstrap/stage2/sha256.c" \
    -o "$WORK/kofun-kif-v1-analyzed" >/dev/null 2>&1
then
    printf '%s\n' 'PASS: GCC analyzer accepts the KIF writer, reader, and adapter'
fi

printf '%s\n' \
    'PASS: canonical KIF bytes are declaration-order and path independent' \
    'PASS: public/internal facts and semantic digests obey visibility' \
    'PASS: defensive reader rejects corruption and cyclic export chains before publication' \
    'PASS: qualified consumer resolves direct and facade functions with dependency source absent' \
    'PASS: module self-symbol validation and atomic consumer output are canonical'
