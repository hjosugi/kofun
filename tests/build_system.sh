#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
KOFUN="$ROOT/bin/kofun"
FIXTURE="$ROOT/bootstrap/fixtures/answer.kofun"
WORK=${KOFUN_BUILD_SYSTEM_TEST_WORK:-"$ROOT/build/build-system-test"}

rm -rf "$WORK"
mkdir -p "$WORK/single"
cp "$FIXTURE" "$WORK/single/main.kofun"

# A source argument is always the zero-manifest fast path. Even an adjacent
# project manifest and a failing Frost spy must not be inspected or invoked.
printf '%s\n' \
    '[workspace]' \
    'default_targets = ["unused"]' \
    >"$WORK/single/kofun.toml"
printf '%s\n' \
    '#!/usr/bin/env sh' \
    'printf "%s\n" invoked >"$KOFUN_FROST_SPY_LOG"' \
    'exit 99' \
    >"$WORK/frost-spy"
chmod +x "$WORK/frost-spy"
(
    cd "$WORK/single"
    KOFUN_BUILD_DIR="$WORK/single-compiler" \
    KOFUN_STAGE2_BUILD_DIR="$WORK/single-compiler" \
    KOFUN_FROST="$WORK/frost-spy" \
    KOFUN_FROST_SPY_LOG="$WORK/frost-spy.log" \
        "$KOFUN" build main.kofun \
        -o "$WORK/single/main" \
        --emit-c "$WORK/single/main.c" >/dev/null
)
test "$("$WORK/single/main")" = 42
test ! -e "$WORK/frost-spy.log"

# Issue #19's <5 ms threshold is compiler-internal, non-Python work. It is not
# CLI wall time and deliberately excludes the external C compiler/link step.
# Take the child CPU-time median of eleven warm process measurements so one
# unusually fast sample cannot pass the gate and shared-host scheduler waits
# are not misreported as compiler work. exec startup and source/output system
# work remain included.
COMPILER="$WORK/single-compiler/kofun-stage2"
"$COMPILER" \
    "$WORK/single/main.kofun" \
    "$WORK/single/warm.c" \
    "$WORK/single/warm.ir" \
    "$WORK/single/warm.tokens" >/dev/null
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror \
    "$ROOT/tests/process_cpu_time.c" \
    -o "$WORK/process-cpu-time"
median_us=$(
    "$WORK/process-cpu-time" \
        "$COMPILER" \
        "$WORK/single/main.kofun" \
        "$WORK/single/measured.c" \
        "$WORK/single/measured.ir" \
        "$WORK/single/measured.tokens"
)
test "$median_us" -lt 5000 || {
    printf '%s\n' \
        "FAIL: compiler-internal non-Python CPU median was ${median_us}us (limit 5000us)" >&2
    exit 1
}

FROST=${KOFUN_FROST_TEST_BIN-}
FROST_REV=external
if test -z "$FROST"; then
    FROST_REPO=${KOFUN_FROST_TEST_SOURCE:-"$ROOT/../frost-build"}
    FROST_SOURCE=
    if test -d "$FROST_REPO/crates/frostbuild-cli"; then
        FROST_MAIN=$(git -C "$FROST_REPO" rev-parse origin/main)
        for candidate in "$FROST_REPO" "$ROOT"/../frost-build-*
        do
            test -d "$candidate/crates/frostbuild-cli" || continue
            test "$(git -C "$candidate" rev-parse HEAD)" = "$FROST_MAIN" ||
                continue
            git -C "$candidate" diff --quiet \
                "$FROST_MAIN" -- \
                Cargo.toml Cargo.lock crates || continue
            FROST_SOURCE=$candidate
            break
        done
    fi
    if test -n "$FROST_SOURCE"; then
        git -C "$FROST_SOURCE" merge-base --is-ancestor \
            7df61e3b6c3eef977910f8d8a7d2f67795ade830 \
            "$FROST_MAIN"
        cargo build --quiet --release \
            --manifest-path "$FROST_SOURCE/Cargo.toml" \
            -p frostbuild-cli
        target_dir=$(cargo metadata \
            --no-deps \
            --format-version 1 \
            --manifest-path "$FROST_SOURCE/Cargo.toml" |
            sed -n 's/.*"target_directory":"\([^"]*\)".*/\1/p')
        FROST=$target_dir/release/frost
        FROST_REV=$(git -C "$FROST_SOURCE" rev-parse --short "$FROST_MAIN")
    elif command -v frost >/dev/null 2>&1; then
        FROST=$(command -v frost)
    fi
