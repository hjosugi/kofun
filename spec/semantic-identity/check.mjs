import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";

import { independentCanonical } from "./independent-encoder.mjs";
import {
  IdentityError,
  analyzeProgram,
  canonical,
  hashDomain,
  insertContentObject,
  makeCacheEntry,
  makeCacheKey,
  readCacheEntry,
  structuralDiff,
} from "./model.mjs";

const work = process.argv[2];
if (!work) throw new Error("usage: check.mjs WORK");
fs.mkdirSync(work, { recursive: true });

const lit = (value, type = "Int") => ({ kind: "lit", type, value });
const variable = (name, type = "Int") => ({ kind: "var", name, type });
const binary = (op, left, right, type = "Int") => ({ kind: "binary", left, op, right, type });

function baseProgram() {
  return {
    compilerSemantics: "bootstrap-0.3",
    definitions: [{
      body: binary("+", variable("value"), lit(1)),
      displayName: "increment",
      effects: [],
      key: "increment-key",
      parameters: [{ mode: "read", name: "value", type: "Int" }],
      result: "Int",
      span: { end: 42, start: 10 },
      symbol: "demo/increment",
    }],
    edition: "kofun-2026",
    numericPolicy: "int64-checked-v1",
    schema: "kofun.semantic-core-model/v1",
    unicodeVersion: "17.0.0",
  };
}

const baseline = baseProgram();
const baselineResult = analyzeProgram(baseline);
assert.equal(canonical(baselineResult.canonical), independentCanonical(baselineResult.canonical));
assert.equal(hashDomain("kofun.semantic-program/v1", baselineResult.canonical), baselineResult.identity);

for (const mutateInvariant of [
  (program) => { program.definitions[0].parameters[0].name = "renamed"; program.definitions[0].body.left.name = "renamed"; },
  (program) => { program.definitions[0].span = { end: 900, start: 800 }; program.definitions[0].comments = ["hello"]; },
  (program) => { program.definitions[0].displayName = "plus_one"; },
]) {
  const changed = structuredClone(baseline);
  mutateInvariant(changed);
  assert.equal(analyzeProgram(changed).identity, baselineResult.identity);
}
const unknownSemanticField = structuredClone(baseline);
unknownSemanticField.definitions[0].body.rounding = "new-semantics";
assert.throws(() => analyzeProgram(unknownSemanticField),
  (error) => error instanceof IdentityError && error.code === "SID13");

const mutations = {
  literal: (program) => { program.definitions[0].body.right.value = 2; },
  operator: (program) => { program.definitions[0].body.op = "*"; },
  control: (program) => { program.definitions[0].body = { condition: lit(true, "Bool"), else: lit(0), kind: "if", then: program.definitions[0].body, type: "Int" }; },
  type: (program) => { program.definitions[0].parameters[0].type = "Nat"; },
  effect: (program) => { program.definitions[0].effects = ["io"]; },
  ownership: (program) => { program.definitions[0].parameters[0].mode = "take"; },
  dependency: (program) => { program.definitions[0].body = { kind: "external", semanticId: "a".repeat(64), type: "Int" }; },
  edition: (program) => { program.edition = "kofun-2027"; },
  compiler: (program) => { program.compilerSemantics = "bootstrap-0.4"; },
  numeric: (program) => { program.numericPolicy = "int64-wrapping-v1"; },
  unicode: (program) => { program.unicodeVersion = "18.0.0"; },
};
for (const [name, mutate] of Object.entries(mutations)) {
  const changed = structuredClone(baseline);
  mutate(changed);
  assert.notEqual(analyzeProgram(changed).identity, baselineResult.identity, name);
}

function independentProgram(order = ["left", "right"]) {
  const definitions = {
    left: { body: lit(1), displayName: "left", effects: [], key: "left", parameters: [], result: "Int", symbol: "demo/left" },
    right: { body: lit(2), displayName: "right", effects: [], key: "right", parameters: [], result: "Int", symbol: "demo/right" },
  };
  const program = baseProgram();
  program.definitions = order.map((name) => definitions[name]);
  return program;
}
assert.equal(analyzeProgram(independentProgram()).identity,
  analyzeProgram(independentProgram(["right", "left"])).identity);

