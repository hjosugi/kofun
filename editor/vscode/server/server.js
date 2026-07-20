#!/usr/bin/env node
'use strict';

/*
 * A deliberately small, dependency-free language server for the syntax that
 * the bootstrap Kofun frontend accepts today.  It indexes one open document at
 * a time; it does not pretend to be a project-wide type checker.
 */

const fs = require('fs');

const documents = new Map();
let input = Buffer.alloc(0);
let shutdownRequested = false;

function send(message) {
  const body = Buffer.from(JSON.stringify(message), 'utf8');
  process.stdout.write(`Content-Length: ${body.length}\r\n\r\n`);
  process.stdout.write(body);
}

function response(id, result) {
  send({ jsonrpc: '2.0', id, result });
}

function error(id, code, message) {
  send({ jsonrpc: '2.0', id: id === undefined ? null : id, error: { code, message } });
}

function lineStarts(text) {
  const starts = [0];
  for (let i = 0; i < text.length; i += 1) {
    if (text.charCodeAt(i) === 10) starts.push(i + 1);
  }
  return starts;
}

function positionToOffset(doc, position) {
  if (!position || !Number.isInteger(position.line) ||
      !Number.isInteger(position.character) ||
      position.line < 0 || position.character < 0 ||
      position.line >= doc.lines.length) return null;
  const start = doc.lines[position.line];
  const lineEnd = position.line + 1 < doc.lines.length
    ? doc.lines[position.line + 1] - 1 : doc.text.length;
  return Math.min(start + position.character, lineEnd);
}

function offsetToPosition(doc, offset) {
  offset = Math.max(0, Math.min(offset, doc.text.length));
  let low = 0;
  let high = doc.lines.length;
  while (low + 1 < high) {
    const middle = (low + high) >> 1;
    if (doc.lines[middle] <= offset) low = middle;
    else high = middle;
  }
  return { line: low, character: offset - doc.lines[low] };
}

function range(doc, start, end) {
  return { start: offsetToPosition(doc, start), end: offsetToPosition(doc, end) };
}

function isIdentifierStart(code) {
  return code === 95 ||
    (code >= 65 && code <= 90) ||
    (code >= 97 && code <= 122) ||
    code >= 128;
}

function isIdentifierContinue(code) {
  return isIdentifierStart(code) || (code >= 48 && code <= 57);
}

function tokenText(doc, token) {
  return doc.text.slice(token.start, token.end);
}

function diagnostic(code, severity, start, end, message) {
  return { code, severity, start, end, message };
}

