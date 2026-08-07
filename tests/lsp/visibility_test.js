'use strict';

// The focused gate for #1033: completion and navigation apply the compiler's
// accepted visibility decision, using the requesting file as caller context.
//
// Every scenario drives the real `tooling/lsp/kofun-lsp` over stdio. Nothing
// here reaches into the server's internals, because the property under test is
// what an editor can observe — and a disclosure is only a disclosure if it
// reaches the wire.
//
// Every assertion names what failed, per #814 and #838.

const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { Client } = require('./client.js');

const SERVER = process.argv[2];
if (!SERVER) {
  console.error('usage: node visibility_test.js PATH_TO_KOFUN_LSP');
  process.exit(2);
}

// `private` is the default, so the unmarked declaration is the one that proves
// an omitted modifier is treated as private rather than as unknown.
const DEPENDENCY = [
  'fn dep_unmarked(value: Int) -> Int {', '    return value', '}', '',
  'private fn dep_private(value: Int) -> Int {', '    return value', '}', '',
  'internal fn dep_internal(value: Int) -> Int {', '    return value', '}', '',
  'pub fn dep_public(value: Int) -> Int {', '    return value', '}', ''
].join('\n');

const CALLER = [
  'private fn own_private(value: Int) -> Int {', '    return value', '}', '',
  'fn caller() -> Int {', '    return dep', '}', '',
  'fn reaches_public() -> Int {', '    return dep_public(1)', '}', '',
  'fn reaches_private() -> Int {', '    return dep_private(1)', '}', '',
  'fn reaches_own() -> Int {', '    return own', '}', ''
].join('\n');

function makeWorkspace(withManifest) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'kofun-lsp-visibility-'));
  fs.mkdirSync(path.join(root, 'demo'), { recursive: true });
  if (withManifest) {
    fs.writeFileSync(path.join(root, 'kofun.toml'), '[package]\nname = "demo"\n');
  }
  return root;
}

class Session {
  constructor(root) {
    this.root = root;
    this.client = new Client(SERVER);
    this.id = 1;
  }

  async start() {
    this.client.send({ jsonrpc: '2.0', id: this.id, method: 'initialize',
      params: { rootUri: `file://${this.root}`, capabilities: {} } });
    await this.client.waitFor((m) => m.id === this.id);
    this.id += 1;
    this.client.send({ jsonrpc: '2.0', method: 'initialized', params: {} });
  }

  async open(relative, text) {
    const uri = `file://${this.root}/${relative}`;
    this.client.send({ jsonrpc: '2.0', method: 'textDocument/didOpen', params: {
      textDocument: { uri, languageId: 'kofun', version: 1, text } } });
    await this.client.waitFor((m) => m.method === 'textDocument/publishDiagnostics' &&
      m.params.uri === uri && m.params.version === 1);
    return uri;
  }

  async request(method, params) {
    const id = this.id++;
    this.client.send({ jsonrpc: '2.0', id, method, params });
    const reply = await this.client.waitFor((m) => m.id === id);
    return reply.result;
  }

  async completionLabels(uri, text, lineText, character) {
    const line = text.split('\n').indexOf(lineText);
    assert.notStrictEqual(line, -1,
      `test fixture has no line ${JSON.stringify(lineText)}; the scenario would assert nothing`);
    const result = await this.request('textDocument/completion',
      { textDocument: { uri }, position: { line, character } });
    return ((result && result.items) || []).map((entry) => entry.label);
  }

  async definitionAt(uri, text, lineText, character) {
    const line = text.split('\n').indexOf(lineText);
    assert.notStrictEqual(line, -1,
      `test fixture has no line ${JSON.stringify(lineText)}; the scenario would assert nothing`);
    return this.request('textDocument/definition',
      { textDocument: { uri }, position: { line, character } });
  }

  async stop() {
    await this.client.stop(this.id++);
  }
}

// The caret sits just past `dep` on the `return dep` line, so the prefix is
// `dep` and the offered set is exactly the dependency's declarations the caller
// may reach.
const DEP_PREFIX_LINE = '    return dep';
const DEP_PREFIX_COLUMN = 14;

