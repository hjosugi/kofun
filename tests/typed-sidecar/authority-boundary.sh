#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)

if rg -n \
    'typed-sidecar/codec|readTypedSidecar|writeTypedSidecarAtomic|kofun-semantic' \
    "$ROOT/bin" "$ROOT/bootstrap" "$ROOT/package" "$ROOT/framework" "$ROOT/stdlib"
then
    printf '%s\n' 'FAIL: compiler/build/package/link authority path imports typed-sidecar tooling' >&2
    exit 1
fi

printf '%s\n' 'PASS: compiler, build, package, linker, and KIF paths cannot import the sidecar codec'