function tokenize(doc) {
  const text = doc.text;
  const tokens = [];
  const diagnostics = [];
  const delimiters = [];
  let curlyDepth = 0;

  function push(kind, start, end, container) {
    tokens.push({ kind, start, end, depth: curlyDepth, container, match: -1 });
    return tokens.length - 1;
  }

  for (let i = 0; i < text.length;) {
    const code = text.charCodeAt(i);
    if (code === 32 || code === 9 || code === 10 || code === 13) {
      i += 1;
      continue;
    }
    if (code === 35) {
      while (i < text.length && text.charCodeAt(i) !== 10) i += 1;
      continue;
    }
    const container = [...delimiters].reverse().find((entry) => entry.kind === '{');
    const containerIndex = container ? container.index : -1;
    if (code === 34) {
      const start = i++;
      let closed = false;
      while (i < text.length) {
        if (text.charCodeAt(i) === 92) {
          i += Math.min(2, text.length - i);
        } else if (text.charCodeAt(i) === 34) {
          i += 1;
          closed = true;
          break;
        } else if (text.charCodeAt(i) === 10) {
          break;
        } else {
          i += 1;
        }
      }
      push('string', start, i, containerIndex);
      if (!closed) {
        diagnostics.push(diagnostic(
          'KLS0001', 1, start, i,
          'unterminated string literal'
        ));
      }
      continue;
    }
    if (isIdentifierStart(code)) {
      const start = i++;
      while (i < text.length && isIdentifierContinue(text.charCodeAt(i))) i += 1;
      push('id', start, i, containerIndex);
      continue;
    }
    if (code >= 48 && code <= 57) {
      const start = i++;
      while (i < text.length) {
        const next = text.charCodeAt(i);
        if (!((next >= 48 && next <= 57) || next === 46 || next === 95 ||
              next === 101 || next === 69 || next === 43 || next === 45)) break;
        i += 1;
      }
      push('number', start, i, containerIndex);
      continue;
    }

    const character = text[i];
    if (character === '-' && text[i + 1] === '>') {
      push('->', i, i + 2, containerIndex);
      i += 2;
      continue;
    }
    if ('{(['.includes(character)) {
      const index = push(character, i, i + 1, containerIndex);
      delimiters.push({ kind: character, index });
      if (character === '{') curlyDepth += 1;
      i += 1;
      continue;
    }
    if ('})]'.includes(character)) {
      const expected = character === '}' ? '{' : character === ')' ? '(' : '[';
      const top = delimiters[delimiters.length - 1];
      if (!top || top.kind !== expected) {
        diagnostics.push(diagnostic(
          'KLS0003', 1, i, i + 1,
          `unmatched '${character}'`
        ));
        push(character, i, i + 1, containerIndex);
      } else {
        if (character === '}') curlyDepth -= 1;
        const index = push(character, i, i + 1, containerIndex);
        tokens[top.index].match = index;
        tokens[index].match = top.index;
        delimiters.pop();
      }
      i += 1;
      continue;
    }
    push(character, i, i + 1, containerIndex);
    i += 1;
  }

  for (const open of delimiters) {
    const token = tokens[open.index];
    diagnostics.push(diagnostic(
      'KLS0002', 1, token.start, token.end,
      `unclosed '${open.kind}'`
    ));
  }
  return { tokens, diagnostics };
}

function normalizedSlice(doc, first, lastExclusive) {
  if (first >= lastExclusive) return '';
  let value = '';
  for (let i = first; i < lastExclusive; i += 1) {
    const current = tokenText(doc, doc.tokens[i]);
    if (value && /^[A-Za-z_\u0080-\uFFFF]/u.test(current) &&
        /[A-Za-z0-9_\u0080-\uFFFF]$/u.test(value)) value += ' ';
    value += current;
  }
  return value;
}

function matchingToken(doc, index) {
  const token = doc.tokens[index];
  return token && token.match >= 0 ? token.match : -1;
}

function scopeEnd(doc, token) {
  if (token.container < 0) return doc.text.length;
  const open = doc.tokens[token.container];
  return open && open.match >= 0 ? doc.tokens[open.match].start : doc.text.length;
}

function inferType(doc, tokenIndex, incomplete) {
  const token = doc.tokens[tokenIndex];
  if (!token) return incomplete
    ? '<unknown: incomplete edit>' : '<unknown: inference unavailable>';
  const value = tokenText(doc, token);
  if (token.kind === 'string') return 'Text';
  if (token.kind === 'number') return value.includes('.') ? 'Float' : 'Int';
  if (value === 'true' || value === 'false') return 'Bool';
  if (value === 'null') return '<unknown: null literal>';
  if (value === '[') return 'List[<unknown>]';
  return incomplete
    ? '<unknown: incomplete edit>' : '<unknown: inference unavailable>';
}

