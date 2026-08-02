#!/usr/bin/env sh
set -eu

# wasm32 host-profile activation gate (#1000).
#
# `spec/wasm-host-profile-v1.md` decides that the host ABI is part of the
# target name: `--target wasm32` keeps the bounded numeric binding, and
# `kofun-wasm-host-abi-v1` is reached only through `--target wasm32-hostabi1`.
# This gate is that decision made checkable, and nothing else — it compiles no
# aggregate value, lowers nothing, and asserts no new capability.
#
# The four properties worth failing over:
#
#   1. `--target wasm32` still emits the legacy binding. If a later change
#      quietly moved it onto the v1 ABI, the compatibility rule would be gone
#      and nothing else in the repository would notice.
#   2. The accepted v1 contract refuses that artifact before instantiation, and
#      accepts its own conforming fixture in the same run, so the refusal is
#      discrimination rather than a validator that says no to everything.
#   3. Selection is fail-closed. A profile name the toolchain cannot emit is
#      refused with no artifact, never downgraded to a binding the caller did
#      not name.
#   4. Source shape does not select an ABI. A `Text` source built for `wasm32`
#      is refused, not silently rebound to a different contract.
#
# Property 2's reserved-name branch is written for both sides of the migration:
# before #1001 the name is refused, after #1001 whatever it emits must satisfy
# the v1 contract. There is no third outcome, and the gate fails on one.

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
HERE="$ROOT/spec/wasm-host-profile-v1"
WORK=${KOFUN_WASM_HOST_PROFILE_WORK:-"$ROOT/build/wasm-host-profile"}
HOSTABI="$ROOT/spec/wasm-host-abi-v1/hostabi.mjs"
SURFACE="$HERE/surface.mjs"
SPEC="$ROOT/spec/wasm-host-profile-v1.md"
ABI_SPEC="$ROOT/spec/wasm-host-abi-v1.md"
TARGET_README="$ROOT/bootstrap/wasm/README.md"
SAMPLE="$ROOT/examples/wasm_arithmetic.kofun"
TEXT_SOURCE="$ROOT/bootstrap/wasm/fixtures/unsupported_text.kofun"

# The three selectors this document is about: the shipped one, the one it
# reserves for `kofun-wasm-host-abi-v1`, and a revision that does not exist and
# cannot, since the contract's revisions start at 1.
LEGACY_TARGET=wasm32
PROFILE_TARGET=wasm32-hostabi1
ABSENT_TARGET=wasm32-hostabi0

ASSERT_CONTEXT='wasm32 host profile'
. "$ROOT/tests/assertions/assert.sh"

command -v node >/dev/null 2>&1 ||
    {
        printf '%s\n' 'FAIL: wasm32 host profile: node is required to read a module surface' >&2
        exit 1
    }

rm -rf "$WORK"
mkdir -p "$WORK"

node --check "$SURFACE"

# 1. `--target wasm32` still emits the legacy binding.
#
# The engine's own view of the artifact, taken by compiling it and never
# instantiating it. Three entries, and every one of them names the earlier host
# binding rather than the v1 one.
"$ROOT/bin/kofun" build "$SAMPLE" \
    --target "$LEGACY_TARGET" -o "$WORK/legacy.wasm" >"$WORK/legacy.build.stdout"
assert_regular_file "the $LEGACY_TARGET artifact" "$WORK/legacy.wasm"
node "$SURFACE" "$WORK/legacy.wasm" >"$WORK/legacy.surface"
assert_num "entries in the $LEGACY_TARGET module surface" \
    "$(wc -l <"$WORK/legacy.surface")" -eq 3
assert_grep "the legacy print import" \
    -Fxq 'import kofun.print_i64 function' "$WORK/legacy.surface"
assert_grep "the legacy panic import" \
    -Fxq 'import kofun.panic function' "$WORK/legacy.surface"
assert_grep "the legacy entry point" \
    -Fxq 'export main function' "$WORK/legacy.surface"
assert_not_grep "the v1 import namespace in the $LEGACY_TARGET module" \
    -Fq 'kofun:host-abi-v1' "$WORK/legacy.surface"
assert_not_grep "a v1 export in the $LEGACY_TARGET module" \
    -Eq '^export (memory|kofun_abi_version|kofun_start|kofun_alloc) ' \
    "$WORK/legacy.surface"

# 2. The accepted contract refuses that artifact, before anything is linked.
#
# `hostabi.mjs module` is phase 1 of `spec/wasm-host-abi-v1.md` pointed at
# bytes its own fixtures did not assemble. A refusal writes no report: a
# partial verdict cannot be told apart from a complete one.
if node "$HOSTABI" module "$WORK/legacy.wasm" \
    >"$WORK/legacy.verdict" 2>"$WORK/legacy.verdict.err"
then
    assert_fail "the v1 contract accepted the $LEGACY_TARGET module; the two bindings are not disjoint"
fi
assert_file_empty "the v1 verdict on the $LEGACY_TARGET module" "$WORK/legacy.verdict"
assert_num "lines in the $LEGACY_TARGET refusal" \
    "$(wc -l <"$WORK/legacy.verdict.err")" -eq 1
