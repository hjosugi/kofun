#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SPEC="$ROOT/spec/type-level-programming-v1.md"
DIRECTORY="$ROOT/spec/type-reduction-trace"
SCHEMA="$DIRECTORY/kofun.type-reduction-trace.v1.schema.json"
VALIDATOR="$DIRECTORY/validate.mjs"
EXAMPLES="$DIRECTORY/examples"

fail() {
    printf '%s\n' "FAIL: $*" >&2
    exit 1
}

require_text() {
    file=$1
    needle=$2
    grep -Fq "$needle" "$file" ||
        fail "$file does not contain required text: $needle"
}

for command in grep node
do
    command -v "$command" >/dev/null 2>&1 ||
        fail "$command is required"
done

node --check "$VALIDATOR"
node "$VALIDATOR" schema "$SCHEMA"
node "$VALIDATOR" validate \
    "$EXAMPLES/alias.json" \
    "$EXAMPLES/reduced.json" \
    "$EXAMPLES/cycle.json"
node "$VALIDATOR" self-test

require_text "$SPEC" 'Kofun selects a Type-only, named, structurally terminating profile.'
require_text "$SPEC" '| Active frames | 32 |'
require_text "$SPEC" '| Logical steps | 256 |'
require_text "$SPEC" '| Constructed logical type nodes | 256 |'
require_text "$SPEC" 'at most eight rendered trace frames'
require_text "$SPEC" 'at most 256 step records and is at'
require_text "$SPEC" 'most 4 MiB of canonical UTF-8 JSON'
require_text "$SPEC" '`type explain` renders complete'
require_text "$SPEC" 'No active compiler'
require_text "$ROOT/docs/TYPE_SYSTEM.md" 'a design target, not an implemented compiler feature'
require_text "$ROOT/spec/README.md" '`kofun.type-reduction-trace/v1` validation gate'

printf '%s\n' \
    'PASS: Type-only named type-function profile is fixed and bounded' \
    'PASS: alias, type-function, and failure traces satisfy the canonical v1 contract' \
    'PASS: exact and one-over frame, step, and node boundaries are enforced' \
    'PASS: invalid authority, counters, identities, spans, and budgets are rejected' \
    'PASS: documentation does not claim an active type-function implementation'
