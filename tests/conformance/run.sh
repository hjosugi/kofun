#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CORPUS=${1-"$ROOT/tests/conformance/numeric"}
BACKENDS="$ROOT/tests/conformance/backends"

test -d "$CORPUS" || {
    printf '%s\n' "conformance: corpus not found: $CORPUS" >&2
    exit 2
}

run_backend() (
    adapter=$1
    KOFUN_ROOT=$ROOT
    export KOFUN_ROOT
    # The adapter supplies BACKEND_NAME and backend_compile SOURCE OUTPUT WORK.
    # Return 125 only for an explicit unsupported-feature diagnostic.
    . "$adapter"
    test -n "${BACKEND_NAME-}" || {
        printf '%s\n' "conformance: adapter has no BACKEND_NAME: $adapter" >&2
        exit 2
    }
    command -v backend_compile >/dev/null 2>&1 || {
        printf '%s\n' "conformance: adapter has no backend_compile: $adapter" >&2
        exit 2
    }

    work=$(mktemp -d "${TMPDIR:-/tmp}/kofun-conformance.XXXXXX")
    trap 'rm -rf "$work"' 0 1 2 15
    passed=0
    failed=0
    skipped=0
    total=0

    for source in "$CORPUS"/*.kofun; do
        test -f "$source" || continue
        test "$(basename "$source")" != "expectations.kofun" || continue
        total=$((total + 1))
        stem=$(basename "${source%.kofun}")
        case_work="$work/$stem"
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

        set +e
        backend_compile \
            "$source" "$case_work/program" "$case_work" \
            >"$case_work/compile.stdout" 2>"$case_work/compile.stderr"
        compile_status=$?
        set -e

        if test "$compile_status" -eq 125; then
            if test ! -s "$case_work/compile.stdout" &&
               test ! -s "$case_work/compile.stderr"
            then
                printf '%s\n' \
                    "FAIL [$BACKEND_NAME] $source (unsupported without diagnostic)"
                failed=$((failed + 1))
            else
                printf '%s\n' "SKIP [$BACKEND_NAME] $source"
                sed 's/^/  /' \
                    "$case_work/compile.stdout" "$case_work/compile.stderr"
                skipped=$((skipped + 1))
            fi
            continue
        fi
        if test "$compile_status" -ne 0 || test ! -x "$case_work/program"; then
            printf '%s\n' "FAIL [$BACKEND_NAME] $source (compile failed)"
            sed 's/^/  /' \
                "$case_work/compile.stdout" "$case_work/compile.stderr"
            failed=$((failed + 1))
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

        if test "$actual_status" -eq 124 || test "$actual_status" -eq 137; then
            printf '%s\n' "FAIL [$BACKEND_NAME] $source (timed out)"
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

    executed=$((passed + failed))
    printf '%s\n' \
        "$passed passed; $failed failed; $skipped explicitly skipped" \
        "coverage: $executed/$total cases executed by $BACKEND_NAME"
    test "$total" -gt 0
    test "$executed" -gt 0
    test "$failed" -eq 0
)

found=0
status=0
for adapter in "$BACKENDS"/*.sh; do
    test -f "$adapter" || continue
    found=$((found + 1))
    if ! run_backend "$adapter"; then
        status=1
    fi
done

test "$found" -gt 0 || {
    printf '%s\n' "conformance: no backend adapters registered in $BACKENDS" >&2
    exit 2
}
exit "$status"
