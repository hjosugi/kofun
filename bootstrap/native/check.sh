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
grep '^function|dwarf_debug_strings_for|1|' \
    "$WORK/encoder.ir" >/dev/null
grep '^function|dwarf_debug_info_for|8|' \
    "$WORK/encoder.ir" >/dev/null
grep '^function|dwarf_debug_line_for|6|' \
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

CORE_SOURCE="$NATIVE/fixtures/core_return_42.kofun"
for target in x86_64-linux aarch64-linux; do
    case $target in
        x86_64-linux) stem=core_return_42-x86_64 ;;
        aarch64-linux) stem=core_return_42-aarch64 ;;
    esac
    "$KOFUN" build "$CORE_SOURCE" \
        --target "$target" -o "$WORK/$stem.elf" >/dev/null
    "$KOFUN" build "$CORE_SOURCE" \
        --target "$target" -o "$WORK/$stem.second.elf" >/dev/null
    cmp "$WORK/$stem.elf" "$WORK/$stem.second.elf"
    test "$(wc -c <"$WORK/$stem.elf" | tr -d ' ')" -eq 4099
done

DEBUG_SOURCE="bootstrap/native/fixtures/core_debug_lines_42.kofun"
(
    cd "$ROOT"
    "$KOFUN" build "$DEBUG_SOURCE" \
        --target x86_64-linux \
        -o "$WORK/core_debug_lines_42-release.elf" >/dev/null
    "$KOFUN" build "$DEBUG_SOURCE" \
        --target x86_64-linux \
        -g \
        -o "$WORK/core_debug_lines_42-debug.elf" >/dev/null
    "$KOFUN" build "$DEBUG_SOURCE" \
        --target x86_64-linux \
        -g \
        -o "$WORK/core_debug_lines_42-debug.second.elf" >/dev/null
)
cmp \
    "$WORK/core_debug_lines_42-debug.elf" \
    "$WORK/core_debug_lines_42-debug.second.elf"

set +e
(
    cd "$ROOT"
    "$KOFUN" build "$DEBUG_SOURCE" \
        -g \
        -o "$WORK/core_debug_lines_42-missing-target.elf"
) >"$WORK/core_debug_lines_42-missing-target.stdout" \
    2>"$WORK/core_debug_lines_42-missing-target.stderr"
missing_target_status=$?
(
    cd "$ROOT"
    "$KOFUN" build "$DEBUG_SOURCE" \
        --target aarch64-linux \
        -g \
        -o "$WORK/core_debug_lines_42-aarch64-debug.elf"
) >"$WORK/core_debug_lines_42-aarch64-debug.stdout" \
    2>"$WORK/core_debug_lines_42-aarch64-debug.stderr"
aarch64_debug_status=$?
set -e
test "$missing_target_status" -eq 2
test "$aarch64_debug_status" -eq 2
test ! -e "$WORK/core_debug_lines_42-missing-target.elf"
test ! -e "$WORK/core_debug_lines_42-aarch64-debug.elf"
grep -q -- '-g requires --target x86_64-linux' \
    "$WORK/core_debug_lines_42-missing-target.stderr"
grep -q -- '-g currently requires --target x86_64-linux' \
    "$WORK/core_debug_lines_42-aarch64-debug.stderr"

# Source formatting and debug mode must not perturb the release artifact.
cmp \
    "$WORK/core_return_42-x86_64.elf" \
    "$WORK/core_debug_lines_42-release.elf"
test "$(wc -c <"$WORK/core_debug_lines_42-release.elf" | tr -d ' ')" \
    -eq 4099
test "$(wc -c <"$WORK/core_debug_lines_42-debug.elf" | tr -d ' ')" \
    -gt 4099

# Apart from the ELF section-table fields in the first 64 bytes, the complete
# loaded release image is byte-identical in the debug file.
dd if="$WORK/core_debug_lines_42-release.elf" \
    of="$WORK/core_debug_lines_42-release.loaded" \
    bs=1 skip=64 count=4035 2>/dev/null
