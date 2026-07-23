#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
NATIVE="$ROOT/bootstrap/native"
KOFUN="$ROOT/bin/kofun"
WORK=${KOFUN_NATIVE_CHECK_WORK:-"$ROOT/build/native-check"}
CC=${CC:-cc}

AARCH64_RUNNER=${QEMU_AARCH64-}
if test -n "$AARCH64_RUNNER" &&
   command -v "$AARCH64_RUNNER" >/dev/null 2>&1
then
    :
elif command -v qemu-aarch64 >/dev/null 2>&1; then
    AARCH64_RUNNER=$(command -v qemu-aarch64)
elif command -v qemu-aarch64-static >/dev/null 2>&1; then
    AARCH64_RUNNER=$(command -v qemu-aarch64-static)
else
    AARCH64_RUNNER=
fi

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

    if test -n "$AARCH64_RUNNER"; then
        "$AARCH64_RUNNER" "$WORK/$name-aarch64.elf" \
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

# The multi-function Int Core is lowered directly to native calls for both
# x86-64 and AArch64. This runs the public example rather than a reduced
# surrogate, so arbitrary-width integer printing, recursion, parameters,
# returns, and call fixups are all observable in one static ELF. x86-64
# executes directly; AArch64 executes under qemu when the emulator is present.
"$KOFUN" build "$ROOT/examples/fibonacci_native.kofun" \
    --target x86_64-linux \
    -o "$WORK/fibonacci-native.elf" >/dev/null
"$WORK/fibonacci-native.elf" \
    >"$WORK/fibonacci-native.stdout" \
    2>"$WORK/fibonacci-native.stderr"
printf '6765\n' >"$WORK/fibonacci-native.expected"
cmp "$WORK/fibonacci-native.expected" "$WORK/fibonacci-native.stdout"
test ! -s "$WORK/fibonacci-native.stderr"

"$KOFUN" build "$ROOT/examples/fibonacci_native.kofun" \
    --target aarch64-linux \
    -o "$WORK/fibonacci-native-aarch64.elf" >/dev/null
readelf -h "$WORK/fibonacci-native-aarch64.elf" \
    >"$WORK/fibonacci-native-aarch64.elf-header.txt"
grep -Eq 'Machine:[[:space:]]+AArch64' \
    "$WORK/fibonacci-native-aarch64.elf-header.txt"

"$KOFUN" build "$NATIVE/fixtures/function_overflow.kofun" \
    --target x86_64-linux \
    -o "$WORK/function-overflow.elf" >/dev/null
"$KOFUN" build "$NATIVE/fixtures/function_overflow.kofun" \
    --target aarch64-linux \
    -o "$WORK/function-overflow-aarch64.elf" >/dev/null
set +e
"$WORK/function-overflow.elf" \
    >"$WORK/function-overflow.stdout" \
    2>"$WORK/function-overflow.stderr"
function_overflow_status=$?
"$KOFUN" build "$NATIVE/fixtures/function_unknown.kofun" \
    --target x86_64-linux \
    -o "$WORK/function-unknown.elf" \
    >"$WORK/function-unknown.stdout" \
    2>"$WORK/function-unknown.stderr"
function_unknown_status=$?
"$KOFUN" build "$NATIVE/fixtures/function_arity.kofun" \
    --target x86_64-linux \
    -o "$WORK/function-arity.elf" \
    >"$WORK/function-arity.stdout" \
    2>"$WORK/function-arity.stderr"
function_arity_status=$?
# The unknown-symbol and arity diagnostics are selected before instruction
# selection, so AArch64 rejects the same programs with the same messages.
"$KOFUN" build "$NATIVE/fixtures/function_unknown.kofun" \
    --target aarch64-linux \
    -o "$WORK/function-unknown-aarch64.elf" \
    >"$WORK/function-unknown-aarch64.stdout" \
    2>"$WORK/function-unknown-aarch64.stderr"
function_unknown_aarch64_status=$?
set -e
test "$function_overflow_status" -eq 1
test ! -s "$WORK/function-overflow.stdout"
printf 'kofun: integer overflow\n' \
    >"$WORK/function-overflow.expected"
cmp "$WORK/function-overflow.expected" \
    "$WORK/function-overflow.stderr"
test "$function_unknown_status" -eq 1
test ! -e "$WORK/function-unknown.elf"
grep 'unknown native Core function `missing`' \
    "$WORK/function-unknown.stderr" >/dev/null
