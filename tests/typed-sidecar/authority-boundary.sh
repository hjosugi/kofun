#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
ASSERT_CONTEXT='typed-sidecar authority'
. "$ROOT/tests/assertions/assert.sh"

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
grep -q 'projectStage2SemanticEvents' \
    "$ROOT/tooling/typed-sidecar/from-stage2.mjs"
grep -q 'authoritative: false' \
    "$ROOT/tooling/typed-sidecar/from-stage2.mjs"

printf '%s\n' \
    'PASS: check invokes one output-only emitter; compiler, build, package, linker, KIF, and cache paths cannot read sidecars'