function buildIndex(doc) {
  const scanned = tokenize(doc);
  doc.tokens = scanned.tokens;
  doc.diagnostics = scanned.diagnostics;
  doc.symbols = [];
  doc.declarations = new Set();
  const tokens = doc.tokens;

  function addSymbol(symbol) {
    symbol.name = doc.text.slice(symbol.start, symbol.end);
    doc.declarations.add(symbol.tokenIndex);
    doc.symbols.push(symbol);
    return symbol;
  }

  // Functions and their parameters.
  for (let i = 0; i + 2 < tokens.length; i += 1) {
    if (tokens[i].kind !== 'id' || tokenText(doc, tokens[i]) !== 'fn' ||
        tokens[i + 1].kind !== 'id' || tokenText(doc, tokens[i + 2]) !== '(') continue;
    const close = matchingToken(doc, i + 2);
    if (close < 0) continue;
    let body = close + 1;
    let returnType = 'Void';
    if (tokenText(doc, tokens[body] || { start: 0, end: 0 }) === '->') {
      const typeStart = body + 1;
      body = typeStart;
      while (body < tokens.length &&
             tokenText(doc, tokens[body]) !== '{' &&
             tokenText(doc, tokens[body]) !== '=' &&
             tokens[body].depth === tokens[i].depth) body += 1;
      returnType = normalizedSlice(doc, typeStart, body) ||
        '<unknown: incomplete edit>';
    }
    const bodyToken = tokens[body];
    const functionScopeStart = bodyToken ? bodyToken.end : tokens[close].end;
    let functionScopeEnd = doc.text.length;
    if (bodyToken && tokenText(doc, bodyToken) === '{' && bodyToken.match >= 0) {
      functionScopeEnd = tokens[bodyToken.match].start;
    } else {
      for (let next = body + 1; next < tokens.length; next += 1) {
        if (tokens[next].depth === tokens[i].depth &&
            tokenText(doc, tokens[next]) === 'fn') {
          functionScopeEnd = tokens[next].start;
          break;
        }
      }
    }

    const parameters = [];
    let segmentStart = i + 3;
    for (let cursor = segmentStart; cursor <= close; cursor += 1) {
      if (cursor !== close && tokenText(doc, tokens[cursor]) !== ',') continue;
      const segmentEnd = cursor;
      let at = segmentStart;
      let mode = '';
      if (at < segmentEnd &&
          ['read', 'edit', 'take'].includes(tokenText(doc, tokens[at]))) {
        mode = tokenText(doc, tokens[at++]);
      }
      if (at < segmentEnd && tokens[at].kind === 'id') {
        const nameToken = at;
        at += 1;
        let type = '<unknown: inference unavailable>';
        if (at < segmentEnd && tokenText(doc, tokens[at]) === ':') {
          type = normalizedSlice(doc, at + 1, segmentEnd) ||
            '<unknown: incomplete edit>';
        }
        parameters.push(`${mode ? `${mode} ` : ''}${tokenText(doc, tokens[nameToken])}: ${type}`);
        addSymbol({
          kind: 'parameter', tokenIndex: nameToken,
          start: tokens[nameToken].start, end: tokens[nameToken].end,
          type, mode, scopeStart: functionScopeStart,
          scopeEnd: functionScopeEnd, depth: tokens[i].depth + 1
        });
      }
      segmentStart = cursor + 1;
    }
    addSymbol({
      kind: 'function', tokenIndex: i + 1,
      start: tokens[i + 1].start, end: tokens[i + 1].end,
      type: `fn ${tokenText(doc, tokens[i + 1])}(${parameters.join(', ')}) -> ${returnType}`,
      mode: '', scopeStart: 0, scopeEnd: doc.text.length,
      depth: tokens[i].depth
    });
  }

  // Planned type declarations are useful to navigate even though Stage 0 only
  // accepts a subset of their forms.
  for (let i = 0; i + 1 < tokens.length; i += 1) {
    if (tokenText(doc, tokens[i]) === 'type' && tokens[i + 1].kind === 'id') {
      addSymbol({
        kind: 'type', tokenIndex: i + 1,
        start: tokens[i + 1].start, end: tokens[i + 1].end,
        type: `type ${tokenText(doc, tokens[i + 1])}`,
        mode: '', scopeStart: 0, scopeEnd: doc.text.length,
        depth: tokens[i].depth
      });
    }
  }

  // let bindings.
  for (let i = 0; i + 1 < tokens.length; i += 1) {
    if (tokenText(doc, tokens[i]) !== 'let') continue;
    let nameIndex = i + 1;
    while (nameIndex < tokens.length &&
           ['own', 'mut'].includes(tokenText(doc, tokens[nameIndex]))) nameIndex += 1;
    if (!tokens[nameIndex] || tokens[nameIndex].kind !== 'id') continue;
    let cursor = nameIndex + 1;
    let type = '';
    if (tokenText(doc, tokens[cursor] || { start: 0, end: 0 }) === ':') {
      const typeStart = ++cursor;
      while (cursor < tokens.length && tokenText(doc, tokens[cursor]) !== '=' &&
             tokens[cursor].container === tokens[i].container) cursor += 1;
      type = normalizedSlice(doc, typeStart, cursor);
    }
    while (cursor < tokens.length && tokenText(doc, tokens[cursor]) !== '=' &&
           tokens[cursor].container === tokens[i].container) cursor += 1;
    const incomplete = doc.diagnostics.length > 0;
    if (!type) type = inferType(doc, cursor + 1, incomplete);
    addSymbol({
      kind: 'binding', tokenIndex: nameIndex,
      start: tokens[nameIndex].start, end: tokens[nameIndex].end,
      type, mode: '', scopeStart: tokens[nameIndex].end,
      scopeEnd: scopeEnd(doc, tokens[nameIndex]), depth: tokens[nameIndex].depth
    });
  }

  // for-loop bindings.
  for (let i = 0; i + 1 < tokens.length; i += 1) {
    if (tokenText(doc, tokens[i]) !== 'for' || tokens[i + 1].kind !== 'id') continue;
    let end = scopeEnd(doc, tokens[i + 1]);
    for (let cursor = i + 2; cursor < tokens.length; cursor += 1) {
      if (tokenText(doc, tokens[cursor]) === '{') {
        if (tokens[cursor].match >= 0) end = tokens[tokens[cursor].match].start;
        break;
      }
    }
    addSymbol({
      kind: 'binding', tokenIndex: i + 1,
      start: tokens[i + 1].start, end: tokens[i + 1].end,
      type: '<unknown: inference unavailable>', mode: '',
      scopeStart: tokens[i + 1].end, scopeEnd: end,
      depth: tokens[i + 1].depth + 1
    });
  }

  // Duplicate declarations in the same lexical scope are deterministic and
  // cheap to diagnose after sorting by their already-linear insertion order.
  const seen = new Map();
  for (const symbol of doc.symbols) {
    const lexicalScope = symbol.kind === 'function' || symbol.kind === 'type'
      ? 'global' : `${symbol.depth}:${symbol.scopeEnd}`;
    const key = `${lexicalScope}:${symbol.name}`;
    if (seen.has(key)) {
      doc.diagnostics.push(diagnostic(
        'KLS1002', 1, symbol.start, symbol.end,
        `duplicate declaration '${symbol.name}'`
      ));
    } else {
      seen.set(key, symbol);
    }
  }

  const builtins = new Set([
    'assert', 'assert_eq', 'debug', 'len', 'panic', 'print',
    'Int', 'Float', 'Text', 'Bool', 'List', 'Map', 'Result', 'Option'
  ]);
  const nonReferences = new Set([
    'fn', 'if', 'else', 'while', 'for', 'return', 'let', 'law', 'meta'
  ]);
  for (let i = 0; i + 1 < tokens.length; i += 1) {
    const token = tokens[i];
    const name = tokenText(doc, token);
    if (token.kind !== 'id' || tokenText(doc, tokens[i + 1]) !== '(' ||
        doc.declarations.has(i) || builtins.has(name) || nonReferences.has(name) ||
        (i > 0 && tokenText(doc, tokens[i - 1]) === '.')) continue;
    if (!resolve(doc, token)) {
      doc.diagnostics.push(diagnostic(
        'KLS1001', 2, token.start, token.end,
        `unresolved function '${name}' in this document`
      ));
    }
  }
}

