#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
NATIVE="$ROOT/bootstrap/native"
KOFUN="$ROOT/bin/kofun"
WORK=${KOFUN_NATIVE_CHECK_WORK:-"$ROOT/build/native-check"}

rm -rf "$WORK"
mkdir -p "$WORK"

expand_fixture() (
    fixture=$1
    stem=$2
    expected_size=$3
    emitter="$WORK/emit-$stem-rle"
    rle="$WORK/$stem.rle"
    image="$WORK/$stem.elf"

    "$KOFUN" build "$fixture" \
        -o "$emitter" \
        --emit-c "$WORK/emit-$stem-rle.c" >/dev/null
    "$emitter" >"$rle"

    : >"$image"
    pending=
    while IFS= read -r field; do
        case $field in
            ''|*[!0-9]*)
                printf '%s\n' "native-check: invalid RLE field: $field" >&2
                exit 1
                ;;
        esac

        if test -z "$pending"; then
            test "$field" -le 255 || {
                printf '%s\n' "native-check: byte outside 0..255: $field" >&2
                exit 1
            }
            pending=$field
            continue
        fi

        test "$field" -gt 0 || {
            printf '%s\n' "native-check: run length must be positive" >&2
            exit 1
        }
        octal=$(printf '%03o' "$pending")
        count=0
        while test "$count" -lt "$field"; do
            printf "\\$octal" >>"$image"
            count=$((count + 1))
        done
        pending=
    done <"$rle"

    test -z "$pending" || {
        printf '%s\n' "native-check: RLE stream ended without a run length" >&2
        exit 1
    }
    test "$(wc -c <"$image" | tr -d ' ')" -eq "$expected_size"
)

expand_fixture \
    "$NATIVE/fixtures/exit_42.rle.kofun" \
    exit_42 \
    188
expand_fixture \
    "$NATIVE/fixtures/print_sum_42.rle.kofun" \
    print_sum_42 \
    4099
expand_fixture \
    "$NATIVE/fixtures/core_answer.rle.kofun" \
    core_answer \
    231

(
    cd "$WORK"
    sha256sum -c "$NATIVE/SHA256SUMS"
)

if command -v readelf >/dev/null 2>&1; then
    for stem in exit_42 print_sum_42 core_answer; do
        image="$WORK/$stem.elf"
        readelf -h "$image" >"$WORK/$stem.elf-header.txt"
        readelf -l "$image" >"$WORK/$stem.program-headers.txt"
        grep -Eq 'Class:[[:space:]]+ELF64' "$WORK/$stem.elf-header.txt"
        grep -Eq 'Machine:[[:space:]]+Advanced Micro Devices X86-64' \
            "$WORK/$stem.elf-header.txt"
        grep -Eq 'Entry point address:[[:space:]]+0x4000b0' \
            "$WORK/$stem.elf-header.txt"
        grep -Eq 'Number of program headers:[[:space:]]+2' \
            "$WORK/$stem.elf-header.txt"
        test "$(grep -c 'LOAD' "$WORK/$stem.program-headers.txt")" -eq 2
    done
fi

chmod +x \
    "$WORK/exit_42.elf" \
    "$WORK/print_sum_42.elf" \
    "$WORK/core_answer.elf"

# The digits are not pre-baked in the file: native arithmetic fills both zero
# bytes before write(1, buffer, 3). Only the newline starts initialized.
printf '\000\000\n' >"$WORK/print_sum_42.initial-buffer.expected"
tail -c 3 "$WORK/print_sum_42.elf" \
    >"$WORK/print_sum_42.initial-buffer"
cmp \
    "$WORK/print_sum_42.initial-buffer.expected" \
    "$WORK/print_sum_42.initial-buffer"

# Prove the compact Core image contains resolved forward call and
# RIP-relative message fixups, not zero placeholders.
call_bytes=$(od -An -tu1 -j 176 -N 5 "$WORK/core_answer.elf" |
    awk '{$1=$1; print}')
lea_bytes=$(od -An -tu1 -j 212 -N 7 "$WORK/core_answer.elf" |
    awk '{$1=$1; print}')
test "$call_bytes" = "232 9 0 0 0"
test "$lea_bytes" = "72 141 53 9 0 0 0"

set +e
"$WORK/exit_42.elf" >"$WORK/exit_42.stdout" 2>"$WORK/exit_42.stderr"
status=$?
set -e
test "$status" -eq 42
test ! -s "$WORK/exit_42.stdout"
test ! -s "$WORK/exit_42.stderr"

set +e
"$WORK/print_sum_42.elf" \
    >"$WORK/print_sum_42.stdout" \
    2>"$WORK/print_sum_42.stderr"
status=$?
set -e
test "$status" -eq 0
printf '42\n' >"$WORK/print_sum_42.expected"
cmp "$WORK/print_sum_42.expected" "$WORK/print_sum_42.stdout"
test ! -s "$WORK/print_sum_42.stderr"

set +e
"$WORK/core_answer.elf" \
    >"$WORK/core_answer.stdout" \
    2>"$WORK/core_answer.stderr"
status=$?
set -e
test "$status" -eq 42
printf '42\n' >"$WORK/core_answer.expected"
cmp "$WORK/core_answer.expected" "$WORK/core_answer.stdout"
test ! -s "$WORK/core_answer.stderr"

printf '%s\n' \
    "PASS: Kofun emitted deterministic 188-, 231-, and 4099-byte ELF64 images" \
    "PASS: native image exited through Linux x86-64 syscall with status 42" \
    "PASS: native code computed 40 + 2, wrote 42 to stdout, and exited 0" \
    "PASS: rel32 Core call/message fixups printed and exited with 42"
