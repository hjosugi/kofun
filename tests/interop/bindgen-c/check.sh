#!/bin/sh
set -eu

# bindgen-c stage-1 gate for #574.
#
# Five things are checked, in this order:
#
#   1. `kofun bindgen-c` turns the pinned fixture header into a raw bindings
#      module and an audit report deterministically: two runs, identical
#      bytes, and nothing in either output names this machine's directories;
#   2. every deliberately unsupported construct in the header (macros, a
#      variadic, a union, a bitfield, a flexible array, an inline function)
#      appears in the report with a reason, and none of them leaks into the
#      module; the callback typedef is present as a review item;
#   3. the recorded interpretation context is real: the header hash and the
#      target triple are read back, and changing one preprocessor define
#      changes both artifacts and adds the conditional declaration;
#   4. the recorded ABI facts agree with the C compiler itself: a generated
#      probe prints sizeof/alignof/offsetof/enum values from the real header
#      and must match the report, and every bound symbol exists in the
#      fixture library;
#   5. the module is mechanically valid in the checked C ABI profile: with a
#      driver appended it builds through `kofun build --backend c --c-abi`,
#      links against the fixture library, runs, and reproduces the recorded
#      decisions — and it is marked raw/trusted, loudly.
#
# Nothing here fetches anything: clang parses one committed header, offline.

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CASES="$ROOT/tests/interop/bindgen-c"
ASSERT_CONTEXT="bindgen-c"
. "$ROOT/tests/assertions/assert.sh"

WORK=$(mktemp -d "${TMPDIR:-/tmp}/kofun-bindgen-c.XXXXXX")
trap 'rm -rf "$WORK"' 0 1 2 15

fail() {
    printf 'bindgen-c: FAIL: %s\n' "$*" >&2
    exit 1
}

require_line() {
    file=$1
    needle=$2
    label=$3
    assert_grep "$label" -Fq -- "$needle" "$file"
}

for required in clang node readelf sha256sum; do
    command -v "$required" >/dev/null 2>&1 ||
        fail "required tool unavailable: $required"
done

# ------------------------------------------------------- corpus hygiene

find "$CASES" -type f \( -name '*.py' -o -name '*.kf' \) >"$WORK/forbidden"
assert_file_empty 'forbidden Python or .kf source in the corpus' \
    "$WORK/forbidden"
assert_regular_file 'pinned fixture header' "$CASES/fixture/kbfix.h"
assert_regular_file 'pinned fixture implementation' "$CASES/fixture/kbfix.c"
assert_regular_file 'driver source' "$CASES/driver.kofun"
assert_regular_file 'driver golden' "$CASES/driver.stdout"

# ------------------------------------------------- generation, twice

run_bindgen() {
    destination=$1
    shift
    (
        CDPATH= cd -- "$CASES" &&
            "$ROOT/bin/kofun" bindgen-c fixture/kbfix.h \
                --out-dir "$destination" --module kbfix "$@"
    )
}

run_bindgen "$WORK/gen-a" >"$WORK/gen-a.stdout" 2>"$WORK/gen-a.stderr" ||
    fail "bindgen failed: $(cat "$WORK/gen-a.stderr")"
module="$WORK/gen-a/kbfix.raw.kofun"
report="$WORK/gen-a/kbfix.bindgen.json"
assert_regular_file 'generated raw module' "$module"
assert_regular_file 'generated audit report' "$report"

run_bindgen "$WORK/gen-b" >"$WORK/gen-b.stdout" 2>"$WORK/gen-b.stderr" ||
    fail "second bindgen run failed: $(cat "$WORK/gen-b.stderr")"
cmp "$module" "$WORK/gen-b/kbfix.raw.kofun" ||
    fail 'two runs produced different module bytes'
cmp "$report" "$WORK/gen-b/kbfix.bindgen.json" ||
    fail 'two runs produced different report bytes'

assert_not_grep 'the module embeds this gate work directory' \
    -Fq -- "$WORK" "$module"
assert_not_grep 'the module embeds the repository root path' \
    -Fq -- "$ROOT" "$module"
