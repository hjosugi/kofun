#!/usr/bin/env node
'use strict';

/*
 * A deliberately small language server for the syntax that the bootstrap
 * Kofun frontend accepts today. Covered Stage 2 semantics come only from the
 * validated typed-sidecar adapter. The bounded tokenizer below remains an
 * explicitly labelled fallback for producer profiles that report ETS04.
 */

const fs = require('fs');
const path = require('path');
const { fileURLToPath } = require('url');

const documents = new Map();
const MAX_HEADER_BYTES = 8 * 1024;

// The unresolved-call diagnostic and the completion list must never disagree
// about what this server claims to know, so both read the same names.
const BUILTIN_FUNCTIONS = Object.freeze([
  'assert', 'assert_eq', 'debug', 'len', 'panic', 'print'
]);
const BUILTIN_TYPES = Object.freeze([
  'Int', 'Float', 'Text', 'Bool', 'List', 'Map', 'Result', 'Option'
]);
const KEYWORDS = Object.freeze([
  'fn', 'if', 'else', 'while', 'for', 'return', 'let', 'law', 'meta'
]);
const MODE_KEYWORDS = Object.freeze(['read', 'edit', 'take']);
const BINDING_KEYWORDS = Object.freeze(['own', 'mut']);
const builtinNames = new Set([...BUILTIN_FUNCTIONS, ...BUILTIN_TYPES]);
const keywordNames = new Set(KEYWORDS);
let input = Buffer.alloc(0);
let shutdownRequested = false;
let framingFailed = false;
let workspaceRoot = null;
let sessionSequence = 0;
let semanticAdapter = null;
let semanticLoadFailureLogged = false;
const semanticAdapterPromise = import('./semantic-sidecar.mjs').then((loaded) => {
  semanticAdapter = loaded;
  return loaded;
});

function send(message, callback) {
  const body = Buffer.from(JSON.stringify(message), 'utf8');
  const header = Buffer.from(`Content-Length: ${body.length}\r\n\r\n`, 'ascii');
  process.stdout.write(Buffer.concat([header, body]), callback);
}

function response(id, result) {
  send({ jsonrpc: '2.0', id, result });
}

function error(id, code, message) {
  send({ jsonrpc: '2.0', id: id === undefined ? null : id, error: { code, message } });
}

