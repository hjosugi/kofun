#!/bin/sh
set -eu

. "${KOFUN_SEMANTIC_PROTOCOL_LIB:?semantic protocol library is required}"

identity=$(basename "$0")
action=${1-}
family=${2-}
case $identity in
    oracle-*) role=oracle ;;
    *) role=backend ;;
esac

case $action in
    capability)
        case $identity in
            backend-capability)
                semantic_capability_write \
                    "$identity" "$role" "$family" unsupported fixture-refusal
                ;;
            backend-unsupported-only)
                semantic_capability_write \
                    "$identity" "$role" "$family" unsupported fixture-unsupported
                ;;
            *)
                semantic_capability_write \
                    "$identity" "$role" "$family" supported -
                ;;
        esac
        ;;
    run)
        # Not tests/assertions/assert.sh: this fixture is executed from a work
        # directory with no repository root in scope, and inventing one to reach
        # the helper would be a heavier dependency than the message it saves.
        if test "$#" -ne 6; then
            printf '%s\n' \
                "protocol adapter: run expected 6 arguments, got $#" >&2
            exit 2
        fi
        result=$5
        work=$6
        mkdir -p "$work"
        : >"$work/stderr"
        printf '%s\n' 42 >"$work/stdout"
        status=0
        case $identity in
            backend-stdout)
                printf '%s\n' 43 >"$work/stdout"
                ;;
            backend-stderr)
                printf '%s\n' fixture-diagnostic >"$work/stderr"
                ;;
            backend-status)
                status=7
                ;;
            backend-omit)
                exit 0
                ;;
            backend-crash)
                kill -SEGV "$$"
                ;;
            backend-timeout)
                sleep 5
                ;;
            backend-malformed)
                mkdir -p "$result"
                printf '%s\n' malformed >"$result/result.tsv"
                : >"$result/stdout.bin"
                : >"$result/stderr.bin"
                exit 0
                ;;
        esac
        semantic_result_write \
            "$result" "$identity" "$role" "$family" supported "$status" \
            "$work/stdout" "$work/stderr" -
        ;;
    *) exit 2 ;;
esac
