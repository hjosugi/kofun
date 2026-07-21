#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
WORK=${KOFUN_WASM_CHECK_WORK:-"$ROOT/build/wasm-check"}
CC=${CC:-cc}

for tool in "$CC" node sha256sum cmp
do
    command -v "$tool" >/dev/null 2>&1 || {
        printf '%s\n' "wasm32 gate requires $tool" >&2
        exit 1
    }
done

rm -rf "$WORK"
mkdir -p "$WORK"

repeat_character() (
    count=$1
    character=$2
    index=0
    while test "$index" -lt "$count"
    do
        printf '%s' "$character"
        index=$((index + 1))
    done
)

{
    printf '%s\n' 'fn main() {'
    printf '%s' '    print('
    repeat_character 256 '('
    printf '1'
    repeat_character 256 ')'
    printf '%s\n' ')'
    printf '%s' '    print('
    repeat_character 256 '+'
    printf '%s\n' '1)'
    printf '%s' '    print('
    repeat_character 128 '('
    repeat_character 128 '+'
    printf '1'
    repeat_character 128 ')'
    printf '%s\n' ')' '}'
} >"$WORK/expression-nesting-256.kofun"

{
    printf '%s\n' 'fn main() {'
    printf '%s' '    print('
    repeat_character 257 '('
    printf '1'
    repeat_character 257 ')'
    printf '%s\n' ')' '}'
} >"$WORK/parenthesized-nesting-257.kofun"

{
    printf '%s\n' 'fn main() {'
    printf '%s' '    print('
    repeat_character 257 '+'
    printf '%s\n' '1)' '}'
} >"$WORK/unary-nesting-257.kofun"

{
    printf '%s\n' 'fn main() {'
    printf '%s' '    print('
    repeat_character 128 '('
    repeat_character 129 '+'
    printf '1'
    repeat_character 128 ')'
    printf '%s\n' ')' '}'
} >"$WORK/mixed-nesting-257.kofun"

printf '%s\n' \
    'fn main() {' \
    '    print(-9223372036854775808)' \
    '}' >"$WORK/int64-minimum.kofun"

printf '%s\n' \
    'fn main() {' \
    '    print(--9223372036854775808)' \
    '}' >"$WORK/negated-int64-minimum.kofun"

(
    cd "$ROOT/bootstrap/wasm"
    sha256sum -c SHA256SUMS
)

"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$ROOT/bootstrap/wasm/compiler.c" -o "$WORK/compiler"
"$CC" -std=c11 -O1 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$ROOT/bootstrap/wasm/compiler.c" -o "$WORK/compiler-sanitized"

"$WORK/compiler" \
    "$ROOT/examples/wasm_arithmetic.kofun" "$WORK/direct.wasm"
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
    "$WORK/compiler-sanitized" \
    "$ROOT/examples/wasm_arithmetic.kofun" "$WORK/sanitized.wasm"
cmp "$WORK/direct.wasm" "$WORK/sanitized.wasm"

"$WORK/compiler" \
    "$WORK/expression-nesting-256.kofun" "$WORK/nesting-256.wasm"
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
    "$WORK/compiler-sanitized" \
    "$WORK/expression-nesting-256.kofun" \
    "$WORK/nesting-256-sanitized.wasm"
cmp "$WORK/nesting-256.wasm" "$WORK/nesting-256-sanitized.wasm"
node "$ROOT/bootstrap/wasm/run.mjs" "$WORK/nesting-256.wasm" \
    >"$WORK/nesting-256.stdout" 2>"$WORK/nesting-256.stderr"
printf '1\n1\n1\n' >"$WORK/nesting-256.expected"
cmp "$WORK/nesting-256.expected" "$WORK/nesting-256.stdout"
test ! -s "$WORK/nesting-256.stderr"

"$WORK/compiler" \
    "$WORK/int64-minimum.kofun" "$WORK/int64-minimum.wasm"
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
    "$WORK/compiler-sanitized" \
    "$WORK/int64-minimum.kofun" "$WORK/int64-minimum-sanitized.wasm"
