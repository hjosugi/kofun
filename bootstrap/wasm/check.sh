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

"$ROOT/bin/kofun" build "$ROOT/examples/wasm_arithmetic.kofun" \
    --target wasm32 -o "$WORK/cli.wasm" >/dev/null
"$ROOT/bin/kofun" build "$ROOT/examples/wasm_arithmetic.kofun" \
    --target wasm32 -o "$WORK/cli-second.wasm" >/dev/null
cmp "$WORK/direct.wasm" "$WORK/cli.wasm"
cmp "$WORK/cli.wasm" "$WORK/cli-second.wasm"

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
    'PASS: wasm32-node matched C11 for all numeric Core observations' \
    'PASS: Kofun browser sample rendered through a lazy DOM host' \
    'PASS: unsupported source and debug mode failed without artifacts'
