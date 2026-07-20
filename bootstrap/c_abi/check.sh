#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
WORK=${KOFUN_C_ABI_WORK:-"$ROOT/build/c-abi"}
CC=${CC:-cc}
SOURCE="$ROOT/tests/ffi/c_abi.kofun"

command -v rustc >/dev/null 2>&1 || {
    printf '%s\n' \
        "FAIL: rustc is required by the active C ABI acceptance gate" >&2
    exit 1
}

rm -rf "$WORK"
mkdir -p "$WORK"

(
    cd "$ROOT/bootstrap/c_abi"
    sha256sum -c SHA256SUMS
)

"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$ROOT/bootstrap/c_abi/compiler.c" -o "$WORK/kofun-c-abi"

"$WORK/kofun-c-abi" "$SOURCE" "$WORK/first.c"
"$WORK/kofun-c-abi" "$SOURCE" "$WORK/second.c"
cmp "$WORK/first.c" "$WORK/second.c"

set +e
"$WORK/kofun-c-abi" \
    "$ROOT/tests/ffi/malformed.kofun" "$WORK/malformed.c" \
    >"$WORK/malformed.stdout" 2>"$WORK/malformed.stderr"
malformed_status=$?
set -e
test "$malformed_status" -ne 0
test ! -e "$WORK/malformed.c"
grep -q 'only `extern "C"` is supported' "$WORK/malformed.stderr"

# libc is available to the explicit host-C path without a separate library.
sed '/rust_add/d; /^extern "C" fn rust_stack_sum(/,/^) -> CLong$/d; /rust_transform/d; /let answer/d; /let stack_answer/d; /let transformed/d; /print(answer)/d; /print(stack_answer)/d; /print(transformed/d' \
    "$SOURCE" >"$WORK/puts.kofun"
"$ROOT/bin/kofun" build "$WORK/puts.kofun" \
    --backend c --c-abi --emit-c "$WORK/puts.c" -o "$WORK/puts"
test "$("$WORK/puts")" = "hello from Kofun C ABI"
grep -Fqx 'extern int puts(const char * message);' "$WORK/puts.c"

set +e
"$ROOT/bin/kofun" build "$WORK/puts.kofun" --c-abi \
    -o "$WORK/implicit-backend" \
    >"$WORK/implicit-backend.stdout" 2>"$WORK/implicit-backend.stderr"
implicit_status=$?
set -e
test "$implicit_status" -ne 0
test ! -e "$WORK/implicit-backend"
grep -q -- '--c-abi requires explicit --backend c' \
    "$WORK/implicit-backend.stderr"

expect_link_rejection() {
    label=$1
    candidate=$2
    set +e
    "$ROOT/bin/kofun" build "$WORK/puts.kofun" \
        --backend c --c-abi --link-library "$candidate" \
        -o "$WORK/rejected-$label" \
        >"$WORK/rejected-$label.stdout" \
        2>"$WORK/rejected-$label.stderr"
    rejected_status=$?
    set -e
    test "$rejected_status" -ne 0
    test ! -e "$WORK/rejected-$label"
}

expect_link_rejection option '-Wl,--export-dynamic'
expect_link_rejection nonregular "$WORK"
newline_path=$(printf 'bad\npath')
expect_link_rejection newline "$newline_path"

printf '%s\n' \
    "PASS: C ABI compiler is deterministic and rejects a non-C ABI" \
    "PASS: Kofun called puts from libc through explicit --backend c --c-abi" \
    "PASS: --c-abi cannot silently select the host-C backend" \
    "PASS: link inputs reject options, non-regular files, and newlines"

if command -v ar >/dev/null 2>&1; then
    "$CC" -std=c11 -O2 -Wall -Wextra -Werror \
        -c "$ROOT/tests/ffi/static_ffi.c" -o "$WORK/static_ffi.o"
    ar rcs "$WORK/libkofun static.a" "$WORK/static_ffi.o"
    "$ROOT/bin/kofun" build "$ROOT/tests/ffi/static_ffi.kofun" \
        --backend c --c-abi \
        --link-library "$WORK/libkofun static.a" \
        --link-library "$WORK/libkofun static.a" \
        -o "$WORK/static-caller"
    test "$("$WORK/static-caller")" = 42
    printf '%s\n' \
        "PASS: repeated --link-library preserves an archive path with spaces"
else
    printf '%s\n' "SKIP: static archive link gate (ar unavailable)"
fi

rustc --crate-type=cdylib \
    "$ROOT/tests/ffi/rust_ffi.rs" -o "$WORK/libkofun_issue21.so"

"$ROOT/bin/kofun" build "$SOURCE" \
    --backend c --c-abi \
    --link-library "$WORK/libkofun_issue21.so" \
    --emit-c "$WORK/kofun.c" -o "$WORK/kofun-caller"

"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$ROOT/tests/ffi/c_caller.c" "$WORK/libkofun_issue21.so" \
    -o "$WORK/c-caller"

"$WORK/kofun-caller" >"$WORK/kofun.stdout"
"$WORK/c-caller" >"$WORK/c.stdout"
sed -n '2,$p' "$WORK/kofun.stdout" >"$WORK/kofun-abi.stdout"
cmp "$WORK/c.stdout" "$WORK/kofun-abi.stdout"
test "$(sed -n '1p' "$WORK/kofun.stdout")" = "hello from Kofun C ABI"
test "$(cat "$WORK/c.stdout")" = "42
36
41
2
3"

grep -Fqx 'typedef struct Pair {' "$WORK/kofun.c"
grep -Fqx 'extern Pair rust_transform(Pair value);' "$WORK/kofun.c"
grep -Fq '_Static_assert(sizeof(Pair) == 24,' "$WORK/kofun.c"
grep -Fq '_Static_assert(_Alignof(Pair) == 8,' "$WORK/kofun.c"
grep -Fqx 'extern long rust_stack_sum(long one, long two, long three, long four, long five, long six, long seven, long eight);' \
    "$WORK/kofun.c"

if command -v readelf >/dev/null 2>&1; then
    readelf -d "$WORK/kofun-caller" >"$WORK/dynamic.txt"
    grep -q 'NEEDED.*libkofun_issue21.so' "$WORK/dynamic.txt"
fi

printf '%s\n' \
    "PASS: Kofun called Rust extern \"C\" functions from a cdylib" \
    "PASS: 24-byte repr(C) hidden-sret pass/return matches the C caller" \
    "PASS: an eight-argument foreign call exercises SysV stack arguments" \
    "PASS: the C ABI output is a dynamically linked executable"
