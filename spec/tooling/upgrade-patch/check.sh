#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)

node --check "$ROOT/spec/tooling/upgrade-patch/model.mjs"
node --check "$ROOT/spec/tooling/upgrade-patch/check.mjs"
node -e 'JSON.parse(require("fs").readFileSync(process.argv[1], "utf8"))' \
    "$ROOT/spec/tooling/upgrade-patch/schema.json"
node "$ROOT/spec/tooling/upgrade-patch/check.mjs"
