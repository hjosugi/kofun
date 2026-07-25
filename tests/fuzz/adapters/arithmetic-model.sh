#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
. "${KOFUN_SEMANTIC_PROTOCOL_LIB:-$ROOT/tests/fuzz/semantic_protocol.sh}"

action=${1-}
family=${2-}
identity=arithmetic-model

case $action in
    capability)
        if test "$family" = arithmetic-int-core; then
            semantic_capability_write "$identity" oracle "$family" supported -
        else
            semantic_capability_write \
                "$identity" oracle "$family" unsupported family-unsupported
        fi
        ;;
    run)
        test "$#" -eq 6
        test "$family" = arithmetic-int-core
        source=$3
        case_meta=$4
        result=$5
        work=$6
        test -f "$source"
        mkdir -p "$work"
        left=$(semantic_case_field "$case_meta" left)
        right=$(semantic_case_field "$case_meta" right)
        factor=$(semantic_case_field "$case_meta" factor)
        shape=$(semantic_case_field "$case_meta" shape)
        case $left:$right:$factor:$shape in
            *[!0-9:]*|:*|*:) exit 2 ;;
        esac
        case $shape in
            0) expected=$((left + right + 20)) ;;
            1) expected=$(((left + right) * factor)) ;;
            2) expected=$((left * right + 10)) ;;
            *) exit 2 ;;
        esac
        printf '%s\n' "$expected" >"$work/stdout"
        : >"$work/stderr"
        semantic_result_write \
            "$result" "$identity" oracle "$family" supported 0 \
            "$work/stdout" "$work/stderr" -
        ;;
    *) exit 2 ;;
esac
