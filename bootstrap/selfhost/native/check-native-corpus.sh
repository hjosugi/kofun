#!/usr/bin/env sh
set -eu

# Self-host Core native parity.
#
# The frozen single-expression self-host Core program is lowered to a native
# executable two fully independent ways, and both must print the pinned golden:
#
#   1. Self-host C11 path: the compiler built from the frozen S (A1), produced
#      exactly as in bootstrap/selfhost/check-compiler-driver.sh via
#      `kofun-stage2 --selfhost-compile`, emits deterministic C11 that the
#      declared host cc links into a native binary.
#   2. Direct-native path: the audited x86-64/AArch64 ELF backend emits a
#      statically linked image with no assembler, linker, or cc.
#
# Honesty boundaries:
#   * This does NOT claim self-application (S compiling S). A1 compiles an
#     ordinary Core input, exactly like the driver gate.
#   * This does NOT add a direct-native dependency to the #271/#272 C11 fixed
#     point, which stays cc-based and native-independent. It is separate parity
#     evidence that the self-host Core reaches a real native binary through two
#     independent backends.

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT"
SELF="$ROOT/bootstrap/selfhost/native"
KOFUN="$ROOT/bin/kofun"
WORK=${KOFUN_SELFHOST_NATIVE_WORK:-"$ROOT/build/selfhost-native"}
CC=${CC:-cc}

fail() {
    printf '%s\n' "FAIL: selfhost native parity: $*" >&2
    exit 1
}

command -v readelf >/dev/null 2>&1 || fail "readelf is required"

AARCH64_RUNNER=${QEMU_AARCH64-}
if test -n "$AARCH64_RUNNER" && command -v "$AARCH64_RUNNER" >/dev/null 2>&1; then
    :
elif command -v qemu-aarch64 >/dev/null 2>&1; then
    AARCH64_RUNNER=$(command -v qemu-aarch64)
elif command -v qemu-aarch64-static >/dev/null 2>&1; then
    AARCH64_RUNNER=$(command -v qemu-aarch64-static)
else
    AARCH64_RUNNER=
fi

rm -rf "$WORK"
mkdir -p "$WORK"

CORE="$SELF/corpus_core.kofun"
GOLDEN="$SELF/corpus_core.stdout"

# The frozen S digest still matches the pinned profile.
profile_digest=$(awk -F '|' '$1 == "source_sha256" { print $2 }' \
    bootstrap/selfhost/profile.meta)
actual_digest=$(sha256sum bootstrap/stage1/compiler.kofun | awk '{ print $1 }')
test "$profile_digest" = "$actual_digest" ||
    fail "S digest differs from the frozen profile"

# Path 1: the compiler built from S (A1) lowers the Core to C11, then cc links.
"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    bootstrap/stage2/compiler.c -o "$WORK/kofun-stage2"
"$WORK/kofun-stage2" --selfhost-compile \
    bootstrap/stage1/compiler.kofun "$WORK/S.c" "$profile_digest" >/dev/null
cmp bootstrap/selfhost/driver/S.c "$WORK/S.c" ||
    fail "compiler-from-S differs from the checked-in driver evidence"
"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    -I unicode "$WORK/S.c" -o "$WORK/kofun-a1"

cp "$CORE" "$WORK/core.kofun"
(cd "$WORK" && ./kofun-a1 core.kofun a1.c >/dev/null)
"$CC" -std=c11 -O2 -Wall -Wextra -Werror "$WORK/a1.c" -o "$WORK/a1-core"
"$WORK/a1-core" >"$WORK/a1.stdout"
cmp "$GOLDEN" "$WORK/a1.stdout" ||
    fail "self-host C11 path output differs from the pinned golden"

# Path 2: the direct-native backend emits a deterministic static ELF per target.
for target in x86_64-linux aarch64-linux; do
    case $target in
        x86_64-linux)
            stem=core-x86_64
            machine='Advanced Micro Devices X86-64'
            ;;
        aarch64-linux)
            stem=core-aarch64
            machine='AArch64'
            ;;
    esac
    "$KOFUN" build "$CORE" --target "$target" -o "$WORK/$stem.elf" >/dev/null
    "$KOFUN" build "$CORE" --target "$target" -o "$WORK/$stem.second.elf" >/dev/null
    cmp "$WORK/$stem.elf" "$WORK/$stem.second.elf" ||
        fail "$target native image is not deterministic"
    test -s "$WORK/$stem.elf" || fail "$target native image is empty"
    readelf -h "$WORK/$stem.elf" >"$WORK/$stem.header.txt"
    grep -Eq 'Class:[[:space:]]+ELF64' "$WORK/$stem.header.txt" ||
        fail "$target native image is not ELF64"
    grep -Eq "Machine:[[:space:]]+$machine" "$WORK/$stem.header.txt" ||
        fail "$target native image reports the wrong machine"
done

# Path independence: the same relative source from two directories emits
# byte-identical images, so no absolute build path leaks into the artifact.
mkdir -p "$WORK/remap-a/nested" "$WORK/remap-b"
cp "$CORE" "$WORK/remap-a/nested/core.kofun"
cp "$CORE" "$WORK/remap-b/core.kofun"
(cd "$WORK/remap-a/nested" &&
    "$KOFUN" build core.kofun --target x86_64-linux -o out >/dev/null)
(cd "$WORK/remap-b" &&
    "$KOFUN" build core.kofun --target x86_64-linux -o out >/dev/null)
cmp "$WORK/remap-a/nested/out" "$WORK/remap-b/out" ||
    fail "native image depends on the build directory"

# Differential: both independent native binaries print the same pinned golden.
"$WORK/core-x86_64.elf" >"$WORK/native-x86_64.stdout"
cmp "$GOLDEN" "$WORK/native-x86_64.stdout" ||
    fail "direct-native x86-64 output differs from the pinned golden"
cmp "$WORK/a1.stdout" "$WORK/native-x86_64.stdout" ||
    fail "the self-host C11 and direct-native backends disagree on Core behavior"

if test -n "$AARCH64_RUNNER"; then
    "$AARCH64_RUNNER" "$WORK/core-aarch64.elf" >"$WORK/native-aarch64.stdout"
    cmp "$GOLDEN" "$WORK/native-aarch64.stdout" ||
        fail "direct-native AArch64 output differs under $AARCH64_RUNNER"
    printf '%s\n' "PASS: AArch64 native parity under $AARCH64_RUNNER"
else
    printf '%s\n' \
        "SKIP: AArch64 execution (no qemu-aarch64 runner); ELF64 machine verified"
fi

# Negative: the single-expression native Core is a bounded subset. It refuses
# the full 5-print self-host success corpus with a stable diagnostic and writes
# no image, while the C11 self-host path above accepts that same corpus.
set +e
"$KOFUN" build bootstrap/selfhost/driver/corpus_answer.kofun \
    --target x86_64-linux -o "$WORK/refused.elf" \
    >"$WORK/refused.stdout" 2>"$WORK/refused.stderr"
refuse_status=$?
set -e
test "$refuse_status" -eq 1 ||
    fail "native backend must refuse the out-of-Core corpus with exit 1"
test ! -e "$WORK/refused.elf" ||
    fail "a refused native build must not write an image"
grep -q 'unsupported Core' "$WORK/refused.stderr" ||
    fail "native refusal is missing its bounded diagnostic"

printf '%s\n' \
    "PASS: the frozen self-host Core lowers to identical native output via A1/C11 and direct-native x86-64" \
    "PASS: direct-native x86-64/AArch64 images are ELF64, deterministic, and path-independent" \
    "PASS: the single-expression native Core refuses the out-of-Core corpus and writes nothing"
