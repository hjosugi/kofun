#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -P -- "$(dirname -- "$0")/../.." && pwd)
CORPUS_INPUT=${1-"$ROOT/tests/conformance/numeric"}
BACKENDS=${KOFUN_CONFORMANCE_BACKENDS-"$ROOT/tests/conformance/backends"}
CAPABILITIES=${KOFUN_CONFORMANCE_CAPABILITIES-"$ROOT/tests/conformance/capabilities.tsv"}
CORPORA=${KOFUN_CONFORMANCE_CORPORA-"$ROOT/tests/conformance"}

test -d "$CORPUS_INPUT" || {
    printf '%s\n' "conformance: corpus not found: $CORPUS_INPUT" >&2
    exit 2
}
test -d "$CORPORA" || {
    printf '%s\n' "conformance: corpus registry not found: $CORPORA" >&2
    exit 2
}
CORPUS=$(CDPATH= cd -P -- "$CORPUS_INPUT" && pwd)
CORPORA=$(CDPATH= cd -P -- "$CORPORA" && pwd)
CORPUS_NAME=$(basename "$CORPUS")
if test "$CORPUS" != "$CORPORA/$CORPUS_NAME"; then
    printf '%s\n' \
        "conformance: selected corpus is not the registered $CORPUS_NAME corpus: $CORPUS" >&2
    exit 2
fi
test -f "$CORPUS/expectations.kofun" || {
    printf '%s\n' \
        "conformance: corpus has no expectations.kofun: $CORPUS" >&2
    exit 2
}

sh "$ROOT/tests/conformance/check-capabilities.sh" \
    "$CAPABILITIES" "$BACKENDS" "$CORPORA" >/dev/null

manifest_field() {
    backend=$1
    corpus=$2
    field=$3
    awk -F '	' \
        -v backend="$backend" \
        -v corpus="$corpus" \
        -v field="$field" \
        '$1 == backend && $2 == corpus { print $field; exit }' \
        "$CAPABILITIES"
}

