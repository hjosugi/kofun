# Adapter for the active Kofun-written Stage 1 C11 backend.

BACKEND_NAME=c11-stage1
BACKEND_CORPORA=numeric

backend_compile() {
    source=$1
    output=$2
    work=$3
    emitted="$work/program.c"

    set +e
    "$KOFUN_ROOT/bin/kofun" emit-c "$source" "$emitted" \
        >"$work/emit.stdout" 2>"$work/emit.stderr"
    emit_status=$?
    set -e
    if test "$emit_status" -ne 0; then
        cat "$work/emit.stdout" "$work/emit.stderr"
        if grep -q 'unsupported Kofun integer Core source' \
            "$work/emit.stdout" "$work/emit.stderr"
        then
            return 125
        fi
        return 1
    fi

    compiler=${CC:-cc}
    "$compiler" -std=c11 -O2 -Wall -Wextra -Werror \
        "$emitted" -o "$output"
}
