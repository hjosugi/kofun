#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
WORK=${KOFUN_TYPED_SIDECAR_AUTHORITY_WORK:-"$ROOT/build/${KOFUN_GATE_WORK_NAMESPACE:+$KOFUN_GATE_WORK_NAMESPACE/}typed-sidecar-authority"}
ASSERT_CONTEXT='typed-sidecar authority'
. "$ROOT/tests/assertions/assert.sh"

case $WORK in
    */typed-sidecar-authority|*/typed-sidecar-authority.*) ;;
    *) printf '%s\n' "FAIL: work directory must end in typed-sidecar-authority[.suffix]: $WORK" >&2; exit 1 ;;
esac
rm -rf "$WORK"
mkdir -p "$WORK"

# POSIX `grep -r`, not `rg`. CI does not install ripgrep, and the two negative
# checks below are `if <search>; then FAIL; fi` — with `rg` missing, the search
# exits non-zero, the `if` is false, and the gate prints PASS without having
# examined a single file. This boundary had been green on CI without ever
# running. Keep every search here to tools the CI image actually has.
require_absent() {
    pattern=$1
    message=$2
    shift 2
    if grep -rnE "$pattern" "$@"; then
        printf '%s\n' "$message" >&2
        exit 1
    fi
}

count_matches() {
    pattern=$1
    file=$2
    grep -cE "$pattern" "$file" || true
}

require_absent \
    'typed-sidecar/(codec|from-stage2|emit-stage2)|readTypedSidecar|writeTypedSidecarAtomic' \
    'FAIL: compiler/build/package/link authority path imports typed-sidecar tooling' \
    "$ROOT/bootstrap" "$ROOT/package" "$ROOT/framework" "$ROOT/stdlib"

require_absent \
    'typed-sidecar/(codec|from-stage2)|readTypedSidecar|writeTypedSidecarAtomic' \
    'FAIL: the CLI reads tooling output or imports the sidecar codec directly' \
    "$ROOT/bin"

assert_num "output of count_matches tooling/typed-sidecar/emit-stage2\\.mjs $ROOT/bin/kofun" \
    "$(count_matches 'tooling/typed-sidecar/emit-stage2\.mjs' "$ROOT/bin/kofun")" \
    -eq 1
assert_num "output of count_matches node \\\$TYPED_SIDECAR_EMITTER $ROOT/bin/kofun" \
    "$(count_matches 'node "\$TYPED_SIDECAR_EMITTER"' "$ROOT/bin/kofun")" -eq 1
assert_grep "tooling/typed-sidecar/from-stage2.mjs" \
    -q \
    'projectStage2SemanticEvents' \
    "$ROOT/tooling/typed-sidecar/from-stage2.mjs"
assert_grep "tooling/typed-sidecar/from-stage2.mjs" \
    -q 'authoritative: false' "$ROOT/tooling/typed-sidecar/from-stage2.mjs"

node --check "$ROOT/tests/typed-sidecar/authority_boundary_test.mjs"
(
    cd "$ROOT"
    node tests/typed-sidecar/authority_boundary_test.mjs "$WORK"
)

printf '%s\n' \
    'PASS: check invokes one output-only emitter; compiler, build, package, linker, KIF, and cache paths cannot read sidecars'
