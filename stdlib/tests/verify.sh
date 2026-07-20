#!/bin/sh
set -eu

stdlib_dir=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
repo_dir=$(CDPATH= cd -- "$stdlib_dir/.." && pwd)
abi="$stdlib_dir/linux_x86_64/abi.kofun"

fail() {
    printf 'stdlib contract: FAIL: %s\n' "$*" >&2
    exit 1
}

if find "$stdlib_dir" -type f \
    \( -name '*.py' -o -name '*.c' -o -name '*.kf' -o \
       -name '*.s' -o -name '*.S' -o -name '*.asm' -o -name '*.ld' \) |
    grep -q .
then
    fail 'forbidden Python, C, assembly, linker, or .kf implementation found'
fi

check_constant() {
    name=$1
    value=$2
    grep -Fqx "let $name = $value" "$abi" ||
        fail "missing Linux x86-64 constant $name=$value"
}

while read -r name value
do
    check_constant "$name" "$value"
done <<'SYSCALLS'
SYS_READ 0
SYS_WRITE 1
SYS_OPEN 2
SYS_CLOSE 3
SYS_STAT 4
SYS_LSEEK 8
SYS_MMAP 9
SYS_MUNMAP 11
SYS_NANOSLEEP 35
SYS_SOCKET 41
SYS_CONNECT 42
SYS_BIND 49
SYS_LISTEN 50
SYS_SETSOCKOPT 54
SYS_EXIT 60
SYS_CLOCK_GETTIME 228
SYS_EXIT_GROUP 231
SYS_EPOLL_WAIT 232
SYS_EPOLL_CTL 233
SYS_ACCEPT4 288
SYS_EPOLL_CREATE1 291
SYS_GETRANDOM 318
SYSCALLS

for arity in 0 1 2 3 4 5 6
do
    grep -Fq "intrinsic fn __linux_syscall$arity(" "$abi" ||
        fail "missing syscall$arity intrinsic"
done

grep -Fq 'raw >= LINUX_ERROR_LOW && raw <= LINUX_ERROR_HIGH' "$abi" ||
    fail 'negative Linux return range is not checked'
grep -Fq 'errno: -raw' "$abi" ||
    fail 'negative Linux return is not converted to positive errno'
grep -Fq 'type SysResult[T]' "$abi" ||
    fail 'SysResult value type is missing'
grep -Fq '| Err(SysError)' "$abi" ||
    fail 'SysResult does not carry SysError'

trusted_outside_abi=$(
    find "$stdlib_dir/linux_x86_64" -type f -name '*.kofun' \
        ! -name 'abi.kofun' -exec grep -Hn 'trusted intrinsic' {} + || true
)
[ -z "$trusted_outside_abi" ] ||
    fail 'trusted intrinsic declaration escaped abi.kofun'

intrinsic_outside_abi=$(
    find "$stdlib_dir/linux_x86_64" -type f -name '*.kofun' \
        ! -name 'abi.kofun' -exec grep -Hn '__linux_syscall\|__[a-z]' {} + ||
        true
)
[ -z "$intrinsic_outside_abi" ] ||
    fail 'trusted intrinsic invocation escaped abi.kofun'

for function in \
    raw_open raw_close raw_read raw_write raw_lseek raw_stat \
    raw_mmap raw_munmap raw_socket raw_bind raw_listen raw_accept4 \
    raw_connect raw_setsockopt raw_epoll_create1 raw_epoll_ctl \
    raw_epoll_wait raw_clock_gettime raw_nanosleep raw_getrandom \
    raw_exit raw_exit_group
do
    grep -Fq "fn $function(" "$abi" ||
        fail "missing raw wrapper $function"
done

for signature in \
    'fn file_close(take file: File)' \
    'fn socket_close(take socket: Socket)' \
    'fn epoll_close(take epoll: Epoll)' \
    'fn memory_unmap(take mapping: MemoryMap)'
do
    grep -R -Fq "$signature" "$stdlib_dir/linux_x86_64" ||
        fail "missing affine signature: $signature"
