#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)

node --check "$ROOT/tooling/typed-sidecar/ownership-view.mjs"
node --check "$ROOT/tests/tooling/ownership-view/ownership_view_test.mjs"
node "$ROOT/tests/tooling/ownership-view/ownership_view_test.mjs"
