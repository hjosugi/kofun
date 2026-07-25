#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
ADAPTERS="$ROOT/tests/diagnostics/adapters.tsv"
WORK=${KOFUN_DIAGNOSTIC_REGISTRY_WORK:-"$ROOT/build/diagnostic-registry"}

rm -rf "$WORK"
mkdir -p "$WORK"

# Six of the eleven adapters run a script that is also its own `make` target, and
# each defaults to one shared work directory. Under `make -j` the two copies
# interleave in that directory: the module-symbols pair appends to one module
# inventory until it trips `E2S55: inventory exceeds 256 modules`. Give the
# adapter copies their own directories so the two invocations cannot meet. Each
# runner already honours these overrides; the `make` targets keep the defaults.
#
# Four of the six runners assert that their work directory ends in a fixed name
# with an optional suffix, so each override keeps that name and adds a
# `.diagnostics` suffix rather than inventing a new one.
KOFUN_ADT_FRONTEND_WORK="$WORK/adt-frontend.diagnostics"
KOFUN_GENERICS_FRONTEND_WORK="$WORK/generics-frontend.diagnostics"
KOFUN_ADT_EXHAUSTIVENESS_WORK="$WORK/adt-exhaustiveness.diagnostics"
KOFUN_MODULE_SYMBOLS_WORK="$WORK/module-symbols.diagnostics"
KOFUN_IMPORTS_SELECTIVE_WORK="$WORK/imports-selective.diagnostics"
KOFUN_RE_EXPORTS_WORK="$WORK/re-exports.diagnostics"
export KOFUN_ADT_FRONTEND_WORK KOFUN_GENERICS_FRONTEND_WORK
export KOFUN_ADT_EXHAUSTIVENESS_WORK KOFUN_MODULE_SYMBOLS_WORK
export KOFUN_IMPORTS_SELECTIVE_WORK KOFUN_RE_EXPORTS_WORK

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