done

for operation in \
    open close read write lseek stat mmap munmap socket bind listen \
    accept4 connect setsockopt epoll_create1 epoll_ctl epoll_wait \
    clock_gettime nanosleep getrandom
do
    grep -R -Fq "\"$operation\"" "$stdlib_dir/linux_x86_64" ||
        fail "safe error operation is missing: $operation"
done

roundtrip="$stdlib_dir/tests/file_roundtrip.kofun"
for call in file_create file_write_all file_seek file_read_exact file_close
do
    grep -Fq "$call(" "$roundtrip" ||
        fail "file round-trip fixture does not call $call"
done
grep -Fq 'bytes_same(payload, received)' "$roundtrip" ||
    fail 'file round-trip fixture does not compare bytes'

text_reference="$stdlib_dir/text/utf8.kofun"
for function in \
    text_from_utf8 text_byte_length text_length text_at text_chars \
    text_concat text_equal text_utf8_bytes
do
    grep -Fq "fn $function(" "$text_reference" ||
        fail "missing Text reference function $function"
done
for error in \
    InvalidByte UnexpectedContinuation InvalidLead InvalidContinuation \
    TruncatedSequence OverlongEncoding SurrogateScalar ScalarOutOfRange \
    IndexOutOfBounds InvalidWidth
do
    grep -Fq "$error" "$text_reference" ||
        fail "missing typed Text boundary error $error"
done
if grep -Eq 'trusted|intrinsic|__[a-z]' "$text_reference"
then
    fail 'Text reference is not pure Kofun'
fi

text_fixture="$stdlib_dir/tests/text_utf8.kofun"
for fixture in ascii japanese arabic hindi emoji
do
    grep -Fq "let $fixture =" "$text_fixture" ||
        fail "missing Text fixture $fixture"
done
grep -Fq '古墳' "$text_fixture" ||
    fail 'missing Japanese Text fixture'
grep -Fq 'مرحبا' "$text_fixture" ||
    fail 'missing Arabic Text fixture'
grep -Fq 'नमस्ते' "$text_fixture" ||
    fail 'missing Hindi Text fixture'
grep -Fq 'decode_utf8_at([], 0)' \
    "$stdlib_dir/tests/text_utf8_invalid.kofun" ||
    fail 'missing direct UTF-8 decoder boundary fixture'

bytes_reference="$stdlib_dir/bytes/reference.kofun"
for function in \
    bytes_from_list bytes_length bytes_get bytes_slice bytes_concat \
    bytes_equal bytes_checked_length_sum \
    bytes_read_uint_le bytes_write_uint_le \
    bytes_read_i64_le bytes_write_i64_le
do
    grep -Fq "fn $function(" "$bytes_reference" ||
        fail "missing Bytes reference function $function"
done
for error in \
    InvalidByte IndexOutOfBounds SliceOutOfBounds InvalidWidth \
    SpanOutOfBounds UnsignedOutOfRange InvalidLength LengthOverflow
do
    grep -Fq "$error" "$bytes_reference" ||
        fail "missing typed Bytes boundary error $error"
done
if grep -Eq 'trusted|intrinsic|__[a-z]' "$bytes_reference"
then
    fail 'Bytes reference is not pure Kofun'
fi
grep -Fq '72057594037927935' \
    "$stdlib_dir/tests/bytes_reference.kofun" ||
    fail 'missing seven-byte unsigned Bytes boundary fixture'
grep -Fq 'UnsignedOutOfRange(65536, 2)' \
    "$stdlib_dir/tests/bytes_invalid.kofun" ||
    fail 'missing Bytes little-endian overflow fixture'
grep -Fq 'LengthOverflow(9223372036854775807, 1)' \
    "$stdlib_dir/tests/bytes_invalid.kofun" ||
    fail 'missing Bytes length overflow fixture'
grep -Fq 'SliceOutOfBounds(3, 2, 4)' \
    "$stdlib_dir/tests/bytes_invalid.kofun" ||
    fail 'missing reversed Bytes slice fixture'