function collectAfterLargeReindex(doc) {
  // V8 otherwise deliberately retains several generations of discarded token
  // objects. Large editing sessions value a flat resident set over that cache.
  if (doc.text.length >= 256 * 1024 && typeof global.gc === 'function') global.gc();
}

function tokenAt(doc, offset) {
  let low = 0;
  let high = doc.tokens.length;
  while (low < high) {
    const middle = (low + high) >> 1;
    if (doc.tokens[middle].end <= offset) low = middle + 1;
    else high = middle;
  }
  const token = doc.tokens[low];
  if (token && token.start <= offset && offset <= token.end && token.kind === 'id') return token;
  if (low > 0) {
    const previous = doc.tokens[low - 1];
    if (previous.kind === 'id' && previous.start <= offset && offset <= previous.end) return previous;
  }
  return null;
}

function resolve(doc, token) {
  const name = tokenText(doc, token);
  let best = null;
  let ambiguous = false;
  for (const symbol of doc.symbols) {
    if (symbol.name !== name) continue;
    if (symbol.start === token.start && symbol.end === token.end) return symbol;
    if (token.start < symbol.scopeStart || token.start > symbol.scopeEnd) continue;
    if (symbol.kind !== 'function' && symbol.kind !== 'type' &&
        symbol.start > token.start) continue;
    if (!best || symbol.depth > best.depth) {
      best = symbol;
      ambiguous = false;
    } else if (symbol.depth === best.depth) {
      if (symbol.scopeEnd === best.scopeEnd) ambiguous = true;
      if (symbol.start > best.start) best = symbol;
    }
  }
  return ambiguous ? null : best;
}

