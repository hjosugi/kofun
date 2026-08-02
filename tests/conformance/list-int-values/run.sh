#!/usr/bin/env sh
set -eu

# Executable bounded `List[Int]` values (#919). The contract this gate holds
# Stage 2 to:
#
#   - a `List[Int]` local binds, from an annotation and from inference alike,
#     and its `len` and its indexed elements are observed end to end;
#   - the bytes it is built in come from the AggregateLayout v1 descriptor,
#     recomputed here rather than read from a checked-in copy, so a
#     representation drift in either the contract or the backend fails;
#   - the capacity bound is explicit, reachable at its last element, and
#     refused one past it;
#   - every shape the slice does not lower is refused with an exact
#     diagnostic and leaves no backend artifact;
#   - the reference executor and the emitted C11 agree byte for byte, and
#     repeated and normalized-directory builds are byte-identical.
#
# `len` is observed by printing it and an index read by printing the element,
# because both have to come out of the emitted object rather than out of a
# constant the lowering could have folded from the source alone.

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CASES="$ROOT/tests/conformance/list-int-values"
LAYOUT="$ROOT/spec/aggregate-layout-v1"
CC=${CC:-cc}
ASSERT_CONTEXT='list int values'
. "$ROOT/tests/assertions/assert.sh"
. "$ROOT/bootstrap/stage2/build.sh"

WORK=$(mktemp -d "${TMPDIR:-/tmp}/kofun-list-int-values.XXXXXX")
trap 'rm -rf "$WORK"' 0 1 2 15

fail() {
    printf '%s\n' "FAIL: list int values: $*" >&2
    exit 1
}

command -v "$CC" >/dev/null 2>&1 || fail 'a C11 compiler is required'
command -v node >/dev/null 2>&1 ||
    fail 'node is required to recompute the AggregateLayout v1 descriptor'

mkdir -p "$WORK/remapped"
kofun_stage2_build "$ROOT" "$WORK/kofun-stage2"

# ------------------------------------------------- the normative descriptor
#
# Every quantity below is recomputed from `layout.mjs` and the accepted
# vectors, never read out of `examples/`. A change to a layout rule therefore
# reaches this gate as a mismatch instead of being absorbed by a stale file,
# and the emitted C is compared against the contract rather than against
# itself.

for target in x86_64-linux wasm32; do
    node "$LAYOUT/layout.mjs" describe \
        "$LAYOUT/targets/$target.json" "$LAYOUT/vectors/core.json" \
        >"$WORK/core.$target.json" ||
        fail "recomputing the $target descriptor failed"
done

list_descriptor() {
    node -e '
        const fs = require("fs");
        const document = JSON.parse(fs.readFileSync(process.argv[1], "utf8"));
        const layout = document.layouts.find((entry) =>
            entry.id === "List[Int]");
        if (layout === undefined) {
            process.stderr.write("no List[Int] layout\n");
            process.exit(1);
        }
        const object = document.objects.find((entry) =>
            entry.id === "list-int-three");
        if (object === undefined) {
            process.stderr.write("no list-int-three object\n");
            process.exit(1);
        }
        const header = object.header.find((entry) =>
            entry.name === "length");
        if (header === undefined) {
            process.stderr.write("no length header\n");
            process.exit(1);
        }
        process.stdout.write([
            layout.kind,
            layout.size,
            layout.align,
            layout.pointers.join(","),
            header.offset,
            header.size,
            object.payload_offset,
            object.element_size,
            object.element_align,
            object.length,
            object.size
        ].join(" ") + "\n");
    ' "$1"
}

descriptor=$(list_descriptor "$WORK/core.x86_64-linux.json") ||
    fail 'the recomputed x86_64-linux descriptor has no usable List[Int]'

# shellcheck disable=SC2086
set -- $descriptor
kind=$1
size=$2
align=$3
pointers=$4
header_offset=$5
header_size=$6
payload_offset=$7
element_size=$8
element_align=$9
shift 9
object_length=$1
object_size=$2

assert_eq 'List[Int] descriptor kind' "$kind" list
assert_eq 'the value is one pointer, at offset zero' "$pointers" 0
assert_eq 'AggregateLayout v1 puts the length header at offset zero' \
    "$header_offset" 0
assert_ne 'the header must occupy bytes of its own' \
    "$payload_offset" "$header_offset"
# The object vector's own arithmetic. If the contract ever stops meaning
# "header then a dense run of elements", the emitted object below stops being
# a lowering of it, and this is where that shows.
assert_num 'the object is its header plus a dense element run' \
    "$((payload_offset + object_length * element_size))" -eq "$object_size"
