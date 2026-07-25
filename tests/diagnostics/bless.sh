#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
ADAPTERS="$ROOT/tests/diagnostics/adapters.tsv"

sh "$ROOT/tests/diagnostics/check.sh" --registry-only

while IFS='	' read -r adapter command bless report; do
    case $adapter in ''|\#*) continue ;; esac
    if test "$bless" = verify; then
        printf '%s\n' "VERIFY [diagnostic-bless] $adapter (inline golden)"
        sh "$ROOT/$command"
    else
        printf '%s\n' "BLESS [diagnostic-bless] $adapter"
        sh "$ROOT/$bless"
    fi
done <"$ADAPTERS"

sh "$ROOT/tests/diagnostics/check.sh" --registry-only
printf '%s\n' \
    "Diagnostic goldens regenerated deterministically; review the resulting diff."