function fatalFraming(message) {
  if (framingFailed) return;
  framingFailed = true;
  input = Buffer.alloc(0);
  process.stdin.pause();
  send({
    jsonrpc: '2.0', id: null,
    error: { code: -32700, message }
  }, () => process.exit(1));
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
  const curlyContainers = [];
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
    const containerIndex = curlyContainers.length > 0
      ? curlyContainers[curlyContainers.length - 1] : -1;
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
      if (character === '{') {
        curlyContainers.push(index);
        curlyDepth += 1;
      }
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
        if (character === '}') {
          curlyContainers.pop();
          curlyDepth -= 1;
        }
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
          MODE_KEYWORDS.includes(tokenText(doc, tokens[at]))) {
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
           BINDING_KEYWORDS.includes(tokenText(doc, tokens[nameIndex]))) nameIndex += 1;
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

  for (let i = 0; i + 1 < tokens.length; i += 1) {
    const token = tokens[i];
    const name = tokenText(doc, token);
    if (token.kind !== 'id' || tokenText(doc, tokens[i + 1]) !== '(' ||
        doc.declarations.has(i) || builtinNames.has(name) || keywordNames.has(name) ||
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

function completionIndex(doc) {
  // Names and lexical scopes are not carried by the typed sidecar, and the
  // semantic path never builds the bounded index. Build it against a detached
  // view so the fallback path's own tokens and diagnostics stay untouched; the
  // KLS diagnostics it collects there are never published, because Stage 2
  // owns diagnostics whenever a validated sidecar exists.
  if (doc.analysisState === 'syntactic-fallback' && doc.tokens) return doc;
  if (doc.lexicalIndex && doc.lexicalIndex.text === doc.text) return doc.lexicalIndex;
  const view = { text: doc.text };
  buildIndex(view);
  doc.lexicalIndex = view;
  return view;
}

function tokenIndexAt(doc, offset) {
  let low = 0;
  let high = doc.tokens.length;
  while (low < high) {
    const middle = (low + high) >> 1;
    if (doc.tokens[middle].end <= offset) low = middle + 1;
    else high = middle;
  }
  return low;
}

function completionContext(doc, offset) {
  // Strings are tokenized; comments are skipped by the tokenizer and so are
  // recovered from the line text. Completing inside either would offer names
  // that are not references at all.
  // A caret at the closing quote ends the string token, so the preceding token
  // has to be considered too, exactly as tokenAt does for identifiers.
  const at = tokenIndexAt(doc, offset);
  for (const candidate of [doc.tokens[at], doc.tokens[at - 1]]) {
    if (candidate && candidate.kind === 'string' &&
        candidate.start < offset && offset <= candidate.end) return 'string';
  }
  let lineStart = offset;
  while (lineStart > 0 && doc.text.charCodeAt(lineStart - 1) !== 10) lineStart -= 1;
  for (let at = lineStart; at < offset; at += 1) {
    if (doc.text.charCodeAt(at) !== 35) continue;
    const covering = doc.tokens[tokenIndexAt(doc, at)];
    const quoted = covering && covering.kind === 'string' &&
      covering.start <= at && at < covering.end;
    if (!quoted) return 'comment';
  }
  return 'code';
}

function visibleSymbols(doc, offset) {
  // Mirrors resolve(): the same visibility window and the same shadowing order,
  // so every name offered here is the declaration that definition would jump
  // to. Two declarations of one name in one scope already raise KLS1002, so the
  // later one wins instead of the name being offered twice.
  const visible = new Map();
  for (const symbol of doc.symbols) {
    if (offset < symbol.scopeStart || offset > symbol.scopeEnd) continue;
    if (symbol.kind !== 'function' && symbol.kind !== 'type' &&
        symbol.start > offset) continue;
    const previous = visible.get(symbol.name);
    if (!previous || symbol.depth > previous.depth ||
        (symbol.depth === previous.depth && symbol.start > previous.start)) {
      visible.set(symbol.name, symbol);
    }
  }
  return visible;
}

// LSP CompletionItemKind: Function, Variable, Class, Keyword.
const COMPLETION_KIND = Object.freeze({
  function: 3, parameter: 6, binding: 6, type: 7, keyword: 14
});

// Which sidecar node kind must have declared a name before its checked facts
// may be attached to that name's completion item.
const SIDECAR_DECLARATION_KINDS = Object.freeze({
  function: ['function.declaration'],
  parameter: ['parameter.binding'],
  binding: ['local.binding'],
  type: ['adt.declaration']
});

// A single scope can hold thousands of bindings, and one keystroke must not
// serialize all of them. The list is filtered by the identifier already typed
// and then bounded; a bounded list is reported as incomplete so the client
// asks again as the prefix narrows, which is what isIncomplete is for.
const MAX_COMPLETION_ITEMS = 200;

function completionPrefix(doc, offset) {
  let start = offset;
  while (start > 0 && isIdentifierContinue(doc.text.charCodeAt(start - 1))) start -= 1;
  // A run starting with a digit is a number literal, not a name being typed.
  if (start < offset && !isIdentifierStart(doc.text.charCodeAt(start))) return '';
  return doc.text.slice(start, offset);
}

function completionItems(doc, offset, facts) {
  const prefix = completionPrefix(doc, offset).toLowerCase();
  const taken = new Set();
  function item(label, kind, detail, group, provenance) {
    if (taken.has(label)) return null;
    if (prefix && !label.toLowerCase().startsWith(prefix)) return null;
    taken.add(label);
    const value = {
      label, kind,
      // Locals before parameters before functions before types before the
      // fixed vocabulary, so the nearest declaration is the first suggestion.
      sortText: `${group}${label}`,
      data: { provenance }
    };
    if (detail) value.detail = detail;
    return value;
  }

  // Declarations are emitted first so a binding named `print` shadows the
  // builtin rather than being dropped as a duplicate, but their share of the
  // bound is reduced by the whole fixed vocabulary: a scope holding thousands
  // of bindings must not push `print` and `let` out of the list.
  const reserved = BUILTIN_FUNCTIONS.length + BUILTIN_TYPES.length +
    KEYWORDS.length + MODE_KEYWORDS.length + BINDING_KEYWORDS.length;
  const declarations = [];
  let truncated = false;
  for (const symbol of visibleSymbols(doc, offset).values()) {
    if (declarations.length >= MAX_COMPLETION_ITEMS - reserved) {
      truncated = true;
      break;
    }
    const fact = facts ? facts.get(symbol.start) : null;
    const type = fact && fact.type ? fact.type : symbol.type;
    const mode = fact && fact.ownership ? fact.ownership : symbol.mode;
    let detail = symbol.kind === 'function' || symbol.kind === 'type'
      ? type : `${symbol.name}: ${type}`;
    if (mode && symbol.kind !== 'function' && symbol.kind !== 'type') {
      detail += ` (mode: ${mode})`;
    }
    // Hover marks a fact from a still-failing document as provisional; a
    // completion item drawn from the same node must not read as settled.
    if (fact && fact.provisional) detail += ' (provisional)';
    const group = symbol.kind === 'binding' ? '0'
      : symbol.kind === 'parameter' ? '1'
      : symbol.kind === 'function' ? '2' : '3';
    const value = item(symbol.name, COMPLETION_KIND[symbol.kind] ?? 6, detail, group,
      fact ? 'validated-sidecar' : 'syntactic-fallback');
    if (value) declarations.push(value);
  }

  const vocabulary = [];
  for (const name of BUILTIN_FUNCTIONS) {
    vocabulary.push(item(name, COMPLETION_KIND.function, `builtin ${name}`, '4', 'builtin'));
  }
  for (const name of BUILTIN_TYPES) {
    vocabulary.push(item(name, COMPLETION_KIND.type, `builtin type ${name}`, '5', 'builtin'));
  }
  for (const name of [...KEYWORDS, ...MODE_KEYWORDS, ...BINDING_KEYWORDS]) {
    vocabulary.push(item(name, COMPLETION_KIND.keyword, undefined, '6', 'keyword'));
  }
  return { items: [...declarations, ...vocabulary.filter(Boolean)], truncated };
}

function publishSyntacticDiagnostics(doc, diagnostics = doc.diagnostics) {
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
        source: 'kofun-syntax',
        message: item.message,
        data: { analysis: 'syntactic' }
      }))
    }
  });
}

