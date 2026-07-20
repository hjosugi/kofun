#!/usr/bin/env node
'use strict';

const assert = require('assert');
const path = require('path');
const vscode = require('vscode');

async function waitFor(predicate) {
  for (let attempt = 0; attempt < 100; attempt += 1) {
    if (predicate()) return;
    await new Promise((resolve) => setTimeout(resolve, 10));
  }
  throw new Error('timed out waiting for VS Code client');
}

async function main() {
  const extensionRoot = path.resolve(process.argv[2] || 'editor/vscode');
  const extension = require(path.join(extensionRoot, 'extension.js'));
  const context = { extensionPath: extensionRoot, subscriptions: [] };
  await extension.activate(context);
  await waitFor(() => vscode.__state.diagnostics.has(
    vscode.__document.uri.toString()
  ));
  assert.deepStrictEqual(
    vscode.__state.diagnostics.get(vscode.__document.uri.toString()), []
  );

  const definition = await vscode.__state.definitionProvider.provideDefinition(
    vscode.__document, new vscode.Position(2, 12)
  );
  assert.strictEqual(definition.range.start.line, 1);
  assert.strictEqual(definition.range.start.character, 8);

  const hover = await vscode.__state.hoverProvider.provideHover(
    vscode.__document, new vscode.Position(1, 16)
  );
  assert.match(hover.contents.value, /value: Int \(mode: read\)/);
  await extension.deactivate();
  process.stdout.write('PASS: packaged VS Code client starts, queries, and stops the bundled server\n');
}

main().catch((caught) => {
  process.stderr.write(`${caught.stack}\n${vscode.__state.output.join('')}`);
  process.exitCode = 1;
});
