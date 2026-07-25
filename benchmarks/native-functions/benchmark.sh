#!/usr/bin/env sh
set -eu

# Reproducible measurement of the direct x86-64 backend's user-defined function
# profile. Every workload is compiled by the producer under test, executed, and
# checked against its expected output before any sample is taken, so a timing
# number can never come from a program that computes the wrong answer.
#
# With BASELINE set to a git revision, the same workloads are also built by that
# revision's `bootstrap/native/core_compiler.c` and the recorded budgets are
# enforced: every workload named in IMPROVE must reduce its median by the
# percentage declared there, no workload may regress by more than 5%, and
# neither corpus compile time nor emitted text size may regress by more than
# 10%.
#
# IMPROVE is what a revision claims, so it belongs to the pair (revision,
# BASELINE) being compared and is expected to change when either does. Its
# default is the claim of the revision that last recorded results.json; the
# claim of an earlier revision is reproduced by naming that revision's BASELINE
# and its IMPROVE together, both of which README.md keeps.

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BENCH="$ROOT/benchmarks/native-functions"
WORK=${KOFUN_NATIVE_BENCH_WORK:-"$ROOT/build/native-functions-benchmark"}
CC=${CC:-cc}
CFLAGS_COMMON="-std=c11 -O2 -Wall -Wextra -Werror"
REFERENCE_CFLAGS=${REFERENCE_CFLAGS:--O3}
SAMPLES=${SAMPLES:-11}
BASELINE=${BASELINE:-}
IMPROVE=${IMPROVE:-"tail_sum30000:60 tail_mutual30000:60"}

WORKLOADS="fib35 mutual_fib32 six_argument_fib30
tail_sum30000 tail_mutual30000"
CORPUS="tests/conformance/functions/arguments_and_forward_reference.kofun
tests/conformance/functions/branch_join.kofun
tests/conformance/functions/mutual_recursion.kofun
tests/conformance/functions/recursion.kofun
tests/conformance/functions/register_pressure.kofun
tests/conformance/functions/signed_and_zero.kofun
tests/conformance/functions/six_arguments.kofun
tests/conformance/functions/tail_position_accumulators.kofun
tests/conformance/functions/tail_position_boundary.kofun
tests/conformance/functions/tail_position_guards.kofun
tests/conformance/functions/values_across_calls.kofun
examples/fibonacci_native.kofun
benchmarks/native-functions/fib35.kofun
benchmarks/native-functions/mutual_fib32.kofun
benchmarks/native-functions/six_argument_fib30.kofun
benchmarks/native-functions/tail_sum30000.kofun
benchmarks/native-functions/tail_mutual30000.kofun"

test "$(uname -s)" = Linux || {
    printf '%s\n' "native function benchmark requires Linux" >&2
    exit 1
}
test "$(uname -m)" = x86_64 || {
    printf '%s\n' \
        "native function benchmark measures the x86-64 backend natively" >&2
    exit 1
}
test "$SAMPLES" -ge 5 || {
    printf '%s\n' "native function benchmark requires at least 5 samples" >&2
    exit 2
}
for tool in "$CC" git sort sed awk od; do
    command -v "$tool" >/dev/null 2>&1 || {
        printf '%s\n' "native function benchmark requires $tool" >&2
        exit 1
    }
done

expected_output() {
    case $1 in
        fib35) printf '%s\n' 9227465 ;;
        mutual_fib32) printf '%s\n' 2178309 ;;
        six_argument_fib30) printf '%s\n' 832040 ;;
        tail_sum30000) printf '%s\n' 157505250000 ;;
        tail_mutual30000) printf '%s\n' 10500000 ;;
        *)
            printf '%s\n' "native function benchmark: unknown workload $1" >&2
            exit 1
            ;;
    esac
}

# p_filesz of the first PT_LOAD program header: the emitted code, without the
# page-aligned data segment that makes every image the same file size.
text_bytes() {
    od -An -v -tu8 -j 96 -N 8 "$1" | awk '{ print $1 }'
}

middle=$((SAMPLES / 2 + 1))

median_of() {
    sort -n "$WORK/$1.samples-us" | sed -n "${middle}p"
}

lowest_of() {
    sort -n "$WORK/$1.samples-us" | sed -n '1p'
}

highest_of() {
    sort -n "$WORK/$1.samples-us" | sed -n "${SAMPLES}p"
}

