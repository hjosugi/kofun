#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
WORK=${KOFUN_TOUR_CHECK_WORK:-"$ROOT/build/tour-check"}

for tool in node grep cmp
do
    command -v "$tool" >/dev/null 2>&1 || {
        printf '%s\n' "browser tour gate requires $tool" >&2
        exit 1
    }
done

rm -rf "$WORK"
mkdir -p "$WORK"

for module in app compiler content runtime share check
do
    node --check "$ROOT/docs/tour/$module.mjs"
done

"$ROOT/bin/kofun" build \
    "$ROOT/examples/wasm-browser/app.kofun" \
    --target wasm32 -o "$WORK/native.wasm" >/dev/null
node "$ROOT/docs/tour/check.mjs" \
    "$ROOT/examples/wasm-browser/app.kofun" "$WORK/native.wasm" \
    >"$WORK/check.stdout"

grep -Fq \
    'PASS: browser compiler matched the native wasm32 seed byte for byte' \
    "$WORK/check.stdout"
grep -Fq \
    'PASS: every editable tour step ran with deterministic observations' \
    "$WORK/check.stdout"
grep -Fq 'data-editor' "$ROOT/docs/tour/index.html"
grep -Fq 'data-direction' "$ROOT/docs/tour/index.html"
grep -Fq 'aria-live="polite"' "$ROOT/docs/tour/index.html"
grep -Fq 'inset-inline-start' "$ROOT/docs/tour/styles.css"
grep -Fq '[dir="rtl"]' "$ROOT/docs/tour/styles.css"
! grep -Eq '(margin|padding|border)-(left|right):' \
    "$ROOT/docs/tour/styles.css"

for language in python typescript go rust
do
    test -s "$ROOT/docs/tour/guides/$language.md"
    grep -Fq 'Where Kofun is worse today' \
        "$ROOT/docs/tour/guides/$language.md"
done

printf '%s\n' \
    'PASS: static browser tour is editable, runnable, and URL-shareable' \
    'PASS: browser compiler matches the current deterministic wasm32 Core' \
    'PASS: logical CSS and direction control cover RTL layout'