# The probe below rebuilds this vector element for element, so a change to its
# length has to be seen here rather than as a C initializer error.
assert_eq 'the list-int-three vector still holds three elements' \
    "$object_length" 3

# `List[Int]` holds a reference, so the two v1 targets legitimately size the
# pointer differently. Only the element geometry, which this backend lowers,
# has to agree.
wasm_descriptor=$(list_descriptor "$WORK/core.wasm32.json") ||
    fail 'the recomputed wasm32 descriptor has no usable List[Int]'
wasm_elements=$(printf '%s\n' "$wasm_descriptor" | cut -d' ' -f8,9)
assert_eq 'List[Int] elements agree on both v1 targets' \
    "$wasm_elements" "$element_size $element_align"

# --------------------------------------------------------------- positives

positives='locals inferred capacity'

compile_stage2() {
    stem=$1
    label=$2
    source=$3
    "$WORK/kofun-stage2" "$source" \
        "$WORK/$label.c" "$WORK/$label.ir" "$WORK/$label.tokens" \
        >"$WORK/$label.stdout" 2>"$WORK/$label.stderr" ||
        fail "$stem did not compile"
    assert_file_empty "$stem wrote internal stderr" "$WORK/$label.stderr"
}

for stem in $positives; do
    cp "$CASES/$stem.kofun" "$WORK/remapped/$stem.kofun"
    compile_stage2 "$stem" "$stem.first" "$CASES/$stem.kofun"
    compile_stage2 "$stem" "$stem.second" "$CASES/$stem.kofun"
    compile_stage2 "$stem" "$stem.remapped" "$WORK/remapped/$stem.kofun"
    cmp "$WORK/$stem.first.c" "$WORK/$stem.second.c" ||
        fail "two builds of $stem emitted different C"
    cmp "$WORK/$stem.first.c" "$WORK/$stem.remapped.c" ||
        fail "the emitted C of $stem depends on the host source path"

    "$CC" -std=c11 -O2 -Wall -Wextra -Werror -pedantic \
        "$WORK/$stem.first.c" -o "$WORK/$stem.bin" ||
        fail "the C emitted for $stem does not compile cleanly"
    "$WORK/$stem.bin" >"$WORK/$stem.observed" 2>"$WORK/$stem.runtime.stderr" ||
        fail "$stem exited non-zero"
    assert_file_empty "$stem wrote runtime stderr" "$WORK/$stem.runtime.stderr"
    cmp "$CASES/$stem.stdout" "$WORK/$stem.observed" ||
        fail "$stem observed output differs from its golden"
    "$WORK/$stem.bin" >"$WORK/$stem.repeated"
    cmp "$WORK/$stem.observed" "$WORK/$stem.repeated" ||
        fail "$stem is not reproducible across runs"
    printf '%s\n' "PASS list int values: $stem"
done

# ------------------------------------------------ the reference executor
#
# `bin/kofun run` is the other executor #919 requires agreement with. It is
# opt-out rather than opt-in: skipping it silently is how a backend-only
# claim gets made, so the skip has to be asked for by name.
if test "${KOFUN_SKIP_REFERENCE_EXECUTOR:-0}" = 1; then
    printf '%s\n' 'SKIP list int values: reference executor (requested)'
else
    for stem in $positives; do
        "$ROOT/bin/kofun" run "$CASES/$stem.kofun" \
            >"$WORK/$stem.reference" 2>"$WORK/$stem.reference.stderr" ||
            fail "$stem did not run on the reference executor: $(
                cat "$WORK/$stem.reference.stderr")"
        cmp "$CASES/$stem.stdout" "$WORK/$stem.reference" ||
            fail "$stem: reference executor and C11 backend disagree"
    done
    printf '%s\n' \
        'PASS list int values: the reference executor agrees byte for byte'
fi

# ------------------------------------------------- layout agreement in the C
#
# One emitted translation unit carries the representation; the rest reuse it.
# Every quantity is compared against the recomputed descriptor, so this fails
# on a drift in the backend and on a drift in the contract alike.

emitted="$WORK/locals.first.c"

require_c() {
    needle=$1
    label=$2
    assert_grep "$label" -Fq -- "$needle" "$emitted"
}

require_c \
    "_Static_assert(offsetof(KofunListIntObject, length) == $header_offset," \
    'the emitted length header offset disagrees with AggregateLayout v1'
require_c \
    "_Static_assert(sizeof(((KofunListIntObject *)0)->length) == $header_size," \
    'the emitted length header size disagrees with AggregateLayout v1'
require_c \
    "_Static_assert(offsetof(KofunListIntObject, elements) == $payload_offset," \
    'the emitted payload offset disagrees with AggregateLayout v1'
