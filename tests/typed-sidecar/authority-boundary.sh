#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)

if rg -n \
    'typed-sidecar/(codec|from-stage2|emit-stage2)|readTypedSidecar|writeTypedSidecarAtomic' \
    "$ROOT/bootstrap" "$ROOT/package" "$ROOT/framework" "$ROOT/stdlib"
then
    printf '%s\n' 'FAIL: compiler/build/package/link authority path imports typed-sidecar tooling' >&2
    exit 1
fi

if rg -n \
    'typed-sidecar/(codec|from-stage2)|readTypedSidecar|writeTypedSidecarAtomic' \
    "$ROOT/bin"
then
    printf '%s\n' 'FAIL: the CLI reads tooling output or imports the sidecar codec directly' >&2
    exit 1
fi

test "$(rg -c 'tooling/typed-sidecar/emit-stage2\.mjs' "$ROOT/bin/kofun")" -eq 1
test "$(rg -c 'node "\$TYPED_SIDECAR_EMITTER"' "$ROOT/bin/kofun")" -eq 1
rg -q 'projectStage2SemanticEvents' \
    "$ROOT/tooling/typed-sidecar/from-stage2.mjs"
rg -q 'authoritative: false' \
    "$ROOT/tooling/typed-sidecar/from-stage2.mjs"

printf '%s\n' \
    'PASS: check invokes one output-only emitter; compiler, build, package, linker, KIF, and cache paths cannot read sidecars'
