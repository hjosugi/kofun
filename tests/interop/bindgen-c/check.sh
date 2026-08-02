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
#      probe type-checks every reported calling convention, prints the
#      convention plus sizeof/alignof/offsetof/enum facts from the real
#      header, and must match the report; every bound symbol exists too;
#   5. the module is mechanically valid in the checked C ABI profile: with a
#      driver appended it builds through `kofun build --backend c --c-abi`,
#      links against the fixture library, runs, and reproduces the recorded
#      decisions — and it is marked raw/trusted, loudly;
#   6. that same crossing runs again with both sides rebuilt under
#      AddressSanitizer and UndefinedBehaviorSanitizer, leak detection on,
#      and three probes prove the instrumentation is actually armed.
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
    kbfix_ms_abi_probe \
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
    fail 'the C compiler disagrees with the recorded calling conventions or layout facts'

# Missing and invented convention data are fatal before a C probe is written.
for corruption in missing unknown; do
    node - "$report" "$WORK/report-$corruption-cc.json" "$corruption" <<'NODE'
const { readFileSync, writeFileSync } = require('node:fs');
const [input, output, corruption] = process.argv.slice(2);
const report = JSON.parse(readFileSync(input, 'utf8'));
if (corruption === 'missing') delete report.layout.functions[0].calling_convention;
else report.layout.functions[0].calling_convention.id = 'invented-convention';
writeFileSync(output, `${JSON.stringify(report)}\n`, 'utf8');
NODE
    if node "$CASES/make-abi-probe.mjs" \
        "$WORK/report-$corruption-cc.json" "$CASES/fixture/kbfix.h" \
        "$WORK/rejected-$corruption.c" "$WORK/rejected-$corruption.expected" \
        >"$WORK/rejected-$corruption.stdout" \
        2>"$WORK/rejected-$corruption.stderr"
    then
        fail "$corruption calling-convention evidence was accepted"
    fi
    require_line "$WORK/rejected-$corruption.stderr" 'calling convention' \
        "$corruption convention refusal does not name the failed boundary"
done

# A structurally plausible report that assigns the accepted SysV convention
# to the fixture's real ms_abi declaration reaches Clang and fails its type
# compatibility check. This is the independent disagreement path.
node - "$report" "$WORK/report-mismatched-cc.json" <<'NODE'
const { readFileSync, writeFileSync } = require('node:fs');
const [input, output] = process.argv.slice(2);
const report = JSON.parse(readFileSync(input, 'utf8'));
report.layout.functions[0].name = 'kbfix_ms_abi_probe';
writeFileSync(output, `${JSON.stringify(report)}\n`, 'utf8');
NODE
node "$CASES/make-abi-probe.mjs" \
    "$WORK/report-mismatched-cc.json" "$CASES/fixture/kbfix.h" \
    "$WORK/mismatched-cc.c" "$WORK/mismatched-cc.expected" \
    >"$WORK/mismatched-cc.stdout" 2>"$WORK/mismatched-cc.stderr" ||
    fail 'mismatched convention report did not reach the independent C probe'
if clang -std=c11 -O2 -Wall -Wextra -Werror "$WORK/mismatched-cc.c" \
    -o "$WORK/mismatched-cc" >"$WORK/mismatched-cc-clang.stdout" \
    2>"$WORK/mismatched-cc-clang.stderr"
then
    fail 'the C compiler accepted a report/header calling-convention disagreement'
fi
require_line "$WORK/mismatched-cc-clang.stderr" 'attributes are not compatible' \
    'the C-side disagreement does not identify incompatible ABI attributes'

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

# ------------------------ the same crossing, under ASan and UBSan

