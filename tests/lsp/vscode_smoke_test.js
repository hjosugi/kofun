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
    vscode.__document, new vscode.Position(6, 10)
  );
  assert.strictEqual(definition.range.start.line, 5);
  assert.strictEqual(definition.range.start.character, 8);

  const hover = await vscode.__state.hoverProvider.provideHover(
    vscode.__document, new vscode.Position(1, 11)
  );
  assert.match(hover.contents.value, /type: Int/);
  assert.doesNotMatch(hover.contents.value, /syntactic fallback/);

  const completion = await vscode.__state.completionProvider.provideCompletionItems(
    vscode.__document, new vscode.Position(6, 10)
  );
  assert.strictEqual(completion.isIncomplete, false);
  const items = new Map(completion.items.map((item) => [item.label, item]));
  // VS Code's Variable/Function/Keyword are the LSP kinds minus one; a client
  // that forwarded the LSP numbers unchanged would render every icon wrong.
  assert.strictEqual(items.get('copy').kind, 5);
  assert.strictEqual(items.get('identity').kind, 2);
  assert.strictEqual(items.get('let').kind, 13);
  assert.strictEqual(items.has('value'), false);

  // The outline nests parameters and locals under the function that declares
  // them, which is what the breadcrumb bar and the symbol picker read.
  const symbols = await vscode.__state.documentSymbolProvider.provideDocumentSymbols(
    vscode.__document
  );
  assert.deepStrictEqual(symbols.map((item) => item.name), ['identity', 'main']);
  // VS Code's SymbolKind.Function is 11; the LSP number is 12.
  assert.strictEqual(symbols[0].kind, 11);
  assert.deepStrictEqual(symbols[0].children.map((item) => item.name), ['value']);
  assert.deepStrictEqual(symbols[1].children.map((item) => item.name), ['copy']);
  assert.match(symbols[0].detail, /Int/);

  const references = await vscode.__state.referenceProvider.provideReferences(
    vscode.__document, new vscode.Position(1, 11), { includeDeclaration: true }
  );
  assert.strictEqual(references.length, 2);
  assert.strictEqual(references[0].range.start.line, 0);
  assert.strictEqual(references[1].range.start.line, 1);

  const highlights = await vscode.__state.documentHighlightProvider.provideDocumentHighlights(
    vscode.__document, new vscode.Position(5, 9)
  );
  // VS Code's DocumentHighlightKind.Write is 2; the LSP number is 3.
  assert.strictEqual(highlights[0].kind, 2);
  assert.strictEqual(highlights.length, 2);

  const hints = await vscode.__state.inlayHintsProvider.provideInlayHints(
    vscode.__document, undefined
  );
  const labels = hints.map((hint) => hint.label);
  assert.ok(labels.includes('value:'), `parameter name hint missing from ${labels}`);
  // InlayHintKind is one of the few enumerations LSP and VS Code agree on, so
  // Parameter stays 2 rather than being shifted like the two above.
  assert.strictEqual(hints.find((hint) => hint.label === 'value:').kind, 2);

  assert.ok(vscode.__state.commands.has('kofun.restartServer'));
  assert.ok(vscode.__state.commands.has('kofun.showOutput'));
  assert.ok(vscode.__state.statusBar.some((text) => text.includes('Kofun')));

  await extension.deactivate();
  process.stdout.write('PASS: packaged VS Code client starts, queries, and stops the bundled server\n');
}

main().catch((caught) => {
  process.stderr.write(`${caught.stack}\n${vscode.__state.output.join('')}`);
  process.exitCode = 1;
});
