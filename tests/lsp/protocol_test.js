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
    'fn add(read 左: Int, right: Int) -> Int {',
    '    let result = 左 + right',
    '    let marker = "e\u0301😀"; let copy = result',
    '    return result',
    '    missing()',
    ''
  ].join('\n');
  client.send({
    jsonrpc: '2.0', method: 'textDocument/didOpen',
    params: { textDocument: { uri, languageId: 'kofun', version: 1, text: source } }
  });
  const opened = await client.waitFor((message) =>
    message.method === 'textDocument/publishDiagnostics' &&
    message.params.uri === uri && message.params.version === 1);
  assert.deepStrictEqual(
    opened.params.diagnostics.map((item) => item.code),
    ['KLS0002', 'KLS1001']
  );

  const unicodeReference = source.split('\n')[2].indexOf('result');
  client.send({
    jsonrpc: '2.0', id, method: 'textDocument/definition',
    params: { textDocument: { uri }, position: { line: 2, character: unicodeReference } }
  });
  const unicodeDefinition = await client.waitFor((message) => message.id === id);
  id += 1;
  assert.deepStrictEqual(unicodeDefinition.result.range, {
    start: { line: 1, character: 8 }, end: { line: 1, character: 14 }
  });

  const leftReference = source.split('\n')[1].indexOf('左');
  client.send({
    jsonrpc: '2.0', id, method: 'textDocument/definition',
    params: { textDocument: { uri }, position: { line: 1, character: leftReference } }
  });
  const leftDefinition = await client.waitFor((message) => message.id === id);
  id += 1;
  assert.deepStrictEqual(leftDefinition.result.range, {
    start: { line: 0, character: 12 }, end: { line: 0, character: 13 }
  });

  client.send({
    jsonrpc: '2.0', id, method: 'textDocument/hover',
    params: { textDocument: { uri }, position: { line: 1, character: leftReference } }
  });
  const hover = await client.waitFor((message) => message.id === id);
  id += 1;
  assert.match(hover.result.contents.value, /左: Int \(mode: read\)/);

  // Complete the partial edit and repair an unresolved call with ordered
  // incremental range changes.
  client.send({
    jsonrpc: '2.0', method: 'textDocument/didChange',
    params: {
      textDocument: { uri, version: 2 },
      contentChanges: [
        {
          range: { start: { line: 4, character: 4 }, end: { line: 4, character: 11 } },
          text: 'print'
        },
        {
          range: { start: { line: 5, character: 0 }, end: { line: 5, character: 0 } },
          text: '}\n'
        }
      ]
    }
  });
  const completed = await client.waitFor((message) =>
    message.method === 'textDocument/publishDiagnostics' &&
    message.params.uri === uri && message.params.version === 2);
  assert.deepStrictEqual(completed.params.diagnostics, []);

  // A stale version must be ignored. A request acts as an in-order barrier.
  const messageCount = client.messages.length;
  client.send({
    jsonrpc: '2.0', method: 'textDocument/didChange',
    params: {
      textDocument: { uri, version: 1 },
      contentChanges: [{ text: 'stale replacement' }]
    }
  });
  client.send({
    jsonrpc: '2.0', id, method: 'textDocument/hover',
    params: { textDocument: { uri }, position: { line: 1, character: leftReference } }
  });
  await client.waitFor((message) => message.id === id);
  id += 1;
  assert.strictEqual(
    client.messages.slice(messageCount).some((message) =>
      message.method === 'textDocument/publishDiagnostics' &&
      message.params.version === 1),
    false
  );

  // Update a declared type and require hover to track the latest version.
  client.send({
    jsonrpc: '2.0', method: 'textDocument/didChange',
    params: {
      textDocument: { uri, version: 3 },
      contentChanges: [{
        range: { start: { line: 0, character: 15 }, end: { line: 0, character: 18 } },
        text: 'Text'
      }]
    }
  });
  await client.waitFor((message) =>
    message.method === 'textDocument/publishDiagnostics' &&
    message.params.version === 3);
  client.send({
    jsonrpc: '2.0', id, method: 'textDocument/hover',
    params: { textDocument: { uri }, position: { line: 1, character: leftReference } }
  });
  const updatedHover = await client.waitFor((message) => message.id === id);
  id += 1;
  assert.match(updatedHover.result.contents.value, /左: Text \(mode: read\)/);

  client.send({
    jsonrpc: '2.0', method: 'textDocument/didClose',
    params: { textDocument: { uri } }
  });
  const closed = await client.waitFor((message) =>
    message.method === 'textDocument/publishDiagnostics' &&
    message.params.uri === uri && message.params.version === 3 &&
    message.params.diagnostics.length === 0);
  assert.ok(closed);

  client.sendRaw('{invalid');
  const parseError = await client.waitFor((message) => message.error && message.error.code === -32700);
  assert.strictEqual(parseError.id, null);

  client.send({ jsonrpc: '2.0', id, method: 'kofun/unknown', params: {} });
  const unknown = await client.waitFor((message) => message.id === id);
  id += 1;
  assert.strictEqual(unknown.error.code, -32601);

  // A deep delimiter stack used to clone and scan the entire stack for every
  // token. This balanced corpus must stay linear and still publish a trailing
  // lexical diagnostic.
  const deepUri = 'file:///workspace/deep-delimiters.kofun';
  const depth = 25000;
  const deepSource = `${'('.repeat(depth)}0${')'.repeat(depth)}\n"`;
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
    `PASS: LSP framing/header bounds, lifecycle, UTF-16, diagnostics, ` +
    `definition, hover, stale-version guard, and ${depth}-deep delimiters ` +
    `(${deepMilliseconds.toFixed(2)}ms)\n`
  );
}

main().catch((caught) => {
  process.stderr.write(`${caught.stack}\n`);
  process.exitCode = 1;
});
