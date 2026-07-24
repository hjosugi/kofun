import assert from "node:assert/strict";

import {
  PLAYGROUND_EXAMPLES,
  runKofun,
} from "../app/kofun-runtime.ts";

const expectedOutput = new Map([
  ["pipeline", "56"],
  ["branches", "good"],
  ["science", "0.375\n1.5625"],
  ["fold", "720"],
]);

for (const example of PLAYGROUND_EXAMPLES) {
  const result = runKofun(example.source);
  assert.equal(result.error, undefined, `${example.id}: ${result.error?.message}`);
  assert.equal(result.output, expectedOutput.get(example.id), example.id);
  assert.ok(result.tokenCount > 0, `${example.id}: token count`);
  assert.ok(result.steps > 0, `${example.id}: step count`);
}

const mutable = runKofun(`fn main() {
    let mut answer = 40
    answer = answer + 2
    print(answer)
}`);
assert.equal(mutable.error, undefined);
assert.equal(mutable.output, "42");

const unicode = runKofun(`fn main() {
    let world = "古墳🌍"
    print(world[2])
    print(len(world))
}`);
assert.equal(unicode.error, undefined);
assert.equal(unicode.output, "🌍\n3");

const immutable = runKofun(`fn main() {
    let answer = 40
    answer = 42
}`);
assert.equal(immutable.error?.code, "R002");
assert.equal(immutable.error?.line, 3);

const syntax = runKofun(`fn main() {
    print("unterminated)
}`);
assert.equal(syntax.error?.code, "P001");
assert.equal(syntax.error?.line, 2);

const bounded = runKofun(`fn main() {
    print(len(0 .. 10001))
}`);
assert.equal(bounded.error?.code, "R008");

console.log(
  `PASS: ${PLAYGROUND_EXAMPLES.length} examples and browser runtime diagnostics`,
);
