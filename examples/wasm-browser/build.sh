#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT=${1-"$ROOT/build/wasm-browser"}

mkdir -p "$OUT"
"$ROOT/bin/kofun" build \
    "$ROOT/examples/wasm-browser/app.kofun" \
    --target wasm32 -o "$OUT/app.wasm"
cp "$ROOT/examples/wasm-browser/index.html" "$OUT/index.html"
cp "$ROOT/examples/wasm-browser/main.mjs" "$OUT/main.mjs"

printf '%s\n' \
    "Built Kofun browser sample in $OUT" \
    "Run: node examples/wasm-browser/serve.mjs $OUT"
