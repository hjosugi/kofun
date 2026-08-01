#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
WORK=${KOFUN_SEMANTIC_IDENTITY_WORK:-"$ROOT/build/semantic-identity"}

case $WORK in
    */semantic-identity|*/semantic-identity.*) ;;
    *) printf '%s\n' "FAIL: work directory must end in semantic-identity[.suffix]: $WORK" >&2; exit 1 ;;
esac

rm -rf "$WORK"
mkdir -p "$WORK"
node --check "$ROOT/spec/semantic-identity/model.mjs"
node --check "$ROOT/spec/semantic-identity/independent-encoder.mjs"
node --check "$ROOT/spec/semantic-identity/check.mjs"
node "$ROOT/spec/semantic-identity/check.mjs" "$WORK"
test -s "$WORK/measurements.json"