fi

if test -z "$FROST" || test ! -x "$FROST"; then
    printf '%s\n' \
        "PASS: single-file path bypassed manifest and Frost" \
        "PASS: compiler-internal non-Python CPU median ${median_us}us < 5ms (CLI/cc wall excluded)" \
        "SKIP: real frost-build unavailable; set KOFUN_FROST_TEST_BIN"
    exit 0
fi
case $("$FROST" --version) in
    frost\ *) ;;
    *)
        printf '%s\n' "FAIL: integration engine is not frost-build: $FROST" >&2
        exit 1
        ;;
esac

mkdir -p "$WORK/project/src"
cp "$FIXTURE" "$WORK/project/src/alpha.kofun"
cp "$FIXTURE" "$WORK/project/src/beta.kofun"
printf '%s\n' \
    '[workspace]' \
    'default_targets = ["alpha", "beta"]' \
    '' \
    '[toolchain]' \
    'kofunc = "kofun"' \
    '' \
    '[target.alpha]' \
    'kind = "kofun_binary"' \
    'srcs = ["src/alpha.kofun"]' \
    '' \
    '[target.beta]' \
    'kind = "kofun_binary"' \
    'srcs = ["src/beta.kofun"]' \
    >"$WORK/project/kofun.toml"

project_build() (
    cd "$WORK/project"
    KOFUN_BUILD_DIR="$WORK/project-compiler" \
    KOFUN_STAGE2_BUILD_DIR="$WORK/project-stage2-compiler" \
    KOFUN_FROST="$FROST" \
        "$KOFUN" build --explain
)

project_build >"$WORK/initial.log" 2>&1
grep -q 'ran kofun:alpha' "$WORK/initial.log"
grep -q 'ran kofun:beta' "$WORK/initial.log"
grep -q '2 built' "$WORK/initial.log"

ENGINE="$WORK/project/.kofun/frost-workspace"
test "$("$ENGINE/.frost/bin/debug/alpha")" = 42
test "$("$ENGINE/.frost/bin/debug/beta")" = 42
test -s "$ENGINE/.frost/obj/debug/alpha/kofun.c"
test -s "$ENGINE/.frost/obj/debug/beta/kofun.c"

project_build >"$WORK/noop.log" 2>&1
grep -q 'up to date' "$WORK/noop.log"
if grep -q '  ran ' "$WORK/noop.log"; then
    printf '%s\n' "FAIL: no-op project build executed an action" >&2
    exit 1
fi

sed 's/\* 6/* 7/' \
    "$FIXTURE" >"$WORK/project/src/alpha.kofun"
project_build >"$WORK/incremental.log" 2>&1
grep -q 'ran kofun:alpha :: input changed: src/alpha.kofun' \
    "$WORK/incremental.log"
if grep -q 'ran kofun:beta' "$WORK/incremental.log"; then
    printf '%s\n' "FAIL: unchanged beta target rebuilt" >&2
    exit 1
fi
grep -q '1 built, 1 cached' "$WORK/incremental.log"
test "$("$ENGINE/.frost/bin/debug/alpha")" = 49
test "$("$ENGINE/.frost/bin/debug/beta")" = 42

# Remove both declared alpha outputs. The next invocation must restore them
# from Frost's content-addressed action cache without invoking Kofun again.
rm -f \
    "$ENGINE/.frost/bin/debug/alpha" \
    "$ENGINE/.frost/obj/debug/alpha/kofun.c"
project_build >"$WORK/cache-hit.log" 2>&1
grep -q 'cached kofun:alpha' "$WORK/cache-hit.log"
if grep -q 'ran kofun:alpha' "$WORK/cache-hit.log"; then
    printf '%s\n' "FAIL: CAS-restored alpha target recompiled" >&2
    exit 1
fi
test "$("$ENGINE/.frost/bin/debug/alpha")" = 49
test -s "$ENGINE/.frost/obj/debug/alpha/kofun.c"

printf '%s\n' \
    "PASS: single-file path bypassed manifest and Frost" \
    "PASS: compiler-internal non-Python CPU median ${median_us}us < 5ms (CLI/cc wall excluded)" \
    "PASS: kofun build used real frost-build $FROST_REV for initial, one-change, no-op, and CAS-hit builds"