cmp "$WORK/int64-minimum.wasm" "$WORK/int64-minimum-sanitized.wasm"
node "$ROOT/bootstrap/wasm/run.mjs" "$WORK/int64-minimum.wasm" \
    >"$WORK/int64-minimum.stdout" 2>"$WORK/int64-minimum.stderr"
printf '%s\n' '-9223372036854775808' >"$WORK/int64-minimum.expected"
cmp "$WORK/int64-minimum.expected" "$WORK/int64-minimum.stdout"
test ! -s "$WORK/int64-minimum.stderr"

"$WORK/compiler" \
    "$WORK/negated-int64-minimum.kofun" \
    "$WORK/negated-int64-minimum.wasm"
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
    "$WORK/compiler-sanitized" \
    "$WORK/negated-int64-minimum.kofun" \
    "$WORK/negated-int64-minimum-sanitized.wasm"
cmp \
    "$WORK/negated-int64-minimum.wasm" \
    "$WORK/negated-int64-minimum-sanitized.wasm"
set +e
node "$ROOT/bootstrap/wasm/run.mjs" \
    "$WORK/negated-int64-minimum.wasm" \
    >"$WORK/negated-int64-minimum.stdout" \
    2>"$WORK/negated-int64-minimum.stderr"
negated_minimum_status=$?
set -e
test "$negated_minimum_status" -eq 1
test ! -s "$WORK/negated-int64-minimum.stdout"
grep -Fxq \
    'error[R010]: integer overflow in unary operator `-`' \
    "$WORK/negated-int64-minimum.stderr"

"$ROOT/bin/kofun" build "$ROOT/examples/wasm_arithmetic.kofun" \
    --target wasm32 -o "$WORK/cli.wasm" >/dev/null
"$ROOT/bin/kofun" build "$ROOT/examples/wasm_arithmetic.kofun" \
    --target wasm32 -o "$WORK/cli-second.wasm" >/dev/null
cmp "$WORK/direct.wasm" "$WORK/cli.wasm"
cmp "$WORK/cli.wasm" "$WORK/cli-second.wasm"

cp "$WORK/cli.wasm" "$WORK/preserved.wasm"
set +e
"$ROOT/bin/kofun" build \
    "$WORK/parenthesized-nesting-257.kofun" \
    --target wasm32 -o "$WORK/preserved.wasm" \
    >"$WORK/nesting-257.stdout" 2>"$WORK/nesting-257.stderr"
nesting_status=$?
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
    "$WORK/compiler-sanitized" \
    "$WORK/parenthesized-nesting-257.kofun" \
    "$WORK/parenthesized-nesting-257.wasm" \
    >"$WORK/parenthesized-nesting-257.stdout" \
    2>"$WORK/parenthesized-nesting-257.stderr"
sanitized_parenthesized_nesting_status=$?
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
    "$WORK/compiler-sanitized" \
    "$WORK/unary-nesting-257.kofun" "$WORK/unary-nesting-257.wasm" \
    >"$WORK/unary-nesting-257.stdout" \
    2>"$WORK/unary-nesting-257.stderr"
sanitized_nesting_status=$?
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1 \
    "$WORK/compiler-sanitized" \
    "$WORK/mixed-nesting-257.kofun" "$WORK/mixed-nesting-257.wasm" \
    >"$WORK/mixed-nesting-257.stdout" \
    2>"$WORK/mixed-nesting-257.stderr"
sanitized_mixed_nesting_status=$?
set -e
test "$nesting_status" -eq 1
test "$sanitized_parenthesized_nesting_status" -eq 1
test "$sanitized_nesting_status" -eq 1
test "$sanitized_mixed_nesting_status" -eq 1
cmp "$WORK/cli.wasm" "$WORK/preserved.wasm"
test ! -e "$WORK/parenthesized-nesting-257.wasm"
test ! -e "$WORK/unary-nesting-257.wasm"
test ! -e "$WORK/mixed-nesting-257.wasm"
test ! -s "$WORK/nesting-257.stdout"
test ! -s "$WORK/parenthesized-nesting-257.stdout"
test ! -s "$WORK/unary-nesting-257.stdout"
test ! -s "$WORK/mixed-nesting-257.stdout"
grep -Fxq \
    'kofun wasm32: line 2: expression nesting exceeds wasm32 limit of 256' \
    "$WORK/nesting-257.stderr"