dd if="$WORK/core_debug_lines_42-debug.elf" \
    of="$WORK/core_debug_lines_42-debug.loaded" \
    bs=1 skip=64 count=4035 2>/dev/null
cmp \
    "$WORK/core_debug_lines_42-release.loaded" \
    "$WORK/core_debug_lines_42-debug.loaded"

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

readelf -h "$WORK/core_return_42-aarch64.elf" \
    >"$WORK/core_return_42-aarch64.elf-header.txt"
readelf -l "$WORK/core_return_42-aarch64.elf" \
    >"$WORK/core_return_42-aarch64.program-headers.txt"
grep -Eq 'Class:[[:space:]]+ELF64' \
    "$WORK/core_return_42-aarch64.elf-header.txt"
grep -Eq 'Machine:[[:space:]]+AArch64' \
    "$WORK/core_return_42-aarch64.elf-header.txt"
grep -Eq 'Entry point address:[[:space:]]+0x4000b0' \
    "$WORK/core_return_42-aarch64.elf-header.txt"
grep -Eq 'Number of program headers:[[:space:]]+2' \
    "$WORK/core_return_42-aarch64.elf-header.txt"
test "$(grep -c 'LOAD' \
    "$WORK/core_return_42-aarch64.program-headers.txt")" -eq 2

readelf -h "$WORK/core_debug_lines_42-release.elf" \
    >"$WORK/core_debug_lines_42-release.header.txt"
readelf -h "$WORK/core_debug_lines_42-debug.elf" \
    >"$WORK/core_debug_lines_42-debug.header.txt"
grep -Eq 'Number of section headers:[[:space:]]+0' \
    "$WORK/core_debug_lines_42-release.header.txt"
grep -Eq 'Number of section headers:[[:space:]]+10' \
    "$WORK/core_debug_lines_42-debug.header.txt"

readelf --wide --sections "$WORK/core_debug_lines_42-debug.elf" \
    >"$WORK/core_debug_lines_42-debug.sections.txt"
for section in \
    .text \
    .data \
    .debug_abbrev \
    .debug_info \
    .debug_line \
    .debug_str \
    .symtab \
    .strtab \
    .shstrtab
do
    grep -F "$section" \
        "$WORK/core_debug_lines_42-debug.sections.txt" >/dev/null
done

readelf --wide --symbols "$WORK/core_debug_lines_42-debug.elf" \
    >"$WORK/core_debug_lines_42-debug.symbols.txt"
grep -Eq '[[:space:]]FUNC[[:space:]]+GLOBAL.*[[:space:]]main$' \
    "$WORK/core_debug_lines_42-debug.symbols.txt"

readelf --wide --debug-dump=info \
    "$WORK/core_debug_lines_42-debug.elf" \
    >"$WORK/core_debug_lines_42-debug.info.txt"
grep -q 'DW_TAG_compile_unit' \
    "$WORK/core_debug_lines_42-debug.info.txt"
grep -q 'DW_TAG_subprogram' \
    "$WORK/core_debug_lines_42-debug.info.txt"
grep -Eq 'DW_AT_name[[:space:]]+:.*core_debug_lines_42.kofun$' \
    "$WORK/core_debug_lines_42-debug.info.txt"
grep -Eq 'DW_AT_name[[:space:]]+:.*main$' \
    "$WORK/core_debug_lines_42-debug.info.txt"

readelf --wide --debug-dump=decodedline \
    "$WORK/core_debug_lines_42-debug.elf" \
    >"$WORK/core_debug_lines_42-debug.lines.txt"
grep -Eq \
    'bootstrap/native/fixtures/core_debug_lines_42.kofun[[:space:]]+3[[:space:]]+0x4000b0' \
    "$WORK/core_debug_lines_42-debug.lines.txt"
grep -Eq \
    'bootstrap/native/fixtures/core_debug_lines_42.kofun[[:space:]]+4[[:space:]]+0x4000c1' \
    "$WORK/core_debug_lines_42-debug.lines.txt"