async function scenarioSamePackage() {
  const root = makeWorkspace(true);
  const session = new Session(root);
  await session.start();
  await session.open('demo/dep.kofun', DEPENDENCY);
  const callerUri = await session.open('demo/caller.kofun', CALLER);

  const labels = await session.completionLabels(
    callerUri, CALLER, DEP_PREFIX_LINE, DEP_PREFIX_COLUMN);

  assert.deepStrictEqual(labels.slice().sort(), ['dep_internal', 'dep_public'],
    `inside one package, completion must offer exactly the dependency's internal and public declarations; it offered ${JSON.stringify(labels)}`);
  assert.ok(!labels.includes('dep_private'),
    'completion disclosed another file\'s `private` declaration');
  assert.ok(!labels.includes('dep_unmarked'),
    'completion disclosed another file\'s unmarked declaration; an omitted modifier is private');

  // The contrast the criterion asks for: the caller's own private helper stays
  // available in the caller's own editor session.
  const ownLabels = await session.completionLabels(
    callerUri, CALLER, '    return own', DEP_PREFIX_COLUMN);
  assert.ok(ownLabels.includes('own_private'),
    `a file's own private declaration must stay available in that file; completion offered ${JSON.stringify(ownLabels)}`);

  await session.stop();
  return labels;
}

async function scenarioAnonymousPackage() {
  const root = makeWorkspace(false);
  const session = new Session(root);
  await session.start();
  await session.open('demo/dep.kofun', DEPENDENCY);
  const callerUri = await session.open('demo/caller.kofun', CALLER);

  const labels = await session.completionLabels(
    callerUri, CALLER, DEP_PREFIX_LINE, DEP_PREFIX_COLUMN);
  // spec/modules/package-roots.md: with no selected manifest each source is an
  // anonymous single-file package, and those are non-importable. Nothing
  // crosses, including `pub`. An editor that cannot identify the package must
  // fail closed, because the failure mode of guessing is disclosure.
  assert.deepStrictEqual(labels, [],
    `without a package manifest nothing may cross a file boundary, not even \`pub\`; completion offered ${JSON.stringify(labels)}`);

  await session.stop();
}

async function scenarioNavigationDoesNotDisclose() {
  const root = makeWorkspace(true);
  const session = new Session(root);
  await session.start();
  const depUri = await session.open('demo/dep.kofun', DEPENDENCY);
  const callerUri = await session.open('demo/caller.kofun', CALLER);

  const reachable = await session.definitionAt(
    callerUri, CALLER, '    return dep_public(1)', 13);
  assert.ok(reachable && reachable.uri === depUri,
    `definition on an accessible public declaration must reach its file; got ${JSON.stringify(reachable)}`);

  const refused = await session.definitionAt(
    callerUri, CALLER, '    return dep_private(1)', 13);
  // Identical to the answer for a name that does not exist anywhere. A
  // distinguishable refusal would disclose that the name exists and which file
  // holds it.
  assert.strictEqual(refused, null,
    `definition on another file's private declaration must be indistinguishable from an unknown name; got ${JSON.stringify(refused)}`);

  const absent = await session.definitionAt(
    callerUri, CALLER, '    return dep', DEP_PREFIX_COLUMN);
  assert.strictEqual(absent, null,
    `definition on a name that exists nowhere must be null; got ${JSON.stringify(absent)}`);

  const serialised = JSON.stringify(refused);
  for (const leaked of ['dep.kofun', 'dep_private', 'line', 'character']) {
    assert.ok(!serialised.includes(leaked),
      `the refusal disclosed ${JSON.stringify(leaked)}; an inaccessible target may not leak a path, a span, or a candidate name`);
  }

  await session.stop();
}

// Caller context must come from the request's own transport identity, never
// from a document's parsed content or its sidecar. The observable consequence:
// two files whose *content is byte-identical* are still two files, so neither
// may reach the other's private declarations. If identity were derived from
// what the compiler read out of the buffer, these two would look like one file
// and the private declaration would leak.
async function scenarioIdentityIsNotContent() {
  const root = makeWorkspace(true);
  const session = new Session(root);
  await session.start();
  const twin = [
    'private fn twin_private(value: Int) -> Int {', '    return value', '}', '',
    'fn twin_caller() -> Int {', '    return twin', '}', ''
  ].join('\n');
  await session.open('demo/left.kofun', twin);
  const rightUri = await session.open('demo/right.kofun', twin);

  const labels = await session.completionLabels(
    rightUri, twin, '    return twin', 15);
  assert.ok(labels.includes('twin_private'),
    `each file must still see its own private declaration; completion offered ${JSON.stringify(labels)}`);
  assert.strictEqual(labels.filter((label) => label === 'twin_private').length, 1,
    `byte-identical content must not make one file's private declaration reachable from the other; completion offered ${JSON.stringify(labels)}`);

  await session.stop();
}