grep -Fxq \
    'kofun wasm32: line 2: expression nesting exceeds wasm32 limit of 256' \
    "$WORK/parenthesized-nesting-257.stderr"
grep -Fxq \
    'kofun wasm32: line 2: expression nesting exceeds wasm32 limit of 256' \
    "$WORK/unary-nesting-257.stderr"
grep -Fxq \
    'kofun wasm32: line 2: expression nesting exceeds wasm32 limit of 256' \
    "$WORK/mixed-nesting-257.stderr"
for temporary in "$WORK"/preserved.wasm.tmp.*
do
    test ! -e "$temporary" && test ! -L "$temporary"
done

node --check "$ROOT/bootstrap/wasm/run.mjs"
node --check "$ROOT/examples/wasm-browser/main.mjs"
node --check "$ROOT/examples/wasm-browser/check.mjs"
node --check "$ROOT/examples/wasm-browser/serve.mjs"
node "$ROOT/bootstrap/wasm/run.mjs" "$WORK/cli.wasm" \
    >"$WORK/sample.stdout" 2>"$WORK/sample.stderr"
printf '42\n-4\n' >"$WORK/sample.expected"
cmp "$WORK/sample.expected" "$WORK/sample.stdout"
test ! -s "$WORK/sample.stderr"

"$ROOT/examples/wasm-browser/build.sh" "$WORK/browser" \
    >"$WORK/browser-build.stdout"
node "$ROOT/examples/wasm-browser/check.mjs" "$WORK/browser/app.wasm" \
    >"$WORK/browser-check.stdout"
grep -Fq \
    'PASS: browser host loaded and rendered Kofun WebAssembly' \
    "$WORK/browser-check.stdout"
grep -Fq \
    'PASS: viewport lazy loading deferred the wasm fetch' \
    "$WORK/browser-check.stdout"
grep -Fq 'data-kofun-wasm="./app.wasm"' "$WORK/browser/index.html"
grep -Fq 'src="./main.mjs"' "$WORK/browser/index.html"
cmp "$ROOT/examples/wasm-browser/main.mjs" "$WORK/browser/main.mjs"

set +e
"$ROOT/bin/kofun" build \
    "$ROOT/bootstrap/wasm/fixtures/unsupported_text.kofun" \
    --target wasm32 -o "$WORK/unsupported.wasm" \
    >"$WORK/unsupported.stdout" 2>"$WORK/unsupported.stderr"
unsupported_status=$?
"$ROOT/bin/kofun" build "$ROOT/examples/wasm_arithmetic.kofun" \
    --target wasm32 -g -o "$WORK/debug.wasm" \
    >"$WORK/debug.stdout" 2>"$WORK/debug.stderr"
debug_status=$?
set -e
test "$unsupported_status" -eq 1
test "$debug_status" -eq 2
test ! -e "$WORK/unsupported.wasm"
test ! -e "$WORK/debug.wasm"
grep -Fq 'unsupported token in wasm32 arithmetic Core' \
    "$WORK/unsupported.stderr"
grep -Fq -- '-g currently requires --target x86_64-linux' \
    "$WORK/debug.stderr"

sh "$ROOT/tests/conformance/run.sh" \
    "$ROOT/tests/conformance/numeric"

printf '%s\n' \
    'PASS: Kofun emitted deterministic, engine-validated WebAssembly' \
    'PASS: separate and mixed nesting accepted 256 levels and rejected 257 atomically' \
    'PASS: direct Int64 minimum parsing and checked re-negation stayed exact' \
    'PASS: wasm32-node matched C11 for all numeric Core observations' \
    'PASS: Kofun browser sample rendered through a lazy DOM host' \
    'PASS: unsupported source and debug mode failed without artifacts'
