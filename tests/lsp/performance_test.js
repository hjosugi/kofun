#!/usr/bin/env node
'use strict';

const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { Client } = require('./client');

function elapsedMilliseconds(start) {
  return Number(process.hrtime.bigint() - start) / 1e6;
}

function percentile(values, fraction) {
  const sorted = [...values].sort((left, right) => left - right);
  return sorted[Math.max(0, Math.ceil(sorted.length * fraction) - 1)];
}

function residentBytes(pid) {
  if (process.platform !== 'linux') return null;
  const status = fs.readFileSync(`/proc/${pid}/status`, 'utf8');
  const match = /^VmRSS:\s+(\d+)\s+kB$/m.exec(status);
  return match ? Number(match[1]) * 1024 : null;
}

function fnv1a64(text) {
  let hash = 0xcbf29ce484222325n;
  const bytes = Buffer.from(text, 'utf8');
  for (const byte of bytes) {
    hash ^= BigInt(byte);
    hash = BigInt.asUintN(64, hash * 0x100000001b3n);
  }
  return hash.toString(16).padStart(16, '0');
}

function generateSource(count) {
  const lines = ['fn huge(seed: Int) -> Int {'];
  for (let index = 0; index < count; index += 1) {
    const name = `value${String(index).padStart(5, '0')}`;
    const value = index === 0
      ? '0' : `value${String(index - 1).padStart(5, '0')}`;
    lines.push(`    let ${name}: Int = ${value}`);
  }
  lines.push(`    return value${String(count - 1).padStart(5, '0')}`);
  lines.push('}', '');
  return lines.join('\n');
}