chmod +x \
    "$WORK/exit_42.elf" \
    "$WORK/print_sum_42.elf" \
    "$WORK/core_answer.elf" \
    "$WORK/core_answer_debug.elf" \
    "$WORK/core_return_42-x86_64.elf" \
    "$WORK/core_return_42-aarch64.elf" \
    "$WORK/core_debug_lines_42-release.elf" \
    "$WORK/core_debug_lines_42-debug.elf"

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

for mode in release debug; do
    set +e
    "$WORK/core_debug_lines_42-$mode.elf" \
        >"$WORK/core_debug_lines_42-$mode.stdout" \
        2>"$WORK/core_debug_lines_42-$mode.stderr"
    status=$?
    set -e
    test "$status" -eq 0
    printf '42\n' >"$WORK/core_debug_lines_42.expected"
    cmp \
        "$WORK/core_debug_lines_42.expected" \
        "$WORK/core_debug_lines_42-$mode.stdout"
    test ! -s "$WORK/core_debug_lines_42-$mode.stderr"
done

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

    (
        cd "$ROOT"
        gdb -q -nx -batch \
            -ex 'set debuginfod enabled off' \
            -ex 'set pagination off' \
            -ex 'break main' \
            -ex 'run' \
            -ex 'bt' \
            -ex 'next' \
            -ex 'frame' \
            "$WORK/core_debug_lines_42-debug.elf"
    ) >"$WORK/core_debug_lines_42-debug.gdb.txt" 2>&1
    grep -Eq \
        'Breakpoint 1, main .*core_debug_lines_42.kofun:3' \
        "$WORK/core_debug_lines_42-debug.gdb.txt"
    grep -Eq \
        '#0[[:space:]]+main .*core_debug_lines_42.kofun:3' \
        "$WORK/core_debug_lines_42-debug.gdb.txt"
    grep -Eq \
        'main .*core_debug_lines_42.kofun:4' \
        "$WORK/core_debug_lines_42-debug.gdb.txt"
    printf '%s\n' \
        "PASS: gdb stepped CLI-built Kofun lines and named main in backtrace"
else
    printf '%s\n' \
        "SKIP: gdb unavailable; readelf DWARF structure was still verified"
fi

# The same target-independent parsed Core must drive both instruction
# encoders. x86-64 executes directly; AArch64 executes under qemu when the
# emulator is installed. The C11 Stage 1 result is the reference observation.
run_native_core_differential() (
    source=$1
    name=$2

    "$KOFUN" build "$source" --backend c \
        -o "$WORK/$name-reference" \
        --emit-c "$WORK/$name-reference.c" >/dev/null
    "$KOFUN" build "$source" --target x86_64-linux \
        -o "$WORK/$name-x86_64.elf" >/dev/null
    "$KOFUN" build "$source" --target aarch64-linux \
        -o "$WORK/$name-aarch64.elf" >/dev/null

    "$WORK/$name-reference" \
        >"$WORK/$name-reference.stdout" \
        2>"$WORK/$name-reference.stderr"
    reference_status=$?
    "$WORK/$name-x86_64.elf" \
        >"$WORK/$name-x86_64.stdout" \
        2>"$WORK/$name-x86_64.stderr"
    x86_status=$?
    test "$x86_status" -eq "$reference_status"
    cmp "$WORK/$name-reference.stdout" "$WORK/$name-x86_64.stdout"
    cmp "$WORK/$name-reference.stderr" "$WORK/$name-x86_64.stderr"

    if command -v qemu-aarch64 >/dev/null 2>&1; then
        qemu-aarch64 "$WORK/$name-aarch64.elf" \
            >"$WORK/$name-aarch64.stdout" \
            2>"$WORK/$name-aarch64.stderr"
        aarch64_status=$?
        test "$aarch64_status" -eq "$reference_status"
        cmp "$WORK/$name-reference.stdout" "$WORK/$name-aarch64.stdout"
        cmp "$WORK/$name-reference.stderr" "$WORK/$name-aarch64.stderr"
        printf '%s\n' "PASS: $name differential under qemu-aarch64"
    else
        printf '%s\n' \
            "SKIP: $name AArch64 execution (qemu-aarch64 unavailable)"
    fi
)