assert_grep "the $LEGACY_TARGET refusal diagnostic" \
    -q '^wasm-host-abi: abi-version-mismatch: ' "$WORK/legacy.verdict.err"

# The same validator, on a module that does conform. Without this the refusal
# above would also pass for a checker that had stopped reading the bytes.
node "$HOSTABI" case "$ROOT/spec/wasm-host-abi-v1/cases/conforming.json" \
    >"$WORK/conforming.json"
assert_grep "the conforming fixture verdict" \
    -q '"contract": "accepted"' "$WORK/conforming.json"
assert_grep "guest execution while identifying the conforming module" \
    -q '"guest_ran": false' "$WORK/conforming.json"

# 3. Selection is fail-closed, and the reserved name is correct at either stage
#    of the migration.
rm -f "$WORK/profile.wasm"
set +e
"$ROOT/bin/kofun" build "$SAMPLE" \
    --target "$PROFILE_TARGET" -o "$WORK/profile.wasm" \
    >"$WORK/profile.stdout" 2>"$WORK/profile.stderr"
profile_status=$?
set -e
if test "$profile_status" -eq 0
then
    # #1001 or later has landed. Whatever the selector emits must be v1, or the
    # name means something other than what this document reserved it for.
    assert_regular_file "the $PROFILE_TARGET artifact" "$WORK/profile.wasm"
    node "$HOSTABI" module "$WORK/profile.wasm" >"$WORK/profile.verdict"
    assert_grep "the $PROFILE_TARGET verdict" \
        -q '"contract": "accepted"' "$WORK/profile.verdict"
    assert_grep "guest execution while identifying the $PROFILE_TARGET artifact" \
        -q '"guest_ran": false' "$WORK/profile.verdict"
    profile_state="emitted, and accepted by kofun-wasm-host-abi-v1"
else
    assert_num "the $PROFILE_TARGET build status" "$profile_status" -eq 2
    assert_absent "the refused $PROFILE_TARGET artifact" "$WORK/profile.wasm"
    assert_file_empty "stdout for the refused $PROFILE_TARGET build" "$WORK/profile.stdout"
    assert_grep "the $PROFILE_TARGET refusal" \
        -Fq "kofun: unsupported target: $PROFILE_TARGET" "$WORK/profile.stderr"
    profile_state="reserved and refused; no backend emits it yet"
fi

rm -f "$WORK/absent.wasm"
set +e
"$ROOT/bin/kofun" build "$SAMPLE" \
    --target "$ABSENT_TARGET" -o "$WORK/absent.wasm" \
    >"$WORK/absent.stdout" 2>"$WORK/absent.stderr"
absent_status=$?
set -e
assert_num "the $ABSENT_TARGET build status" "$absent_status" -eq 2
assert_absent "the refused $ABSENT_TARGET artifact" "$WORK/absent.wasm"
assert_file_empty "stdout for the refused $ABSENT_TARGET build" "$WORK/absent.stdout"
assert_grep "the $ABSENT_TARGET refusal" \
    -Fq "kofun: unsupported target: $ABSENT_TARGET" "$WORK/absent.stderr"

# 4. Source shape does not select an ABI. A source the numeric binding cannot
#    lower is refused with no artifact, rather than rebound to the contract
#    that does describe `Text`.
rm -f "$WORK/text-source.wasm"
set +e
"$ROOT/bin/kofun" build "$TEXT_SOURCE" \
    --target "$LEGACY_TARGET" -o "$WORK/text-source.wasm" \
    >"$WORK/text-source.stdout" 2>"$WORK/text-source.stderr"
text_status=$?
set -e
assert_num "the Text build status" "$text_status" -eq 1
assert_absent "the refused Text artifact" "$WORK/text-source.wasm"
assert_file_empty "stdout for the refused Text build" "$WORK/text-source.stdout"
assert_grep "the Text refusal" \
    -Fq 'unsupported token in wasm32 arithmetic Core' "$WORK/text-source.stderr"

# 5. Activation cannot drift back into prose. The decision names one selector,
#    and the two documents a reader arrives from must name it too.
assert_grep "the selector in $SPEC" -Fq "$PROFILE_TARGET" "$SPEC"
assert_grep "the selector in bootstrap/wasm/README.md" -Fq "$PROFILE_TARGET" "$TARGET_README"
assert_grep "the selector in spec/wasm-host-abi-v1.md" -Fq "$PROFILE_TARGET" "$ABI_SPEC"
assert_grep "the legacy binding in $SPEC" -Fq -- "--target $LEGACY_TARGET" "$SPEC"
assert_grep "the recorded alternatives in $SPEC" -Fq 'Rejected alternatives' "$SPEC"
assert_grep "the semantic oracle in $SPEC" -Fq 'tests/conformance/capabilities.tsv' "$SPEC"
assert_grep "the byte oracle in $SPEC" -Fq 'spec/wasm-host-abi-v1/check.sh' "$SPEC"

printf '%s\n' \
    "PASS: --target $LEGACY_TARGET still emits the legacy kofun.print_i64/main binding" \
    'PASS: the v1 contract refuses that module with abi-version-mismatch and accepts its conforming fixture' \
    "PASS: $PROFILE_TARGET is $profile_state" \
    "PASS: $ABSENT_TARGET and a Text source were refused without an artifact"
