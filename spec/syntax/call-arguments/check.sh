#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)

node --check "$ROOT/spec/syntax/call-arguments/model.mjs"
node --check "$ROOT/spec/syntax/call-arguments/check.mjs"
node "$ROOT/spec/syntax/call-arguments/check.mjs"
