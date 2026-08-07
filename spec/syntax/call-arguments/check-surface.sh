#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)

fail() {
    printf '%s\n' "call-arguments-surface: $*" >&2
    exit 1
}

command -v node >/dev/null 2>&1 ||
    fail "node is required to run the surface parser gate"

node --check "$ROOT/spec/syntax/call-arguments/parser.mjs" ||
    fail "spec/syntax/call-arguments/parser.mjs is not valid JavaScript"
node --check "$ROOT/spec/syntax/call-arguments/format.mjs" ||
    fail "spec/syntax/call-arguments/format.mjs is not valid JavaScript"
node --check "$ROOT/spec/syntax/call-arguments/check-surface.mjs" ||
    fail "spec/syntax/call-arguments/check-surface.mjs is not valid JavaScript"

node "$ROOT/spec/syntax/call-arguments/check-surface.mjs" ||
    fail "the surface parser, formatter, or ambiguity corpus disagrees with spec/syntax/call-arguments-v1.md"
