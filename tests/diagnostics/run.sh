#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
ADAPTERS="$ROOT/tests/diagnostics/adapters.tsv"
WORK=${KOFUN_DIAGNOSTIC_REGISTRY_WORK:-"$ROOT/build/diagnostic-registry"}

rm -rf "$WORK"
mkdir -p "$WORK"

# Several adapters run a script that is also its own task, and each defaults to
# one shared work directory. Under a concurrent `task verify` the two
# copies interleave in that directory: the module-symbols pair appends to one
# module inventory until it trips `E2S55: inventory exceeds 256 modules`. Give the
# adapter copies their own directories so the two invocations cannot meet. Each
# runner already honours these overrides; the tasks keep the defaults.
#
# Five of the seven runners assert that their work directory ends in a fixed name
# with an optional suffix, so each override keeps that name and adds a
# `.diagnostics` suffix rather than inventing a new one.
KOFUN_ADT_FRONTEND_WORK="$WORK/adt-frontend.diagnostics"
KOFUN_RECORD_FRONTEND_WORK="$WORK/record-frontend.diagnostics"
KOFUN_GENERICS_FRONTEND_WORK="$WORK/generics-frontend.diagnostics"
KOFUN_HM_LEVELS_WORK="$WORK/hm-levels.diagnostics"
KOFUN_ADT_EXHAUSTIVENESS_WORK="$WORK/adt-exhaustiveness.diagnostics"
KOFUN_MODULE_SYMBOLS_WORK="$WORK/module-symbols.diagnostics"
KOFUN_IMPORTS_SELECTIVE_WORK="$WORK/imports-selective.diagnostics"
KOFUN_RE_EXPORTS_WORK="$WORK/re-exports.diagnostics"
export KOFUN_ADT_FRONTEND_WORK KOFUN_RECORD_FRONTEND_WORK
export KOFUN_GENERICS_FRONTEND_WORK
export KOFUN_HM_LEVELS_WORK
export KOFUN_ADT_EXHAUSTIVENESS_WORK KOFUN_MODULE_SYMBOLS_WORK
export KOFUN_IMPORTS_SELECTIVE_WORK KOFUN_RE_EXPORTS_WORK

sh "$ROOT/tests/diagnostics/check.sh" --registry-only
: >"$WORK/observed.tsv"

while IFS='	' read -r adapter command bless report; do
    case $adapter in ''|\#*) continue ;; esac
    printf '%s\n' "RUN [diagnostic-adapter] $adapter"
    # An adapter is a full gate script that other `task verify` targets also
    # run. Give each one its own build namespace so this gate cannot race them
    # or its own siblings (#713).
    KOFUN_GATE_WORK_NAMESPACE="diagnostic-adapter/$adapter" \
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
