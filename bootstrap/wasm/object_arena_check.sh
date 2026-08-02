#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
WORK=${KOFUN_WASM_ARENA_CHECK_WORK:-"$ROOT/build/wasm-object-arena"}
CC=${CC:-cc}
ASSERT_CONTEXT='wasm32 object arena'
. "$ROOT/tests/assertions/assert.sh"

for tool in "$CC" node cmp sha256sum
do
    command -v "$tool" >/dev/null 2>&1 || {
        printf '%s\n' "wasm32 object arena gate requires $tool" >&2
        exit 1
    }
done

rm -rf "$WORK"
mkdir -p "$WORK"

# Recompute the boundary vectors before comparing the production header
# encoder with them. A stale edited golden must not become an oracle.
sh "$ROOT/spec/wasm-host-abi-v1/check.sh" >"$WORK/host-abi-gate.stdout"

"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$ROOT/bootstrap/wasm/compiler.c" -o "$WORK/compiler"
"$CC" -std=c11 -O1 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$ROOT/bootstrap/wasm/compiler.c" -o "$WORK/compiler-sanitized"
"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$ROOT/bootstrap/wasm/object_header_probe.c" -o "$WORK/header-probe"

SOURCE="$ROOT/bootstrap/wasm/fixtures/hostabi1_empty.kofun"
"$WORK/compiler" --hostabi1 "$SOURCE" "$WORK/direct.wasm"
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
    "$WORK/compiler-sanitized" --hostabi1 "$SOURCE" "$WORK/sanitized.wasm"
cmp "$WORK/direct.wasm" "$WORK/sanitized.wasm"

# `wasm32` is a compatibility target, not an alias for the new profile. Pin
# the pre-#1001 sample bytes so a surface-compatible rewrite cannot pass while
# silently changing an existing module.
"$WORK/compiler" \
    "$ROOT/examples/wasm_arithmetic.kofun" "$WORK/legacy.wasm"
legacy_digest=$(sha256sum "$WORK/legacy.wasm" | cut -d ' ' -f 1)
assert_eq "legacy wasm32 sample digest" \
    "$legacy_digest" \
    ead99da7862aee50ec77099e16d8382cd5ef3b75920136c78734e788525856da

KOFUN_WASM_BUILD_DIR="$WORK/cli-compiler" \
    "$ROOT/bin/kofun" build "$SOURCE" \
    --target wasm32-hostabi1 -o "$WORK/cli.wasm" >"$WORK/cli.stdout"
KOFUN_WASM_BUILD_DIR="$WORK/cli-compiler" \
    "$ROOT/bin/kofun" build "$SOURCE" \
    --target wasm32-hostabi1 -o "$WORK/cli-second.wasm" >"$WORK/cli-second.stdout"
cmp "$WORK/direct.wasm" "$WORK/cli.wasm"
cmp "$WORK/cli.wasm" "$WORK/cli-second.wasm"

node "$ROOT/spec/wasm-host-abi-v1/hostabi.mjs" module "$WORK/cli.wasm" \
    >"$WORK/module-verdict.json"
assert_grep "v1 module verdict" -q '"contract": "accepted"' "$WORK/module-verdict.json"
assert_grep "no guest execution during identification" -q '"guest_ran": false' "$WORK/module-verdict.json"

node "$ROOT/bootstrap/wasm/object_arena_check.mjs" \
    "$WORK/cli.wasm" \
    "$ROOT/spec/wasm-host-abi-v1/vectors/boundary.wasm32.json" \
    "$WORK/header-probe"

# The empty source has the same empty observable semantics under the selected
# native oracle.  This is not a Text/List capability claim.
KOFUN_NATIVE_BUILD_DIR="$WORK/native-compiler" \
    "$ROOT/bin/kofun" build "$SOURCE" \
    --target x86_64-linux -o "$WORK/native" >"$WORK/native-build.stdout"
"$WORK/native" >"$WORK/native.stdout" 2>"$WORK/native.stderr"
assert_file_empty "native empty-program stdout" "$WORK/native.stdout"
assert_file_empty "native empty-program stderr" "$WORK/native.stderr"

# Non-empty source must be refused rather than silently compiled into the
# arena-only no-op entry point, and an existing artifact must be preserved.
cp "$WORK/cli.wasm" "$WORK/preserved.wasm"
set +e
KOFUN_WASM_BUILD_DIR="$WORK/cli-compiler" \
    "$ROOT/bin/kofun" build "$ROOT/examples/wasm_arithmetic.kofun" \
    --target wasm32-hostabi1 -o "$WORK/preserved.wasm" \
    >"$WORK/refused.stdout" 2>"$WORK/refused.stderr"
status=$?
set -e
assert_num "non-empty profile build status" "$status" -eq 1
cmp "$WORK/cli.wasm" "$WORK/preserved.wasm"
assert_file_empty "non-empty profile stdout" "$WORK/refused.stdout"
assert_grep "non-empty profile diagnostic" -Fxq \
    'kofun wasm32: wasm32-hostabi1 currently supports only an empty fn main() arena program' \
    "$WORK/refused.stderr"

set +e
KOFUN_WASM_BUILD_DIR="$WORK/cli-compiler" \
    "$ROOT/bin/kofun" build "$ROOT/examples/wasm_arithmetic.kofun" \
    --target wasm32-hostabi1 -o "$WORK/cold-refused.wasm" \
    >"$WORK/cold-refused.stdout" 2>"$WORK/cold-refused.stderr"
cold_status=$?
set -e
assert_num "cold non-empty profile build status" "$cold_status" -eq 1
assert_absent "cold refused profile artifact" "$WORK/cold-refused.wasm"
assert_file_empty "cold non-empty profile stdout" "$WORK/cold-refused.stdout"
assert_grep "cold non-empty profile diagnostic" -Fxq \
    'kofun wasm32: wasm32-hostabi1 currently supports only an empty fn main() arena program' \
    "$WORK/cold-refused.stderr"

printf '%s\n' \
    'PASS: empty-program semantics agree with native x86-64 and non-empty source fails without an artifact' \
    'PASS: legacy wasm32 sample bytes retain their pre-profile digest' \
    'PASS: repeated and sanitized wasm32-hostabi1 builds are byte-identical'