async function main() {
  const server = path.resolve(process.argv[2] || 'tooling/lsp/kofun-lsp');
  const output = path.resolve(process.argv[3] || 'build/lsp/performance.json');
  const client = new Client(server);
  const uri = 'file:///workspace/generated-10000.kofun';
  const source = generateSource(10000);
  const lines = source.split('\n');
  let id = 1;
  let stopped = false;

  try {
  client.send({ jsonrpc: '2.0', id, method: 'initialize', params: {
    rootUri: 'file:///workspace',
    capabilities: { general: { positionEncodings: ['utf-16'] } }
  } });
  await client.waitFor((message) => message.id === id, 10000);
  id += 1;
  client.send({ jsonrpc: '2.0', method: 'initialized', params: {} });
  client.send({
    jsonrpc: '2.0', method: 'textDocument/didOpen',
    params: { textDocument: { uri, languageId: 'kofun', version: 1, text: source } }
  });
  const opened = await client.waitFor((message) =>
    message.method === 'textDocument/publishDiagnostics' &&
    message.params.version === 1, 10000);
  assert.deepStrictEqual(opened.params.diagnostics, []);

  const nearLine = 2;
  const farLine = 10000;
  const nearCharacter = lines[nearLine].lastIndexOf('0');
  const farCharacter = lines[farLine].lastIndexOf('8');

  // One unmeasured range edit warms the parser, index, and allocator.
  client.send({
    jsonrpc: '2.0', method: 'textDocument/didChange',
    params: {
      textDocument: { uri, version: 2 },
      contentChanges: [{
        range: {
          start: { line: nearLine, character: nearCharacter },
          end: { line: nearLine, character: nearCharacter + 1 }
        },
        text: '1'
      }]
    }
  });
  await client.waitFor((message) =>
    message.method === 'textDocument/publishDiagnostics' &&
    message.params.version === 2, 10000);
  const firstRss = residentBytes(client.child.pid);

  const diagnosticMs = [];
  let nearDigit = '1';
  let farDigit = '8';
  for (let edit = 0; edit < 100; edit += 1) {
    const version = edit + 3;
    const near = edit % 2 === 0;
    let line;
    let character;
    let text;
    if (near) {
      nearDigit = nearDigit === '1' ? '0' : '1';
      line = nearLine;
      character = nearCharacter;
      text = nearDigit;
    } else {
      farDigit = farDigit === '8' ? '7' : '8';
      line = farLine;
      character = farCharacter;
      text = farDigit;
    }
    const start = process.hrtime.bigint();
    client.send({
      jsonrpc: '2.0', method: 'textDocument/didChange',
      params: {
        textDocument: { uri, version },
        contentChanges: [{
          range: {
            start: { line, character },
            end: { line, character: character + 1 }
          },
          text
        }]
      }
    });
    const publication = await client.waitFor((message) =>
      message.method === 'textDocument/publishDiagnostics' &&
      message.params.version === version, 10000);
    diagnosticMs.push(elapsedMilliseconds(start));
    assert.deepStrictEqual(publication.params.diagnostics, []);
  }
  const lastRss = residentBytes(client.child.pid);

  const definitionMs = [];
  const hoverMs = [];
  const nearReferenceCharacter = lines[nearLine].lastIndexOf('value');
  const farReferenceCharacter = lines[farLine].lastIndexOf('value');
  for (let request = 0; request < 100; request += 1) {
    const position = request % 2 === 0
      ? { line: nearLine, character: nearReferenceCharacter }
      : { line: farLine, character: farReferenceCharacter };
    let start = process.hrtime.bigint();
    client.send({
      jsonrpc: '2.0', id, method: 'textDocument/definition',
      params: { textDocument: { uri }, position }
    });
    const definition = await client.waitFor((message) => message.id === id, 10000);
    id += 1;
    definitionMs.push(elapsedMilliseconds(start));
    assert.ok(definition.result && definition.result.uri === uri);

    start = process.hrtime.bigint();
    client.send({
      jsonrpc: '2.0', id, method: 'textDocument/hover',
      params: { textDocument: { uri }, position }
    });
    const hover = await client.waitFor((message) => message.id === id, 10000);
    id += 1;
    hoverMs.push(elapsedMilliseconds(start));
    assert.ok(hover.result && /: Int/.test(hover.result.contents.value));
  }

  const metrics = {
    schemaVersion: 1,
    serverRevision: process.env.KOFUN_LSP_REVISION || 'working-tree',
    machine: {
      platform: `${os.platform()} ${os.release()} ${os.arch()}`,
      cpu: os.cpus()[0] ? os.cpus()[0].model : 'unknown',
      node: process.version
    },
    corpus: {
      declarations: 10000,
      utf8Bytes: Buffer.byteLength(source),
      digest: `fnv1a64:${fnv1a64(source)}`
    },
    memoryBytes: { afterWarmup: firstRss, afterEdit100: lastRss },
    diagnosticMs,
    definitionMs,
    hoverMs,
    summary: {
      diagnosticP95Ms: percentile(diagnosticMs, 0.95),
      diagnosticMaxMs: Math.max(...diagnosticMs),
      definitionP95Ms: percentile(definitionMs, 0.95),
      hoverP95Ms: percentile(hoverMs, 0.95),
      residentGrowthRatio: firstRss === null || lastRss === null
        ? null : (lastRss - firstRss) / firstRss
    }
  };

  assert.ok(metrics.summary.diagnosticP95Ms <= 100,
    `diagnostic p95 ${metrics.summary.diagnosticP95Ms.toFixed(2)}ms exceeds 100ms`);
  assert.ok(metrics.summary.diagnosticMaxMs <= 250,
    `diagnostic max ${metrics.summary.diagnosticMaxMs.toFixed(2)}ms exceeds 250ms`);
  assert.ok(metrics.summary.definitionP95Ms <= 50,
    `definition p95 ${metrics.summary.definitionP95Ms.toFixed(2)}ms exceeds 50ms`);
  assert.ok(metrics.summary.hoverP95Ms <= 50,
    `hover p95 ${metrics.summary.hoverP95Ms.toFixed(2)}ms exceeds 50ms`);
  if (metrics.summary.residentGrowthRatio !== null) {
    assert.ok(metrics.summary.residentGrowthRatio < 0.10,
      `resident growth ${(metrics.summary.residentGrowthRatio * 100).toFixed(2)}% is not below 10%`);
  }

  fs.mkdirSync(path.dirname(output), { recursive: true });
  fs.writeFileSync(output, `${JSON.stringify(metrics, null, 2)}\n`);
  await client.stop(id);
  stopped = true;
  process.stdout.write(
    `PASS: 10k declarations, diagnostics p95=${metrics.summary.diagnosticP95Ms.toFixed(2)}ms ` +
    `max=${metrics.summary.diagnosticMaxMs.toFixed(2)}ms, ` +
    `definition p95=${metrics.summary.definitionP95Ms.toFixed(2)}ms, ` +
    `hover p95=${metrics.summary.hoverP95Ms.toFixed(2)}ms, ` +
    `RSS growth=${metrics.summary.residentGrowthRatio === null ? 'n/a' :
      `${(metrics.summary.residentGrowthRatio * 100).toFixed(2)}%`}\n`
  );
  process.stdout.write(`raw measurements: ${output}\n`);
  } finally {
    if (!stopped && client.child.exitCode === null) client.child.kill('SIGTERM');
  }
}

main().catch((caught) => {
  process.stderr.write(`${caught.stack}\n`);
  process.exitCode = 1;
});
