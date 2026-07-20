# Adapter for the direct WebAssembly arithmetic Core.

BACKEND_NAME=wasm32-node
BACKEND_CORPORA=numeric

backend_compile() {
    source=$1
    output=$2
    work=$3
    wasm="$output.wasm"

    command -v node >/dev/null 2>&1 || {
        printf '%s\n' \
            "conformance: wasm32-node requires the Node WebAssembly runtime"
        return 1
    }
    "$KOFUN_ROOT/bin/kofun" build "$source" \
        --target wasm32 -o "$wasm" \
        >"$work/build.stdout" 2>"$work/build.stderr" || {
        cat "$work/build.stdout" "$work/build.stderr"
        return 1
    }
    printf '%s\n' \
        '#!/usr/bin/env sh' \
        'exec node "$KOFUN_ROOT/bootstrap/wasm/run.mjs" "$0.wasm"' \
        >"$output"
    chmod +x "$output"
}
