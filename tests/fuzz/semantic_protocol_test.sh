#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
RUNNER="$ROOT/tests/fuzz/semantic_runner.sh"
FIXTURE="$ROOT/tests/fuzz/fixtures/protocol-adapter.sh"
WORK=${KOFUN_SEMANTIC_PROTOCOL_TEST_WORK:-"$ROOT/build/semantic-protocol-test"}
ASSERT_CONTEXT='semantic protocol'
. "$ROOT/tests/assertions/assert.sh"

rm -rf "$WORK"
mkdir -p "$WORK/adapters" "$WORK/manifests" "$WORK/cases" "$WORK/failures"

for identity in \
    oracle-good \
    backend-good \
    backend-stdout \
    backend-stderr \
    backend-status \
    backend-capability \
    backend-omit \
    backend-crash \
    backend-timeout \
    backend-malformed \
    backend-unsupported-only
do
    ln -s "$FIXTURE" "$WORK/adapters/$identity"
done

source="$WORK/source.kofun"
printf '%s\n' 'fn main() { print(42) }' >"$source"
case_meta="$WORK/case.tsv"
printf '%s\t%s\n' \
    protocol kofun.semantic-case/v1 \
    family protocol-selftest \
    generator protocol-fixture-v1 \
    seed 668 \
    case-index 0 >"$case_meta"

write_manifest() {
    destination=$1
    shift
    {
        printf '%s\t%s\n' \
            protocol kofun.semantic-family/v1 \
            family protocol-selftest \
            generator protocol-fixture-v1 \
            scope normalized-observation-fixture-v1
        while test "$#" -gt 0; do
            role=$1
            identity=$2
            support=$3
            reason=$4
            adapter=$5
            shift 5
            printf 'participant\t%s\t%s\t%s\t%s\t%s\n' \
                "$role" "$identity" "$support" "$reason" "$adapter"
        done
    } >"$destination"
}

expect_failure() {
    name=$1
    needle=$2
    manifest=$3
    set +e
    KOFUN_SEMANTIC_TIMEOUT=1 \
        "$RUNNER" "$manifest" "$source" "$case_meta" \
        "$WORK/cases/$name" "$WORK/failures" "$name" \
        >"$WORK/$name.stdout" 2>"$WORK/$name.stderr"
    status=$?
    set -e
    test "$status" -eq 1 || {
        printf '%s\n' \
            "semantic protocol self-test: $name returned $status" >&2
        exit 1
    }
    grep -Fq "$needle" "$WORK/$name.stderr" || {
        printf '%s\n' \
            "semantic protocol self-test: $name did not report $needle" >&2
        sed 's/^/  /' "$WORK/$name.stderr" >&2
        exit 1
    }
    artifact="$WORK/failures/$name"
    test -f "$artifact/source.kofun" &&
        test -f "$artifact/case.tsv" &&
        test -f "$artifact/family.manifest" &&
        test -f "$artifact/failure.txt" &&
        test -f "$artifact/tools.tsv" &&
        test -d "$artifact/raw-observations" &&
        test -d "$artifact/results" &&
        test -x "$artifact/reproduce.sh" &&
        test -f "$artifact/reproducer.txt" || {
        printf '%s\n' \
            "semantic protocol self-test: incomplete artifact for $name" >&2
        exit 1
    }
    assert_grep "$artifact/case.tsv" -Fq 'seed	668' "$artifact/case.tsv"
    grep -Fq 'case-index	0' "$artifact/case.tsv"
}

manifest="$WORK/manifests/good.tsv"
write_manifest "$manifest" \
    oracle oracle-good supported - "$WORK/adapters/oracle-good" \
    backend backend-good supported - "$WORK/adapters/backend-good"
KOFUN_SEMANTIC_TIMEOUT=1 \
    "$RUNNER" "$manifest" "$source" "$case_meta" \
    "$WORK/cases/good" "$WORK/failures" good

manifest="$WORK/manifests/explicit-unsupported.tsv"
write_manifest "$manifest" \
    oracle oracle-good supported - "$WORK/adapters/oracle-good" \
    backend backend-good supported - "$WORK/adapters/backend-good" \
    backend backend-unsupported-only unsupported fixture-unsupported \
    "$WORK/adapters/backend-unsupported-only"