// Two documents whose *display path* collides must still be two files.
//
// `logicalPath` is not injective: it falls back to the basename for a file
// outside the workspace root, and to a fixed string for an untitled buffer. An
// implementation that used it as the file identity — the first one written
// here did — hands each of these two files the other's private declarations,
// because `accessible` sees one file. Both collision shapes are exercised.
async function scenarioCollidingDisplayPaths() {
  const root = makeWorkspace(true);
  const outsideLeft = fs.mkdtempSync(path.join(os.tmpdir(), 'kofun-lsp-left-'));
  const outsideRight = fs.mkdtempSync(path.join(os.tmpdir(), 'kofun-lsp-right-'));
  const session = new Session(root);
  await session.start();

  const secret = [
    'private fn collide_private(value: Int) -> Int {', '    return value', '}', ''
  ].join('\n');
  const prober = ['fn prober() -> Int {', '    return collide', '}', ''].join('\n');

  // Same basename, different directories, both outside the workspace root.
  const leftUri = `file://${outsideLeft}/util.kofun`;
  const rightUri = `file://${outsideRight}/util.kofun`;
  for (const [uri, text] of [[leftUri, secret], [rightUri, prober]]) {
    session.client.send({ jsonrpc: '2.0', method: 'textDocument/didOpen', params: {
      textDocument: { uri, languageId: 'kofun', version: 1, text } } });
    await session.client.waitFor((m) => m.method === 'textDocument/publishDiagnostics' &&
      m.params.uri === uri && m.params.version === 1);
  }
  const labels = await session.completionLabels(rightUri, prober, '    return collide', 18);
  assert.ok(!labels.includes('collide_private'),
    `two files outside the workspace root that share a basename must stay separate files; completion offered ${JSON.stringify(labels)}`);

  // Untitled buffers, which share one fixed display path.
  const untitledLeft = 'untitled:Untitled-1';
  const untitledRight = 'untitled:Untitled-2';
  for (const [uri, text] of [[untitledLeft, secret], [untitledRight, prober]]) {
    session.client.send({ jsonrpc: '2.0', method: 'textDocument/didOpen', params: {
      textDocument: { uri, languageId: 'kofun', version: 1, text } } });
    await session.client.waitFor((m) => m.method === 'textDocument/publishDiagnostics' &&
      m.params.uri === uri && m.params.version === 1);
  }
  const untitledLabels = await session.completionLabels(
    untitledRight, prober, '    return collide', 18);
  assert.ok(!untitledLabels.includes('collide_private'),
    `two untitled buffers share one display path but are two files; completion offered ${JSON.stringify(untitledLabels)}`);

  await session.stop();
}

// A visibility modifier is contextual: `spec/modules/visibility.md` says the
// lexer keeps emitting ordinary identifier tokens outside a position where a
// declaration may begin. A module or import path whose last segment happens to
// be `internal` or `pub` is such a position, and reading it as a modifier
// disclosed the private declarations of a file that carries no modifier at all
// — the default, most conservative way to write one.
async function scenarioModifierPositionIsContextual() {
  const root = makeWorkspace(true);
  const session = new Session(root);
  await session.start();

  // No modifier anywhere in this file. Every declaration is private by default.
  for (const [name, header] of [
    ['module', 'module demo.internal'],
    ['import', 'import demo.internal'],
    ['pub-segment', 'module demo.pub'],
  ]) {
    const dep = `${header}\n\nfn hidden_${name.replace('-', '_')}(value: Int) -> Int {\n    return value\n}\n`;
    await session.open(`demo/dep_${name}.kofun`, dep);
  }
  const probe = 'fn probe() -> Int {\n    return hidden\n}\n';
  const probeUri = await session.open('demo/probe.kofun', probe);

  const labels = await session.completionLabels(probeUri, probe, '    return hidden', 17);
  assert.deepStrictEqual(labels, [],
    `a path segment spelled like a modifier must not make a file's unmarked declarations public; completion offered ${JSON.stringify(labels)}`);

  // The control: a real modifier, at the start of its own line, still works.
  const marked = 'module demo.core\n\ninternal fn marked_visible(value: Int) -> Int {\n    return value\n}\n';
  await session.open('demo/marked.kofun', marked);
  const markedLabels = await session.completionLabels(probeUri, probe, '    return hidden', 11);
  assert.ok(markedLabels.includes('marked_visible'),
    `an \`internal\` modifier in declaration position must still be honoured; completion offered ${JSON.stringify(markedLabels)}`);

  await session.stop();
}

