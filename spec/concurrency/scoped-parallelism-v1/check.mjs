#!/usr/bin/env node

import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

import {
  MODEL_SCHEMA,
  RESULT_SCHEMA,
  analyzeScopedParallelism,
} from "./model.mjs";

const ROOT = dirname(fileURLToPath(import.meta.url));
const MODEL = join(ROOT, "model.mjs");

const CASES = Object.freeze([
  ["positive/shared-read.json", "accepted", "success", []],
  ["positive/distinct-fields.json", "accepted", "success", []],
  ["positive/disjoint-slices.json", "accepted", "success", []],
  ["positive/result-join.json", "accepted", "success", []],
  ["positive/scope-exit-unused.json", "accepted", "success", []],
  ["positive/panic-drain.json", "accepted", "panicked", []],
  ["positive/cancellation-drain.json", "accepted", "cancelled", []],
  ["positive/panic-over-cancellation.json", "accepted", "panicked", []],
  ["negative/read-edit.json", "rejected", "not-run", ["SPV1-CAPTURE-CONFLICT"]],
  ["negative/edit-edit.json", "rejected", "not-run", ["SPV1-CAPTURE-CONFLICT"]],
  ["negative/concurrent-take-read.json", "rejected", "not-run", ["SPV1-CAPTURE-CONFLICT"]],
  ["negative/overlapping-slices.json", "rejected", "not-run", ["SPV1-CAPTURE-CONFLICT"]],
  ["negative/unknown-slices.json", "rejected", "not-run", ["SPV1-OVERLAP-UNKNOWN"]],
  ["negative/parent-edit-live.json", "rejected", "not-run", ["SPV1-PARENT-CONFLICT"]],
  ["negative/take-parent-use.json", "rejected", "not-run", ["SPV1-USE-AFTER-TAKE"]],
  ["negative/sequential-reuse-after-take.json", "rejected", "not-run", ["SPV1-USE-AFTER-TAKE"]],
  ["negative/sequential-unknown-after-take.json", "rejected", "not-run", ["SPV1-OVERLAP-UNKNOWN"]],
  ["negative/handle-return.json", "rejected", "not-run", ["SPV1-HANDLE-ESCAPE"]],
  ["negative/join-at-scope-exit.json", "rejected", "not-run", ["SPV1-INVALID-MODEL"]],
  ["negative/parent-at-scope-exit.json", "rejected", "not-run", ["SPV1-INVALID-MODEL"]],
]);

function load(relative) {
  return JSON.parse(readFileSync(join(ROOT, "fixtures", relative), "utf8"));
}

function bytes(value) {
  return `${JSON.stringify(value, null, 2)}\n`;
}

let assertions = 0;

for (const [relative, status, outcome, codes] of CASES) {
  const input = load(relative);
  const first = analyzeScopedParallelism(input);
  const second = analyzeScopedParallelism(input);
  assert.equal(first.schema, RESULT_SCHEMA, `${relative}: result schema`);
  assert.equal(first.status, status, `${relative}: status`);
  assert.equal(first.scope_outcome, outcome, `${relative}: scope outcome`);
  assert.deepEqual(first.diagnostics.map(({ code }) => code), codes, `${relative}: diagnostics`);
  assert.equal(bytes(first), bytes(second), `${relative}: repeated byte identity`);
  assert.equal(first.guarantees.data_race_freedom, true, `${relative}: data-race promise`);
  assert.equal(first.guarantees.race_condition_freedom, false, `${relative}: race-condition boundary`);
  assert.equal(first.guarantees.runtime_schedule_modeled, false, `${relative}: schedule boundary`);
  assert.equal(bytes(first).includes(ROOT), false, `${relative}: no absolute path`);
  assertions += 9;
}

const shared = load("positive/shared-read.json");
const reordered = structuredClone(shared);
reordered.scope.tasks.reverse();
assert.equal(
  bytes(analyzeScopedParallelism(reordered)),
  bytes(analyzeScopedParallelism(shared)),
  "lexically identified tasks are canonical despite declaration-array order",
);
assertions += 1;

const resultJoin = analyzeScopedParallelism(load("positive/result-join.json"));
assert.deepEqual(resultJoin.joins, [{
  task: "compute",
  kind: "explicit",
  step: 3,
  outcome: "success",
  result: "42",
  discarded: false,
}], "explicit join yields its result and ends the edit capture");
assertions += 1;

for (const relative of [
  "negative/join-at-scope-exit.json",
  "negative/parent-at-scope-exit.json",
]) {
  const invalid = analyzeScopedParallelism(load(relative));
  assert.equal(invalid.status, "rejected", `${relative}: exit step is reserved`);
  assert.deepEqual(invalid.diagnostics.map(({ code }) => code), ["SPV1-INVALID-MODEL"]);
  assert.match(invalid.diagnostics[0].detail, /strictly before scope exit/);
  assertions += 3;
}

