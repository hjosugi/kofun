#!/bin/sh
# RFC ledger gate.
#
# Two things are checked:
#
#   1. the ledger still keeps accepted, implemented, enabled and amended apart;
#      and
#   2. the checker still refuses every way that separation can collapse.
#
# (2) is not optional. A checker that quietly stopped enforcing a rule would
# keep reporting PASS on the honest ledger, and the first sign of trouble would
# be a design document being read as shipped behaviour.
set -eu

ROOT=$(CDPATH= cd -P -- "$(dirname -- "$0")/../.." && pwd)
VALIDATOR="$ROOT/tests/rfc/validate-registry.mjs"
GENERATOR="$ROOT/tests/rfc/make-invalid.mjs"

if test "$#" -gt 0; then
    printf '%s\n' "rfc-ledger: unexpected argument: $1" >&2
    printf '%s\n' "rfc-ledger: usage: sh tests/rfc/check-registry.sh" >&2
    exit 2
fi

TMP_PARENT="$ROOT/build/tmp"
mkdir -p "$TMP_PARENT"
WORK=$(mktemp -d "$TMP_PARENT/rfc-ledger.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM

node --check "$VALIDATOR"
node --check "$GENERATOR"

node "$VALIDATOR" schema
node "$VALIDATOR" validate

mutations=0
node "$GENERATOR" list > "$WORK/mutations.tsv"
while IFS='	' read -r mutation blame; do
    test -n "$mutation" || continue
    mutations=$((mutations + 1))
    node "$GENERATOR" "$mutation" "$WORK/$mutation.json"
    if node "$VALIDATOR" validate "$WORK/$mutation.json" \
        > "$WORK/$mutation.out" 2> "$WORK/$mutation.err"
    then
        printf '%s\n' \
            "FAIL: rfc ledger: the checker accepted the \`$mutation\` mutation" >&2
        exit 1
    fi
    if ! grep -qF -- "$blame" "$WORK/$mutation.err"; then
        printf '%s\n' \
            "FAIL: rfc ledger: the \`$mutation\` refusal does not name \`$blame\`" >&2
        cat "$WORK/$mutation.err" >&2
        exit 1
    fi
    if ! grep -q 'Repair: ' "$WORK/$mutation.err"; then
        printf '%s\n' \
            "FAIL: rfc ledger: the \`$mutation\` refusal carries no repair instruction" >&2
        exit 1
    fi
done < "$WORK/mutations.tsv"

if test "$mutations" -eq 0; then
    printf '%s\n' "FAIL: rfc ledger: no negative mutations were exercised" >&2
    exit 1
fi

printf '%s\n' \
    "PASS: the RFC ledger separates acceptance from implementation, and $mutations mutations are refused"
