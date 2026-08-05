#!/usr/bin/env sh
set -eu

# Executable Optional(Int) (#924). The contract this gate holds Stage 2 to:
#
#   - present and absent `Optional(Int)` values are constructed, carried
#     across a same-typed argument and return, and observed end to end;
#   - the bytes they are constructed in come from the AggregateLayout v1
#     descriptor, recomputed here rather than read from a checked-in copy, so
#     a representation drift in either the contract or the backend fails;
#   - each of the four recognized narrowing shapes and the definitely
#     returning guard executes its narrowed use;
#   - every shape the slice does not lower is refused with an exact
#     diagnostic and leaves no backend artifact;
#   - no extraction, coalescing, or force-unwrap spelling exists.
#
# Presence is observed by printing the narrowed `Int`. There is deliberately
# no operator here that reads a payload without a tag test dominating it.

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CASES="$ROOT/tests/conformance/optional-construction"
LAYOUT="$ROOT/spec/aggregate-layout-v1"
CC=${CC:-cc}
ASSERT_CONTEXT='optional construction'
. "$ROOT/tests/assertions/assert.sh"
. "$ROOT/bootstrap/stage2/build.sh"

WORK=$(mktemp -d "${TMPDIR:-/tmp}/kofun-optional-construction.XXXXXX")
trap 'rm -rf "$WORK"' 0 1 2 15

fail() {
    printf '%s\n' "FAIL: optional construction: $*" >&2
    exit 1
}

command -v "$CC" >/dev/null 2>&1 || fail 'a C11 compiler is required'
command -v node >/dev/null 2>&1 ||
    fail 'node is required to recompute the AggregateLayout v1 descriptor'

mkdir -p "$WORK/remapped"
kofun_stage2_build "$ROOT" "$WORK/kofun-stage2"

# ------------------------------------------------- the normative descriptor
#
# The quantities below are recomputed from `layout.mjs` and the accepted
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

optional_descriptor() {
    node -e '
        const fs = require("fs");
        const document = JSON.parse(fs.readFileSync(process.argv[1], "utf8"));
        const layout = document.layouts.find((entry) =>
            entry.id === "Optional[Int]");
        if (layout === undefined) {
            process.stderr.write("no Optional[Int] layout\n");
            process.exit(1);
        }
        const tag = (name) => {
            const constructor = layout.constructors.find((entry) =>
                entry.name === name);
            if (constructor === undefined) {
                process.stderr.write(`no ${name} constructor\n`);
                process.exit(1);
            }
            return constructor.tag;
        };
        process.stdout.write([
            layout.kind,
            layout.size,
            layout.align,
            layout.tag_width,
            layout.tag_offset,
            layout.payload_offset,
            layout.payload_size,
            tag("None"),
            tag("Some")
        ].join(" ") + "\n");
    ' "$1"
}

descriptor=$(optional_descriptor "$WORK/core.x86_64-linux.json") ||
    fail 'the recomputed x86_64-linux descriptor has no usable Optional[Int]'
wasm_descriptor=$(optional_descriptor "$WORK/core.wasm32.json") ||
    fail 'the recomputed wasm32 descriptor has no usable Optional[Int]'

# `Optional[Int]` holds no reference, so both targets must agree on it. If they
# ever diverge, this backend's single lowering stops being a claim it can make.
assert_eq 'Optional[Int] agrees on both v1 targets' \
    "$wasm_descriptor" "$descriptor"

# shellcheck disable=SC2086
set -- $descriptor
kind=$1
size=$2
align=$3
tag_width=$4
tag_offset=$5
payload_offset=$6
payload_size=$7
none_tag=$8
some_tag=$9

assert_eq 'Optional[Int] descriptor kind' "$kind" optional
assert_eq 'AggregateLayout v1 puts the tag at offset zero' "$tag_offset" 0
assert_ne 'the tag must occupy bytes of its own, not a niche' \
    "$payload_offset" "$tag_offset"

# --------------------------------------------------------------- positives

positives='construction not_null_name_first not_null_null_first
is_null_name_first is_null_null_first guard'

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
    printf '%s\n' "PASS optional construction: $stem"