list_reference="$stdlib_dir/list/reference.kofun"
for function in \
    int_list_allocation_size int_list_checked_length_sum int_list_validate \
    int_list_length int_list_get int_list_slice int_list_concat \
    int_list_equal int_list_map int_list_filter int_list_fold
do
    grep -Fq "fn $function(" "$list_reference" ||
        fail "missing immutable List[Int] reference function $function"
done
for error in \
    InvalidLength IndexOutOfBounds SliceOutOfBounds LengthOverflow \
    AllocationSizeOverflow
do
    grep -Fq "$error" "$list_reference" ||
        fail "missing typed immutable List[Int] error $error"
done
grep -Fq 'let INT_LIST_HEADER_BYTES = 8' "$list_reference" ||
    fail 'List[Int] v1 header is not eight bytes'
grep -Fq 'let INT_LIST_ITEM_BYTES = 8' "$list_reference" ||
    fail 'List[Int] v1 item width is not eight bytes'
grep -Fq 'let INT_LIST_ALIGNMENT = 16' "$list_reference" ||
    fail 'List[Int] v1 allocation is not 16-byte aligned'
grep -Fq 'let MAX_INT_LIST_LENGTH = 1152921504606846973' \
    "$list_reference" ||
    fail 'List[Int] maximum aligned allocation length differs'
if grep -Eq 'trusted|intrinsic|__[a-z]' "$list_reference"
then
    fail 'immutable List[Int] reference is not pure Kofun'
fi
grep -Fq 'Ok(48)' "$stdlib_dir/tests/list_reference.kofun" ||
    fail 'missing five-element List[Int] aligned-size fixture'
grep -Fq 'Ok(3)' "$stdlib_dir/tests/list_reference.kofun" ||
    fail 'missing List[Int] index-two positive fixture'
grep -Fq 'int_list_fold(filtered, 0, add) == 6' \
    "$stdlib_dir/tests/list_reference.kofun" ||
    fail 'missing List[Int] map/filter/fold semantic fixture'
grep -Fq 'IndexOutOfBounds(5, 5)' \
    "$stdlib_dir/tests/list_invalid.kofun" ||
    fail 'missing List[Int] upper bounds fixture'
grep -Fq 'LengthOverflow(1152921504606846973, 1)' \
    "$stdlib_dir/tests/list_invalid.kofun" ||
    fail 'missing List[Int] length overflow fixture'

tmp_dir=${TMPDIR:-/tmp}/kofun-stdlib-verify.$$
mkdir -p "$tmp_dir"
fixture_file="$tmp_dir/kofun-syscall-roundtrip.fixture"

cleanup() {
    rm -rf "$tmp_dir"
}
trap cleanup EXIT HUP INT TERM

"$repo_dir/bin/kofun" run "$stdlib_dir/tests/errno_core.kofun" \
    >"$tmp_dir/errno-core.out"
printf '1\n2\n4095\n7\n' >"$tmp_dir/errno-core.expected"
cmp "$tmp_dir/errno-core.expected" "$tmp_dir/errno-core.out" ||
    fail 'Stage 1 executable errno Core fixture differs'

printf 'stdlib contract: PASS\n'
printf 'stdlib Stage 1 errno Core: PASS\n'

[ "$(uname -s)" = Linux ] ||
    fail 'native file round-trip requires Linux'
[ "$(uname -m)" = x86_64 ] ||
    fail 'native file round-trip requires x86-64'

native_source="$stdlib_dir/tests/file_roundtrip_native.packed.kofun"
native_emitter="$tmp_dir/emit-file-roundtrip"
native_image="$tmp_dir/file_roundtrip.elf"

"$repo_dir/bin/kofun" build "$native_source" \
    -o "$native_emitter" \
    --emit-c "$tmp_dir/emit-file-roundtrip.c" >/dev/null
"$native_emitter" >"$tmp_dir/file_roundtrip.packed"

