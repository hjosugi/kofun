# Adapter for the Python-free direct AArch64 static ELF backend.
#
# AArch64 images execute under qemu-aarch64. When the emulator is absent the
# adapter claims a sentinel corpus so the conformance runner records an explicit
# UNSUPPORTED skip for every real corpus instead of failing. AArch64 currently
# lowers the multi-function Int Core; the numeric, list, and text corpora remain
# x86-64-only and the build rejects them with an explicit diagnostic.

BACKEND_NAME=native-aarch64
if command -v qemu-aarch64 >/dev/null 2>&1; then
    BACKEND_CORPORA=functions
else
    BACKEND_CORPORA=requires-qemu-aarch64
fi

backend_compile() {
    source=$1
    output=$2
    work=$3
    elf="$output.aarch64.elf"

    set +e
    "$KOFUN_ROOT/bin/kofun" build "$source" \
        --target aarch64-linux \
        -o "$elf" \
        >"$work/build.stdout" 2>"$work/build.stderr"
    build_status=$?
    set -e
    if test "$build_status" -ne 0; then
        cat "$work/build.stdout" "$work/build.stderr"
        if grep -q 'unsupported Core\|does not support' \
            "$work/build.stdout" "$work/build.stderr"
        then
            return 125
        fi
        return 1
    fi

    printf '%s\n' \
        '#!/usr/bin/env sh' \
        'exec qemu-aarch64 "$0.aarch64.elf" "$@"' \
        >"$output"
    chmod +x "$output"
}