const sequentialReuse = analyzeScopedParallelism(load("negative/sequential-reuse-after-take.json"));
assert.deepEqual(sequentialReuse.diagnostics.map(({ code }) => code), ["SPV1-USE-AFTER-TAKE"]);
assert.match(sequentialReuse.diagnostics[0].detail, /unavailable after take by task owner/);
assertions += 2;

const sequentialUnknown = analyzeScopedParallelism(load("negative/sequential-unknown-after-take.json"));
assert.deepEqual(sequentialUnknown.diagnostics.map(({ code }) => code), ["SPV1-OVERLAP-UNKNOWN"]);
assert.match(sequentialUnknown.diagnostics[0].detail, /cannot prove .* disjoint from place taken by task owner/);
assertions += 2;

const implicitJoin = analyzeScopedParallelism(load("positive/scope-exit-unused.json"));
assert.equal(implicitJoin.joins[0].kind, "scope-exit", "unused handle joins at scope exit");
assert.equal(implicitJoin.joins[0].discarded, true, "implicit join discards the result");
assert.equal(implicitJoin.joins[0].result, null, "discarded result is not exposed");
assertions += 3;

const panic = analyzeScopedParallelism(load("positive/panic-drain.json"));
assert.deepEqual(panic.primary_failure, { kind: "panic", task: "first" });
assert.equal(panic.joins.length, 2, "panic drains every task");
assert.equal(panic.joins.every(({ kind }) => kind === "scope-exit"), true);
assertions += 3;

const cancellation = analyzeScopedParallelism(load("positive/cancellation-drain.json"));
assert.deepEqual(cancellation.primary_failure, { kind: "cancellation", task: null });
assert.equal(cancellation.joins.length, 2, "cancellation drains every task");
assertions += 2;

const precedence = analyzeScopedParallelism(load("positive/panic-over-cancellation.json"));
assert.deepEqual(precedence.primary_failure, { kind: "panic", task: "panicked" });
assert.equal(precedence.joins.length, 3, "panic precedence still drains all handles");
assertions += 2;

const emptySlice = {
  schema: MODEL_SCHEMA,
  scope: {
    exit_step: 5,
    tasks: [
      { id: "empty", spawn_step: 1, captures: [{ mode: "edit", place: { base: "items", path: [{ slice: [4, 4] }] } }] },
      { id: "whole", spawn_step: 2, captures: [{ mode: "edit", place: { base: "items", path: [{ slice: [0, 8] }] } }] },
    ],
  },
};
assert.equal(analyzeScopedParallelism(emptySlice).status, "accepted", "empty slice is disjoint");
assertions += 1;

const readUnknown = {
  schema: MODEL_SCHEMA,
  scope: {
    exit_step: 5,
    tasks: [
      { id: "left", spawn_step: 1, captures: [{ mode: "read", place: { base: "items", path: [{ slice: ["lo", "split"] }] } }] },
      { id: "right", spawn_step: 2, captures: [{ mode: "read", place: { base: "items", path: [{ slice: ["split", "hi"] }] } }] },
    ],
  },
};
assert.equal(analyzeScopedParallelism(readUnknown).status, "accepted", "unknown read/read overlap is safe");
assertions += 1;

const overLimit = {
  schema: MODEL_SCHEMA,
  scope: {
    exit_step: 5,
    tasks: Array.from({ length: 65 }, (_, index) => ({ id: `task-${index}`, spawn_step: 1, captures: [] })),
  },
};
assert.deepEqual(
  analyzeScopedParallelism(overLimit).diagnostics.map(({ code }) => code),
  ["SPV1-INVALID-MODEL"],
  "task limit fails closed",
);
assertions += 1;

const cliFixture = join(ROOT, "fixtures", "positive", "shared-read.json");
const cli = spawnSync(process.execPath, [MODEL, cliFixture], { encoding: "utf8" });
assert.equal(cli.status, 0, cli.stderr);
assert.equal(cli.stderr, "");
assert.equal(cli.stdout, bytes(analyzeScopedParallelism(load("positive/shared-read.json"))));
assertions += 3;

const anchorNames = new Set(JSON.parse(cli.stdout).semantic_anchors.map(({ event }) => event));
assert.deepEqual(anchorNames, new Set([
  "scope.enter", "task.spawn", "task.join.explicit", "scope.exit",
]));
for (const forbidden of ["task.start", "task.wake", "task.yield", "worker.select", "clock"] ) {
  assert.equal(anchorNames.has(forbidden), false, `schedule event ${forbidden} must remain #736-owned`);
  assertions += 1;
}
assertions += 1;

process.stdout.write(`PASS: scoped parallelism v1 model (${CASES.length} fixtures, ${assertions} assertions)\n`);