assert_not_grep 'the report embeds this gate work directory' \
    -Fq -- "$WORK" "$report"
assert_not_grep 'the report embeds the repository root path' \
    -Fq -- "$ROOT" "$report"

# --------------------------------------------------- raw/trusted marking

case $module in
    *.raw.kofun) ;;
    *) fail "module file name lost its .raw. segment: $module" ;;
esac
require_line "$module" 'RAW TRUSTED FOREIGN BINDINGS - NOT A SAFE INTERFACE' \
    'module lost its prominent raw-trust banner'
require_line "$module" 'trust: raw-trusted-foreign' \
    'module lost its machine-readable trust line'
require_line "$module" 'DO NOT EDIT' \
    'module lost its generated-file marking'
require_line "$module" 'Import this module only behind a hand-reviewed safe facade.' \
    'module no longer warns against direct import'
require_line "$report" '"trust": "raw-trusted-foreign"' \
    'report lost the trust field'

# ------------------------------------ context is recorded and verifiable

triple=$(clang -print-effective-triple)
header_sha=$(
    CDPATH= cd -- "$CASES" &&
        sha256sum fixture/kbfix.h | cut -d' ' -f1
)
require_line "$module" "target:   $triple" \
    'module does not record the effective target triple'
require_line "$module" "sha256=$header_sha" \
    'module does not record the header content hash'
require_line "$module" 'context-sha256:' \
    'module does not record the interpretation-context digest'

# -------------------------- structural report checks and audit coverage

node "$CASES/check-report.mjs" verify "$report" "$module" \
    "$triple" "$header_sha" >"$WORK/check-report.stdout" ||
    fail 'structural report verification failed (see check-report lines above)'

# Skipped constructs are reported, and only reported: none may surface as a
# declaration in the module.
for name in kbfix_log kbfix_word kbfix_flags kbfix_message kbfix_double \
    KBFIX_MAX_LABEL KBFIX_CLAMP
do
    assert_not_grep "skipped construct $name leaked into the module as a function" \
        -Fq -- "fn $name(" "$module"
    assert_not_grep "skipped construct $name leaked into the module as a struct" \
        -Fq -- "struct $name {" "$module"
done

# --------------------------- preprocessor context invalidates the output

run_bindgen "$WORK/gen-d" -D KBFIX_EXTRA=1 \
    >"$WORK/gen-d.stdout" 2>"$WORK/gen-d.stderr" ||
    fail "bindgen with -D KBFIX_EXTRA=1 failed: $(cat "$WORK/gen-d.stderr")"
if cmp -s "$module" "$WORK/gen-d/kbfix.raw.kofun"; then
    fail 'changing a define left the module bytes unchanged'
fi
if cmp -s "$report" "$WORK/gen-d/kbfix.bindgen.json"; then
    fail 'changing a define left the report bytes unchanged'
fi
require_line "$WORK/gen-d/kbfix.raw.kofun" 'defines:  KBFIX_EXTRA=1' \
    'redefined context is not recorded in the module'
require_line "$WORK/gen-d/kbfix.bindgen.json" '"KBFIX_EXTRA=1"' \
    'redefined context is not recorded in the report'
require_line "$WORK/gen-d/kbfix.raw.kofun" \
    'extern "C" fn kbfix_extra_probe(value: CLong) -> CLong' \
    'the define did not reach clang: conditional declaration is missing'
assert_not_grep 'the conditional declaration appears without its define' \
    -Fq -- 'kbfix_extra_probe' "$module"

# ------------------------------- ABI probe: ask the C compiler directly

node "$CASES/make-abi-probe.mjs" "$report" "$CASES/fixture/kbfix.h" \
    "$WORK/probe.c" "$WORK/probe.expected" >"$WORK/probe.gen.stdout" ||
    fail 'ABI probe generation failed'
clang -std=c11 -O2 -Wall -Wextra -Werror "$WORK/probe.c" -o "$WORK/probe" ||
    fail 'ABI probe did not compile against the fixture header'
