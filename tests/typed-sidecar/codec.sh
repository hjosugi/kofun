#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
node --check "$ROOT/tooling/typed-sidecar/codec.mjs"
node --check "$ROOT/tests/typed-sidecar/codec_test.mjs"
node "$ROOT/tests/typed-sidecar/codec_test.mjs"
