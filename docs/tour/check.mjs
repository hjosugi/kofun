import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { analyzeKofun, compileKofun, KofunCompileError } from "./compiler.mjs";
import { GUIDES, STEPS } from "./content.mjs";
import { completionAt, hoverAt, VOCABULARY } from "./intelligence.mjs";
import { KofunRuntimeError, runKofun } from "./runtime.mjs";
import {
  decodeShareHash,
  encodeShareHash,
  MAX_SHARED_SOURCE_BYTES,
} from "./share.mjs";

if (process.argv.length !== 4) {
  console.error("usage: node docs/tour/check.mjs SOURCE.kofun NATIVE.wasm");
  process.exit(2);
}

const referenceSource = await readFile(process.argv[2], "utf8");
const nativeModule = await readFile(process.argv[3]);
const firstModule = compileKofun(referenceSource);
const secondModule = compileKofun(referenceSource);
assert.deepEqual(firstModule, secondModule, "browser emission must be deterministic");
assert.deepEqual(
  Buffer.from(firstModule),
  nativeModule,
  "browser compiler must match bootstrap/wasm/compiler.c byte for byte",
);
assert.equal(WebAssembly.validate(firstModule), true);

for (const step of STEPS) {
  assert.ok(step.id);
  assert.ok(step.exercise.length > 20, `${step.id} must include an exercise`);
  try {
    const result = await runKofun(step.source);
    assert.deepEqual(result.lines, step.expected, `${step.id} output`);
  } catch (error) {
    if (step.expectedError === undefined) throw error;
    assert.ok(error instanceof KofunRuntimeError);
    assert.equal(error.message, step.expectedError, `${step.id} diagnostic`);
  }
}
assert.ok(STEPS.find((step) => step.id === "ownership-bug")?.ownership);

assert.deepEqual(
  GUIDES.map((guide) => guide.id),
  ["python", "typescript", "go", "rust"],
);
for (const guide of GUIDES) {
  assert.ok(guide.transfers.length > 20);
  assert.ok(guide.surprise.length > 20);
  assert.ok(guide.worse.length > 20);
}

const sharedSource = "# مرحبا 🌱\nfn main() { print(6 * 7) }";
const hash = encodeShareHash("first-result", sharedSource);
assert.deepEqual(decodeShareHash(hash), {
  stepId: "first-result",
  source: sharedSource,
});
assert.equal(decodeShareHash(""), null);
assert.throws(() => decodeShareHash("#step=x&code=%25"), /base64url/u);
assert.throws(
  () => encodeShareHash("x", "a".repeat(MAX_SHARED_SOURCE_BYTES + 1)),
  /16 KiB/u,
);

assert.throws(
  () => compileKofun("fn main() { print(unknown) }"),
  (error) =>
    error instanceof KofunCompileError &&
    error.message ===
      "kofun wasm32: line 1: unknown binding in wasm32 Core expression",
);
await assert.rejects(
  runKofun(`fn main() {
    let maximum = 9223372036854775807
    print(maximum + 1)
  }`),
  (error) =>
    error instanceof KofunRuntimeError &&
    error.message === "error[R010]: integer overflow in operator `+`",
);
const arithmetic = await runKofun(`fn main() {
  print(7 // 2)
  print(-7 // 2)
  print(7 % -2)
  print((8 - 2) * 7 + 1)
}`);
assert.deepEqual(arithmetic.lines, ["3", "-4", "-1", "43"]);

// Hover and completion answer from the compiler's own parse, so they are
// checked against it rather than against a transcript of what the UI shows.
const editorSource = `fn main() {
    let hue = 210
    let distance = hue - 160
    print(distance)  # hue is not a name here
}
`;
const editorAnalysis = analyzeKofun(editorSource);
assert.equal(editorAnalysis.error, null);
assert.deepEqual(
  editorAnalysis.declarations.map((entry) => [entry.name, entry.type, entry.line]),
  [["hue", "Int", 2], ["distance", "Int", 3]],
);
// Every recorded span must actually name that binding in the source.
for (const declaration of editorAnalysis.declarations) {
  assert.equal(
    editorSource.slice(declaration.start, declaration.end), declaration.name);
}

const labels = (offset) =>
  completionAt(editorAnalysis, editorSource, offset).items.map((item) => item.label);
// A binding is not in scope inside its own initializer, which is where the
// compiler pushes it, so completion must not offer it to itself.
assert.equal(labels(editorSource.indexOf("210")).includes("hue"), false);
assert.deepEqual(
  labels(editorSource.indexOf("hue - 160")),
  ["hue", ...VOCABULARY.map((entry) => entry.label)],
);
// Both bindings are in scope by the last statement, in declaration order.
assert.deepEqual(
  labels(editorSource.indexOf("print(") + "print(".length),
  ["hue", "distance", ...VOCABULARY.map((entry) => entry.label)],
);
// With a name already typed, only that name survives.
assert.deepEqual(
  labels(editorSource.indexOf("distance)") + "distance".length),
  ["distance"],
);
// Typing narrows the list, case-insensitively, over both bindings and keywords.
assert.deepEqual(labels(editorSource.indexOf("print(distance") + 2), ["print"]);
// Nothing inside a comment is a reference.
assert.deepEqual(labels(editorSource.indexOf("is not a name")), []);
assert.equal(hoverAt(editorAnalysis, editorSource, editorSource.indexOf("hue is not")), null);

const hoverUse = hoverAt(editorAnalysis, editorSource, editorSource.indexOf("hue - 160"));
assert.equal(hoverUse.title, "hue: Int");
assert.equal(hoverUse.source, "compiler");
assert.match(hoverUse.body, /line 2/u);
assert.equal(
  hoverAt(editorAnalysis, editorSource, editorSource.indexOf("print")).source,
  "vocabulary",
);
// An unresolved name is reported as unknown rather than given a plausible type.
const unresolved = analyzeKofun("fn main() { print(mystery) }");
const unknown = hoverAt(unresolved, "fn main() { print(mystery) }", 20);
assert.equal(unknown.title, "mystery: unknown");
assert.equal(unknown.source, "unresolved");
// A half-typed program still yields the bindings accepted before the failure.
const partial = analyzeKofun("fn main() {\n    let ready = 1\n    let broken = \n}");
assert.notEqual(partial.error, null);
assert.deepEqual(partial.declarations.map((entry) => entry.name), ["ready"]);

console.log("PASS: browser compiler matched the native wasm32 seed byte for byte");
console.log("PASS: every editable tour step ran with deterministic observations");
console.log("PASS: URL snippets round-tripped UTF-8 without a server");
console.log("PASS: ownership and four candid coming-from guides are present");
console.log(
  "PASS: hover and completion answered from the compiler's own parse, in scope",
);