samples_of() {
    sort -n "$WORK/$1.samples-us" |
        awk '{ printf "%s%s", separator, $0; separator="," } END { print "" }'
}

change_percent() {
    awk -v from="$1" -v to="$2" \
        'BEGIN { printf "%.2f", (to - from) * 100 / from }'
}

# How much of the baseline median the current median removes.
reduction_percent() {
    awk -v from="$1" -v to="$2" \
        'BEGIN { printf "%.2f", (from - to) * 100 / from }'
}

ratio() {
    awk -v left="$1" -v right="$2" \
        'BEGIN { printf "%.3f", left / right }'
}

at_least() {
    awk -v value="$1" -v bound="$2" 'BEGIN { exit !(value >= bound) }'
}

at_most() {
    awk -v value="$1" -v bound="$2" 'BEGIN { exit !(value <= bound) }'
}

rm -rf "$WORK"
mkdir -p "$WORK"

# shellcheck disable=SC2086
$CC $CFLAGS_COMMON "$BENCH/cpu_samples.c" -o "$WORK/cpu-samples"
# shellcheck disable=SC2086
$CC $CFLAGS_COMMON "$ROOT/bootstrap/native/core_compiler.c" \
    -o "$WORK/producer-current"
# shellcheck disable=SC2086
$CC $REFERENCE_CFLAGS -std=c11 -Wall -Wextra -Werror \
    "$BENCH/fib35.c" -o "$WORK/reference-fib35"

VARIANTS=current
if test -n "$BASELINE"; then
    git -C "$ROOT" show "$BASELINE:bootstrap/native/core_compiler.c" \
        >"$WORK/baseline_core_compiler.c"
    # shellcheck disable=SC2086
    $CC $CFLAGS_COMMON "$WORK/baseline_core_compiler.c" \
        -o "$WORK/producer-baseline"
    VARIANTS="current baseline"
fi

for variant in $VARIANTS; do
    for stem in $WORKLOADS; do
        image="$WORK/$variant-$stem.elf"
        "$WORK/producer-$variant" \
            "$BENCH/$stem.kofun" x86_64-linux "$image"
        chmod +x "$image"
        observed=$("$image")
        test "$observed" = "$(expected_output "$stem")" || {
            printf '%s\n' \
                "native function benchmark: $variant $stem printed" \
                "$observed instead of $(expected_output "$stem")" >&2
            exit 1
        }
    done

    driver="$WORK/compile-corpus-$variant"
    {
        printf '%s\n' '#!/usr/bin/env sh' 'set -eu'
        printf '%s\n' "$CORPUS" | while IFS= read -r source; do
            test -n "$source" || continue
            printf '"%s" "%s" x86_64-linux "%s"\n' \
                "$WORK/producer-$variant" \
                "$ROOT/$source" \
                "$WORK/corpus-$variant.elf"
        done
    } >"$driver"
    chmod +x "$driver"
    "$driver"
done

collect() {
    label=$1
    shift
    "$WORK/cpu-samples" 1 "$@" >>"$WORK/$label.samples-us"
}

for variant in $VARIANTS; do
    for stem in $WORKLOADS; do
        : >"$WORK/$variant-$stem.samples-us"
    done
    : >"$WORK/compile-$variant.samples-us"
done
: >"$WORK/reference-fib35.samples-us"

# Samples are interleaved round by round so machine drift lands on every
# variant instead of on whichever one ran last.
round=1
while test "$round" -le "$SAMPLES"; do
    for variant in $VARIANTS; do
        for stem in $WORKLOADS; do
            collect "$variant-$stem" "$WORK/$variant-$stem.elf"
        done
        collect "compile-$variant" "$WORK/compile-corpus-$variant"
    done
    collect reference-fib35 "$WORK/reference-fib35"
    round=$((round + 1))
done

printf '%s\n' \
    "schema=kofun.native-function-benchmark/v1" \
    "implementation_commit=$(git -C "$ROOT" rev-parse HEAD)" \
    "baseline_revision=${BASELINE:-none}" \
    "host_kernel=$(uname -srvm)" \
    "compiler_version=$("$CC" --version | sed -n '1p')" \
    "producer_flags=$CFLAGS_COMMON" \
    "reference_flags=$REFERENCE_CFLAGS -std=c11" \
    "producer_sha256=$(
        sha256sum "$ROOT/bootstrap/native/core_compiler.c" |
            awk '{ print $1 }'
    )" \
    "samples=$SAMPLES" \
    "warmup_runs_per_sample=1" \
    "summary_statistic=median process CPU microseconds"