test "$function_arity_status" -eq 1
test ! -e "$WORK/function-arity.elf"
grep 'native Core function `add` expects 2 arguments, got 1' \
    "$WORK/function-arity.stderr" >/dev/null
test "$function_unknown_aarch64_status" -eq 1
test ! -e "$WORK/function-unknown-aarch64.elf"
grep 'unknown native Core function `missing`' \
    "$WORK/function-unknown-aarch64.stderr" >/dev/null

# AArch64 user-defined functions now execute. Under qemu the fibonacci example
# and the checked-overflow fixture must match the x86-64 observations exactly:
# identical stdout, identical diagnostic text, and identical exit status.
if test -n "$AARCH64_RUNNER"; then
    "$AARCH64_RUNNER" "$WORK/fibonacci-native-aarch64.elf" \
        >"$WORK/fibonacci-native-aarch64.stdout" \
        2>"$WORK/fibonacci-native-aarch64.stderr"
    cmp "$WORK/fibonacci-native.expected" \
        "$WORK/fibonacci-native-aarch64.stdout"
    test ! -s "$WORK/fibonacci-native-aarch64.stderr"

    set +e
    "$AARCH64_RUNNER" "$WORK/function-overflow-aarch64.elf" \
        >"$WORK/function-overflow-aarch64.stdout" \
        2>"$WORK/function-overflow-aarch64.stderr"
    function_overflow_aarch64_status=$?
    set -e
    test "$function_overflow_aarch64_status" -eq 1
    test ! -s "$WORK/function-overflow-aarch64.stdout"
    cmp "$WORK/function-overflow.expected" \
        "$WORK/function-overflow-aarch64.stderr"
    printf '%s\n' \
        "PASS: fibonacci and overflow differential under qemu-aarch64"
else
    printf '%s\n' \
        "SKIP: AArch64 function execution (qemu-aarch64 unavailable)"
fi

# List[Int] uses the same Core AST and value ABI on x86-64 and AArch64. An
# independent C11 executable is the normative Python-free differential
# reference for bindings, indexing, map, filter, fold, and their edge cases.
# Every AArch64 case is built twice and audited even without qemu.
"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$NATIVE/fixtures/list_int_reference.c" \
    -o "$WORK/core-list-reference"
LIST_CORPUS="$ROOT/tests/conformance/list"

run_native_list_differential() {
    source=$1
    stem=$2
    mode=$3
    "$KOFUN" build "$source" \
        --target x86_64-linux \
        -o "$WORK/$stem-x86_64.elf" >/dev/null
    "$KOFUN" build "$source" \
        --target aarch64-linux \
        -o "$WORK/$stem-aarch64.elf" >/dev/null
    "$KOFUN" build "$source" \
        --target aarch64-linux \
        -o "$WORK/$stem-aarch64.second.elf" >/dev/null
    cmp \
        "$WORK/$stem-aarch64.elf" \
        "$WORK/$stem-aarch64.second.elf"
    readelf -h "$WORK/$stem-aarch64.elf" \
        >"$WORK/$stem-aarch64.header"
    grep -Eq 'Machine:[[:space:]]+AArch64' \
        "$WORK/$stem-aarch64.header"
    "$WORK/core-list-reference" "$mode" \
        >"$WORK/$stem-reference.stdout"
    "$WORK/$stem-x86_64.elf" \
        >"$WORK/$stem.stdout" \
        2>"$WORK/$stem.stderr"
    cmp "$WORK/$stem-reference.stdout" "$WORK/$stem.stdout"
    test ! -s "$WORK/$stem.stderr"

    if test -n "$AARCH64_RUNNER"; then
        "$AARCH64_RUNNER" "$WORK/$stem-aarch64.elf" \
            >"$WORK/$stem-aarch64.stdout" \
            2>"$WORK/$stem-aarch64.stderr"
        cmp \
            "$WORK/$stem-reference.stdout" \
            "$WORK/$stem-aarch64.stdout"
        test ! -s "$WORK/$stem-aarch64.stderr"
    fi
}

run_native_list_differential \
    "$LIST_CORPUS/negative_index.kofun" \
    core-list-index \
    index-negative
run_native_list_differential \
    "$LIST_CORPUS/binding_index.kofun" \
    core-list-positive \
    binding
run_native_list_differential \
    "$LIST_CORPUS/length.kofun" \
    core-list-len \
    length
run_native_list_differential \
    "$LIST_CORPUS/binding_index.kofun" \
    core-list-binding \
    binding
