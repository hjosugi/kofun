#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
. "${KOFUN_SEMANTIC_PROTOCOL_LIB:-$ROOT/tests/fuzz/semantic_protocol.sh}"

action=${1-}
family=${2-}
identity=wasm32-node

case $action in
    capability)
        if test "$family" != arithmetic-int-core; then
            semantic_capability_write \
                "$identity" backend "$family" unsupported family-unsupported
        elif command -v node >/dev/null 2>&1; then
            semantic_capability_write "$identity" backend "$family" supported -
        else
            semantic_capability_write \
                "$identity" backend "$family" unsupported node-unavailable
        fi
        ;;
    run)
        test "$#" -eq 6
        test "$family" = arithmetic-int-core
        source=$3
        result=$5
        work=$6
        mkdir -p "$work"
        semantic_record_tool "$work/tools.tsv" kofun "$ROOT/bin/kofun"
        node_path=$(command -v node)
        semantic_record_tool "$work/tools.tsv" node "$node_path"
        semantic_record_tool \
            "$work/tools.tsv" wasm-runner "$ROOT/bootstrap/wasm/run.mjs"
        set +e
        "$ROOT/bin/kofun" build "$source" \
            --target wasm32 -o "$work/program.wasm" \
            >"$work/compile.stdout" 2>"$work/compile.stderr"
        compile_status=$?
        set -e
        printf '%s\n' "$compile_status" >"$work/compile.status"
        test "$compile_status" -eq 0 && test -f "$work/program.wasm" || exit 1
        set +e
        "$node_path" "$ROOT/bootstrap/wasm/run.mjs" "$work/program.wasm" \
            >"$work/stdout" 2>"$work/stderr"
        status=$?
        set -e
        semantic_result_write \
            "$result" "$identity" backend "$family" supported "$status" \
            "$work/stdout" "$work/stderr" -
        ;;
    *) exit 2 ;;
esac
