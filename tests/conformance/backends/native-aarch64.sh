# Adapter for the Python-free direct AArch64 static ELF backend.
#
# AArch64 images execute under qemu-aarch64. When the emulator is absent the
# adapter claims a sentinel corpus so the conformance runner records an explicit
# UNSUPPORTED skip for every real corpus instead of failing. AArch64 lowers the
# multi-function Int Core plus the closed List[Int] and UTF-8 Text Cores.

BACKEND_NAME=native-aarch64
if test -n "${QEMU_AARCH64-}" &&
   command -v "$QEMU_AARCH64" >/dev/null 2>&1
then
    :
elif command -v qemu-aarch64 >/dev/null 2>&1; then
    QEMU_AARCH64=$(command -v qemu-aarch64)
elif command -v qemu-aarch64-static >/dev/null 2>&1; then
    QEMU_AARCH64=$(command -v qemu-aarch64-static)
else
    QEMU_AARCH64=
fi
export QEMU_AARCH64

if test -n "$QEMU_AARCH64"; then
    BACKEND_CORPORA='functions list text'
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
        'exec "$QEMU_AARCH64" "$0.aarch64.elf" "$@"' \
        >"$output"
    chmod +x "$output"
}