function publishSemanticDiagnostics(doc, diagnostics) {
  send({
    jsonrpc: '2.0',
    method: 'textDocument/publishDiagnostics',
    params: { uri: doc.uri, version: doc.version, diagnostics }
  });
}

function logicalPath(uri) {
  try {
    const parsed = new URL(uri);
    if (parsed.protocol === 'file:') {
      const filename = fileURLToPath(parsed);
      let value = workspaceRoot ? path.relative(workspaceRoot, filename) :
        path.basename(filename);
      if (value && !path.isAbsolute(value) &&
          value !== '..' && !value.startsWith(`..${path.sep}`)) {
        value = value.split(path.sep).join('/').normalize('NFC');
        if (value && !value.split('/').some((part) =>
          part === '' || part === '.' || part === '..')) return value;
      }
      const basename = path.basename(filename).normalize('NFC');
      if (basename && basename !== '.' && basename !== '..') return basename;
    }
  } catch {
    // Untitled and custom-scheme buffers use a transport-independent identity.
  }
  return 'editor-buffer.kofun';
}

function currentAnalysis(doc, captured) {
  return documents.get(doc.uri) === doc &&
    doc.version === captured.version &&
    doc.generation === captured.generation &&
    doc.sessionEpoch === captured.sessionEpoch &&
    doc.text === captured.sourceText &&
    !captured.signal.aborted;
}