function publishDiagnostics(doc, diagnostics = doc.diagnostics) {
  send({
    jsonrpc: '2.0',
    method: 'textDocument/publishDiagnostics',
    params: {
      uri: doc.uri,
      version: doc.version,
      diagnostics: diagnostics.map((item) => ({
        range: range(doc, item.start, item.end),
        severity: item.severity,
        code: item.code,
        source: 'kofun',
        message: item.message
      }))
    }
  });
}

function applyChanges(doc, changes) {
  for (const change of changes) {
    if (!change || typeof change.text !== 'string') return false;
    if (!change.range) {
      doc.text = change.text;
      doc.lines = lineStarts(doc.text);
      continue;
    }
    const start = positionToOffset(doc, change.range.start);
    const end = positionToOffset(doc, change.range.end);
    if (start === null || end === null || start > end) return false;
    doc.text = doc.text.slice(0, start) + change.text + doc.text.slice(end);
    doc.lines = lineStarts(doc.text);
  }
  return true;
}

function handle(message) {
  if (!message || message.jsonrpc !== '2.0' || typeof message.method !== 'string') {
    error(message && message.id, -32600, 'invalid request');
    return;
  }
  const params = message.params || {};
  switch (message.method) {
    case 'initialize':
      response(message.id, {
        capabilities: {
          positionEncoding: 'utf-16',
          textDocumentSync: { openClose: true, change: 2, save: false },
          definitionProvider: true,
          hoverProvider: true
        },
        serverInfo: { name: 'kofun-lsp', version: '0.1.0' }
      });
      break;
    case 'initialized':
    case '$/cancelRequest':
      break;
    case 'shutdown':
      shutdownRequested = true;
      response(message.id, null);
      break;
    case 'exit':
      process.exit(shutdownRequested ? 0 : 1);
      break;
    case 'textDocument/didOpen': {
      const item = params.textDocument;
      if (!item || typeof item.uri !== 'string' || typeof item.text !== 'string') return;
      const doc = {
        uri: item.uri, version: Number.isInteger(item.version) ? item.version : 0,
        text: item.text, lines: lineStarts(item.text)
      };
      buildIndex(doc);
      collectAfterLargeReindex(doc);
      documents.set(doc.uri, doc);
      publishDiagnostics(doc);
      break;
    }
    case 'textDocument/didChange': {
      const item = params.textDocument;
      const doc = item && documents.get(item.uri);
      if (!doc || !Number.isInteger(item.version) || item.version <= doc.version ||
          !Array.isArray(params.contentChanges)) return;
      if (!applyChanges(doc, params.contentChanges)) return;
      doc.version = item.version;
      buildIndex(doc);
      collectAfterLargeReindex(doc);
      publishDiagnostics(doc);
      break;
    }
    case 'textDocument/didClose': {
      const item = params.textDocument;
      const doc = item && documents.get(item.uri);
      if (!doc) return;
      publishDiagnostics(doc, []);
      documents.delete(item.uri);
      break;
    }
    case 'textDocument/definition': {
      const item = params.textDocument;
      const doc = item && documents.get(item.uri);
      const offset = doc ? positionToOffset(doc, params.position) : null;
      const token = offset === null || !doc ? null : tokenAt(doc, offset);
      const symbol = token ? resolve(doc, token) : null;
      response(message.id, symbol ? {
        uri: doc.uri,
        range: range(doc, symbol.start, symbol.end)
      } : null);
      break;
    }
    case 'textDocument/hover': {
      const item = params.textDocument;
      const doc = item && documents.get(item.uri);
      const offset = doc ? positionToOffset(doc, params.position) : null;
      const token = offset === null || !doc ? null : tokenAt(doc, offset);
      const symbol = token ? resolve(doc, token) : null;
      if (!doc || !token || !symbol) {
        response(message.id, null);
        break;
      }
      let value = symbol.type;
      if (symbol.kind !== 'function' && symbol.kind !== 'type') {
        value = `${symbol.name}: ${symbol.type}`;
        if (symbol.mode) value += ` (mode: ${symbol.mode})`;
      }
      response(message.id, {
        contents: { kind: 'markdown', value: `\`\`\`kofun\n${value}\n\`\`\`` },
        range: range(doc, token.start, token.end)
      });
      break;
    }
    default:
      if (Object.prototype.hasOwnProperty.call(message, 'id')) {
        error(message.id, -32601, `method not found: ${message.method}`);
      }
  }
}