"$WORK/probe" >"$WORK/probe.actual"
cmp "$WORK/probe.expected" "$WORK/probe.actual" ||
    fail 'the C compiler disagrees with the recorded sizes, offsets, or enum values'

# ------------------------------- every bound symbol exists in the library

clang -std=c11 -O2 -Wall -Wextra -Werror -shared -fPIC -DKBFIX_EXTRA=1 \
    "$CASES/fixture/kbfix.c" -o "$WORK/libkbfix.so" ||
    fail 'fixture library did not compile'
readelf --wide --dyn-syms "$WORK/libkbfix.so" >"$WORK/symbols.txt"
node "$CASES/check-report.mjs" symbols "$report" >"$WORK/bound-symbols.txt"
assert_file_nonempty 'bound symbol list' "$WORK/bound-symbols.txt"
while IFS= read -r symbol; do
    assert_grep "bound symbol $symbol is not exported by the fixture library" \
        -Fq -- "$symbol" "$WORK/symbols.txt"
done <"$WORK/bound-symbols.txt"

# ---------------- the module is valid input to the checked C ABI profile

cat "$module" "$CASES/driver.kofun" >"$WORK/program.kofun"
"$ROOT/bin/kofun" build "$WORK/program.kofun" --backend c --c-abi \
    --link-library "$WORK/libkbfix.so" --emit-c "$WORK/program.c" \
    -o "$WORK/program" >"$WORK/build.stdout" 2>"$WORK/build.stderr" ||
    fail "generated module did not build in the C ABI profile: $(cat "$WORK/build.stderr")"

# The emitted C carries the same declarations and the same layout the
# report recorded, restated by the independent c_abi compiler.
require_line "$WORK/program.c" \
    'extern const void * kbfix_counter_new(long initial);' \
    'emitted C lost the opaque-handle constructor declaration'
require_line "$WORK/program.c" \
    '_Static_assert(sizeof(kbfix_stats_t) == 16, "C ABI size: kbfix_stats_t");' \
    'emitted C lost the record size assertion'
require_line "$WORK/program.c" \
    '_Static_assert(offsetof(kbfix_stats_t, flags) == 12, "C ABI offset: kbfix_stats_t.flags");' \
    'emitted C lost the record offset assertion'

"$WORK/program" >"$WORK/program.stdout"
cmp "$CASES/driver.stdout" "$WORK/program.stdout" ||
    fail 'the driver decisions differ from the recorded golden'
"$WORK/program" >"$WORK/program.second"
cmp "$WORK/program.stdout" "$WORK/program.second" ||
    fail 'the driver is not reproducible across runs'

# ---------------------------------------- rejection paths stay rejections

printf '%s\n' 'this is not a C header {{{' >"$WORK/bad.h"
if (
    CDPATH= cd -- "$WORK" &&
        "$ROOT/bin/kofun" bindgen-c bad.h --out-dir out
) >"$WORK/bad.stdout" 2>"$WORK/bad.stderr"; then
    fail 'a malformed header was accepted'
fi
require_line "$WORK/bad.stderr" 'clang failed' \
    'the malformed-header refusal does not name clang'
assert_absent 'output directory for the malformed header' "$WORK/out"

if "$ROOT/bin/kofun" bindgen-c "$WORK/missing.h" --out-dir "$WORK/out" \
    >"$WORK/missing.stdout" 2>"$WORK/missing.stderr"; then
    fail 'a missing header was accepted'
fi
require_line "$WORK/missing.stderr" 'header not found' \
    'the missing-header refusal does not say what is missing'

printf 'bindgen-c: raw module and audit report are deterministic and context-pinned: PASS\n'
printf 'bindgen-c: unsupported constructs are reported with reasons, never bound: PASS\n'
printf 'bindgen-c: preprocessor context invalidates and regenerates the artifact: PASS\n'
printf 'bindgen-c: C compiler agrees with recorded sizes, offsets, and enum values: PASS\n'
printf 'bindgen-c: bindings build, link, and run in the checked C ABI profile: PASS\n'
printf 'bindgen-c: raw-trusted marking is present; no safe facade is claimed: PASS\n'