async function runSemanticAnalysis(doc, captured, retry = 0) {
  let adapter;
  let result;
  try {
    adapter = await semanticAdapterPromise;
    result = await adapter.analyzeDocument({
      uri: doc.uri,
      version: captured.version,
      generation: captured.generation,
      sessionEpoch: captured.sessionEpoch,
      logicalPath: doc.logicalPath,
      sourceText: captured.sourceText,
      expectedFileId: doc.fileId,
    }, captured.signal);
  } catch (caught) {
    result = { ok: false, code: 'ETS03', detail: caught.message };
    if (!semanticLoadFailureLogged) {
      semanticLoadFailureLogged = true;
      fs.writeSync(2, `kofun-lsp semantic adapter: ${caught.message}\n`);
    }
  }
  if (!currentAnalysis(doc, captured) || result.cancelled) return;
  if (!result.ok) {
    if (result.code === 'ETS04') {
      buildIndex(doc);
      collectAfterLargeReindex(doc);
      doc.analysisState = 'syntactic-fallback';
      doc.validatedSidecar = null;
      publishSyntacticDiagnostics(doc);
      return;
    }
    if (retry === 0) {
      setImmediate(() => runSemanticAnalysis(doc, captured, 1));
      return;
    }
    doc.analysisState = 'discarded-invalid-sidecar';
    doc.validatedSidecar = null;
    publishSemanticDiagnostics(doc, []);
    return;
  }
  if (!currentAnalysis(doc, captured)) return;
  doc.fileId = result.snapshot.fileId;
  doc.validatedSidecar = result.snapshot;
  doc.analysisState = 'semantic';
  publishSemanticDiagnostics(doc, adapter.publishDiagnostics(result.snapshot));
}

