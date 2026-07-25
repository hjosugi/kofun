import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";

import { readTypedSidecar } from "../../tooling/typed-sidecar/codec.mjs";
import {
  emitStage2TypedSidecar,
  projectStage2SemanticEvents,
  readStage2SemanticEvents,
} from "../../tooling/typed-sidecar/from-stage2.mjs";

const work = process.argv[2];
const source = process.argv[3];
const failedSource = process.argv[4];
if (!work || !source || !failedSource) {
  throw new Error("usage: producer_races_test.mjs WORK SOURCE FAILED_SOURCE");
}

const sourceBytes = fs.readFileSync(source);
const failedSourceBytes = fs.readFileSync(failedSource);
const eventBytes = (name) => fs.readFileSync(path.join(work, `${name}.kse`));
const output = path.join(work, "winner.kofun-semantic.json");
const emit = (name, destination = output, currentSourceBytes = sourceBytes) =>
  emitStage2TypedSidecar(eventBytes(name), destination, { currentSourceBytes });

let result = await emit("generation-1");
assert.equal(result.ok, true, result.error?.message);
let winner = fs.readFileSync(output);
assert.equal(readTypedSidecar(winner).document.generation.sequence, 1);

result = await emit("generation-1");
assert.equal(result.ok, false);
assert.equal(result.error.code, "ETS05");
assert.equal(result.error.reason, "stale-sequence");
assert.deepEqual(fs.readFileSync(output), winner);

result = await emit("generation-2");
assert.equal(result.ok, true, result.error?.message);
winner = fs.readFileSync(output);
assert.equal(readTypedSidecar(winner).document.generation.sequence, 2);

const partialOutput = path.join(work, "partial-winner.kofun-semantic.json");
result = await emit("generation-1", partialOutput);
assert.equal(result.ok, true);
result = await emit("partial-generation-4", partialOutput, failedSourceBytes);
assert.equal(result.ok, true, result.error?.message);
const partialWinner = readTypedSidecar(fs.readFileSync(partialOutput)).document;
assert.equal(partialWinner.generation.sequence, 4);
assert.equal(partialWinner.completeness, "partial");
assert.equal(partialWinner.source_status, "failed");

const cancelledOutput = path.join(work, "cancelled.kofun-semantic.json");
result = await emit("cancelled-generation-5", cancelledOutput);
assert.equal(result.ok, true, result.error?.message);
assert.equal(
  readTypedSidecar(fs.readFileSync(cancelledOutput)).document.source_status,
  "cancelled",
);

result = await emit("wrong-file");
assert.equal(result.ok, false);
assert.equal(result.error.code, "ETS05");
assert.equal(result.error.reason, "wrong-file");
assert.deepEqual(fs.readFileSync(output), winner);

result = await emit(
  "generation-3",
  output,
  Buffer.concat([sourceBytes, Buffer.from("\n")]),
);
assert.equal(result.ok, false);
assert.equal(result.error.code, "ETS05");
assert.equal(result.error.reason, "source-mismatch");
assert.deepEqual(fs.readFileSync(output), winner);

const raceOutput = path.join(work, "race.kofun-semantic.json");
result = await emit("generation-1", raceOutput);
assert.equal(result.ok, true);
const contenders = await Promise.all([
  emit("generation-3", raceOutput),
  emit("generation-2", raceOutput),
]);
assert.equal(contenders.filter((item) => item.ok).length, 1);
assert.equal(contenders.filter((item) =>
  !item.ok && item.error.code === "ETS05" &&
  item.error.reason === "stale-sequence").length, 1);
assert.equal(
  readTypedSidecar(fs.readFileSync(raceOutput)).document.generation.sequence,
  3,
);

const busyOutput = path.join(work, "busy.kofun-semantic.json");
result = await emit("generation-1", busyOutput);
assert.equal(result.ok, true);
const busyBefore = fs.readFileSync(busyOutput);
fs.writeFileSync(path.join(work, ".busy.kofun-semantic.json.typed-sidecar.lock"), "busy");
result = await emit("generation-2", busyOutput);
assert.equal(result.ok, false);
assert.equal(result.error.code, "ETS06");
assert.deepEqual(fs.readFileSync(busyOutput), busyBefore);
fs.unlinkSync(path.join(work, ".busy.kofun-semantic.json.typed-sidecar.lock"));

const projected = projectStage2SemanticEvents(
  readStage2SemanticEvents(eventBytes("generation-3")).events,
);
assert.equal(projected.ok, true);
const encoded = JSON.stringify(projected.document);
assert.equal(encoded.includes(work), false);
assert.equal(encoded.includes(process.cwd()), false);
assert.equal(encoded.includes("semantic-events.kse"), false);

const leftovers = fs.readdirSync(work)
  .filter((name) => name.includes(".tmp-") ||
    name.endsWith(".typed-sidecar.lock"));
assert.deepEqual(leftovers, []);

console.log("PASS: Stage 2 sidecar stale/equal/wrong-file/race writers preserve the winner");
