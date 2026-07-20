#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SERVER="$ROOT/editor/vscode/server/kofun-lsp"
RESULTS="${KOFUN_LSP_RESULTS:-$ROOT/build/lsp/performance.json}"
REVISION=$(git -C "$ROOT" rev-parse --verify HEAD)

node --check "$ROOT/tooling/lsp/server.js"
node --check "$ROOT/editor/vscode/server/server.js"
node --check "$ROOT/editor/vscode/extension.js"
node --check "$ROOT/tests/lsp/client.js"
node --check "$ROOT/tests/lsp/protocol_test.js"
node --check "$ROOT/tests/lsp/performance_test.js"
node --check "$ROOT/tests/lsp/vscode_smoke_test.js"
node "$ROOT/tests/lsp/protocol_test.js" "$SERVER"
NODE_PATH="$ROOT/tests/lsp/vscode-mock" \
    node "$ROOT/tests/lsp/vscode_smoke_test.js" "$ROOT/editor/vscode"
KOFUN_LSP_REVISION="$REVISION" \
    node "$ROOT/tests/lsp/performance_test.js" "$SERVER" "$RESULTS"