run_backend() (
    adapter=$1
    adapter_name=$(basename "${adapter%.sh}")
    KOFUN_ROOT=$ROOT
    export KOFUN_ROOT
    # The adapter supplies BACKEND_NAME and backend_compile SOURCE OUTPUT WORK.
    # Capability policy comes only from capabilities.tsv. An optional
    # backend_check_available function reports whether the executor exists on
    # this host; backend_compile must never turn a supported case into a skip.
    . "$adapter"
    test -n "${BACKEND_NAME-}" || {
        printf '%s\n' "conformance: adapter has no BACKEND_NAME: $adapter" >&2
        exit 2
    }
    test "$BACKEND_NAME" = "$adapter_name" || {
        printf '%s\n' \
            "conformance: adapter identity mismatch: $adapter_name declares $BACKEND_NAME" >&2
        exit 2
    }
    capability_state=$(manifest_field "$BACKEND_NAME" "$CORPUS_NAME" 3)
    if test "$capability_state" = unsupported; then
        capability_reason=$(manifest_field "$BACKEND_NAME" "$CORPUS_NAME" 5)
        printf '%s\n' \
            "UNSUPPORTED [$BACKEND_NAME] corpus $CORPUS_NAME: $capability_reason"
        exit 125
    fi
    test "$capability_state" = supported || {
        printf '%s\n' \
            "conformance: no valid capability for $BACKEND_NAME / $CORPUS_NAME" >&2
        exit 2
    }
    command -v backend_compile >/dev/null 2>&1 || {
        printf '%s\n' "conformance: adapter has no backend_compile: $adapter" >&2
        exit 2
    }

    runner_work=$(mktemp -d "${TMPDIR:-/tmp}/kofun-conformance.XXXXXX")
    trap 'rm -rf "$runner_work"' 0 1 2 15

    executor_available=1
    if command -v backend_check_available >/dev/null 2>&1; then
        set +e
        backend_check_available \
            >"$runner_work/availability.stdout" 2>"$runner_work/availability.stderr"
        availability_status=$?
        set -e
        if test "$availability_status" -eq 125; then
            if test ! -s "$runner_work/availability.stdout" &&
               test ! -s "$runner_work/availability.stderr"
            then
                printf '%s\n' \
                    "FAIL [$BACKEND_NAME] executor unavailable without diagnostic"
                exit 1
            fi
            executor_available=0
        fi
        if test "$availability_status" -ne 0 &&
           test "$availability_status" -ne 125
        then
            printf '%s\n' \
                "FAIL [$BACKEND_NAME] executor availability check failed"
            sed 's/^/  /' \
                "$runner_work/availability.stdout" "$runner_work/availability.stderr"
            exit 1
        fi
    fi

    passed=0
    failed=0
    skipped=0
    total=0
    built=0

    for source in "$CORPUS"/*.kofun; do
        test -f "$source" || continue
        test "$(basename "$source")" != "expectations.kofun" || continue
        total=$((total + 1))
        stem=$(basename "${source%.kofun}")
        case_work="$runner_work/$stem"
        mkdir -p "$case_work"

        : >"$case_work/expected.stdout"
        : >"$case_work/expected.stderr"
        sed -n \
            -e 's/^# expect: //p' \
            -e 's/^# expect-stdout: //p' \
            "$source" >"$case_work/expected.stdout"
        sed -n 's/^# expect-stderr: //p' \
            "$source" >"$case_work/expected.stderr"
        expected_status=$(sed -n 's/^# expect-exit: //p' "$source")
        test -n "$expected_status" || expected_status=0
        case $expected_status in
            *[!0-9]*|'')
                printf '%s\n' \
                    "FAIL [$BACKEND_NAME] $source (invalid expected exit status)"
                failed=$((failed + 1))
                continue
                ;;
        esac
        if test "$expected_status" -gt 127; then
            printf '%s\n' \
                "FAIL [$BACKEND_NAME] $source (expected exit status must be between 0 and 127)"
            failed=$((failed + 1))
            continue
        fi
        if test "$expected_status" -eq 124; then
            printf '%s\n' \
                "FAIL [$BACKEND_NAME] $source (expected exit 124 is reserved for the timeout harness)"
            failed=$((failed + 1))
            continue
        fi

        set +e
        backend_compile \
            "$source" "$case_work/program" "$case_work" \
            >"$case_work/compile.stdout" 2>"$case_work/compile.stderr"
        compile_status=$?
        set -e

        if test "$compile_status" -eq 125; then
            printf '%s\n' \
                "FAIL [$BACKEND_NAME] $source (supported capability returned status 125)"
            sed 's/^/  /' \
                "$case_work/compile.stdout" "$case_work/compile.stderr"
            failed=$((failed + 1))
            continue
        fi
        if test "$compile_status" -ne 0 || test ! -x "$case_work/program"; then
            printf '%s\n' "FAIL [$BACKEND_NAME] $source (compile failed)"
            sed 's/^/  /' \
                "$case_work/compile.stdout" "$case_work/compile.stderr"
            failed=$((failed + 1))
            continue
        fi
        built=$((built + 1))

        if test "$executor_available" -eq 0; then
            printf '%s\n' "BUILD PASS [$BACKEND_NAME] $source"
            continue
        fi

        set +e
        if command -v timeout >/dev/null 2>&1; then
            timeout 10 "$case_work/program" \
                >"$case_work/actual.stdout" 2>"$case_work/actual.stderr"
        else
            "$case_work/program" \
                >"$case_work/actual.stdout" 2>"$case_work/actual.stderr"
        fi
        actual_status=$?
        set -e

        if test "$actual_status" -eq 124; then
            printf '%s\n' "FAIL [$BACKEND_NAME] $source (timed out)"
            failed=$((failed + 1))
            continue
        fi
        if test "$actual_status" -ge 128; then
            printf '%s\n' \
                "FAIL [$BACKEND_NAME] $source (terminated by signal)"
            failed=$((failed + 1))
            continue
        fi
        if test "$actual_status" -ne "$expected_status"; then
            printf '%s\n' \
                "FAIL [$BACKEND_NAME] $source (expected exit $expected_status, got $actual_status)"
            failed=$((failed + 1))
            continue
        fi
        if ! cmp -s "$case_work/expected.stdout" "$case_work/actual.stdout"; then
            printf '%s\n' "FAIL [$BACKEND_NAME] $source (stdout mismatch)"
            failed=$((failed + 1))
            continue
        fi
        if ! cmp -s "$case_work/expected.stderr" "$case_work/actual.stderr"; then
            printf '%s\n' "FAIL [$BACKEND_NAME] $source (stderr mismatch)"
            failed=$((failed + 1))
            continue
        fi

        printf '%s\n' "PASS [$BACKEND_NAME] $source"
        passed=$((passed + 1))
    done

    if test "$executor_available" -eq 0; then
        printf '%s\n' \
            "$built built; $failed failed; 0 executed by $BACKEND_NAME" \
            "coverage: 0/$total cases executed by $BACKEND_NAME" \
            "UNAVAILABLE [$BACKEND_NAME] executor for corpus $CORPUS_NAME"
        sed 's/^/  /' \
            "$runner_work/availability.stdout" "$runner_work/availability.stderr"
        if test "$total" -eq 0 ||
           test "$built" -eq 0 ||
           test "$failed" -ne 0
        then
            exit 1
        fi
        exit 125
    fi

    executed=$((passed + failed))
    printf '%s\n' \
        "$passed passed; $failed failed; $skipped explicitly skipped" \
        "coverage: $executed/$total cases executed by $BACKEND_NAME"
    if test "$total" -eq 0 ||
       test "$executed" -eq 0 ||
       test "$failed" -ne 0
    then
        exit 1
    fi
)

found=0
applicable=0
status=0
for adapter in "$BACKENDS"/*.sh; do
    test -f "$adapter" || continue
    found=$((found + 1))
    set +e
    run_backend "$adapter"
    backend_status=$?
    set -e
    if test "$backend_status" -eq 125; then
        continue
    fi
    applicable=$((applicable + 1))
    if test "$backend_status" -ne 0; then
        status=1
    fi
done

test "$found" -gt 0 || {
    printf '%s\n' "conformance: no backend adapters registered in $BACKENDS" >&2
    exit 2
}
test "$applicable" -gt 0 || {
    printf '%s\n' \
        "conformance: no backend supports corpus $CORPUS_NAME" >&2
    exit 2
}
exit "$status"
