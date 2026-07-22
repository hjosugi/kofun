#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)
CASES="$ROOT/tests/conformance/modules/visibility-access"
CC=${CC:-cc}
WORK=${KOFUN_VISIBILITY_ACCESS_WORK:-"$ROOT/build/visibility-access"}

fail() {
    printf '%s\n' "FAIL: $*" >&2
    exit 1
}

command -v "$CC" >/dev/null 2>&1 || fail 'a C11 compiler is required'
case $WORK in
    */visibility-access|*/visibility-access.*) ;;
    *) fail "work directory must end in visibility-access[.suffix]: $WORK" ;;
esac
rm -rf "$WORK"
mkdir -p "$WORK"

"$CC" -std=c11 -O2 -Wall -Wextra -Werror -pedantic \
    -I"$ROOT/bootstrap/stage2" \
    "$ROOT/bootstrap/stage2/visibility_access.c" \
    "$CASES/driver.c" \
    -o "$WORK/visibility-access-driver"

"$WORK/visibility-access-driver" "$CASES/cases.tsv" >"$WORK/actual"
"$WORK/visibility-access-driver" "$CASES/cases.tsv" >"$WORK/second"
cmp "$WORK/actual" "$WORK/second" || fail 'decision output is nondeterministic'
cmp "$CASES/expected.txt" "$WORK/actual" || fail 'decision golden differs'

decision_for() {
    name=$1
    sed -n "s/^$name|//p" "$WORK/actual"
}

test "$(decision_for permutation-a)" = "$(decision_for permutation-b)" ||
    fail 'display/source permutation changed the access decision'
test "$(decision_for path-remap-a)" = "$(decision_for path-remap-b)" ||
    fail 'absolute/logical path remap changed the access decision'
test "$(decision_for boundary-order-a)" = "$(decision_for boundary-order-b)" ||
    fail 'enclosing-boundary order changed the effective decision'

awk -F '|' '
    $2 != "Allowed" && $8 != "usable=no" { exit 1 }
    $2 == "Allowed" && $8 != "usable=yes" { exit 1 }
    END { if (NR != 31) exit 1 }
' "$WORK/actual" || fail 'denied/unsupported result yielded a usable reference'

printf '%s\n' \
    'PASS: private file and owner boundaries use canonical IDs only' \
    'PASS: internal/public/enclosing reachability is deterministic' \
    'PASS: denial disclosure, remedies, schemas, and depth limits are exact' \
    'PASS: aliases, permutations, and path remaps cannot widen access'
