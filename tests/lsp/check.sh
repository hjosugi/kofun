#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SERVER="$ROOT/editor/vscode/server/kofun-lsp"
RESULTS="${KOFUN_LSP_RESULTS:-$ROOT/build/${KOFUN_GATE_WORK_NAMESPACE:+$KOFUN_GATE_WORK_NAMESPACE/}lsp/performance.json}"
REVISION=$(git -C "$ROOT" rev-parse --verify HEAD)

npm --prefix "$ROOT/editor/vscode" run vscode:prepublish --silent
cmp "$ROOT/tooling/typed-sidecar/from-stage2.mjs" \
    "$ROOT/editor/vscode/server/generated/from-stage2.mjs"
cmp "$ROOT/tooling/typed-sidecar/codec.mjs" \
    "$ROOT/editor/vscode/server/generated/codec.mjs"
test -s "$ROOT/editor/vscode/server/generated/semantic-bridge.node"

node --check "$ROOT/tooling/lsp/server.js"
node --check "$ROOT/editor/vscode/server/server.js"
node --check "$ROOT/editor/vscode/server/semantic-sidecar.mjs"
node --check "$ROOT/editor/vscode/server/semantic-worker.mjs"
node --check "$ROOT/editor/vscode/server/generated/from-stage2.mjs"
node --check "$ROOT/editor/vscode/server/generated/codec.mjs"
node --check "$ROOT/editor/vscode/extension.js"
node --check "$ROOT/tests/lsp/client.js"
node --check "$ROOT/tests/lsp/protocol_test.js"
node --check "$ROOT/tests/lsp/semantic_sidecar_test.mjs"
node --check "$ROOT/tests/lsp/performance_test.js"
node --check "$ROOT/tests/lsp/vscode_smoke_test.js"
node --expose-gc "$ROOT/tests/lsp/semantic_sidecar_test.mjs"
node "$ROOT/tests/lsp/protocol_test.js" "$SERVER"
NODE_PATH="$ROOT/tests/lsp/vscode-mock" \
    node "$ROOT/tests/lsp/vscode_smoke_test.js" "$ROOT/editor/vscode"
KOFUN_LSP_REVISION="$REVISION" \
    node "$ROOT/tests/lsp/performance_test.js" "$SERVER" "$RESULTS"
