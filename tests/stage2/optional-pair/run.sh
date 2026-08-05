#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)

node --check "$ROOT/tests/stage2/optional-pair/check.mjs"
node "$ROOT/tests/stage2/optional-pair/check.mjs" \
    "$ROOT/bootstrap/stage2/compiler.c" \
    "$ROOT/bootstrap/stage2/compiler.kofun" \
    self-test