# Transport six-byte little-endian words from Kofun stdout into the ELF file.
# This loop does not select or generate any ELF header or instruction value.
: >"$native_image"
packed=
while IFS= read -r field
do
    case $field in
        ''|*[!0-9]*)
            fail "invalid native packed field: $field"
            ;;
    esac

    if [ -z "$packed" ]
    then
        [ "$field" -le 281474976710655 ] ||
            fail "packed native word exceeds six bytes: $field"
        packed=$field
        continue
    fi

    count=$field
    [ "$count" -ge 1 ] && [ "$count" -le 6 ] ||
        fail "invalid packed native byte count: $count"
    while [ "$count" -gt 0 ]
    do
        byte=$((packed % 256))
        octal=$(printf '%03o' "$byte")
        printf "\\$octal" >>"$native_image"
        packed=$((packed / 256))
        count=$((count - 1))
    done
    [ "$packed" -eq 0 ] ||
        fail 'packed native word has data beyond its declared byte count'
    packed=
done <"$tmp_dir/file_roundtrip.packed"
[ -z "$packed" ] ||
    fail 'packed native stream ended without a byte count'

[ "$(wc -c <"$native_image" | tr -d ' ')" -eq 459 ] ||
    fail 'native file round-trip ELF has the wrong size'
(
    cd "$tmp_dir"
    sha256sum -c "$stdlib_dir/tests/SHA256SUMS"
) >/dev/null

if command -v readelf >/dev/null 2>&1
then
    readelf -h "$native_image" >"$tmp_dir/elf-header.txt"
    readelf -l "$native_image" >"$tmp_dir/program-headers.txt"
    grep -Eq 'Class:[[:space:]]+ELF64' "$tmp_dir/elf-header.txt" ||
        fail 'native fixture is not ELF64'
    grep -Eq 'Machine:[[:space:]]+Advanced Micro Devices X86-64' \
        "$tmp_dir/elf-header.txt" ||
        fail 'native fixture is not x86-64'
    [ "$(grep -c 'LOAD' "$tmp_dir/program-headers.txt")" -eq 2 ] ||
        fail 'native fixture does not have separate RX and RW load segments'
fi

[ ! -e "$fixture_file" ] ||
    fail 'native fixture path unexpectedly exists before execution'
chmod +x "$native_image"

# Force open(2) to return -EISDIR and prove the image takes a nonzero failure
# path instead of treating a negative raw return as a descriptor.
mkdir "$fixture_file"
set +e
(
    cd "$tmp_dir"
    ./file_roundtrip.elf
) >"$tmp_dir/negative.out" 2>"$tmp_dir/negative.err"
negative_status=$?
set -e
[ "$negative_status" -eq 1 ] ||
    fail "native negative-return probe exited $negative_status instead of 1"
[ ! -s "$tmp_dir/negative.out" ] ||
    fail 'native negative-return probe printed a success result'
[ ! -s "$tmp_dir/negative.err" ] ||
    fail 'native negative-return probe wrote to stderr'
rmdir "$fixture_file"

set +e
(
    cd "$tmp_dir"
    ./file_roundtrip.elf
) >"$tmp_dir/roundtrip.out" 2>"$tmp_dir/roundtrip.err"
native_status=$?
set -e
[ "$native_status" -eq 0 ] ||
    fail "native file round-trip exited $native_status"
[ ! -s "$tmp_dir/roundtrip.err" ] ||
    fail 'native file round-trip wrote to stderr'
printf 'syscall-file-roundtrip: ok\n' >"$tmp_dir/roundtrip.expected"
cmp "$tmp_dir/roundtrip.expected" "$tmp_dir/roundtrip.out" ||
    fail 'native file round-trip output differs'
[ -f "$fixture_file" ] ||
    fail 'native file round-trip did not create the fixture'
[ "$(wc -c <"$fixture_file" | tr -d ' ')" -eq 6 ] ||
    fail 'native file round-trip fixture has the wrong size'
rm -f "$fixture_file"
[ ! -e "$fixture_file" ] ||
    fail 'native file round-trip fixture cleanup failed'

printf 'stdlib native file round-trip: PASS\n'