// `internal` means one package. A neighbouring checkout with its own manifest
// is a different package, and an untitled buffer is an anonymous single-file
// package that `spec/modules/package-roots.md` defines as non-importable.
// Deriving package identity from the session rather than from the document made
// every comparison trivially equal, so `internal` restricted nothing beyond the
// same-file clause.
async function scenarioPackageIdentityIsPerDocument() {
  const root = makeWorkspace(true);
  const neighbour = makeWorkspace(true);
  const session = new Session(root);
  await session.start();

  const foreign = 'internal fn neighbour_internal(value: Int) -> Int {\n    return value\n}\n';
  const neighbourUri = `file://${neighbour}/demo/lib.kofun`;
  session.client.send({ jsonrpc: '2.0', method: 'textDocument/didOpen', params: {
    textDocument: { uri: neighbourUri, languageId: 'kofun', version: 1, text: foreign } } });
  await session.client.waitFor((m) => m.method === 'textDocument/publishDiagnostics' &&
    m.params.uri === neighbourUri && m.params.version === 1);

  const probe = 'fn probe() -> Int {\n    return neighbour\n}\n';
  const probeUri = await session.open('demo/probe.kofun', probe);
  const labels = await session.completionLabels(probeUri, probe, '    return neighbour', 20);
  assert.deepStrictEqual(labels, [],
    `a neighbouring checkout with its own manifest is a different package, so its internal API must not cross; completion offered ${JSON.stringify(labels)}`);

  // And the workspace's own internal API must not leak outward into an
  // anonymous buffer either.
  const scratch = 'fn scratch() -> Int {\n    return probe\n}\n';
  const scratchUri = 'untitled:Untitled-7';
  session.client.send({ jsonrpc: '2.0', method: 'textDocument/didOpen', params: {
    textDocument: { uri: scratchUri, languageId: 'kofun', version: 1, text: scratch } } });
  await session.client.waitFor((m) => m.method === 'textDocument/publishDiagnostics' &&
    m.params.uri === scratchUri && m.params.version === 1);
  const outward = await session.completionLabels(scratchUri, scratch, '    return probe', 16);
  assert.deepStrictEqual(outward, [],
    `an untitled buffer is a non-importable anonymous package, so the workspace's declarations must not reach it; completion offered ${JSON.stringify(outward)}`);

  await session.stop();
}

// Two checkouts of one project at different absolute paths, and one workspace
// whose files arrive in the opposite order, must answer identically. Package
// identity is semantic; `spec/modules/package-roots.md` says so directly: "A
// canonical or absolute filesystem path is not a package identity."
async function scenarioDeterminism(expected) {
  const remapped = makeWorkspace(true);
  const first = new Session(remapped);
  await first.start();
  await first.open('demo/dep.kofun', DEPENDENCY);
  const remappedCaller = await first.open('demo/caller.kofun', CALLER);
  const remappedLabels = await first.completionLabels(
    remappedCaller, CALLER, DEP_PREFIX_LINE, DEP_PREFIX_COLUMN);
  await first.stop();
  assert.deepStrictEqual(remappedLabels, expected,
    `a workspace at a different absolute path answered differently: ${JSON.stringify(remappedLabels)} vs ${JSON.stringify(expected)}`);

  const reordered = makeWorkspace(true);
  const second = new Session(reordered);
  await second.start();
  // The caller opens first this time, so the dependency is indexed after the
  // request's own document. Iterating the session's documents in didOpen order
  // would show up right here.
  const reorderedCaller = await second.open('demo/caller.kofun', CALLER);
  await second.open('demo/dep.kofun', DEPENDENCY);
  const reorderedLabels = await second.completionLabels(
    reorderedCaller, CALLER, DEP_PREFIX_LINE, DEP_PREFIX_COLUMN);
  await second.stop();
  assert.deepStrictEqual(reorderedLabels, expected,
    `opening the same files in the opposite order answered differently: ${JSON.stringify(reorderedLabels)} vs ${JSON.stringify(expected)}`);
}

(async () => {
  const baseline = await scenarioSamePackage();
  console.log('PASS visibility: inside one package, completion offers internal and public and hides private');
  await scenarioAnonymousPackage();
  console.log('PASS visibility: without a manifest every file is an anonymous package and nothing crosses');
  await scenarioNavigationDoesNotDisclose();
  console.log('PASS visibility: navigation reaches accessible declarations and refuses others indistinguishably');
  await scenarioIdentityIsNotContent();
  console.log('PASS visibility: caller identity comes from the request, not from document content');
  await scenarioCollidingDisplayPaths();
  console.log('PASS visibility: documents sharing a display path are still separate files');
  await scenarioModifierPositionIsContextual();
  console.log("PASS visibility: a path segment spelled like a modifier is not a modifier");
  await scenarioPackageIdentityIsPerDocument();
  console.log("PASS visibility: package identity is per document, so internal does not cross a checkout");
  await scenarioDeterminism(baseline);
  console.log('PASS visibility: path remap and open order produce identical results');
})().catch((error) => {
  console.error(`FAIL lsp visibility: ${error.message}`);
  process.exit(1);
});
