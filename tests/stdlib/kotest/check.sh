#!/bin/sh
# Gate for the kotest unit-test framework, its runner, and the executable
# stdlib samples.  Unlike the projection gates, every assertion here is
# about sources that actually compile and run: the framework library, the
# Go-style sample/test companions under examples/stdlib/, and the runner's
# failure and filter behaviour.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
RUNNER="$ROOT/tooling/kotest/run.sh"
KOTEST_LIB="$ROOT/stdlib/testing/kotest.kofun"
SAMPLES="$ROOT/examples/stdlib"
WORK=$(mktemp -d "${TMPDIR:-/tmp}/kofun-kotest-gate.XXXXXX")
trap 'rm -rf "$WORK"' 0 1 2 15

fail() {
    printf 'kotest gate: FAIL: %s\n' "$*" >&2
    exit 1
}

test -f "$RUNNER" || fail 'runner tooling/kotest/run.sh is missing'
test -f "$KOTEST_LIB" || fail 'framework stdlib/testing/kotest.kofun is missing'

if find "$ROOT/tooling/kotest" "$SAMPLES" -type f \
    \( -name '*.py' -o -name '*.kf' \) | grep -q .
then
    fail 'forbidden Python or .kf source found'
fi

# ---------------------------------------------------------------- framework
# The framework must contain no trusted declarations and no main of its own.
grep -q 'trusted' "$KOTEST_LIB" && fail 'kotest.kofun contains a trusted declaration'
grep -q '^fn main(' "$KOTEST_LIB" && fail 'kotest.kofun must not define fn main'
for assertion in \
    expect_eq_int expect_ne_int expect_lt_int expect_le_int expect_gt_int \
    expect_ge_int expect_between_int expect_that_int expect_eq_text \
    expect_ne_text expect_text_len expect_text_starts_with \
    expect_text_contains fail_now assert_equals_int assert_equals_text \
    spy_init spy_record spy_count spy_last verify_called_times \
    verify_last_argument stub_unreachable kotest_summary kotest_selfcheck
do
    grep -q "^fn $assertion(" "$KOTEST_LIB" ||
        fail "framework function is missing: $assertion"
done

# ------------------------------------------------- samples: every module
# Each stdlib sample is a Go-style companion pair: X_sample.kofun runs on
# its own (the examples gate owns the golden), X_sample_test.kofun is the
# kotest suite over the same functions.  This gate owns exactly these
# suites (examples/README.md binds each row here by name):
#   array_sample_test.kofun        binary_heap_sample_test.kofun
#   clock_sample_test.kofun        csv_sample_test.kofun
#   date_time_sample_test.kofun    decimal_sample_test.kofun
#   json_sample_test.kofun         list_sample_test.kofun
#   logging_sample_test.kofun      map_sample_test.kofun
#   random_sample_test.kofun       regex_sample_test.kofun
#   set_sample_test.kofun          testing_sample_test.kofun
#   toml_sample_test.kofun         tuple_sample_test.kofun
#   vector_sample_test.kofun
modules='array binary_heap clock csv date_time decimal json list logging
map random regex set testing toml tuple vector'
for module in $modules; do
    test -f "$SAMPLES/${module}_sample.kofun" ||
        fail "missing sample: ${module}_sample.kofun"
    test -f "$SAMPLES/${module}_sample_test.kofun" ||
        fail "missing suite: ${module}_sample_test.kofun"
    test -f "$SAMPLES/${module}_sample.expected" ||
        fail "missing golden: ${module}_sample.expected"
    grep -q '^fn test_' "$SAMPLES/${module}_sample_test.kofun" ||
        fail "suite has no tests: ${module}_sample_test.kofun"
done

# ---------------------------------------------------------- passing sweep
# The full suite must pass: samples plus the framework's own suite.
if ! sh "$RUNNER" "$SAMPLES" "$ROOT/stdlib/testing" --no-color \
    >"$WORK/pass.out" 2>"$WORK/pass.err"; then
    cat "$WORK/pass.out" "$WORK/pass.err" >&2
    fail 'passing sweep exited nonzero'
fi
grep -q 'KOTEST-ASSERT-FAIL kotest' "$WORK/pass.out" &&
    fail 'framework selfcheck failed inside the passing sweep'
grep -Eq 'Tests  [0-9]+ passed \([0-9]+ total, [0-9]+ suites\)' "$WORK/pass.out" ||
    fail 'passing sweep did not print the green summary'
grep -q '✗' "$WORK/pass.out" && fail 'passing sweep contains a failed test'
total=$(sed -n 's/^Tests  \([0-9][0-9]*\) passed.*/\1/p' "$WORK/pass.out")
test "$total" -ge 100 ||
    fail "passing sweep ran only $total tests; the samples are not all wired"

# --------------------------------------------------------- failing fixture
# The deliberately failing suite must exit 1 and name its failures; a
# harness that swallows red is worse than no harness.
set +e
sh "$RUNNER" "$ROOT/tests/stdlib/kotest/fixtures/failing_test.kofun" \
    --no-color >"$WORK/red.out" 2>"$WORK/red.err"
red_status=$?
set -e
test "$red_status" -eq 1 ||
    fail "failing fixture exited $red_status instead of 1"
grep -q '✗ failing_test.test_equality_that_fails' "$WORK/red.out" ||
    fail 'failing fixture did not mark the failed test'
grep -q 'KOTEST-ASSERT-FAIL expect_eq_int' "$WORK/red.out" ||
    fail 'failing fixture lost the assertion diagnostic'
grep -q '✓ failing_test.test_that_passes_beside_failures' "$WORK/red.out" ||
    fail 'failing fixture did not keep running after a failure'
grep -q 'Tests  2 failed | 1 passed (3 total, 1 suites)' "$WORK/red.out" ||
    fail 'failing fixture summary is wrong'

# ------------------------------------------------------------------ filter
sh "$RUNNER" "$SAMPLES/list_sample_test.kofun" --filter test_fold \
    --no-color >"$WORK/filter.out" 2>&1 ||
    fail 'filtered run exited nonzero'
grep -q 'Tests  1 passed (1 total, 1 suites)' "$WORK/filter.out" ||
    fail '--filter did not narrow to one test'

# -------------------------------------------------------------------- list
sh "$RUNNER" "$SAMPLES/list_sample_test.kofun" --list \
    >"$WORK/list.out" 2>&1 || fail '--list exited nonzero'
grep -q 'list_sample_test.test_push_appends_in_order' "$WORK/list.out" ||
    fail '--list did not enumerate tests'

printf 'kotest framework and runner behaviour: PASS\n'
printf 'kotest failing-suite detection and exit codes: PASS\n'
printf 'kotest stdlib sample suites (%s tests): PASS\n' "$total"