function scheduleSemanticAnalysis(doc) {
  if (doc.abortController) doc.abortController.abort();
  doc.abortController = new AbortController();
  doc.generation += 1;
  doc.analysisState = 'pending';
  doc.validatedSidecar = null;
  const captured = Object.freeze({
    version: doc.version,
    generation: doc.generation,
    sessionEpoch: doc.sessionEpoch,
    sourceText: doc.text,
    signal: doc.abortController.signal,
  });
  void runSemanticAnalysis(doc, captured);
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
      if (typeof params.rootUri === 'string') {
        try {
          const root = new URL(params.rootUri);
          workspaceRoot = root.protocol === 'file:' ? fileURLToPath(root) : null;
        } catch {
          workspaceRoot = null;
        }
      }
      response(message.id, {
        capabilities: {
          positionEncoding: 'utf-16',
          textDocumentSync: { openClose: true, change: 2, save: false },
          definitionProvider: true,
          hoverProvider: true,
          // No trigger characters: member and field completion is not
          // implemented, so '.' must not advertise a list this server cannot
          // produce. Completion is driven by the identifier being typed.
          completionProvider: { resolveProvider: false }
        },
        serverInfo: { name: 'kofun-lsp', version: '0.1.0' }
      });
      break;
    case 'initialized':
    case '$/cancelRequest':
      break;
    case 'shutdown':
      shutdownRequested = true;
      for (const doc of documents.values()) doc.abortController?.abort();
      response(message.id, null);
      break;
    case 'exit':
      if (semanticAdapter) {
        void semanticAdapter.shutdownSemanticAnalysis().finally(() =>
          process.exit(shutdownRequested ? 0 : 1));
      } else {
        process.exit(shutdownRequested ? 0 : 1);
      }
      break;
    case 'textDocument/didOpen': {
      const item = params.textDocument;
      if (!item || typeof item.uri !== 'string' || typeof item.text !== 'string') return;
      const doc = {
        uri: item.uri, version: Number.isInteger(item.version) ? item.version : 0,
        text: item.text, lines: lineStarts(item.text),
        logicalPath: logicalPath(item.uri),
        sessionEpoch: ++sessionSequence,
        generation: 0,
        fileId: null,
        validatedSidecar: null,
        analysisState: 'pending',
        abortController: null,
      };
      const previous = documents.get(doc.uri);
      if (previous) previous.abortController?.abort();
      documents.set(doc.uri, doc);
      scheduleSemanticAnalysis(doc);
      break;
    }
    case 'textDocument/didChange': {
      const item = params.textDocument;
      const doc = item && documents.get(item.uri);
      if (!doc || !Number.isInteger(item.version) || item.version <= doc.version ||
          !Array.isArray(params.contentChanges)) return;
      if (!applyChanges(doc, params.contentChanges)) return;
      doc.version = item.version;
      scheduleSemanticAnalysis(doc);
      break;
    }
    case 'textDocument/didClose': {
      const item = params.textDocument;
      const doc = item && documents.get(item.uri);
      if (!doc) return;
      doc.abortController?.abort();
      doc.validatedSidecar = null;
      publishSemanticDiagnostics(doc, []);
      documents.delete(item.uri);
      break;
    }
    case 'textDocument/definition': {
      const item = params.textDocument;
      const doc = item && documents.get(item.uri);
      if (doc && doc.analysisState === 'semantic' &&
          doc.validatedSidecar && semanticAdapter) {
        response(message.id, semanticAdapter.definitionAt(
          doc.validatedSidecar, params.position));
        break;
      }
      if (!doc || doc.analysisState !== 'syntactic-fallback') {
        response(message.id, null);
        break;
      }
      const offset = doc ? positionToOffset(doc, params.position) : null;
      const token = offset === null || !doc ? null : tokenAt(doc, offset);
      const symbol = token ? resolve(doc, token) : null;
      response(message.id, symbol ? {
        uri: doc.uri,
        range: range(doc, symbol.start, symbol.end)
      } : null);
      break;
    }
    case 'textDocument/completion': {
      const item = params.textDocument;
      const doc = item && documents.get(item.uri);
      const offset = doc ? positionToOffset(doc, params.position) : null;
      if (!doc || offset === null) {
        response(message.id, { isIncomplete: false, items: [] });
        break;
      }
      // Analysis that has not settled must not answer with a list the next
      // version would contradict; isIncomplete asks the client to come back.
      if (doc.analysisState !== 'semantic' &&
          doc.analysisState !== 'syntactic-fallback') {
        response(message.id, { isIncomplete: true, items: [] });
        break;
      }
      const index = completionIndex(doc);
      if (completionContext(index, offset) !== 'code') {
        response(message.id, { isIncomplete: false, items: [] });
        break;
      }
      let facts = null;
      if (doc.analysisState === 'semantic' && doc.validatedSidecar && semanticAdapter) {
        const symbols = [...visibleSymbols(index, offset).values()];
        const requests = symbols.map((symbol) => {
          const kinds = SIDECAR_DECLARATION_KINDS[symbol.kind];
          if (!kinds) return null;
          return {
            offset: semanticAdapter.positionToByte(
              doc.validatedSidecar, offsetToPosition(doc, symbol.start)),
            kinds
          };
        });
        const resolved = semanticAdapter.declarationFactsAt(doc.validatedSidecar, requests);
        facts = new Map();
        for (const [at, symbol] of symbols.entries()) {
          if (resolved[at]) facts.set(symbol.start, resolved[at]);
        }
      }
      const completion = completionItems(index, offset, facts);
      response(message.id, {
        // A bounded list must say so, or the client caches it and stops asking
        // as the prefix narrows to names that were cut.
        isIncomplete: completion.truncated,
        items: completion.items
      });
      break;
    }
    case 'textDocument/hover': {
      const item = params.textDocument;
      const doc = item && documents.get(item.uri);
      if (doc && doc.analysisState === 'semantic' &&
          doc.validatedSidecar && semanticAdapter) {
        response(message.id, semanticAdapter.hoverAt(
          doc.validatedSidecar, params.position));
        break;
      }
      if (!doc || doc.analysisState !== 'syntactic-fallback') {
        response(message.id, null);
        break;
      }
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
        contents: {
          kind: 'markdown',
          value: `**syntactic fallback**\n\n\`\`\`kofun\n${value}\n\`\`\``
        },
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
    if (headerEnd < 0) {
      if (input.length > MAX_HEADER_BYTES) {
        fatalFraming(`header exceeds ${MAX_HEADER_BYTES} bytes`);
      }
      return;
    }
    if (headerEnd > MAX_HEADER_BYTES) {
      fatalFraming(`header exceeds ${MAX_HEADER_BYTES} bytes`);
      return;
    }
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
  if (framingFailed) return;
  input = Buffer.concat([input, chunk]);
  drain();
});
process.stdin.on('end', () => process.exit(shutdownRequested ? 0 : 1));
process.stdin.on('error', (caught) => {
  fs.writeSync(2, `kofun-lsp: ${caught.message}\n`);
  process.exit(1);
});
