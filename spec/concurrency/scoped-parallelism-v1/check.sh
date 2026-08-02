#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

if ! command -v node >/dev/null 2>&1; then
    printf '%s\n' 'FAIL: node is required for the scoped-parallelism v1 model' >&2
    exit 1
fi

node --check "$ROOT/model.mjs"
node --check "$ROOT/check.mjs"
node "$ROOT/check.mjs"

printf '%s\n' 'PASS: scoped parallelism v1 contract and bounded executable gate'
