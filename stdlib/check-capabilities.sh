#!/usr/bin/env sh
# Standard-library charter matrix gate.
#
# `stdlib/capabilities.tsv` is the machine-checked coverage matrix required by
# `docs/STANDARD_LIBRARY_CHARTER.md`. Every row assigns one capability a tier,
# exactly one state, and evidence. This gate refuses:
#
#   1. an unknown tier or state, so a row cannot invent a softer category;
#   2. empty evidence, so no state is self-certifying; and
#   3. evidence paths that do not exist in the repository, so `implemented`
#      and `specified` cannot point at files that were never written.
#
# Issue evidence is written `issue:#NNN` and is accepted for `planned`,
# `deferred`, and `non-goal` rows only: an open issue is never implementation
# or specification evidence.
set -eu

ROOT=$(CDPATH= cd -P -- "$(dirname -- "$0")/.." && pwd)
MATRIX=${1-"$ROOT/stdlib/capabilities.tsv"}

test -f "$MATRIX" || {
    printf '%s\n' "stdlib capabilities: matrix not found: $MATRIX" >&2
    exit 2
}

header=$(head -n 1 "$MATRIX")
expected=$(printf 'capability\ttier\tstate\tevidence')
if test "$header" != "$expected"; then
    printf '%s\n' \
        "stdlib capabilities: header must be: capability<TAB>tier<TAB>state<TAB>evidence" >&2
    exit 1
fi

rows=0
failures=0
line_no=0
while IFS='	' read -r capability tier state evidence; do
    line_no=$((line_no + 1))
    test "$line_no" -gt 1 || continue
    test -n "$capability" || continue
    rows=$((rows + 1))

    case $capability in
        *[!a-z0-9-]*)
            printf '%s\n' \
                "stdlib capabilities: line $line_no: capability is not lower-kebab-case: $capability" >&2
            failures=$((failures + 1))
            ;;
    esac

    case $tier in
        T0|T1|T2|T3) ;;
        *)
            printf '%s\n' \
                "stdlib capabilities: line $line_no: unknown tier for $capability: $tier" >&2
            failures=$((failures + 1))
            ;;
    esac

    case $state in
        implemented|specified|planned|deferred|non-goal) ;;
        *)
            printf '%s\n' \
                "stdlib capabilities: line $line_no: unknown state for $capability: $state" >&2
            failures=$((failures + 1))
            ;;
    esac

    if test -z "${evidence-}"; then
        printf '%s\n' \
            "stdlib capabilities: line $line_no: $capability has no evidence" >&2
        failures=$((failures + 1))
        continue
    fi

    for item in $evidence; do
        case $item in
            issue:\#[0-9]*)
                case $state in
                    implemented|specified)
                        printf '%s\n' \
                            "stdlib capabilities: line $line_no: $capability is $state but cites only an issue: $item" >&2
                        failures=$((failures + 1))
                        ;;
                esac
                ;;
            *)
                if ! test -e "$ROOT/$item"; then
                    printf '%s\n' \
                        "stdlib capabilities: line $line_no: $capability evidence path does not exist: $item" >&2
                    failures=$((failures + 1))
                fi
                ;;
        esac
    done
done < "$MATRIX"

if test "$rows" -eq 0; then
    printf '%s\n' "stdlib capabilities: matrix has no rows" >&2
    exit 1
fi

if test "$failures" -gt 0; then
    printf '%s\n' \
        "FAIL: stdlib capabilities: $failures problem(s) across $rows row(s)" >&2
    exit 1
fi

printf '%s\n' "PASS: stdlib capabilities: $rows rows, every state bounded by evidence"
