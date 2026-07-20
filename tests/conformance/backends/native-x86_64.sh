# Adapter for the Python-free direct x86-64 static ELF backend.

BACKEND_NAME=native-x86_64
BACKEND_CORPORA=text

backend_compile() {
    source=$1
    output=$2
    work=$3

    set +e
    "$KOFUN_ROOT/bin/kofun" build "$source" \
        --target x86_64-linux \
        -o "$output" \
        >"$work/build.stdout" 2>"$work/build.stderr"
    build_status=$?
    set -e
    if test "$build_status" -ne 0; then
        cat "$work/build.stdout" "$work/build.stderr"
        if grep -q 'unsupported Core' \
            "$work/build.stdout" "$work/build.stderr"
        then
            return 125
        fi
        return 1
    fi
}
