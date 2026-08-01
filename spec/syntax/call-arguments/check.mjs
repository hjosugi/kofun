import assert from "node:assert/strict";
import fs from "node:fs";

import {
  CallModelError,
  assertNoRuntimeLabels,
  bindCall,
  classifySurface,
  interfaceFingerprint,
} from "./model.mjs";

const corpus = JSON.parse(fs.readFileSync(new URL("./corpus.json", import.meta.url), "utf8"));
assert.equal(corpus.schema, "kofun.call-arguments-corpus/v1");

const replace = {
  name: "replace",
  result: "Text",
  parameters: [
    { external: "in", internal: "text", mode: "read", type: "Text" },
    { external: "from", internal: "old", mode: "read", type: "Text" },
    { external: "to", internal: "replacement", mode: "read", type: "Text" },
  ],
};

const reordered = bindCall(replace, {
  arguments: [
    { label: "to", expression: "effect-c" },
    { label: "in", expression: "effect-a" },
    { label: "from", expression: "effect-b" },
  ],
});
assert.deepEqual(reordered.evaluation, ["effect-c", "effect-a", "effect-b"]);
assert.deepEqual(reordered.abi.map((entry) => entry.expression),
  ["effect-a", "effect-b", "effect-c"]);
assert.equal(assertNoRuntimeLabels(reordered.lowering), true);
assert.deepEqual([...new Set(reordered.lowering.map((item) => item.op))].sort(),
  ["call", "eval-temp", "store-slot"]);
const repeatedExpression = bindCall(replace, { arguments: [
  { label: "in", expression: "same" },
  { label: "from", expression: "same" },
  { label: "to", expression: "same" },
] });
assert.deepEqual(repeatedExpression.lowering.filter((item) => item.op === "store-slot")
  .map((item) => item.temp), [0, 1, 2]);

const fold = {
  name: "fold",
  result: "Int",
  parameters: [
    { internal: "values", mode: "read", type: "List[Int]" },
    { external: "initial", internal: "initial", mode: "read", type: "Int" },
    { internal: "combine", mode: "read", type: "Function" },
  ],
};
const piped = bindCall(fold, {
  pipeline: "values-effect",
  arguments: [{ label: "initial", expression: "initial-effect" }],
  trailingLambda: "lambda-effect",
});
assert.deepEqual(piped.evaluation,
  ["values-effect", "initial-effect", "lambda-effect"]);
assert.deepEqual(piped.abi.map((entry) => entry.expression),
  ["values-effect", "initial-effect", "lambda-effect"]);

function rejected(code, signature, call) {
  assert.throws(() => bindCall(signature, call), (error) =>
    error instanceof CallModelError && error.code === code);
}

rejected("CALL01", replace, { arguments: [
  { label: "inside", expression: "a" },
  { label: "from", expression: "b" },
  { label: "to", expression: "c" },
] });
rejected("CALL02", replace, { arguments: [
  { label: "in", expression: "a" },
  { label: "in", expression: "b" },
  { label: "from", expression: "c" },
  { label: "to", expression: "d" },
] });
rejected("CALL03", replace, { arguments: [
  { label: "in", expression: "a" },
  { label: "from", expression: "b" },
] });
rejected("CALL05", fold, { arguments: [
  { label: "initial", expression: "zero" },
  { label: null, expression: "values" },
  { label: null, expression: "lambda" },
] });
rejected("CALL07", replace, { arguments: [
  { label: null, expression: "a" },
  { label: "from", expression: "b" },
  { label: "to", expression: "c" },
] });
rejected("CALL08", replace, { arguments: [
  { label: "in", expression: "a" },
  { label: "from", expression: "b" },
  { label: "to", expression: "c" },
], trailingLambda: "lambda" });
const labelledFold = structuredClone(fold);
labelledFold.parameters[2].external = "combine";
rejected("CALL09", labelledFold, { pipeline: "values", arguments: [
  { label: "initial", expression: "zero" },
  { label: "combine", expression: "lambda-inside" },
], trailingLambda: "lambda-outside" });

for (const test of corpus.surface) {
  assert.equal(classifySurface(test.source).kind, test.kind, test.name);
}

const internalRename = structuredClone(replace);
internalRename.parameters[0].internal = "input";
assert.equal(interfaceFingerprint(internalRename), interfaceFingerprint(replace));
const externalRename = structuredClone(replace);
externalRename.parameters[0].external = "inside";
assert.notEqual(interfaceFingerprint(externalRename), interfaceFingerprint(replace));
const effectChange = structuredClone(replace);
effectChange.effects = ["io"];
assert.notEqual(interfaceFingerprint(effectChange), interfaceFingerprint(replace));

assert.ok(corpus.usability.some((item) => item.material && item.call.includes("from:")));
assert.ok(corpus.usability.some((item) => !item.material && item.call === item.ordinary));
assert.ok(corpus.usability.every((item) => item.reason.length >= 20));

const doc = fs.readFileSync(new URL("../call-arguments-v1.md", import.meta.url), "utf8");
for (const required of [
  "Default arguments are rejected in v1",
  "Labels MUST NOT participate in overload selection",
  "Every explicit expression evaluates exactly once",
  "allocate a dictionary",
  "parser plus canonical formatter",
  "HIR/type checking",
  "pipeline/trailing lowering plus C11/direct-native differential",
]) assert.ok(doc.includes(required), required);

console.log("call-arguments: grammar and one canonical trailing-lambda spelling: PASS");
console.log("call-arguments: labels bind statically; source evaluation and ABI order stay distinct: PASS");
console.log("call-arguments: ambiguity, diagnostics, pipeline, fingerprint, and no-map lowering: PASS");