done

# ------------------------------------------------- layout agreement in the C
#
# One emitted translation unit carries the representation; the rest reuse it.
# Every quantity is compared against the recomputed descriptor, so this fails
# on a drift in the backend and on a drift in the contract alike.

emitted="$WORK/construction.first.c"

require_c() {
    needle=$1
    label=$2
    assert_grep "$label" -Fq -- "$needle" "$emitted"
}

require_c "_Static_assert(offsetof(KofunOptionalInt, tag) == $tag_offset," \
    'the emitted tag offset disagrees with AggregateLayout v1'
require_c \
    "_Static_assert(sizeof(((KofunOptionalInt *)0)->tag) == $tag_width," \
    'the emitted tag width disagrees with AggregateLayout v1'
require_c \
    "_Static_assert(offsetof(KofunOptionalInt, payload) == $payload_offset," \
    'the emitted payload offset disagrees with AggregateLayout v1'
require_c \
    "_Static_assert(sizeof(((KofunOptionalInt *)0)->payload) == $payload_size," \
    'the emitted payload size disagrees with AggregateLayout v1'
require_c "_Static_assert(sizeof(KofunOptionalInt) == $size," \
    'the emitted size disagrees with AggregateLayout v1'
require_c "_Static_assert(_Alignof(KofunOptionalInt) == $align," \
    'the emitted alignment disagrees with AggregateLayout v1'
require_c "#define KOFUN_OPTIONAL_INT_NONE_TAG UINT8_C($none_tag)" \
    'the absent tag disagrees with the None constructor'
require_c "#define KOFUN_OPTIONAL_INT_SOME_TAG UINT8_C($some_tag)" \
    'the present tag disagrees with the Some constructor'

# The tag is a stored byte, and both constructors are built from it. A niche
# would show up here as a payload written without a tag beside it.
assert_grep 'the absent value does not carry an explicit tag' \
    -Fq -- 'KofunOptionalInt k_b' "$emitted"
assert_grep 'absence is constructed from the None tag' \
    -Fq -- '= KOFUN_OPTIONAL_INT_NONE;' "$emitted"
assert_grep 'presence is constructed from the Some tag' \
    -Fq -- '= KOFUN_OPTIONAL_INT_SOME(' "$emitted"
assert_grep 'a narrowing condition is a tag test' \
    -Eq -- 'kofun_condition = \(k_b[0-9]+\.tag (!=|==) KOFUN_OPTIONAL_INT_NONE_TAG\)' \
    "$emitted"
assert_grep 'a narrowed use reads the payload' \
    -Eq -- 'k_b[0-9]+\.payload' "$emitted"

# A same-typed boundary carries the whole value, not a payload.
assert_grep 'an Int? parameter is lowered as the aggregate' \
    -Fq -- 'kofun_fn_passthrough(KofunOptionalInt ' "$emitted"
assert_grep 'an Int? result is lowered as the aggregate' \
    -Fq -- 'static KofunOptionalInt kofun_fn_passthrough(' "$emitted"

