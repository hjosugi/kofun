#!/usr/bin/env node
// The manifest is the extension's release artifact: it is what the marketplace
// shows, what VS Code activates, and what a VSIX packages. A capability the
// server advertises but the client never registers is invisible to a user, and
// a setting the manifest offers but nothing reads is a promise the extension
// does not keep. Both are checked here rather than left to a reviewer's memory.

import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import path from "node:path";

const root = path.resolve(process.argv[2] || "editor/vscode");
const read = (name) => readFileSync(path.join(root, name), "utf8");
const manifest = JSON.parse(read("package.json"));
const extension = read("extension.js");
const server = read("server/server.js");
const changelog = read("CHANGELOG.md");

// Marketplace metadata. A missing field here is not cosmetic: without
// `repository` the marketplace page has no source link, and without `license`
// it shows the extension as unlicensed.
for (const field of ["name", "displayName", "description", "version", "publisher",
  "license", "homepage", "main", "engines", "categories", "keywords"]) {
  assert.ok(manifest[field], `package.json is missing ${field}`);
}
assert.ok(manifest.repository && manifest.repository.url,
  "package.json needs a repository URL for the marketplace source link");
assert.ok(manifest.bugs && manifest.bugs.url, "package.json needs a bugs URL");
assert.match(manifest.version, /^\d+\.\d+\.\d+$/u);
assert.ok(manifest.keywords.length >= 3, "too few marketplace keywords");
assert.ok(manifest.activationEvents.includes("onLanguage:kofun"));

// The changelog must describe the version being shipped, so a release is never
// published with notes for an older one.
assert.ok(changelog.includes(`## ${manifest.version}`),
  `CHANGELOG.md has no entry for ${manifest.version}`);

// Every capability the server advertises must have a client provider, or the
// feature exists over the wire and nowhere a user can see it.
const CAPABILITY_PROVIDERS = Object.freeze({
  definitionProvider: "registerDefinitionProvider",
  hoverProvider: "registerHoverProvider",
  completionProvider: "registerCompletionItemProvider",
  documentSymbolProvider: "registerDocumentSymbolProvider",
  referencesProvider: "registerReferenceProvider",
  documentHighlightProvider: "registerDocumentHighlightProvider",
  inlayHintProvider: "registerInlayHintsProvider",
  signatureHelpProvider: "registerSignatureHelpProvider",
  foldingRangeProvider: "registerFoldingRangeProvider",
  selectionRangeProvider: "registerSelectionRangeProvider",
  semanticTokensProvider: "registerDocumentSemanticTokensProvider",
  renameProvider: "registerRenameProvider",
});
const advertised = [...server.matchAll(/^\s{10}(\w+Provider)\s*:/gmu)]
  .map((match) => match[1]);
assert.ok(advertised.length >= 12,
  `expected the server to advertise its capabilities, found ${advertised}`);
for (const capability of advertised) {
  const register = CAPABILITY_PROVIDERS[capability];
  assert.ok(register, `${capability} is advertised but this test does not know it`);
  assert.ok(extension.includes(`vscode.languages.${register}`),
    `${capability} is advertised but the client never calls ${register}`);
}

// A capability that cannot produce a result must not be advertised at all.
// Matched as a declaration rather than as text, so the comment recording why
// it is absent does not read as the capability being present.
assert.strictEqual(/^\s+codeActionProvider\s*:/mu.test(server), false,
  "no registered diagnostic carries a remedy, so codeActionProvider must stay off");

// Every contributed command needs a handler, and every handler needs to be
// contributed — a command missing from the manifest never reaches the palette.
const contributed = manifest.contributes.commands.map((entry) => entry.command);
const handled = [...extension.matchAll(/registerCommand\('([^']+)'/gu)]
  .map((match) => match[1]);
assert.deepStrictEqual([...contributed].sort(), [...handled].sort());
for (const entry of manifest.contributes.commands) {
  assert.ok(entry.title && entry.category, `${entry.command} needs a title and category`);
}

// Every contributed setting must be read somewhere, and every setting the
// extension reads must be contributed, or it cannot be discovered.
const declared = Object.keys(manifest.contributes.configuration.properties)
  .map((key) => key.replace(/^kofun\./u, ""));
for (const setting of declared) {
  assert.ok(extension.includes(`'${setting}'`),
    `kofun.${setting} is contributed but never read`);
  const property = manifest.contributes.configuration.properties[`kofun.${setting}`];
  assert.ok(property.description, `kofun.${setting} needs a description`);
  assert.ok("default" in property, `kofun.${setting} needs a default`);
}
const read_settings = [...extension.matchAll(/configuration\.get\('([^']+)'/gu)]
  .map((match) => match[1]);
for (const setting of read_settings) {
  assert.ok(declared.includes(setting), `kofun.${setting} is read but not contributed`);
}

// Tasks are contributed with a definition, and deliberately without a problem
// matcher: the CLI reports byte offsets, so a matcher would misplace every
// problem. The comment recording that must stay with the code.
assert.ok(manifest.contributes.taskDefinitions.some((entry) => entry.type === "kofun"));
assert.strictEqual("problemMatchers" in manifest.contributes, false,
  "the CLI reports byte offsets, so a problem matcher would misplace every problem");
assert.match(extension, /byte offsets rather than line and column/u);

// Snippets and grammar are contributed and their files parse.
assert.ok(manifest.contributes.snippets.length > 0);
for (const entry of manifest.contributes.snippets) {
  const snippets = JSON.parse(read(entry.path));
  for (const [name, snippet] of Object.entries(snippets)) {
    assert.ok(snippet.prefix, `${name} needs a prefix`);
    assert.ok(Array.isArray(snippet.body), `${name} needs a body`);
    assert.ok(snippet.description, `${name} needs a description`);
  }
  // The ownership modes are the language's central idea; each gets a snippet.
  for (const mode of ["read", "edit", "take"]) {
    assert.ok(Object.values(snippets).some((snippet) => snippet.prefix === mode),
      `no snippet for the ${mode} mode`);
  }
}
JSON.parse(read(manifest.contributes.grammars[0].path));
JSON.parse(read(manifest.contributes.languages[0].configuration));
assert.deepStrictEqual(manifest.contributes.languages[0].extensions, [".kofun"]);
// `task repository-check` greps for this registration as one line. Reformatting
// package.json — a JSON pretty-printer expanding short arrays, say — keeps the
// document identical but breaks that gate with no explanation, so the shape is
// asserted here where the reason is written down.
assert.match(read("package.json"), /"extensions": \[".kofun"\]/u,
  'repository-check greps for `"extensions": [".kofun"]` on one line');

// The VSIX must carry the server it starts, and must not carry the build inputs
// used to produce it.
const ignored = read(".vscodeignore").split("\n")
  .map((line) => line.trim()).filter(Boolean);
assert.ok(ignored.includes("server/native/**"),
  "the native bridge sources must not ship in the VSIX");
assert.ok(ignored.includes("server/build-semantic-bundle.sh"),
  "the bundle build script must not ship in the VSIX");
for (const required of ["server/server.js", "server/semantic-sidecar.mjs",
  "server/semantic-worker.mjs", "extension.js", "language-configuration.json"]) {
  read(required);
  assert.strictEqual(ignored.includes(required), false,
    `${required} is needed at runtime but is excluded from the VSIX`);
}

process.stdout.write(
  `PASS: extension manifest ships ${advertised.length} advertised capabilities, ` +
  `${contributed.length} commands, and ${declared.length} documented settings, ` +
  `each wired and released as ${manifest.version}\n`
);