text_native_source="$stdlib_dir/tests/text_utf8_native.packed.kofun"
text_native_emitter="$tmp_dir/emit-text-utf8"
text_native_image="$tmp_dir/text_utf8.elf"

"$repo_dir/bin/kofun" build "$text_native_source" \
    -o "$text_native_emitter" \
    --emit-c "$tmp_dir/emit-text-utf8.c" >/dev/null
"$text_native_emitter" >"$tmp_dir/text_utf8.packed"

# Transport seven-byte little-endian words from Kofun stdout into the UTF-8
# scanner image. Header, instruction, fixture, and message bytes are all values
# emitted by the Kofun source.
: >"$text_native_image"
packed=
while IFS= read -r field
do
    case $field in
        ''|*[!0-9]*)
            fail "invalid Text native packed field: $field"
            ;;
    esac

    if [ -z "$packed" ]
    then
        [ "$field" -le 72057594037927935 ] ||
            fail "packed Text native word exceeds seven bytes: $field"
        packed=$field
        continue
    fi

    count=$field
    [ "$count" -ge 1 ] && [ "$count" -le 7 ] ||
        fail "invalid packed Text native byte count: $count"
    while [ "$count" -gt 0 ]
    do
        byte=$((packed % 256))
        octal=$(printf '%03o' "$byte")
        printf "\\$octal" >>"$text_native_image"
        packed=$((packed / 256))
        count=$((count - 1))
    done
    [ "$packed" -eq 0 ] ||
        fail 'packed Text word has data beyond its declared byte count'
    packed=
done <"$tmp_dir/text_utf8.packed"
[ -z "$packed" ] ||
    fail 'packed Text native stream ended without a byte count'

[ "$(wc -c <"$text_native_image" | tr -d ' ')" -eq 1084 ] ||
    fail 'native Text UTF-8 ELF has the wrong size'
(
    cd "$tmp_dir"
    sha256sum -c "$stdlib_dir/tests/TEXT_SHA256SUMS"
) >/dev/null

if command -v readelf >/dev/null 2>&1
then
    readelf -h "$text_native_image" >"$tmp_dir/text-elf-header.txt"
    readelf -l "$text_native_image" >"$tmp_dir/text-program-headers.txt"
    grep -Eq 'Class:[[:space:]]+ELF64' "$tmp_dir/text-elf-header.txt" ||
        fail 'native Text fixture is not ELF64'
    grep -Eq 'Machine:[[:space:]]+Advanced Micro Devices X86-64' \
        "$tmp_dir/text-elf-header.txt" ||
        fail 'native Text fixture is not x86-64'
    [ "$(grep -c 'LOAD' "$tmp_dir/text-program-headers.txt")" -eq 2 ] ||
        fail 'native Text fixture lacks separate RX and RW load segments'
fi

chmod +x "$text_native_image"
set +e
"$text_native_image" \
    >"$tmp_dir/text-utf8.out" 2>"$tmp_dir/text-utf8.err"
text_native_status=$?
set -e
[ "$text_native_status" -eq 0 ] ||
    fail "native Text UTF-8 scanner exited $text_native_status"
[ ! -s "$tmp_dir/text-utf8.err" ] ||
    fail 'native Text UTF-8 scanner wrote to stderr'
printf '%s\n' \
    'text-utf8: ascii=5 japanese=2 arabic=5 hindi=6 emoji=1 invalid=6' \
    >"$tmp_dir/text-utf8.expected"
cmp "$tmp_dir/text-utf8.expected" "$tmp_dir/text-utf8.out" ||
    fail 'native Text UTF-8 observations differ'

printf 'stdlib Text UTF-8 native scan: PASS\n'

bytes_native_source="$stdlib_dir/tests/bytes_native.packed.kofun"
bytes_native_emitter="$tmp_dir/emit-bytes"
bytes_native_image="$tmp_dir/bytes.elf"

"$repo_dir/bin/kofun" build "$bytes_native_source" \
    -o "$bytes_native_emitter" \
    --emit-c "$tmp_dir/emit-bytes.c" >/dev/null