function drain() {
  for (;;) {
    let headerEnd = input.indexOf('\r\n\r\n');
    let separatorLength = 4;
    if (headerEnd < 0) {
      headerEnd = input.indexOf('\n\n');
      separatorLength = 2;
    }
    if (headerEnd < 0) return;
    const header = input.subarray(0, headerEnd).toString('ascii');
    const match = /(?:^|\r?\n)Content-Length:\s*(\d+)\s*(?:\r?\n|$)/i.exec(header);
    if (!match) {
      input = input.subarray(headerEnd + separatorLength);
      error(null, -32700, 'missing Content-Length header');
      continue;
    }
    const length = Number(match[1]);
    if (!Number.isSafeInteger(length) || length < 0 || length > 32 * 1024 * 1024) {
      input = input.subarray(headerEnd + separatorLength);
      error(null, -32700, 'invalid Content-Length header');
      continue;
    }
    const messageEnd = headerEnd + separatorLength + length;
    if (input.length < messageEnd) return;
    const body = input.subarray(headerEnd + separatorLength, messageEnd);
    input = input.subarray(messageEnd);
    try {
      handle(JSON.parse(body.toString('utf8')));
    } catch (caught) {
      error(null, -32700, 'invalid JSON');
    }
  }
}

process.stdin.on('data', (chunk) => {
  input = Buffer.concat([input, chunk]);
  drain();
});
process.stdin.on('end', () => process.exit(shutdownRequested ? 0 : 1));
process.stdin.on('error', (caught) => {
  fs.writeSync(2, `kofun-lsp: ${caught.message}\n`);
  process.exit(1);
});