function recursiveProgram(order, symmetric = false) {
  const defs = {
    even: {
      body: { args: [variable("n")], kind: "call", target: "odd", type: "Bool" },
      displayName: "even", effects: [], key: "even",
      parameters: [{ mode: "read", name: "n", type: "Int" }], result: "Bool", symbol: "demo/even",
    },
    odd: {
      body: { args: [variable("n")], kind: "call", target: "even", type: "Bool" },
      displayName: "odd", effects: [], key: "odd",
      parameters: [{ mode: "read", name: "n", type: "Int" }], result: "Bool", symbol: "demo/odd",
    },
  };
  if (!symmetric) defs.even.effects = ["pure-marker"];
  const program = baseProgram();
  program.definitions = order.map((name) => defs[name]);
  return program;
}
const mutualA = analyzeProgram(recursiveProgram(["even", "odd"]));
const mutualB = analyzeProgram(recursiveProgram(["odd", "even"]));
assert.equal(mutualA.identity, mutualB.identity);
assert.equal(mutualA.components[0].ambiguous, false);
const symmetricA = analyzeProgram(recursiveProgram(["even", "odd"], true));
const symmetricB = analyzeProgram(recursiveProgram(["odd", "even"], true));
assert.equal(symmetricA.identity, symmetricB.identity);
assert.equal(symmetricA.components[0].ambiguous, true);
assert.deepEqual(Object.values(symmetricA.membersBySymbol), [null, null]);
const ambiguousExport = recursiveProgram(["even", "odd"], true);
ambiguousExport.definitions.push({
  body: { args: [lit(1)], kind: "call", target: "even", type: "Bool" },
  displayName: "outside", effects: [], key: "outside", parameters: [],
  result: "Bool", symbol: "demo/outside",
});
assert.throws(() => analyzeProgram(ambiguousExport),
  (error) => error instanceof IdentityError && error.code === "SID09");

const self = baseProgram();
self.definitions[0].body = { args: [variable("value")], kind: "call", target: "increment-key", type: "Int" };
assert.match(analyzeProgram(self).identity, /^[0-9a-f]{64}$/);

const helperProgram = independentProgram();
helperProgram.definitions[1].body = { args: [], kind: "call", target: "left", type: "Int" };
const helperResult = analyzeProgram(helperProgram);
assert.equal(helperResult.components.length, 2);

const computedVectors = {
  baseline: baselineResult.identity,
  crossScc: helperResult.identity,
  mutualRecursive: mutualA.identity,
  selfRecursive: analyzeProgram(self).identity,
  symmetricComponent: symmetricA.identity,
};
const recordedVectors = JSON.parse(fs.readFileSync(new URL("./vectors.json", import.meta.url), "utf8"));
assert.equal(recordedVectors.schema, "kofun.semantic-identity-vectors/v1");
const { schema: _vectorSchema, ...recordedHashes } = recordedVectors;
assert.deepEqual(computedVectors, recordedHashes);
if (process.env.KOFUN_EMIT_SEMANTIC_VECTORS === "1") {
  console.log(JSON.stringify(computedVectors));
}

assert.throws(() => analyzeProgram({ ...baseline, schema: "future/v2" }),
  (error) => error instanceof IdentityError && error.code === "SID08");
const tooDeep = baseProgram();
let expression = lit(0);
for (let index = 0; index < 140; index += 1) {
  expression = { body: expression, kind: "let", name: `v${index}`, type: "Int", value: lit(index) };
}
tooDeep.definitions[0].body = expression;
assert.throws(() => analyzeProgram(tooDeep),
  (error) => error instanceof IdentityError && error.code === "SID05");