run_native_core_differential \
    "$NATIVE/fixtures/core_return_42.kofun" \
    core_return_42
run_native_core_differential \
    "$NATIVE/fixtures/core_precedence_42.kofun" \
    core_precedence_42

# List[Int] is currently an x86-64 Native Core extension. Its in-memory ABI is
# checked against an independent C11 reference, then the actual static ELF
# exercises length, negative indexing, bounds failure, and mmap failure.
"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$NATIVE/fixtures/list_int_reference.c" \
    -o "$WORK/core-list-reference"
"$KOFUN" build "$NATIVE/fixtures/core_list_index_42.kofun" \
    --target x86_64-linux \
    -o "$WORK/core-list-index-x86_64.elf" >/dev/null
"$KOFUN" build "$NATIVE/fixtures/core_list_positive_42.kofun" \
    --target x86_64-linux \
    -o "$WORK/core-list-positive-x86_64.elf" >/dev/null
"$KOFUN" build "$NATIVE/fixtures/core_list_len_42.kofun" \
    --target x86_64-linux \
    -o "$WORK/core-list-len-x86_64.elf" >/dev/null
"$KOFUN" build "$NATIVE/fixtures/core_list_oob.kofun" \
    --target x86_64-linux \
    -o "$WORK/core-list-oob-x86_64.elf" >/dev/null

"$WORK/core-list-reference" >"$WORK/core-list-reference.stdout"
"$WORK/core-list-index-x86_64.elf" \
    >"$WORK/core-list-index.stdout" \
    2>"$WORK/core-list-index.stderr"
cmp "$WORK/core-list-reference.stdout" "$WORK/core-list-index.stdout"
test ! -s "$WORK/core-list-index.stderr"

"$WORK/core-list-positive-x86_64.elf" \
    >"$WORK/core-list-positive.stdout" \
    2>"$WORK/core-list-positive.stderr"
cmp "$WORK/core-list-reference.stdout" "$WORK/core-list-positive.stdout"
test ! -s "$WORK/core-list-positive.stderr"

"$WORK/core-list-len-x86_64.elf" \
    >"$WORK/core-list-len.stdout" \
    2>"$WORK/core-list-len.stderr"
cmp "$WORK/core-list-reference.stdout" "$WORK/core-list-len.stdout"
test ! -s "$WORK/core-list-len.stderr"

set +e
"$WORK/core-list-oob-x86_64.elf" \
    >"$WORK/core-list-oob.stdout" \
    2>"$WORK/core-list-oob.stderr"
list_oob_status=$?
(
    ulimit -v 512
    exec "$WORK/core-list-index-x86_64.elf"
) >"$WORK/core-list-oom.stdout" 2>"$WORK/core-list-oom.stderr"
list_oom_status=$?
"$KOFUN" build "$NATIVE/fixtures/core_list_index_42.kofun" \
    --target aarch64-linux \
    -o "$WORK/core-list-index-aarch64.elf" \
    >"$WORK/core-list-index-aarch64.stdout" \
    2>"$WORK/core-list-index-aarch64.stderr"
list_aarch64_status=$?
"$KOFUN" build \
    "$NATIVE/fixtures/core_list_higher_order_unsupported.kofun" \
    --target x86_64-linux \
    -o "$WORK/core-list-higher-order.elf" \
    >"$WORK/core-list-higher-order.stdout" \
    2>"$WORK/core-list-higher-order.stderr"
list_higher_order_status=$?
set -e

test "$list_oob_status" -eq 1
test ! -s "$WORK/core-list-oob.stdout"
printf 'kofun: list index out of range\n' \
    >"$WORK/core-list-oob.expected"
