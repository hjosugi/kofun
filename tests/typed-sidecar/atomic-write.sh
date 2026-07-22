#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
TMP_PARENT="$ROOT/build/tmp"
mkdir -p "$TMP_PARENT"
WORK=$(mktemp -d "$TMP_PARENT/typed-sidecar-atomic.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM

node --check "$ROOT/tests/typed-sidecar/atomic_write_test.mjs"
node "$ROOT/tests/typed-sidecar/atomic_write_test.mjs" "$WORK"