const keyInputs = {
  compilerSemantics: baseline.compilerSemantics,
  configDigest: "b".repeat(64),
  dependencyIdentities: ["d".repeat(64), "c".repeat(64)],
  edition: baseline.edition,
  environmentDigest: "e".repeat(64),
  targetSemantics: "portable-int64-v1",
  termIdentity: baselineResult.identity,
  testIdentity: "test/demo/increment",
};
const cacheKey = makeCacheKey(keyInputs);
const observation = { exitCategory: "success", ownershipEvent: "none", stderr: "", stdout: "42\n" };
const entry = makeCacheEntry(cacheKey, observation);
assert.deepEqual(readCacheEntry(entry, cacheKey), { hit: true, observation });
assert.deepEqual(readCacheEntry(entry, "0".repeat(64)), { hit: false, reason: "stale-key" });
const corrupt = structuredClone(entry);
corrupt.observation.stdout = "wrong\n";
assert.throws(() => readCacheEntry(corrupt, cacheKey),
  (error) => error instanceof IdentityError && error.code === "CACHE05");
const future = structuredClone(entry);
future.schema = "kofun.test-cache-entry/v2";
assert.throws(() => readCacheEntry(future, cacheKey),
  (error) => error instanceof IdentityError && error.code === "CACHE04");
assert.throws(() => makeCacheEntry(cacheKey, { ...observation, command: "run" }),
  (error) => error instanceof IdentityError && error.code === "CACHE03");
const contentStore = new Map();
insertContentObject(contentStore, baselineResult.identity, canonical(baselineResult.canonical));
assert.throws(() => insertContentObject(contentStore, baselineResult.identity, "different canonical bytes"),
  (error) => error instanceof IdentityError && error.code === "SID15");

const renamed = structuredClone(baseline);
renamed.definitions[0].displayName = "plus_one";
renamed.definitions[0].parameters[0].name = "input";
renamed.definitions[0].body.left.name = "input";
renamed.definitions[0].span = { end: 120, start: 90 };
assert.deepEqual(structuralDiff(baseline, renamed), [{
  kind: "source-rename", span: { end: 120, start: 90 }, symbol: "demo/increment",
}]);
const changedBody = structuredClone(baseline);
changedBody.definitions[0].body.right.value = 2;
changedBody.definitions[0].span = { end: 77, start: 50 };
assert.deepEqual(structuralDiff(baseline, changedBody), [{
  categories: ["body"], kind: "modified", span: { end: 77, start: 50 }, symbol: "demo/increment",
}]);

const workload = [];
for (let index = 0; index < 256; index += 1) {
  const program = baseProgram();
  program.definitions[0].symbol = `bench/f${index}`;
  program.definitions[0].key = `f${index}`;
  program.definitions[0].body.right.value = index;
  workload.push(program);
}
const coldStart = process.hrtime.bigint();
const cached = workload.map((program, index) => {
  const result = analyzeProgram(program);
  const key = makeCacheKey({ ...keyInputs, termIdentity: result.identity, testIdentity: `bench/${index}` });
  return { entry: makeCacheEntry(key, observation), key, result };
});
const coldNs = Number(process.hrtime.bigint() - coldStart);
const hitStart = process.hrtime.bigint();
let hits = 0;
for (const item of cached) if (readCacheEntry(item.entry, item.key).hit) hits += 1;
const hitNs = Number(process.hrtime.bigint() - hitStart);
const measurements = {
  canonicalBytes: cached.reduce((sum, item) => sum + Buffer.byteLength(canonical(item.result.canonical)), 0),
  coldNs,
  corpus: workload.length,
  hitNs,
  hits,
  model: "kofun.semantic-identity-measurement/v1",
  ratio: coldNs / Math.max(hitNs, 1),
};
fs.writeFileSync(path.join(work, "measurements.json"), `${JSON.stringify(measurements, null, 2)}\n`);
assert.equal(hits, workload.length);
assert.ok(measurements.canonicalBytes > 10000);

console.log("semantic-identity: invariant, mutation, independent-order, and recursive vectors: PASS");
console.log("semantic-identity: cache corruption/staleness/authority and structural diff: PASS");
console.log(`semantic-identity: measured ${hits} hits; cold=${coldNs}ns hit=${hitNs}ns ratio=${measurements.ratio.toFixed(2)}x`);