# The tag values the backend stores are the descriptor's, checked at run time
# and not only in a `_Static_assert` the same file could have been wrong about.
cat >"$WORK/probe.c" <<PROBE
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
PROBE
awk '
    /^typedef struct \{$/ { block = $0 "\n"; inside = 1; next }
    inside {
        block = block $0 "\n"
        if ($0 == "} KofunOptionalInt;") {
            printf "%s", block
            inside = 0
            emit = 1
        } else if ($0 ~ /^\}/) {
            inside = 0
        }
        next
    }
    emit {
        print
        if ($0 ~ /^#define KOFUN_OPTIONAL_INT_SOME\(/) { emit = 0 }
    }
' "$emitted" >>"$WORK/probe.c"
cat >>"$WORK/probe.c" <<'PROBE'
int main(void) {
    KofunOptionalInt absent = KOFUN_OPTIONAL_INT_NONE;
    KofunOptionalInt present = KOFUN_OPTIONAL_INT_SOME(INT64_C(7));
    unsigned char bytes[sizeof(KofunOptionalInt)];
    memcpy(bytes, &present, sizeof present);
    printf("%zu %zu %zu %u %u %u\n",
        sizeof(KofunOptionalInt),
        offsetof(KofunOptionalInt, tag),
        offsetof(KofunOptionalInt, payload),
        (unsigned)absent.tag,
        (unsigned)present.tag,
        (unsigned)bytes[offsetof(KofunOptionalInt, tag)]);
    return 0;
}
PROBE
"$CC" -std=c11 -O2 -Wall -Wextra -Werror -pedantic \
    "$WORK/probe.c" -o "$WORK/probe" ||
    fail 'the emitted representation does not compile on its own'
observed_probe=$("$WORK/probe")
assert_eq 'the stored representation matches the recomputed descriptor' \
    "$observed_probe" \
    "$size $tag_offset $payload_offset $none_tag $some_tag $some_tag"

# --------------------------------------------------------------- negatives
#
# Each refusal names the rule it pins and the code that pins it.
# `sibling_branch` and `non_dominating_guard` are the two invalidation shapes
# this slice can express; the mutation-driven ones are refused at the
# declaration instead, which `mutable_binding` and `assignment` pin.
#
# `unconstrained_null` and `null_argument` carry `E2S35` rather than `E2S147`
# on purpose. `null` is exempted from lexical resolution only where an `Int?`
# is expected, so in every other position it keeps exactly the verdict it had
# before this slice existed. Refusing to change that is the point: no program
# that compiled or failed on `main` compiles or fails differently now.

negatives='
direct_use:E2S147
sibling_branch:E2S147
non_dominating_guard:E2S147
mutable_binding:E2S147
assignment:E2S147
nested_optional:E2S147
coalescing:E2S147
property_path:E2S147
optional_as_int_argument:E2S147
optional_result_as_int:E2S147
unconstrained_null:E2S35
null_argument:E2S35
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
    printf '%s\n' "PASS optional refusal: $stem ($code)"
done
IFS=$previous_ifs

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

# ------------------------------------------------ no extraction, no unwrap
#
# The parent's standing constraint: no child may introduce an unchecked
# extraction operation, in syntax, implementation, or documentation.

# Source spellings only: the README says the words in order to record that the
# operators do not exist, which is the opposite of introducing them.
find "$CASES" -type f -name '*.kofun' \
    -exec grep -l -E '!!|\bunwrap\b|force_unwrap|expect_some' {} + \
    >"$WORK/extraction" 2>/dev/null || :
assert_file_empty 'the corpus introduced an extraction spelling' \
    "$WORK/extraction"
assert_not_grep 'Stage 2 introduced an extraction spelling' \
    -E -- 'KOFUN_OPTIONAL_INT_UNWRAP|optional_int_unwrap|force_unwrap' \
    "$ROOT/bootstrap/stage2/compiler.c"

# Coalescing stays with #314: this change may not have started implementing it.
assert_not_grep 'Stage 2 began lowering `??`' \
    -Fq -- 'KOFUN_OPTIONAL_INT_COALESCE' \
    "$ROOT/bootstrap/stage2/compiler.c"

# The registered identity has to stay registered.
assert_grep 'E2S147 is a registered diagnostic identity' \
    -Fq -- 'E2S147	optional-construction	frontend' \
    "$ROOT/tests/diagnostics/registry.tsv"
assert_grep 'E2S147 has an adapter report row' \
    -Fq -- 'E2S147	optional-construction' \
    "$ROOT/tests/diagnostics/reports/optional-construction.tsv"

printf '%s\n' \
    'PASS: present and absent Optional(Int) values construct and run' \
    'PASS: a value crosses a same-typed argument and return with its tag' \
    'PASS: the emitted representation is the recomputed AggregateLayout v1 one' \
    'PASS: every recognized narrowing shape executes its narrowed use' \
    'PASS: unsupported shapes stay exact refusals with no backend artifact' \
    'PASS: no extraction, coalescing, or force-unwrap spelling exists'