require_c \
    "_Static_assert(sizeof(((KofunListIntObject *)0)->elements[0]) == $element_size," \
    'the emitted element size disagrees with AggregateLayout v1'
require_c "_Static_assert(_Alignof(KofunListIntObject) == $element_align," \
    'the emitted element alignment disagrees with AggregateLayout v1'
require_c "_Static_assert(offsetof(KofunListInt, object) == $pointers," \
    'the emitted pointer offset disagrees with AggregateLayout v1'
require_c "_Static_assert(sizeof(KofunListInt) == $size," \
    'the emitted size disagrees with AggregateLayout v1'
require_c "_Static_assert(_Alignof(KofunListInt) == $align," \
    'the emitted alignment disagrees with AggregateLayout v1'

# The capacity bound is a quantity of the emitted type, not a comment.
require_c '#define KOFUN_LIST_INT_CAPACITY 64' \
    'the emitted representation has no explicit capacity bound'
require_c "_Static_assert(sizeof(KofunListIntObject) ==" \
    'the emitted object does not pin its own size'

# `len` reads the header and an index reads the payload. A `strlen` here would
# be the pointer read as text, which is what the Text overload does.
assert_grep 'len reads the object length header' \
    -Eq -- 'KOFUN_LIST_INT_LEN\(k_b[0-9]+\)' "$emitted"
assert_grep 'an index read reads the element payload' \
    -Eq -- 'KOFUN_LIST_INT_AT\(k_b[0-9]+, [0-9]+\)' "$emitted"
assert_not_grep 'a list length was lowered as a text length' \
    -Fq -- 'strlen(k_b' "$emitted"
# The length is stored beside the elements, not recovered from a sentinel.
assert_grep 'the object stores its length' \
    -Eq -- 'KofunListIntObject kofun_list_b[0-9]+ = \{UINT64_C\([0-9]+\), \{' \
    "$emitted"
assert_grep 'the value is the address of the object' \
    -Eq -- 'KofunListInt k_b[0-9]+ = \{&kofun_list_b[0-9]+\};' "$emitted"

