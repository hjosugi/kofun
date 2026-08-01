#!/usr/bin/env node
'use strict';

const assert = require('assert');
const path = require('path');
const { Client } = require('./client');

async function main() {
  const server = path.resolve(process.argv[2] || 'tooling/lsp/kofun-lsp');
  const client = new Client(server);
  const uri = 'file:///workspace/unicode.kofun';
  let id = 1;

  // A one-byte-at-a-time first frame covers every possible header/body split.
  client.send({
    jsonrpc: '2.0', id, method: 'initialize',
    params: {
      rootUri: 'file:///workspace',
      capabilities: { general: { positionEncodings: ['utf-16'] } }
    }
  }, true);
  const initialized = await client.waitFor((message) => message.id === id);
  id += 1;
  assert.strictEqual(initialized.result.capabilities.positionEncoding, 'utf-16');
  assert.strictEqual(initialized.result.capabilities.textDocumentSync.change, 2);
  assert.strictEqual(initialized.result.capabilities.definitionProvider, true);
  assert.strictEqual(initialized.result.capabilities.hoverProvider, true);
  // Member completion is not implemented, so no trigger character may promise
  // a list this server cannot produce.
  assert.deepStrictEqual(initialized.result.capabilities.completionProvider,
    { resolveProvider: false });
  assert.strictEqual(initialized.result.capabilities.documentSymbolProvider, true);
  assert.strictEqual(initialized.result.capabilities.referencesProvider, true);
  assert.strictEqual(initialized.result.capabilities.documentHighlightProvider, true);
  assert.deepStrictEqual(initialized.result.capabilities.inlayHintProvider,
    { resolveProvider: false });
  assert.deepStrictEqual(initialized.result.capabilities.signatureHelpProvider,
    { triggerCharacters: ['(', ','] });
  assert.strictEqual(initialized.result.capabilities.foldingRangeProvider, true);
  assert.strictEqual(initialized.result.capabilities.selectionRangeProvider, true);
  assert.deepStrictEqual(initialized.result.capabilities.renameProvider,
    { prepareProvider: true });
  const legend = initialized.result.capabilities.semanticTokensProvider.legend;
  assert.deepStrictEqual(legend.tokenTypes,
    ['function', 'parameter', 'variable', 'type', 'keyword', 'number', 'string']);
  assert.deepStrictEqual(legend.tokenModifiers, ['declaration', 'defaultLibrary']);
  // No diagnostic in the registry carries a remedy, so the server must not
  // advertise an action list it can never fill.
  assert.strictEqual(initialized.result.capabilities.codeActionProvider, undefined);
  client.send({ jsonrpc: '2.0', method: 'initialized', params: {} });

  const source = [
    'fn identity(value: Int) -> Int {',
    '    return value',
    '}',
    '',
    'fn main() {',
    '    # e\u0301😀',
    '    let result = identity(41)',
    '    print(result)',
    '}',
    ''
  ].join('\n');
  client.send({
    jsonrpc: '2.0', method: 'textDocument/didOpen',
    params: { textDocument: { uri, languageId: 'kofun', version: 1, text: source } }
  });
  const opened = await client.waitFor((message) =>
    message.method === 'textDocument/publishDiagnostics' &&
    message.params.uri === uri && message.params.version === 1);
  assert.deepStrictEqual(opened.params.diagnostics, []);

  // The combining sequence and astral scalar precede both references in UTF-8
  // while LSP positions remain UTF-16. Definition must still land exactly.
  const unicodeReference = source.split('\n')[7].indexOf('result');
  client.send({
    jsonrpc: '2.0', id, method: 'textDocument/definition',
    params: { textDocument: { uri }, position: { line: 7, character: unicodeReference } }
  });
  const unicodeDefinition = await client.waitFor((message) => message.id === id);
  id += 1;
  assert.deepStrictEqual(unicodeDefinition.result.range, {
    start: { line: 6, character: 8 }, end: { line: 6, character: 14 }
  });

  const parameterReference = source.split('\n')[1].indexOf('value');
  client.send({
    jsonrpc: '2.0', id, method: 'textDocument/definition',
    params: { textDocument: { uri }, position: { line: 1, character: parameterReference } }
  });
  const parameterDefinition = await client.waitFor((message) => message.id === id);
  id += 1;
  assert.deepStrictEqual(parameterDefinition.result.range, {
    start: { line: 0, character: 12 }, end: { line: 0, character: 17 }
  });

  client.send({
    jsonrpc: '2.0', id, method: 'textDocument/hover',
    params: { textDocument: { uri }, position: { line: 1, character: parameterReference } }
  });
  const hover = await client.waitFor((message) => message.id === id);
  id += 1;
  assert.match(hover.result.contents.value, /type: Int/);
  assert.doesNotMatch(hover.result.contents.value, /syntactic fallback/);

  async function completionAt(line, character) {
    client.send({
      jsonrpc: '2.0', id, method: 'textDocument/completion',
      params: { textDocument: { uri }, position: { line, character } }
    });
    const reply = await client.waitFor((message) => message.id === id);
    id += 1;
    return reply.result;
  }

  // Inside main's body: the local, both functions, and the fixed vocabulary are
  // offered, and the checked type comes from the sidecar rather than the
  // bounded tokenizer's guess.
  const inMain = await completionAt(7, 10);
  assert.strictEqual(inMain.isIncomplete, false);
  const byLabel = new Map(inMain.items.map((item) => [item.label, item]));
  assert.strictEqual(byLabel.get('result').data.provenance, 'validated-sidecar');
  assert.match(byLabel.get('result').detail, /^result: Int/);
  assert.strictEqual(byLabel.get('result').kind, 6);
  assert.strictEqual(byLabel.get('identity').kind, 3);
  assert.match(byLabel.get('identity').detail, /Int/);
  assert.strictEqual(byLabel.get('print').data.provenance, 'builtin');
  assert.strictEqual(byLabel.get('let').kind, 14);
  assert.strictEqual(byLabel.get('take').data.provenance, 'keyword');
  // A parameter of another function is not in scope here, and completion must
  // not offer a name that definition would refuse to resolve.
  assert.strictEqual(byLabel.has('value'), false);
  // The local is offered exactly once even though it is also a declaration.
  assert.strictEqual(inMain.items.filter((item) => item.label === 'result').length, 1);
  // Locals sort ahead of functions, which sort ahead of the fixed vocabulary.
  assert.ok(byLabel.get('result').sortText < byLabel.get('identity').sortText);
  assert.ok(byLabel.get('identity').sortText < byLabel.get('print').sortText);

  // Typing narrows the list server-side: only names carrying the prefix are
  // sent, and the fixed vocabulary is filtered by the same rule.
  // Matching is case-insensitive, as editors filter, so the builtin type and
  // the ownership keyword belong in this list too.
  const narrowed = await completionAt(7, 12);
  const narrowedLabels = narrowed.items.map((item) => item.label).sort();
  assert.deepStrictEqual(narrowedLabels, ['Result', 'read', 'result', 'return']);
  assert.strictEqual(narrowed.isIncomplete, false);

  // The parameter is in scope only inside its own function body.
  const inIdentity = await completionAt(1, 11);
  const identityLabels = new Map(inIdentity.items.map((item) => [item.label, item]));
  assert.strictEqual(identityLabels.get('value').data.provenance, 'validated-sidecar');
  assert.match(identityLabels.get('value').detail, /^value: Int/);
  assert.strictEqual(identityLabels.has('result'), false);

  // A local is not visible before its own declaration.
  const beforeLocal = await completionAt(6, 8);
  assert.strictEqual(beforeLocal.items.some((item) => item.label === 'result'), false);

  // Comments carry no references. The position sits after a combining mark and
  // an astral scalar, so UTF-16 conversion is exercised here too.
  const inComment = await completionAt(5, 8);
  assert.deepStrictEqual(inComment.items, []);
  assert.strictEqual(inComment.isIncomplete, false);

  async function requestAt(method, extra) {
    client.send({
      jsonrpc: '2.0', id, method,
      params: { textDocument: { uri }, ...extra }
    });
    const reply = await client.waitFor((message) => message.id === id);
    id += 1;
    return reply.result;
  }

  // The outline nests each function's parameters and locals under it.
  const outline = await requestAt('textDocument/documentSymbol', {});
  assert.deepStrictEqual(outline.map((item) => item.name), ['identity', 'main']);
  assert.strictEqual(outline[0].kind, 12);
  assert.deepStrictEqual(outline[0].children.map((item) => item.name), ['value']);
  assert.deepStrictEqual(outline[1].children.map((item) => item.name), ['result']);
  assert.match(outline[0].detail, /fn identity\(value: Int\) -> Int/);
  // The function's range covers its body; the selection range is just the name.
  assert.strictEqual(outline[0].selectionRange.start.character, 3);
  assert.ok(outline[0].range.end.line >= outline[0].selectionRange.end.line);

  // References resolve through `resolve`, so a shadowed name is not swept up.
  const references = await requestAt('textDocument/references', {
    position: { line: 1, character: parameterReference },
    context: { includeDeclaration: true }
  });
  assert.deepStrictEqual(references.map((item) => item.range.start.line), [0, 1]);
  const withoutDeclaration = await requestAt('textDocument/references', {
    position: { line: 1, character: parameterReference },
    context: { includeDeclaration: false }
  });
  assert.deepStrictEqual(withoutDeclaration.map((item) => item.range.start.line), [1]);

  const highlights = await requestAt('textDocument/documentHighlight', {
    position: { line: 6, character: 9 }
  });
  // LSP DocumentHighlightKind: the declaration is a Write (3), its use a Read (2).
  assert.deepStrictEqual(highlights.map((item) => item.kind), [3, 2]);
  assert.deepStrictEqual(highlights.map((item) => item.range.start.line), [6, 7]);

  // The parameter name is shown at the call argument, which is where the
  // callee's signature actually lands on the caller.
  const allHints = await requestAt('textDocument/inlayHint', {});
  const callHint = allHints.find((hint) => hint.label === 'value:');
  assert.ok(callHint, `expected a parameter-name hint, got ${JSON.stringify(allHints)}`);
  assert.strictEqual(callHint.kind, 2);
  assert.strictEqual(callHint.position.line, 6);
  assert.match(callHint.tooltip, /value: Int/);
  // A range request must not answer for lines outside it.
  const narrowHints = await requestAt('textDocument/inlayHint', {
    range: { start: { line: 0, character: 0 }, end: { line: 2, character: 0 } }
  });
  assert.strictEqual(narrowHints.some((hint) => hint.position.line > 2), false);

  // Signature help tracks which argument the caret is in.
  const firstArgument = await requestAt('textDocument/signatureHelp', {
    position: { line: 6, character: 26 }
  });
  assert.match(firstArgument.signatures[0].label, /fn identity\(value: Int\) -> Int/);
  assert.strictEqual(firstArgument.activeParameter, 0);
  // Outside any call there is no signature to show, and the server says so
  // rather than returning the last one it saw.
  assert.strictEqual(await requestAt('textDocument/signatureHelp', {
    position: { line: 2, character: 0 }
  }), null);

  const folds = await requestAt('textDocument/foldingRange', {});
  const bodies = folds.filter((item) => item.kind === 'region');
  assert.deepStrictEqual(bodies.map((item) => item.startLine), [0, 4]);
  // A block that opens and closes on one line has nothing to fold.
  assert.strictEqual(folds.some((item) => item.endLine < item.startLine), false);

  const selections = await requestAt('textDocument/selectionRange', {
    positions: [{ line: 1, character: parameterReference }]
  });
  let node = selections[0];
  let widened = 0;
  while (node.parent) {
    // Each parent strictly contains its child, which is what makes repeated
    // expand-selection converge instead of jumping.
    assert.ok(node.parent.range.start.line <= node.range.start.line);
    assert.ok(node.parent.range.end.line >= node.range.end.line);
    node = node.parent;
    widened += 1;
  }
  assert.ok(widened >= 2, `expected an expanding chain, got ${widened}`);

  // Semantic tokens colour by what a name resolved to, not by its spelling:
  // `value` is a parameter at its declaration and at its use, `result` is a
  // variable, and `print` is a function from the default library.
  const semantic = await requestAt('textDocument/semanticTokens/full', {});
  assert.strictEqual(semantic.data.length % 5, 0);
  const decoded = [];
  let tokenLine = 0;
  let tokenStart = 0;
  for (let at = 0; at < semantic.data.length; at += 5) {
    const [deltaLine, deltaStart, length, type, modifiers] = semantic.data.slice(at, at + 5);
    tokenLine += deltaLine;
    tokenStart = deltaLine === 0 ? tokenStart + deltaStart : deltaStart;
    decoded.push({
      text: source.split('\n')[tokenLine].substr(tokenStart, length),
      type: legend.tokenTypes[type], modifiers
    });
  }
  const classified = (text) => decoded.filter((item) => item.text === text);
  assert.deepStrictEqual(classified('value').map((item) => item.type),
    ['parameter', 'parameter']);
  // The declaration carries the declaration modifier; the use does not.
  assert.deepStrictEqual(classified('value').map((item) => item.modifiers), [1, 0]);
  assert.deepStrictEqual(classified('result').map((item) => item.type),
    ['variable', 'variable']);
  assert.deepStrictEqual(classified('print').map((item) => item.modifiers), [2]);
  assert.strictEqual(classified('fn').every((item) => item.type === 'keyword'), true);
  assert.strictEqual(classified('41')[0].type, 'number');
  // Nothing inside the comment is classified; that stays TextMate's job.
  assert.strictEqual(decoded.some((item) => item.text.includes('😀')), false);

  // Renaming a local rewrites its declaration and every use.
  const prepared = await requestAt('textDocument/prepareRename', {
    position: { line: 6, character: 9 }
  });
  assert.strictEqual(prepared.placeholder, 'result');
  const renamed = await requestAt('textDocument/rename', {
    position: { line: 6, character: 9 }, newName: 'answer'
  });
  assert.deepStrictEqual(renamed.changes[uri].map((item) => item.range.start.line), [6, 7]);
  assert.ok(renamed.changes[uri].every((item) => item.newText === 'answer'));

  async function renameError(position, newName) {
    client.send({
      jsonrpc: '2.0', id, method: 'textDocument/rename',
      params: { textDocument: { uri }, position, newName }
    });
    const reply = await client.waitFor((message) => message.id === id);
    id += 1;
    return reply.error;
  }
  // A function may be named from a file this server never reads, so renaming
  // one is refused rather than silently leaving those uses behind.
  assert.match((await renameError({ line: 0, character: 4 }, 'other')).message,
    /only the open document/u);
  assert.match((await renameError({ line: 6, character: 9 }, 'let')).message,
    /keyword/u);
  assert.match((await renameError({ line: 6, character: 9 }, 'print')).message,
    /builtin/u);
  assert.match((await renameError({ line: 6, character: 9 }, '2bad')).message,
    /not a valid name/u);
  // Renaming onto a name already visible at a use would capture that other
  // declaration instead of the one being renamed.
  assert.match((await renameError({ line: 6, character: 9 }, 'identity')).message,
    /already declared/u);

  // A failed partial document keeps an earlier validated local available.
  client.send({
    jsonrpc: '2.0', method: 'textDocument/didChange',
    params: {
      textDocument: { uri, version: 2 },
      contentChanges: [{
        range: { start: { line: 7, character: 4 }, end: { line: 7, character: 9 } },
        text: 'missing'
      }]
    }
  });
  const partial = await client.waitFor((message) =>
    message.method === 'textDocument/publishDiagnostics' &&
    message.params.uri === uri && message.params.version === 2);
  assert.deepStrictEqual(partial.params.diagnostics.map((item) => item.code), ['E2S16']);
  assert.strictEqual(partial.params.diagnostics[0].data.category, 'stage2');
  client.send({
    jsonrpc: '2.0', id, method: 'textDocument/hover',
    params: { textDocument: { uri }, position: { line: 6, character: 9 } }
  });
  const partialHover = await client.waitFor((message) => message.id === id);
  id += 1;
  assert.match(partialHover.result.contents.value, /type: Int/);

  client.send({
    jsonrpc: '2.0', method: 'textDocument/didChange',
    params: {
      textDocument: { uri, version: 3 },
      contentChanges: [{
        range: { start: { line: 7, character: 4 }, end: { line: 7, character: 11 } },
        text: 'print'
      }]
    }
  });
  const completed = await client.waitFor((message) =>
    message.method === 'textDocument/publishDiagnostics' &&
    message.params.uri === uri && message.params.version === 3);
  assert.deepStrictEqual(completed.params.diagnostics, []);

  // A stale version must be ignored. A request acts as an in-order barrier.
  const messageCount = client.messages.length;
  client.send({
    jsonrpc: '2.0', method: 'textDocument/didChange',
    params: {
      textDocument: { uri, version: 2 },
      contentChanges: [{ text: 'stale replacement' }]
    }
  });
  client.send({
    jsonrpc: '2.0', id, method: 'textDocument/hover',
    params: { textDocument: { uri }, position: { line: 1, character: parameterReference } }
  });
  await client.waitFor((message) => message.id === id);
  id += 1;
  assert.strictEqual(
    client.messages.slice(messageCount).some((message) =>
      message.method === 'textDocument/publishDiagnostics' &&
      message.params.version === 2),
    false
  );

  // Two queued replacements prove that cancellation prevents the older
  // successful/partial analysis from publishing after the newer version.
  const raceStart = client.messages.length;
  const missingSource = source.replace('print(result)', 'missing(result)');
  client.send({
    jsonrpc: '2.0', method: 'textDocument/didChange',
    params: {
      textDocument: { uri, version: 4 }, contentChanges: [{ text: missingSource }]
    }
  });
  client.send({
    jsonrpc: '2.0', method: 'textDocument/didChange',
    params: {
      textDocument: { uri, version: 5 }, contentChanges: [{ text: source }]
    }
  });
  await client.waitFor((message) =>
    message.method === 'textDocument/publishDiagnostics' &&
    message.params.uri === uri && message.params.version === 5);
  assert.strictEqual(client.messages.slice(raceStart).some((message) =>
    message.method === 'textDocument/publishDiagnostics' &&
    message.params.uri === uri && message.params.version === 4), false);

  // Close/reopen creates a new session epoch. An obsolete result with a
  // numerically larger version cannot win over the reopened version 1.
  const reopenStart = client.messages.length;
  client.send({
    jsonrpc: '2.0', method: 'textDocument/didChange',
    params: {
      textDocument: { uri, version: 6 }, contentChanges: [{ text: missingSource }]
    }
  });
  client.send({
    jsonrpc: '2.0', method: 'textDocument/didClose',
    params: { textDocument: { uri } }
  });
  client.send({
    jsonrpc: '2.0', method: 'textDocument/didOpen',
    params: { textDocument: { uri, languageId: 'kofun', version: 1, text: source } }
  });
  const reopened = await client.waitFor((message) =>
    message.method === 'textDocument/publishDiagnostics' &&
    message.params.uri === uri && message.params.version === 1 &&
    message.params.diagnostics.length === 0);
  assert.ok(reopened);
  assert.strictEqual(client.messages.slice(reopenStart).some((message) =>
    message.method === 'textDocument/publishDiagnostics' &&
    message.params.uri === uri && message.params.version === 6), false);

  client.send({
    jsonrpc: '2.0', method: 'textDocument/didClose',
    params: { textDocument: { uri } }
  });
  const closed = await client.waitFor((message) =>
    message.method === 'textDocument/publishDiagnostics' &&
    message.params.uri === uri && message.params.version === 1 &&
    message.params.diagnostics.length === 0);
  assert.ok(closed);

  client.sendRaw('{invalid');
  const parseError = await client.waitFor((message) => message.error && message.error.code === -32700);
  assert.strictEqual(parseError.id, null);

  client.send({ jsonrpc: '2.0', id, method: 'kofun/unknown', params: {} });
  const unknown = await client.waitFor((message) => message.id === id);
  id += 1;
  assert.strictEqual(unknown.error.code, -32601);

  // More than 64 functions is an explicit ETS04 producer-profile miss. The
  // bounded tokenizer remains available, visibly labelled as syntactic.
  const deepUri = 'file:///workspace/deep-delimiters.kofun';
  const depth = 25000;
  const unsupportedFunctions = Array.from({ length: 65 }, (_value, index) =>
    `fn f${index}(value: Int) -> Int { return value }`).join('\n');
  const deepSource = `${unsupportedFunctions}\n${'('.repeat(depth)}0${')'.repeat(depth)}\n"`;
  const deepStart = process.hrtime.bigint();
  client.send({
    jsonrpc: '2.0', method: 'textDocument/didOpen',
    params: {
      textDocument: {
        uri: deepUri, languageId: 'kofun', version: 1, text: deepSource
      }
    }
  });
  const deepDiagnostics = await client.waitFor((message) =>
    message.method === 'textDocument/publishDiagnostics' &&
    message.params.uri === deepUri && message.params.version === 1, 2000);
  const deepMilliseconds = Number(process.hrtime.bigint() - deepStart) / 1e6;
  assert.ok(deepMilliseconds < 1000,
    `deep delimiter analysis took ${deepMilliseconds.toFixed(2)}ms`);
  assert.deepStrictEqual(
    deepDiagnostics.params.diagnostics.map((item) => item.code), ['KLS0001']
  );
  assert.strictEqual(deepDiagnostics.params.diagnostics[0].source, 'kofun-syntax');
  assert.strictEqual(deepDiagnostics.params.diagnostics[0].data.analysis, 'syntactic');

  // Completion stays available on the fallback path and says so, so a type
  // there is never mistaken for a checked one.
  client.send({
    jsonrpc: '2.0', id, method: 'textDocument/completion',
    params: { textDocument: { uri: deepUri }, position: { line: 0, character: 27 } }
  });
  const deepCompletion = await client.waitFor((message) => message.id === id);
  id += 1;
  const deepLabels = new Map(deepCompletion.result.items.map((item) => [item.label, item]));
  assert.strictEqual(deepLabels.get('value').data.provenance, 'syntactic-fallback');
  assert.strictEqual(deepLabels.get('f1').data.provenance, 'syntactic-fallback');
  // f1's own parameter is a different declaration of the same name and must not
  // leak out of its function body; only f0's is in scope here.
  assert.strictEqual(
    deepCompletion.result.items.filter((item) => item.label === 'value').length, 1);
  // 65 functions plus the fixed vocabulary stay under the bound, so this list
  // is complete; the bound itself is exercised by the 10k performance corpus.
  assert.strictEqual(deepCompletion.result.isIncomplete, false);
  assert.ok(deepCompletion.result.items.length <= 200);
  // Reserving the vocabulary means `print` and `let` survive a crowded scope.
  assert.ok(deepLabels.has('print') && deepLabels.has('let'));

  // The final line is an unterminated string. Nothing inside it is a reference.
  client.send({
    jsonrpc: '2.0', id, method: 'textDocument/completion',
    params: {
      textDocument: { uri: deepUri },
      position: { line: deepSource.split('\n').length - 1, character: 1 }
    }
  });
  const stringCompletion = await client.waitFor((message) => message.id === id);
  id += 1;
  assert.deepStrictEqual(stringCompletion.result.items, []);
  client.send({
    jsonrpc: '2.0', method: 'textDocument/didClose',
    params: { textDocument: { uri: deepUri } }
  });
  await client.waitFor((message) =>
    message.method === 'textDocument/publishDiagnostics' &&
    message.params.uri === deepUri && message.params.diagnostics.length === 0);

  await client.stop(id);

  // A peer that never terminates its header cannot grow the server buffer
  // without bound. It receives one explicit parse error before termination.
  const oversized = new Client(server);
  oversized.child.stdin.write(Buffer.alloc(8193, 65));
  const headerError = await oversized.waitFor((message) =>
    message.error && message.error.code === -32700);
  assert.match(headerError.error.message, /header exceeds 8192 bytes/);
  const oversizedExit = await oversized.exitPromise;
  assert.strictEqual(oversizedExit.code, 1);

  process.stdout.write(
    `PASS: sidecar-backed LSP framing/lifecycle, UTF-16, diagnostics, ` +
    `definition, hover, scoped completion, outline, references, highlights, ` +
    `inlay hints, signature help, folding, selection ranges, ` +
    `semantic tokens, guarded rename, ` +
    `edit/reopen guards, and ` +
    `${depth}-deep fallback (${deepMilliseconds.toFixed(2)}ms)\n`
  );
}

main().catch((caught) => {
  process.stderr.write(`${caught.stack}\n`);
  process.exitCode = 1;
});