KOFUN_SEMANTIC_TIMEOUT=1 \
    "$RUNNER" "$manifest" "$source" "$case_meta" \
    "$WORK/cases/explicit-unsupported" "$WORK/failures" explicit-unsupported
assert_regular_file "cases/explicit-unsupported/runs/backend-unsupported-only/capability.stdout" \
    "$WORK/cases/explicit-unsupported/runs/backend-unsupported-only/capability.stdout"
assert_absent "cases/explicit-unsupported/runs/backend-unsupported-only/run.status" \
    "$WORK/cases/explicit-unsupported/runs/backend-unsupported-only/run.status"
assert_absent "cases/explicit-unsupported/results/backend-unsupported-only" \
    "$WORK/cases/explicit-unsupported/results/backend-unsupported-only"

for mismatch in stdout stderr status
do
    manifest="$WORK/manifests/$mismatch.tsv"
    write_manifest "$manifest" \
        oracle oracle-good supported - "$WORK/adapters/oracle-good" \
        backend "backend-$mismatch" supported - \
        "$WORK/adapters/backend-$mismatch"
    case $mismatch in
        stdout) needle='stdout mismatch' ;;
        stderr) needle='stderr mismatch' ;;
        status) needle='exit status mismatch' ;;
    esac
    expect_failure "$mismatch" "$needle" "$manifest"
done

manifest="$WORK/manifests/capability.tsv"
write_manifest "$manifest" \
    oracle oracle-good supported - "$WORK/adapters/oracle-good" \
    backend backend-capability supported - "$WORK/adapters/backend-capability"
expect_failure capability 'capability mismatch' "$manifest"

manifest="$WORK/manifests/omission.tsv"
write_manifest "$manifest" \
    oracle oracle-good supported - "$WORK/adapters/oracle-good" \
    backend backend-omit supported - "$WORK/adapters/backend-omit"
expect_failure omission 'missing or malformed adapter result' "$manifest"

manifest="$WORK/manifests/crash.tsv"
write_manifest "$manifest" \
    oracle oracle-good supported - "$WORK/adapters/oracle-good" \
    backend backend-crash supported - "$WORK/adapters/backend-crash"
expect_failure crash 'adapter crashed' "$manifest"

manifest="$WORK/manifests/timeout.tsv"
write_manifest "$manifest" \
    oracle oracle-good supported - "$WORK/adapters/oracle-good" \
    backend backend-timeout supported - "$WORK/adapters/backend-timeout"
expect_failure timeout 'adapter timed out' "$manifest"

manifest="$WORK/manifests/malformed.tsv"
write_manifest "$manifest" \
    oracle oracle-good supported - "$WORK/adapters/oracle-good" \
    backend backend-malformed supported - "$WORK/adapters/backend-malformed"
expect_failure malformed 'missing or malformed adapter result' "$manifest"

manifest="$WORK/manifests/missing-oracle.tsv"
write_manifest "$manifest" \
    backend backend-good supported - "$WORK/adapters/backend-good"
expect_failure missing-oracle 'missing or ambiguous accepted oracle' "$manifest"

manifest="$WORK/manifests/missing-backend.tsv"
write_manifest "$manifest" \
    oracle oracle-good supported - "$WORK/adapters/oracle-good" \
    backend backend-missing supported - "$WORK/adapters/backend-missing"
expect_failure missing-backend 'missing backend or oracle adapter' "$manifest"

manifest="$WORK/manifests/unsupported-only.tsv"
write_manifest "$manifest" \
    oracle oracle-good supported - "$WORK/adapters/oracle-good" \
    backend backend-unsupported-only unsupported fixture-unsupported \
    "$WORK/adapters/backend-unsupported-only"
expect_failure unsupported-only 'missing supported backend implementation' "$manifest"

set +e
"$WORK/failures/stdout/reproduce.sh" \
    >"$WORK/replay.stdout" 2>"$WORK/replay.stderr"
replay_status=$?
set -e
assert_num "replay status" "$replay_status" -eq 1
assert_grep "replay.stderr" -Fq 'stdout mismatch' "$WORK/replay.stderr"

printf '%s\n' \
    'PASS: semantic protocol rejected observation, capability, omission, crash, timeout, malformed, unsupported, and missing-authority fixtures'