# The build above proves the boundary links, runs, and decides correctly. It
# does not prove the crossing is free of the faults a generated binding is
# most likely to have: a length that is right for the header and wrong for
# the call, a signedness that silently wraps, an owner that frees twice.
#
# Both sides are rebuilt instrumented — the emitted boundary and the fixture
# implementation it calls — so a fault on either side of the crossing is a
# failure here rather than a value that happens to look right. The fixture is
# compiled in directly instead of linked as the shared object above, because
# an uninstrumented library would leave half the crossing unchecked.
#
# `-fno-sanitize-recover=all` is what makes this a gate: UndefinedBehaviorSanitizer
# prints and continues by default, so a diagnosed program still exits 0 and a
# check that only reads the exit status reports PASS.
sanitized_clang() {
    clang -std=c11 -O1 -g \
        -fsanitize=address,undefined -fno-sanitize-recover=all \
        -fno-omit-frame-pointer "$@"
}

sanitized_clang -DKBFIX_EXTRA=1 \
    "$WORK/program.c" "$CASES/fixture/kbfix.c" \
    -o "$WORK/program-sanitized" \
    >"$WORK/sanitized-build.stdout" 2>"$WORK/sanitized-build.stderr" ||
    fail "sanitized boundary did not build (is the clang sanitizer runtime installed?): $(cat "$WORK/sanitized-build.stderr")"

if ! ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 \
    "$WORK/program-sanitized" \
    >"$WORK/sanitized.stdout" 2>"$WORK/sanitized.stderr"
then
    fail "the sanitized boundary exited nonzero: $(head -n 3 "$WORK/sanitized.stderr")"
fi
assert_file_empty 'sanitizer diagnostics crossing the generated boundary' \
    "$WORK/sanitized.stderr"
cmp "$CASES/driver.stdout" "$WORK/sanitized.stdout" ||
    fail 'the sanitized boundary decided differently from the recorded golden'

# A sanitizer that was never linked in is silent in exactly the way a clean
# run is, so the section above cannot tell the two apart on its own. Each
# probe is built with the same flags and must be caught by a different arm:
# memory error, leak, and undefined behavior. If any probe survives, the
# green result above proves nothing and this gate says so.
probe_run() {
    probe_name=$1
    probe_expected=$2
    sanitized_clang "$WORK/probe-$probe_name.c" -o "$WORK/probe-$probe_name" \
        >"$WORK/probe-$probe_name-build.stdout" \
        2>"$WORK/probe-$probe_name-build.stderr" ||
        fail "the $probe_name sanitizer probe did not build: $(cat "$WORK/probe-$probe_name-build.stderr")"
    if ASAN_OPTIONS=detect_leaks=1 "$WORK/probe-$probe_name" \
        >"$WORK/probe-$probe_name.stdout" 2>"$WORK/probe-$probe_name.stderr"
    then
        fail "the $probe_name probe was not diagnosed: the sanitizers are not armed"
    fi
    require_line "$WORK/probe-$probe_name.stderr" "$probe_expected" \
        "the $probe_name probe failed without a \`$probe_expected\` diagnostic"
}

cat >"$WORK/probe-use-after-free.c" <<'PROBE'
#include <stdlib.h>

int main(void) {
    int *cells = malloc(sizeof *cells);
    *cells = 1;
    free(cells);
    return *cells;
}
PROBE
probe_run use-after-free 'AddressSanitizer: heap-use-after-free'

cat >"$WORK/probe-leak.c" <<'PROBE'
#include <stdlib.h>

int main(void) {
    volatile int *held = malloc(sizeof *held);
    *held = 1;
    return 0;
}
PROBE
probe_run leak 'LeakSanitizer: detected memory leaks'

cat >"$WORK/probe-overflow.c" <<'PROBE'
#include <stdint.h>

int main(void) {
    volatile int32_t limit = INT32_MAX;
    limit = limit + 1;
    return (int)limit;
}
PROBE
probe_run overflow 'signed integer overflow'

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
printf 'bindgen-c: target-derived calling conventions match clang function types: PASS\n'
printf 'bindgen-c: bindings build, link, and run in the checked C ABI profile: PASS\n'
printf 'bindgen-c: the crossing is clean under ASan/UBSan with leak detection, and three probes prove it is armed: PASS\n'
printf 'bindgen-c: raw-trusted marking is present; no safe facade is claimed: PASS\n'
