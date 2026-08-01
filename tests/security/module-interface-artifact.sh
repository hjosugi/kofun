#!/usr/bin/env sh

set -eu

LC_ALL=C
export LC_ALL

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
WORK=${KOFUN_MODULE_INTERFACE_SECURITY_WORK:-"$ROOT/build/${KOFUN_GATE_WORK_NAMESPACE:+$KOFUN_GATE_WORK_NAMESPACE/}module-interface-artifact"}
CC=${CC:-cc}

fail() {
    printf '%s\n' "FAIL: $*" >&2
    exit 1
}

case $WORK in
    */module-interface-artifact|*/module-interface-artifact.*) ;;
    *) fail "work directory must end in module-interface-artifact[.suffix]: $WORK" ;;
esac
command -v "$CC" >/dev/null 2>&1 || fail 'a C11 compiler is required'
rm -rf "$WORK"
mkdir -p "$WORK"

"$CC" -std=c11 -O2 -Wall -Wextra -Werror -pedantic \
    -I"$ROOT/bootstrap/stage2" \
    "$ROOT/tests/security/module_interface_artifact_test.c" \
    "$ROOT/bootstrap/stage2/sha256.c" \
    -o "$WORK/module-interface-artifact-test"

"$WORK/module-interface-artifact-test" "$WORK/interface.kif"

test -s "$WORK/interface.kif" || fail 'valid prior interface disappeared'
if grep -aEq 'PublicWrap|hidden semantic dependency|visibility-leak' \
    "$WORK/interface.kif"; then
    fail 'failed validation appended diagnostic material to the artifact'
fi
