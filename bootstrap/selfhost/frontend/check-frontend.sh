#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/../../.." && pwd)
cd "$repo_root"

fail() {
    printf '%s\n' "FAIL: selfhost frontend: $*" >&2
    exit 1
}

if command -v cc >/dev/null 2>&1; then
    compiler=cc
elif command -v clang >/dev/null 2>&1; then
    compiler=clang
elif command -v gcc >/dev/null 2>&1; then
    compiler=gcc
else
    fail "a C11 compiler is required"
fi

temporary=${TMPDIR:-/tmp}/kofun-selfhost-frontend.$$
trap 'rm -rf "$temporary"' EXIT HUP INT TERM
mkdir -p "$temporary"

"$compiler" -std=c11 -O2 -Wall -Wextra -Werror \
    bootstrap/stage2/compiler.c -o "$temporary/kofun-stage2"

# The frozen S emits one complete typed kofun.selfhost-hir/v1 document,
# byte-identical to the checked-in evidence and across repeated runs. The
# digest is the exact profile digest, so profile drift fails here too.
profile_digest=$(awk -F '|' '$1 == "source_sha256" { print $2 }' \
    bootstrap/selfhost/profile.meta)
actual_digest=$(sha256sum bootstrap/stage1/compiler.kofun | awk '{ print $1 }')
test "$profile_digest" = "$actual_digest" ||
    fail "S digest differs from the frozen profile"

"$temporary/kofun-stage2" --emit-selfhost-hir \
    bootstrap/stage1/compiler.kofun \
    "$temporary/S.hir" \
    "$profile_digest" >/dev/null
cmp bootstrap/selfhost/frontend/S.hir "$temporary/S.hir" ||
    fail "S typed document differs from the checked-in evidence"
"$temporary/kofun-stage2" --emit-selfhost-hir \
    bootstrap/stage1/compiler.kofun \
    "$temporary/S.second.hir" \
    "$profile_digest" >/dev/null
cmp "$temporary/S.hir" "$temporary/S.second.hir" ||
    fail "S typed document is not deterministic"

grep '^status|complete$' "$temporary/S.hir" >/dev/null ||
    fail "S document must be complete"
test "$(grep -c '^function|' "$temporary/S.hir")" -eq 11 ||
    fail "S must type all 11 functions"

# Every rejection fixture produces exit 1 and its exact rejected document:
# diagnostics with stable codes and spans, never a partial typed document.
for fixture in bootstrap/selfhost/frontend/reject_*.kofun; do
    stem=$(basename "$fixture" .kofun)
    digest=$(sha256sum "$fixture" | awk '{ print $1 }')
    set +e
    "$temporary/kofun-stage2" --emit-selfhost-hir \
        "$fixture" \
        "$temporary/$stem.hir" \
        "$digest" >"$temporary/$stem.stdout" 2>"$temporary/$stem.stderr"
    status=$?
    set -e
    test "$status" -eq 1 || fail "$stem must reject with exit 1"
    test ! -s "$temporary/$stem.stderr" ||
        fail "$stem wrote unexpected internal stderr"
    cmp "bootstrap/selfhost/frontend/$stem.hir" "$temporary/$stem.hir" ||
        fail "$stem rejected document differs from the checked-in evidence"
    grep '^status|rejected$' "$temporary/$stem.hir" >/dev/null ||
        fail "$stem document must be rejected"
    grep '^node|' "$temporary/$stem.hir" >/dev/null &&
        fail "$stem rejected document must not carry typed nodes"
done

printf '%s\n' \
    "PASS: the frozen S emits one complete, deterministic typed-HIR document" \
    "PASS: 8 rejection fixtures pin stable diagnostics without partial documents"
