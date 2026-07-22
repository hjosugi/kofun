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

cp "$WORK/interface.kif" "$WORK/corrupt.kif"
printf '\001' | dd of="$WORK/corrupt.kif" bs=1 seek=0 conv=notrunc status=none
printf '%s\n' stale >"$WORK/corrupt.hir"
if "$TOOL" resolve "$WORK/corrupt.kif" "$PACKAGE_ID" demo.api \
    "$WORK/consumer.kofun" "$WORK/corrupt.hir" >"$WORK/corrupt.log" 2>&1
then
    fail 'corrupt dependency KIF resolved'
fi
grep -F 'error[KIF-corrupt]:' "$WORK/corrupt.log" >/dev/null
test ! -e "$WORK/corrupt.hir" || fail 'corrupt KIF published partial HIR'

printf '%s\n' stale >"$WORK/corrupt.json"
if "$TOOL" read "$WORK/corrupt.kif" "$WORK/corrupt.json" >"$WORK/read-corrupt.log" 2>&1
then
    fail 'corrupt KIF read succeeded'
fi
test ! -e "$WORK/corrupt.json" || fail 'failed reader left a diagnostic success artifact'

"$WORK/codec-test" "$WORK/interface.kif" "$WORK"

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
    'PASS: defensive reader rejects corruption before publication' \
    'PASS: qualified consumer resolves from KIF with dependency source absent'