cmp "$WORK/core-list-oob.expected" "$WORK/core-list-oob.stderr"
test "$list_oom_status" -eq 70
test ! -s "$WORK/core-list-oom.stdout"
printf 'kofun: out of memory\n' >"$WORK/core-list-oom.expected"
cmp "$WORK/core-list-oom.expected" "$WORK/core-list-oom.stderr"
test "$list_aarch64_status" -eq 1
test ! -e "$WORK/core-list-index-aarch64.elf"
grep 'AArch64 native Core does not support List\[Int\] yet' \
    "$WORK/core-list-index-aarch64.stderr" >/dev/null
test "$list_higher_order_status" -eq 1
test ! -e "$WORK/core-list-higher-order.elf"
grep 'unsupported Core' "$WORK/core-list-higher-order.stderr" >/dev/null

if cmp -s \
    "$WORK/core_return_42-aarch64.elf" \
    "$WORK/core_precedence_42-aarch64.elf"
then
    printf '%s\n' \
        "native-check: distinct Core programs emitted identical code" >&2
    exit 1
fi

# e_machine is little-endian 183 and the first five instructions prove that
# the AArch64 image computes (6 + 1) * 6 instead of embedding output bytes.
machine_bytes=$(od -An -tu1 -j 18 -N 2 \
    "$WORK/core_return_42-aarch64.elf" | awk '{$1=$1; print}')
core_bytes=$(od -An -tu1 -j 176 -N 20 \
    "$WORK/core_return_42-aarch64.elf" |
    awk '{$1=$1; printf "%s%s", separator, $0; separator=" "} END{print ""}')
test "$machine_bytes" = "183 0"
test "$core_bytes" = \
    "192 0 128 210 33 0 128 210 0 0 1 139 193 0 128 210 0 124 1 155"

if command -v llvm-objdump >/dev/null 2>&1; then
    llvm-objdump -d --triple=aarch64 \
        "$WORK/core_return_42-aarch64.elf" \
        >"$WORK/core_return_42-aarch64.disassembly"
    grep -Eq 'mov[[:space:]]+x0, #0x6' \
        "$WORK/core_return_42-aarch64.disassembly"
    grep -Eq 'add[[:space:]]+x0, x0, x1' \
        "$WORK/core_return_42-aarch64.disassembly"
    grep -Eq 'mul[[:space:]]+x0, x0, x1' \
        "$WORK/core_return_42-aarch64.disassembly"
    grep -Eq 'udiv[[:space:]]+x4, x0, x3' \
        "$WORK/core_return_42-aarch64.disassembly"
    grep -Eq 'svc[[:space:]]+#0' \
        "$WORK/core_return_42-aarch64.disassembly"
fi

unsupported="$WORK/unsupported-native-core.elf"
set +e
"$KOFUN" build "$NATIVE/fixtures/unsupported_native_core.kofun" \
    --target aarch64-linux -o "$unsupported" \
    >"$WORK/unsupported-native-core.stdout" \
    2>"$WORK/unsupported-native-core.stderr"
unsupported_status=$?
set -e
test "$unsupported_status" -eq 1
test ! -e "$unsupported"
grep 'unsupported Core' "$WORK/unsupported-native-core.stderr" >/dev/null

printf '%s\n' \
    "PASS: Kofun emitted deterministic 188-, 231-, and 4099-byte ELF64 images" \
    "PASS: native image exited through Linux x86-64 syscall with status 42" \
    "PASS: native code computed 40 + 2, wrote 42 to stdout, and exited 0" \
    "PASS: rel32 Core call/message fixups printed and exited with 42" \
    "PASS: opt-in debug image has ELF sections, DWARF lines, and a main DIE" \
    "PASS: release Core image remains byte-identical and 231 bytes" \
    "PASS: build --target x86_64-linux -g emitted source-specific DWARF" \
    "PASS: general Native Core release stayed byte-identical and 4099 bytes" \
    "PASS: --target aarch64-linux emitted deterministic static EM_AARCH64 ELF" \
    "PASS: x86-64 and AArch64 consume one target-independent parsed Core" \
    "PASS: x86-64 List[Int] matched C11 and defined OOB/OOM diagnostics"
