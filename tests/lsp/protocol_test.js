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
    `definition, hover, edit/reopen guards, and ${depth}-deep fallback ` +
    `(${deepMilliseconds.toFixed(2)}ms)\n`
  );
}

main().catch((caught) => {
  process.stderr.write(`${caught.stack}\n`);
  process.exitCode = 1;
});
