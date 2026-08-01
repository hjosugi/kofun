#!/usr/bin/env node
'use strict';

const assert = require('assert');
const path = require('path');
const { Client } = require('./client');

async function main() {
  const server = path.resolve(process.argv[2] || 'tooling/lsp/kofun-lsp');
  const client = new Client(server);
  const uri = 'file:///workspace/bridge-fallback.kofun';
  let id = 1;

  client.send({
    jsonrpc: '2.0', id, method: 'initialize',
    params: { rootUri: 'file:///workspace', capabilities: {} }
  });
  await client.waitFor((message) => message.id === id);
  id += 1;
  client.send({ jsonrpc: '2.0', method: 'initialized', params: {} });

  const source = [
    'fn identity(value: Int) -> Int {',
    '    return value',
    '}',
    'fn main() {',
    '    let answer = identity(42)',
    '    print(answer)',
    '}',
    ''
  ].join('\n');
  client.send({
    jsonrpc: '2.0', method: 'textDocument/didOpen',
    params: { textDocument: { uri, languageId: 'kofun', version: 1, text: source } }
  });

  const warning = await client.waitFor((message) =>
    message.method === 'window/logMessage' &&
    /semantic bridge unavailable/u.test(message.params?.message));
  assert.strictEqual(warning.params.type, 2);
  assert.match(warning.params.message, /semantic-bridge\.node/u);
  assert.match(warning.params.message, new RegExp(`${process.platform}/${process.arch}`, 'u'));
  assert.match(warning.params.message, /using syntactic fallback/u);

  const diagnostics = await client.waitFor((message) =>
    message.method === 'textDocument/publishDiagnostics' &&
    message.params.uri === uri && message.params.version === 1);
  assert.deepStrictEqual(diagnostics.params.diagnostics, []);

  client.send({
    jsonrpc: '2.0', id, method: 'textDocument/hover',
    params: { textDocument: { uri }, position: { line: 1, character: 11 } }
  });
  const hover = await client.waitFor((message) => message.id === id);
  id += 1;
  assert.match(hover.result.contents.value, /syntactic fallback/u);
  assert.match(hover.result.contents.value, /value: Int/u);

  client.send({
    jsonrpc: '2.0', id, method: 'textDocument/completion',
    params: { textDocument: { uri }, position: { line: 5, character: 10 } }
  });
  const completion = await client.waitFor((message) => message.id === id);
  id += 1;
  const answer = completion.result.items.find((item) => item.label === 'answer');
  assert.ok(answer, 'syntactic fallback did not return the visible local');
  assert.strictEqual(answer.data.provenance, 'syntactic-fallback');

  client.send({
    jsonrpc: '2.0', method: 'textDocument/didChange',
    params: {
      textDocument: { uri, version: 2 },
      contentChanges: [{ text: source.replace('42', '43') }]
    }
  });
  await client.waitFor((message) =>
    message.method === 'textDocument/publishDiagnostics' &&
    message.params.uri === uri && message.params.version === 2);
  assert.strictEqual(client.messages.filter((message) =>
    message.method === 'window/logMessage' &&
    /semantic bridge unavailable/u.test(message.params?.message)).length, 1);

  await client.stop(id);
  process.stdout.write(
    'PASS: missing native bridge logs once and serves labelled syntactic fallback\n'
  );
}

main().catch((caught) => {
  process.stderr.write(`${caught.stack}\n`);
  process.exitCode = 1;
});
