#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
. "${KOFUN_SEMANTIC_PROTOCOL_LIB:-$ROOT/tests/fuzz/semantic_protocol.sh}"
ASSERT_CONTEXT='arithmetic native adapter'
. "$ROOT/tests/assertions/assert.sh"

action=${1-}
family=${2-}
identity=native-x86_64

case $action in
    capability)
        if test "$family" = arithmetic-int-core; then
            semantic_capability_write "$identity" backend "$family" supported -
        else
            semantic_capability_write \
                "$identity" backend "$family" unsupported family-unsupported
        fi
        ;;
    run)
        assert_num "argument count" "$#" -eq 6
        assert_eq "family" "$family" arithmetic-int-core
        source=$3
        result=$5
        work=$6
        mkdir -p "$work"
        semantic_record_tool "$work/tools.tsv" kofun "$ROOT/bin/kofun"
        set +e
        "$ROOT/bin/kofun" build "$source" \
            --target x86_64-linux -o "$work/program" \
            >"$work/compile.stdout" 2>"$work/compile.stderr"
        compile_status=$?
        set -e
        printf '%s\n' "$compile_status" >"$work/compile.status"
        test "$compile_status" -eq 0 && test -x "$work/program" || exit 1
        set +e
        "$work/program" >"$work/stdout" 2>"$work/stderr"
        status=$?
        set -e
        semantic_result_write \
            "$result" "$identity" backend "$family" supported "$status" \
            "$work/stdout" "$work/stderr" -
        ;;
    *) exit 2 ;;
esac