for stem in $WORKLOADS; do
    printf '%s\n' \
        "workload=$stem" \
        "  source_sha256=$(
            sha256sum "$BENCH/$stem.kofun" | awk '{ print $1 }'
        )"
    for variant in $VARIANTS; do
        printf '%s\n' \
            "  ${variant}_samples_us=$(samples_of "$variant-$stem")" \
            "  ${variant}_median_us=$(median_of "$variant-$stem")" \
            "  ${variant}_lowest_us=$(lowest_of "$variant-$stem")" \
            "  ${variant}_highest_us=$(highest_of "$variant-$stem")" \
            "  ${variant}_text_bytes=$(text_bytes "$WORK/$variant-$stem.elf")"
    done
    if test -n "$BASELINE"; then
        printf '%s\n' \
            "  median_change_percent=$(
                change_percent \
                    "$(median_of "baseline-$stem")" \
                    "$(median_of "current-$stem")"
            )" \
            "  text_bytes_change_percent=$(
                change_percent \
                    "$(text_bytes "$WORK/baseline-$stem.elf")" \
                    "$(text_bytes "$WORK/current-$stem.elf")"
            )"
    fi
done

printf '%s\n' \
    "reference_fib35_samples_us=$(samples_of reference-fib35)" \
    "reference_fib35_median_us=$(median_of reference-fib35)" \
    "current_fib35_to_reference_ratio=$(
        ratio "$(median_of current-fib35)" "$(median_of reference-fib35)"
    )"

for variant in $VARIANTS; do
    printf '%s\n' \
        "${variant}_corpus_compile_samples_us=$(samples_of "compile-$variant")" \
        "${variant}_corpus_compile_median_us=$(median_of "compile-$variant")"
done

test -n "$BASELINE" || {
    printf '%s\n' \
        "budgets=not evaluated (set BASELINE to a git revision to enforce them)"
    exit 0
}

for claim in $IMPROVE; do
    claim_stem=${claim%%:*}
    claim_percent=${claim##*:}
    test "$claim_stem" != "$claim" || {
        printf '%s\n' \
            "native function benchmark: IMPROVE entry needs stem:percent: $claim" \
            >&2
        exit 2
    }
    claim_measured=false
    for stem in $WORKLOADS; do
        test "$stem" != "$claim_stem" || claim_measured=true
    done
    test "$claim_measured" = true || {
        printf '%s\n' \
            "native function benchmark: IMPROVE names unmeasured workload $claim_stem" \
            >&2
        exit 2
    }
    improvement=$(
        reduction_percent \
            "$(median_of "baseline-$claim_stem")" \
            "$(median_of "current-$claim_stem")"
    )
    printf '%s\n' "${claim_stem}_median_improvement_percent=$improvement"
    at_least "$improvement" "$claim_percent" || {
        printf '%s\n' \
            "native function benchmark: $claim_stem improved $improvement%, needs $claim_percent%" \
            >&2
        exit 1
    }
done

for stem in $WORKLOADS; do
    regression=$(
        change_percent "$(median_of "baseline-$stem")" \
            "$(median_of "current-$stem")"
    )
    at_most "$regression" 5 || {
        printf '%s\n' \
            "native function benchmark: $stem regressed $regression%" >&2
        exit 1
    }
    size_change=$(
        change_percent \
            "$(text_bytes "$WORK/baseline-$stem.elf")" \
            "$(text_bytes "$WORK/current-$stem.elf")"
    )
    at_most "$size_change" 10 || {
        printf '%s\n' \
            "native function benchmark: $stem text grew $size_change%" >&2
        exit 1
    }
done

compile_change=$(
    change_percent "$(median_of compile-baseline)" "$(median_of compile-current)"
)
printf '%s\n' "corpus_compile_change_percent=$compile_change"
at_most "$compile_change" 10 || {
    printf '%s\n' \
        "native function benchmark: corpus compile time grew $compile_change%" \
        >&2
    exit 1
}

printf '%s\n' "budgets=all recorded budgets satisfied"