"$bytes_native_emitter" >"$tmp_dir/bytes.packed"

# Transport Kofun-authored seven-byte words into the Bytes acceptance image.
: >"$bytes_native_image"
packed=
while IFS= read -r field
do
    case $field in
        ''|*[!0-9]*)
            fail "invalid Bytes native packed field: $field"
            ;;
    esac

    if [ -z "$packed" ]
    then
        [ "$field" -le 72057594037927935 ] ||
            fail "packed Bytes native word exceeds seven bytes: $field"
        packed=$field
        continue
    fi

    count=$field
    [ "$count" -ge 1 ] && [ "$count" -le 7 ] ||
        fail "invalid packed Bytes native byte count: $count"
    while [ "$count" -gt 0 ]
    do
        byte=$((packed % 256))
        octal=$(printf '%03o' "$byte")
        printf "\\$octal" >>"$bytes_native_image"
        packed=$((packed / 256))
        count=$((count - 1))
    done
    [ "$packed" -eq 0 ] ||
        fail 'packed Bytes word has data beyond its declared byte count'
    packed=
done <"$tmp_dir/bytes.packed"
[ -z "$packed" ] ||
    fail 'packed Bytes native stream ended without a byte count'

[ "$(wc -c <"$bytes_native_image" | tr -d ' ')" -eq 1621 ] ||
    fail 'native Bytes ELF has the wrong size'
(
    cd "$tmp_dir"
    sha256sum -c "$stdlib_dir/tests/BYTES_SHA256SUMS"
) >/dev/null

if command -v readelf >/dev/null 2>&1
then
    readelf -h "$bytes_native_image" >"$tmp_dir/bytes-elf-header.txt"
    readelf -l "$bytes_native_image" >"$tmp_dir/bytes-program-headers.txt"
    grep -Eq 'Class:[[:space:]]+ELF64' "$tmp_dir/bytes-elf-header.txt" ||
        fail 'native Bytes fixture is not ELF64'
    grep -Eq 'Machine:[[:space:]]+Advanced Micro Devices X86-64' \
        "$tmp_dir/bytes-elf-header.txt" ||
        fail 'native Bytes fixture is not x86-64'
    [ "$(grep -c 'LOAD' "$tmp_dir/bytes-program-headers.txt")" -eq 2 ] ||
        fail 'native Bytes fixture lacks separate RX and RW load segments'
fi

chmod +x "$bytes_native_image"
set +e
"$bytes_native_image" \
    >"$tmp_dir/bytes.out" 2>"$tmp_dir/bytes.err"
bytes_native_status=$?
set -e
[ "$bytes_native_status" -eq 0 ] ||
    fail "native Bytes reference exited $bytes_native_status"
[ ! -s "$tmp_dir/bytes.err" ] ||
    fail 'native Bytes reference wrote to stderr'
printf '%s\n' \
    'bytes-native: validate=3 length=5 get=255 slice=3 concat=5 le16=4660 le32=305419896 negatives=9' \
    >"$tmp_dir/bytes.expected"
cmp "$tmp_dir/bytes.expected" "$tmp_dir/bytes.out" ||
    fail 'native Bytes observations differ'

printf 'stdlib Bytes native reference: PASS\n'

list_native_source="$stdlib_dir/tests/list_native.packed.kofun"
list_native_emitter="$tmp_dir/emit-list"
list_native_image="$tmp_dir/list.elf"

"$repo_dir/bin/kofun" build "$list_native_source" \
    -o "$list_native_emitter" \
    --emit-c "$tmp_dir/emit-list.c" >/dev/null
"$list_native_emitter" >"$tmp_dir/list.packed"

