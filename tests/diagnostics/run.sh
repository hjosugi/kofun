#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
ADAPTERS="$ROOT/tests/diagnostics/adapters.tsv"
WORK=${KOFUN_DIAGNOSTIC_REGISTRY_WORK:-"$ROOT/build/diagnostic-registry"}

rm -rf "$WORK"
mkdir -p "$WORK"

sh "$ROOT/tests/diagnostics/check.sh" --registry-only
: >"$WORK/observed.tsv"

while IFS='	' read -r adapter command bless report; do
    case $adapter in ''|\#*) continue ;; esac
    printf '%s\n' "RUN [diagnostic-adapter] $adapter"
    sh "$ROOT/$command"
    awk -F '\t' -v adapter="$adapter" '
        /^#/ || NF == 0 { next }
        $2 != adapter {
            printf "diagnostic registry: adapter %s reported owner %s\n",
                adapter, $2 > "/dev/stderr"
            exit 1
        }
        { print }
    ' "$ROOT/$report" >>"$WORK/observed.tsv"
done <"$ADAPTERS"

KOFUN_DIAGNOSTIC_OBSERVED="$WORK/observed.tsv" \
    sh "$ROOT/tests/diagnostics/check.sh"
sh "$ROOT/tests/diagnostics/self-test.sh"