run_native_list_differential \
    "$LIST_CORPUS/map_runtime.kofun" \
    core-list-map \
    map
run_native_list_differential \
    "$LIST_CORPUS/filter_runtime.kofun" \
    core-list-filter \
    filter
run_native_list_differential \
    "$LIST_CORPUS/fold_runtime.kofun" \
    core-list-fold \
    fold
run_native_list_differential \
    "$LIST_CORPUS/pipeline_runtime.kofun" \
    core-list-pipeline \
    pipeline
run_native_list_differential \
    "$LIST_CORPUS/empty_map.kofun" \
    core-list-empty-map \
    empty-map
run_native_list_differential \
    "$LIST_CORPUS/empty_filter.kofun" \
    core-list-empty-filter \
    empty-filter
run_native_list_differential \
    "$LIST_CORPUS/empty_fold.kofun" \
    core-list-empty-fold \
    empty-fold
run_native_list_differential \
    "$LIST_CORPUS/filter_all_false.kofun" \
    core-list-all-false \
    all-false
run_native_list_differential \
    "$LIST_CORPUS/filter_negative_values.kofun" \
    core-list-negative-predicate \
    negative-predicate
"$KOFUN" build "$LIST_CORPUS/index_out_of_range.kofun" \
    --target x86_64-linux \
    -o "$WORK/core-list-variable-oob-x86_64.elf" >/dev/null
"$KOFUN" build "$LIST_CORPUS/index_out_of_range.kofun" \
    --target aarch64-linux \
    -o "$WORK/core-list-variable-oob-aarch64.elf" >/dev/null
"$KOFUN" build "$LIST_CORPUS/index_out_of_range.kofun" \
    --target aarch64-linux \
    -o "$WORK/core-list-variable-oob-aarch64.second.elf" >/dev/null
cmp \
    "$WORK/core-list-variable-oob-aarch64.elf" \
    "$WORK/core-list-variable-oob-aarch64.second.elf"
readelf -h "$WORK/core-list-variable-oob-aarch64.elf" \
    >"$WORK/core-list-variable-oob-aarch64.header"
grep -Eq 'Machine:[[:space:]]+AArch64' \
    "$WORK/core-list-variable-oob-aarch64.header"

# At 2560 KiB the source and map output allocations both succeed. The chained
# filter/map/fold case needs a third 1 MiB mmap and must take the exact OOM
# path. This proves the failure is observed during real multi-allocation
# higher-order execution rather than only while materializing a source literal.
(
    ulimit -v 2560
    exec "$WORK/core-list-map-x86_64.elf"
) >"$WORK/core-list-two-allocations.stdout" \
    2>"$WORK/core-list-two-allocations.stderr"
cmp \
    "$WORK/core-list-map-reference.stdout" \
    "$WORK/core-list-two-allocations.stdout"
test ! -s "$WORK/core-list-two-allocations.stderr"

set +e
"$WORK/core-list-variable-oob-x86_64.elf" \
    >"$WORK/core-list-oob.stdout" \
    2>"$WORK/core-list-oob.stderr"
list_oob_status=$?
(
    ulimit -v 2560
    exec "$WORK/core-list-pipeline-x86_64.elf"
) >"$WORK/core-list-oom.stdout" 2>"$WORK/core-list-oom.stderr"
list_oom_status=$?
if test -n "$AARCH64_RUNNER"; then
    "$AARCH64_RUNNER" "$WORK/core-list-variable-oob-aarch64.elf" \
        >"$WORK/core-list-oob-aarch64.stdout" \
        2>"$WORK/core-list-oob-aarch64.stderr"
    list_oob_aarch64_status=$?
fi
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
if test -n "$AARCH64_RUNNER"; then
    test "$list_oob_aarch64_status" -eq 1
    test ! -s "$WORK/core-list-oob-aarch64.stdout"
    cmp \
        "$WORK/core-list-oob.expected" \
        "$WORK/core-list-oob-aarch64.stderr"
    printf '%s\n' \
        "PASS: AArch64 List differential under $AARCH64_RUNNER"
else
    printf '%s\n' \
        "SKIP: AArch64 List execution (qemu-aarch64 unavailable)"
fi

# Text uses `[byte length: i64][UTF-8 bytes]`. Each generated static ELF is
# compared with an independent C11 codepoint scanner, including multi-byte
# Japanese, accented Latin, and emoji input.
"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$NATIVE/fixtures/text_reference.c" \
    -o "$WORK/core-text-reference"