# Transport Kofun-authored seven-byte words into the immutable List[Int]
# acceptance image. No header, instruction, layout, or expected value is
# selected by this loop.
: >"$list_native_image"
packed=
while IFS= read -r field
do
    case $field in
        ''|*[!0-9]*)
            fail "invalid List native packed field: $field"
            ;;
    esac

    if [ -z "$packed" ]
    then
        [ "$field" -le 72057594037927935 ] ||
            fail "packed List native word exceeds seven bytes: $field"
        packed=$field
        continue
    fi

    count=$field
    [ "$count" -ge 1 ] && [ "$count" -le 7 ] ||
        fail "invalid packed List native byte count: $count"
    while [ "$count" -gt 0 ]
    do
        byte=$((packed % 256))
        octal=$(printf '%03o' "$byte")
        printf "\\$octal" >>"$list_native_image"
        packed=$((packed / 256))
        count=$((count - 1))
    done
    [ "$packed" -eq 0 ] ||
        fail 'packed List word has data beyond its declared byte count'
    packed=
done <"$tmp_dir/list.packed"
[ -z "$packed" ] ||
    fail 'packed List native stream ended without a byte count'

[ "$(wc -c <"$list_native_image" | tr -d ' ')" -eq 1464 ] ||
    fail 'native immutable List[Int] ELF has the wrong size'
(
    cd "$tmp_dir"
    sha256sum -c "$stdlib_dir/tests/LIST_SHA256SUMS"
) >/dev/null

if command -v readelf >/dev/null 2>&1
then
    readelf -h "$list_native_image" >"$tmp_dir/list-elf-header.txt"
    readelf -l "$list_native_image" >"$tmp_dir/list-program-headers.txt"
    grep -Eq 'Class:[[:space:]]+ELF64' "$tmp_dir/list-elf-header.txt" ||
        fail 'native List fixture is not ELF64'
    grep -Eq 'Machine:[[:space:]]+Advanced Micro Devices X86-64' \
        "$tmp_dir/list-elf-header.txt" ||
        fail 'native List fixture is not x86-64'
    [ "$(grep -c 'LOAD' "$tmp_dir/list-program-headers.txt")" -eq 2 ] ||
        fail 'native List fixture lacks separate RX and RW load segments'
fi

chmod +x "$list_native_image"
set +e
"$list_native_image" \
    >"$tmp_dir/list.out" 2>"$tmp_dir/list.err"
list_native_status=$?
set -e
[ "$list_native_status" -eq 0 ] ||
    fail "native immutable List[Int] reference exited $list_native_status"
[ ! -s "$tmp_dir/list.err" ] ||
    fail 'native immutable List[Int] reference wrote to stderr'
printf '3\n15\n20\n6\n' >"$tmp_dir/list.expected"
cmp "$tmp_dir/list.expected" "$tmp_dir/list.out" ||
    fail 'native immutable List[Int] observations differ'

set +e
"$list_native_image" bounds \
    >"$tmp_dir/list-bounds.out" 2>"$tmp_dir/list-bounds.err"
list_bounds_status=$?
set -e
[ "$list_bounds_status" -eq 1 ] ||
    fail "native List bounds path exited $list_bounds_status instead of 1"
[ ! -s "$tmp_dir/list-bounds.out" ] ||
    fail 'native List bounds path wrote to stdout'
printf 'error[R023]: List index out of bounds\n' \
    >"$tmp_dir/list-bounds.expected"
cmp "$tmp_dir/list-bounds.expected" "$tmp_dir/list-bounds.err" ||
    fail 'native List bounds diagnostic differs'

set +e
"$list_native_image" overflow \
    >"$tmp_dir/list-overflow.out" 2>"$tmp_dir/list-overflow.err"
list_overflow_status=$?
set -e
[ "$list_overflow_status" -eq 1 ] ||
    fail "native List overflow path exited $list_overflow_status instead of 1"
[ ! -s "$tmp_dir/list-overflow.out" ] ||
    fail 'native List overflow path wrote to stdout'
printf 'error[R024]: List length overflow\n' \
    >"$tmp_dir/list-overflow.expected"
cmp "$tmp_dir/list-overflow.expected" "$tmp_dir/list-overflow.err" ||
    fail 'native List overflow diagnostic differs'

printf 'stdlib immutable List[Int] native reference: PASS\n'