# The geometry the backend stores is the descriptor's, measured at run time and
# not only in a `_Static_assert` the same file could have been wrong about.
cat >"$WORK/probe.c" <<PROBE
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
PROBE
awk '
    /^#define KOFUN_LIST_INT_CAPACITY/ { emit = 1 }
    emit {
        print
        if ($0 ~ /^#define KOFUN_LIST_INT_AT\(/) { exit }
    }
' "$emitted" >>"$WORK/probe.c"
cat >>"$WORK/probe.c" <<PROBE
int main(void) {
    KofunListIntObject object = {UINT64_C($object_length),
        {INT64_C(11), INT64_C(22), INT64_C(33)}};
    KofunListInt list = {&object};
    unsigned char bytes[sizeof object];
    uint64_t stored = 0;
    memcpy(bytes, &object, sizeof object);
    memcpy(&stored, bytes + offsetof(KofunListIntObject, length),
        sizeof stored);
    printf("%zu %zu %zu %zu %" PRIu64 " %" PRId64 " %" PRId64 "\n",
        sizeof(KofunListInt),
        offsetof(KofunListInt, object),
        offsetof(KofunListIntObject, elements),
        offsetof(KofunListIntObject, elements) +
            (size_t)$object_length * sizeof object.elements[0],
        stored,
        KOFUN_LIST_INT_LEN(list),
        KOFUN_LIST_INT_AT(list, 2));
    return 0;
}
PROBE
"$CC" -std=c11 -O2 -Wall -Wextra -Werror -pedantic \
    "$WORK/probe.c" -o "$WORK/probe" ||
    fail 'the emitted representation does not compile on its own'
observed_probe=$("$WORK/probe")
assert_eq 'the stored representation matches the recomputed descriptor' \
    "$observed_probe" \
    "$size $pointers $payload_offset $object_size $object_length $object_length 33"

# --------------------------------------------------------------- negatives
#
# Each refusal names the rule it pins and the code that pins it.
#
# `list_parameter` and `list_result` carry `E2S15` rather than `E2S153` on
# purpose: they are increment 3 of #868, and they already state their own
# boundary truthfully in the position they stand at. Pinning them here is
# what will notice when increment 3 moves them.

negatives='
oversized:E2S153
index_above_range:E2S153
index_below_range:E2S153
index_empty_list:E2S153
index_not_literal:E2S153
text_element:E2S153
text_annotation:E2S153
literal_argument:E2S153
mutable_binding:E2S153
whole_value_use:E2S153
list_parameter:E2S15
list_result:E2S15
'

previous_ifs=$IFS
IFS='
'
for entry in $negatives; do
    test -n "$entry" || continue
    stem=${entry%%:*}
    code=${entry#*:}
    set +e
    "$WORK/kofun-stage2" "$CASES/$stem.kofun" \
        "$WORK/$stem.c" "$WORK/$stem.ir" "$WORK/$stem.tokens" \
        >"$WORK/$stem.actual" 2>"$WORK/$stem.internal.stderr"
    status=$?
    set -e
    assert_num "$stem exit status" "$status" -eq 1
    assert_file_empty "$stem wrote internal stderr" \
        "$WORK/$stem.internal.stderr"
    assert_absent "$stem emitted a backend artifact" "$WORK/$stem.c"
    cmp "$CASES/$stem.stderr" "$WORK/$stem.actual" ||
        fail "$stem diagnostic differs from its golden"
    assert_grep "$stem names its stable code" -Fq -- "error[$code]:" \
        "$WORK/$stem.actual"
    printf '%s\n' "PASS list int refusal: $stem ($code)"
done
IFS=$previous_ifs

# ------------------------------------- the three rows #868 measured as wrong
#
# Each row is pinned by the fixture that spells it, so this notices if one
# starts reporting its old, untrue shape again. Two of them are no longer
# refusals at all; the third names the boundary it actually stands at.

assert_grep 'the annotated-local row lost its fixture' \
    -Fq -- 'let one: List[Int] = [7]' "$CASES/locals.kofun"
assert_not_grep 'the annotated local reports an unknown lexical binding' \
    -Fq -- 'E2S35' "$WORK/locals.first.stdout"
assert_grep 'the inferred-local row lost its fixture' \
    -Fq -- 'let values = [3, 5, 8]' "$CASES/inferred.kofun"
assert_not_grep 'the inferred local is counted as a Text' \
    -Fq -- 'TextOrList' "$WORK/inferred.first.stdout"
assert_grep 'the call row lost its fixture' \
    -Fq -- 'total([1, 2, 3])' "$CASES/literal_argument.kofun"
assert_not_grep 'the call row reports an arity the source never had' \
    -Fq -- 'got -1' "$CASES/literal_argument.stderr"

# The corpus is globbed rather than listed twice, so a fixture added without a
# gate entry stops the build (DD-022).
declared=0
for stem in $positives; do
    declared=$((declared + 1))
done
IFS='
'
for entry in $negatives; do
    test -n "$entry" || continue
    declared=$((declared + 1))
done
IFS=$previous_ifs
present_count=$(find "$CASES" -name '*.kofun' -type f | wc -l | tr -d ' ')
assert_num 'every corpus fixture is run by this gate' \
    "$present_count" -eq "$declared"

goldens=$(find "$CASES" \( -name '*.stdout' -o -name '*.stderr' \) -type f |
    wc -l | tr -d ' ')
assert_num 'every corpus fixture has exactly one golden' \
    "$goldens" -eq "$declared"

# ------------------------------------------------- the bound stays a bound
#
# The capacity is reachable at its last element and refused one past it, and
# the two fixtures that say so have to keep saying it.
assert_grep 'the capacity fixture stops naming the last element' \
    -Fq -- 'full[63]' "$CASES/capacity.kofun"
assert_grep 'the oversized fixture stops being oversized' \
    -Fq -- 'literal limit is 64 elements; this literal has 65' \
    "$CASES/oversized.stderr"

# No allocator was introduced: the object is storage the emitted function
# owns, and nothing frees it because nothing allocated it.
assert_not_grep 'Stage 2 began allocating list storage' \
    -Eq -- 'kofun_list_(alloc|free|grow|retain|release)' \
    "$ROOT/bootstrap/stage2/compiler.c"
assert_not_grep 'the emitted list translation unit allocates' \
    -Eq -- 'malloc|calloc|realloc' "$emitted"

# The registered identity has to stay registered.
assert_grep 'E2S153 is a registered diagnostic identity' \
    -Fq -- 'E2S153	list-int-values	frontend' \
    "$ROOT/tests/diagnostics/registry.tsv"
assert_grep 'E2S153 has an adapter report row' \
    -Fq -- 'E2S153	list-int-values' \
    "$ROOT/tests/diagnostics/reports/list-int-values.tsv"

printf '%s\n' \
    'PASS: bounded List[Int] locals bind from an annotation and from inference' \
    'PASS: len and checked index reads execute and observe real elements' \
    'PASS: the emitted representation is the recomputed AggregateLayout v1 one' \
    'PASS: the capacity bound is explicit, reachable, and refused one past' \
    'PASS: unsupported shapes stay exact refusals with no backend artifact'