run_native_text_differential() {
    source=$1
    stem=$2
    mode=$3
    "$KOFUN" build "$source" \
        --target x86_64-linux \
        -o "$WORK/$stem.elf" >/dev/null
    "$WORK/core-text-reference" "$mode" \
        >"$WORK/$stem.reference"
    "$WORK/$stem.elf" \
        >"$WORK/$stem.stdout" \
        2>"$WORK/$stem.stderr"
    cmp "$WORK/$stem.reference" "$WORK/$stem.stdout"
    test ! -s "$WORK/$stem.stderr"
}

run_native_text_differential \
    "$NATIVE/fixtures/core_text_concat.kofun" \
    core-text-concat \
    concat
run_native_text_differential \
    "$NATIVE/fixtures/core_text_equal.kofun" \
    core-text-equal \
    equal
run_native_text_differential \
    "$NATIVE/fixtures/core_text_not_equal.kofun" \
    core-text-not-equal \
    not-equal
run_native_text_differential \
    "$NATIVE/fixtures/core_text_len_42.kofun" \
    core-text-len-42 \
    len
run_native_text_differential \
    "$NATIVE/fixtures/core_text_index.kofun" \
    core-text-index \
    index
run_native_text_differential \
    "$NATIVE/fixtures/core_text_negative_index.kofun" \
    core-text-negative-index \
    negative-index
run_native_text_differential \
    "$NATIVE/fixtures/core_text_chars_index.kofun" \
    core-text-chars-index \
    chars-index
run_native_text_differential \
    "$NATIVE/fixtures/core_text_empty_chars_len_42.kofun" \
    core-text-empty-chars-len-42 \
    empty-chars-len

"$KOFUN" build "$NATIVE/fixtures/core_text_oob.kofun" \
    --target x86_64-linux \
    -o "$WORK/core-text-oob.elf" >/dev/null
{
    printf 'fn main() {\n    print("'
    printf '\300\257'
    printf '")\n}\n'
} >"$WORK/core-text-invalid-utf8.kofun"
set +e
"$WORK/core-text-oob.elf" \
    >"$WORK/core-text-oob.stdout" \
    2>"$WORK/core-text-oob.stderr"
text_oob_status=$?
"$KOFUN" build "$WORK/core-text-invalid-utf8.kofun" \
    --target x86_64-linux \
    -o "$WORK/core-text-invalid-utf8.elf" \
    >"$WORK/core-text-invalid-utf8.stdout" \
    2>"$WORK/core-text-invalid-utf8.stderr"
text_invalid_utf8_status=$?
(
    ulimit -v 512
    exec "$WORK/core-text-concat.elf"
) >"$WORK/core-text-oom.stdout" 2>"$WORK/core-text-oom.stderr"
text_oom_status=$?
"$KOFUN" build "$NATIVE/fixtures/core_text_concat.kofun" \
    --target aarch64-linux \
    -o "$WORK/core-text-aarch64.elf" \
    >"$WORK/core-text-aarch64.stdout" \
    2>"$WORK/core-text-aarch64.stderr"
text_aarch64_status=$?
set -e

test "$text_oob_status" -eq 1
test ! -s "$WORK/core-text-oob.stdout"
printf 'kofun: text index out of range\n' \
    >"$WORK/core-text-oob.expected"
cmp "$WORK/core-text-oob.expected" "$WORK/core-text-oob.stderr"
test "$text_invalid_utf8_status" -eq 1
test ! -e "$WORK/core-text-invalid-utf8.elf"
grep 'Text literal is not valid UTF-8' \
    "$WORK/core-text-invalid-utf8.stderr" >/dev/null
test "$text_oom_status" -eq 70
test ! -s "$WORK/core-text-oom.stdout"
printf 'kofun: out of memory\n' >"$WORK/core-text-oom.expected"
cmp "$WORK/core-text-oom.expected" "$WORK/core-text-oom.stderr"
test "$text_aarch64_status" -eq 1
test ! -e "$WORK/core-text-aarch64.elf"
grep 'AArch64 native Core does not support Text yet' \
    "$WORK/core-text-aarch64.stderr" >/dev/null

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
    "PASS: x86-64/AArch64 List Core built with shared ABI and diagnostics" \
    "PASS: x86-64 List execution matched C11 with OOB/OOM contracts" \
    "PASS: x86-64 Text matched C11 UTF-8 codepoint and failure semantics"
