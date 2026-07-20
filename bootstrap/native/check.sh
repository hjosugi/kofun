#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
NATIVE="$ROOT/bootstrap/native"
KOFUN="$ROOT/bin/kofun"
WORK=${KOFUN_NATIVE_CHECK_WORK:-"$ROOT/build/native-check"}
CC=${CC:-cc}

rm -rf "$WORK"
mkdir -p "$WORK"

"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$ROOT/bootstrap/stage2/compiler.c" \
    -o "$WORK/kofun-stage2"
"$WORK/kofun-stage2" \
    "$NATIVE/encoder.kofun" \
    "$WORK/encoder.kofun" \
    "$WORK/encoder.ir" \
    "$WORK/encoder.tokens" >/dev/null
cmp "$NATIVE/encoder.kofun" "$WORK/encoder.kofun"
grep '^function|elf64_core_answer_debug_image|0|' \
    "$WORK/encoder.ir" >/dev/null

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

"$NATIVE/emit-fixture.sh" \
    -o "$WORK/core_answer_release_option.elf"
cmp \
    "$WORK/core_answer.elf" \
    "$WORK/core_answer_release_option.elf"

"$NATIVE/emit-fixture.sh" \
    -g \
    -o "$WORK/core_answer_debug.elf"

(
    cd "$WORK"
    sha256sum -c "$NATIVE/SHA256SUMS"
)

command -v readelf >/dev/null 2>&1 || {
    printf '%s\n' "native-check: readelf is required" >&2
    exit 1
}

for stem in exit_42 print_sum_42 core_answer core_answer_debug; do
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

# Debug metadata is opt-in. The canonical 231-byte release image still has no
# section headers, while `-g` appends a complete section table and DWARF v4.
grep -Eq 'Number of section headers:[[:space:]]+0' \
    "$WORK/core_answer.elf-header.txt"
grep -Eq 'Number of section headers:[[:space:]]+10' \
    "$WORK/core_answer_debug.elf-header.txt"
test "$(wc -c <"$WORK/core_answer.elf" | tr -d ' ')" -eq 231
test "$(wc -c <"$WORK/core_answer_debug.elf" | tr -d ' ')" -eq 1360

dd if="$WORK/core_answer.elf" \
    of="$WORK/core_answer.release-program-headers" \
    bs=1 skip=64 count=112 2>/dev/null
dd if="$WORK/core_answer_debug.elf" \
    of="$WORK/core_answer.debug-program-headers" \
    bs=1 skip=64 count=112 2>/dev/null
cmp \
    "$WORK/core_answer.release-program-headers" \
    "$WORK/core_answer.debug-program-headers"

dd if="$WORK/core_answer.elf" \
    of="$WORK/core_answer.release-runtime" \
    bs=1 skip=176 count=55 2>/dev/null
dd if="$WORK/core_answer_debug.elf" \
    of="$WORK/core_answer.debug-runtime" \
    bs=1 skip=176 count=55 2>/dev/null
cmp \
    "$WORK/core_answer.release-runtime" \
    "$WORK/core_answer.debug-runtime"

readelf --wide --sections "$WORK/core_answer_debug.elf" \
    >"$WORK/core_answer_debug.sections.txt"
for section in \
    .text \
    .rodata \
    .debug_abbrev \
    .debug_info \
    .debug_line \
    .debug_str \
    .symtab \
    .strtab \
    .shstrtab
do
    grep -F "$section" "$WORK/core_answer_debug.sections.txt" >/dev/null
done

readelf --wide --symbols "$WORK/core_answer_debug.elf" \
    >"$WORK/core_answer_debug.symbols.txt"
grep -Eq '[[:space:]]FUNC[[:space:]]+GLOBAL.*[[:space:]]main$' \
    "$WORK/core_answer_debug.symbols.txt"

readelf --wide --debug-dump=info "$WORK/core_answer_debug.elf" \
    >"$WORK/core_answer_debug.info.txt"
grep -q 'DW_TAG_compile_unit' "$WORK/core_answer_debug.info.txt"
grep -q 'DW_TAG_subprogram' "$WORK/core_answer_debug.info.txt"
grep -Eq 'DW_AT_name[[:space:]]+:.*main$' \
    "$WORK/core_answer_debug.info.txt"

readelf --wide --debug-dump=decodedline "$WORK/core_answer_debug.elf" \
    >"$WORK/core_answer_debug.lines.txt"
grep -Eq \
    'bootstrap/native/fixtures/core_answer_debug.kofun[[:space:]]+2[[:space:]]+0x4000be' \
    "$WORK/core_answer_debug.lines.txt"
grep -Eq \
    'bootstrap/native/fixtures/core_answer_debug.kofun[[:space:]]+6[[:space:]]+0x4000e2' \
    "$WORK/core_answer_debug.lines.txt"

chmod +x \
    "$WORK/exit_42.elf" \
    "$WORK/print_sum_42.elf" \
    "$WORK/core_answer.elf" \
    "$WORK/core_answer_debug.elf"

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

set +e
"$WORK/core_answer_debug.elf" \
    >"$WORK/core_answer_debug.stdout" \
    2>"$WORK/core_answer_debug.stderr"
status=$?
set -e
test "$status" -eq 42
cmp "$WORK/core_answer.expected" "$WORK/core_answer_debug.stdout"
test ! -s "$WORK/core_answer_debug.stderr"

if command -v gdb >/dev/null 2>&1; then
    (
        cd "$ROOT"
        gdb -q -nx -batch \
            -ex 'set debuginfod enabled off' \
            -ex 'set pagination off' \
            -ex 'break main' \
            -ex 'run' \
            -ex 'bt' \
            -ex 'next' \
            -ex 'next' \
            -ex 'frame' \
            "$WORK/core_answer_debug.elf"
    ) >"$WORK/core_answer_debug.gdb.txt" 2>&1
    grep -Eq \
        'Breakpoint 1, main .*core_answer_debug.kofun:2' \
        "$WORK/core_answer_debug.gdb.txt"
    grep -Eq \
        '#0[[:space:]]+main .*core_answer_debug.kofun:2' \
        "$WORK/core_answer_debug.gdb.txt"
    grep -Eq \
        'main .*core_answer_debug.kofun:4' \
        "$WORK/core_answer_debug.gdb.txt"
    printf '%s\n' \
        "PASS: gdb stepped Kofun lines and named main in the backtrace"
else
    printf '%s\n' \
        "SKIP: gdb unavailable; readelf DWARF structure was still verified"
fi

printf '%s\n' \
    "PASS: Kofun emitted deterministic 188-, 231-, and 4099-byte ELF64 images" \
    "PASS: native image exited through Linux x86-64 syscall with status 42" \
    "PASS: native code computed 40 + 2, wrote 42 to stdout, and exited 0" \
    "PASS: rel32 Core call/message fixups printed and exited with 42" \
    "PASS: opt-in debug image has ELF sections, DWARF lines, and a main DIE" \
    "PASS: release Core image remains byte-identical and 231 bytes"
