import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

import {
  readTypedSidecar,
  writeTypedSidecarAtomic,
} from "../../tooling/typed-sidecar/codec.mjs";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");
const EXAMPLES = path.join(ROOT, "spec/typed-sidecar/examples");
const work = process.argv[2];
if (!work) throw new Error("usage: atomic_write_test.mjs WORK_DIRECTORY");

function input(name) {
  return fs.readFileSync(path.join(EXAMPLES, `${name}.json`));
}

function document(name) {
  const result = readTypedSidecar(input(name));
  assert.equal(result.ok, true);
  return result.document;
}

function cloneWithSequence(documentValue, sequence) {
  const clone = structuredClone(documentValue);
  clone.generation.sequence = sequence;
  return clone;
}

async function write(destination, doc, digest, signal) {
  return writeTypedSidecarAtomic(destination, doc, {
    currentSourceDigest: digest,
    signal,
  });
}

const complete = document("complete");
const partial = document("partial");
const destination = path.join(work, "main.kofun-semantic.json");

let result = await write(destination, complete, complete.file.content_sha256);
assert.deepEqual(result, {
  ok: true,
  bytes: input("complete").length,
  sequence: complete.generation.sequence,
});
assert.deepEqual(fs.readFileSync(destination), input("complete"));
assert.equal(fs.statSync(destination).mode & 0o777, 0o600);

result = await write(destination, partial, partial.file.content_sha256);
assert.equal(result.ok, true);
assert.deepEqual(fs.readFileSync(destination), input("partial"));

const winnerBytes = fs.readFileSync(destination);
for (const [label, doc, digest, reason] of [
  ["stale", complete, complete.file.content_sha256, "stale-sequence"],
  ["equal", partial, partial.file.content_sha256, "stale-sequence"],
  ["wrong-source", cloneWithSequence(partial, 9), complete.file.content_sha256, "source-mismatch"],
]) {
  result = await write(destination, doc, digest);
  assert.equal(result.ok, false, label);
  assert.equal(result.error.code, "TS005", label);
  assert.equal(result.error.reason, reason, label);
  assert.deepEqual(fs.readFileSync(destination), winnerBytes, label);
}

const wrongFile = cloneWithSequence(complete, 9);
wrongFile.file.file_id = "0000000000000000000000000000000000000000000000000000000000000000";
result = await write(destination, wrongFile, wrongFile.file.content_sha256);
assert.equal(result.ok, false);
assert.equal(result.error.reason, "wrong-file");
assert.deepEqual(fs.readFileSync(destination), winnerBytes);

const controller = new AbortController();
controller.abort();
result = await write(destination, cloneWithSequence(partial, 9), partial.file.content_sha256, controller.signal);
assert.equal(result.ok, false);
assert.equal(result.error.code, "TS006");
assert.deepEqual(fs.readFileSync(destination), winnerBytes);

const cancelledNew = path.join(work, "cancelled-new.kofun-semantic.json");
result = await write(cancelledNew, complete, complete.file.content_sha256, controller.signal);
assert.equal(result.ok, false);
assert.equal(result.error.code, "TS006");
assert.equal(fs.existsSync(cancelledNew), false);

const lockPath = path.join(work, ".main.kofun-semantic.json.typed-sidecar.lock");
fs.writeFileSync(lockPath, "occupied", { mode: 0o600 });
result = await write(destination, cloneWithSequence(partial, 9), partial.file.content_sha256);
assert.equal(result.ok, false);
assert.equal(result.error.code, "TS006");
assert.deepEqual(fs.readFileSync(destination), winnerBytes);
fs.unlinkSync(lockPath);

const victim = path.join(work, "victim.txt");
const symlink = path.join(work, "symlink.kofun-semantic.json");
fs.writeFileSync(victim, "keep");
fs.symlinkSync(victim, symlink);
result = await write(symlink, complete, complete.file.content_sha256);
assert.equal(result.ok, false);
assert.equal(result.error.code, "TS006");
assert.equal(fs.readFileSync(victim, "utf8"), "keep");

const directoryDestination = path.join(work, "directory.kofun-semantic.json");
fs.mkdirSync(directoryDestination);
result = await write(directoryDestination, complete, complete.file.content_sha256);
assert.equal(result.ok, false);
assert.equal(result.error.code, "TS006");

const invalidDestination = path.join(work, "invalid.kofun-semantic.json");
fs.writeFileSync(invalidDestination, "not-json\n");
const invalidBefore = fs.readFileSync(invalidDestination);
result = await write(invalidDestination, complete, complete.file.content_sha256);
assert.equal(result.ok, false);
assert.equal(result.error.code, "TS005");
assert.equal(result.error.reason, "invalid-old");
assert.deepEqual(fs.readFileSync(invalidDestination), invalidBefore);

const raceDestination = path.join(work, "race.kofun-semantic.json");
result = await write(raceDestination, complete, complete.file.content_sha256);
assert.equal(result.ok, true);
const contender = cloneWithSequence(complete, 8);
const race = await Promise.all([
  write(raceDestination, contender, contender.file.content_sha256),
  write(raceDestination, contender, contender.file.content_sha256),
]);
assert.equal(race.filter((item) => item.ok).length, 1);
assert.equal(race.filter((item) => !item.ok && item.error.reason === "stale-sequence").length, 1);
assert.equal(readTypedSidecar(fs.readFileSync(raceDestination)).document.generation.sequence, 8);

const leftovers = fs.readdirSync(work).filter((name) => name.includes(".tmp-") || name.endsWith(".typed-sidecar.lock"));
assert.deepEqual(leftovers, []);

console.log("PASS: typed-sidecar atomic replacement, failure preservation, and race guards");
