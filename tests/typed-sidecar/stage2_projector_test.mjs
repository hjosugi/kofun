import assert from "node:assert/strict";
import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";

import { encodeTypedSidecar, readTypedSidecar } from "../../tooling/typed-sidecar/codec.mjs";
import {
  STAGE2_SEMANTIC_EVENT_LIMITS,
  emitStage2TypedSidecar,
  projectStage2SemanticEvents,
  readStage2SemanticEvents,
} from "../../tooling/typed-sidecar/from-stage2.mjs";

const work = process.argv[2];
const source = process.argv[3];
if (!work || !source) {
  throw new Error("usage: stage2_projector_test.mjs WORK SOURCE");
}

function stream(name) {
  return fs.readFileSync(path.join(work, `${name}.kse`));
}

function read(name) {
  const result = readStage2SemanticEvents(stream(name));
  assert.equal(result.ok, true, `${name}: ${result.error?.message ?? "read failed"}`);
  return result.events;
}

function project(name) {
  const result = projectStage2SemanticEvents(read(name));
  assert.equal(result.ok, true, `${name}: ${result.error?.message ?? "projection failed"}`);
  return result.document;
}

function assertDeepFrozen(root) {
  const pending = [root];
  const seen = new WeakSet();
  while (pending.length > 0) {
    const value = pending.pop();
    if (value === null || typeof value !== "object" || seen.has(value)) continue;
    seen.add(value);
    assert.equal(Object.isFrozen(value), true);
    pending.push(...(Array.isArray(value) ? value : Object.values(value)));
  }
}

function resign(bytes) {
  const copy = Buffer.from(bytes);
  const payloadBytes = copy.readUInt32BE(12);
  crypto.createHash("sha256").update(copy.subarray(0, 16 + payloadBytes))
    .digest().copy(copy, 16 + payloadBytes);
  return copy;
}

function eventFrames(bytes) {
  const payloadEnd = 16 + bytes.readUInt32BE(12);
  const result = [];
  let cursor = 16;
  while (cursor < payloadEnd) {
    const kind = bytes[cursor];
    const fieldCount = bytes.readUInt16BE(cursor + 2);
    const frameBytes = bytes.readUInt32BE(cursor + 4);
    const frame = { start: cursor, kind, fields: [] };
    cursor += 8;
    const frameEnd = cursor + frameBytes;
    for (let index = 0; index < fieldCount; index += 1) {
      const tag = bytes[cursor];
      const wire = bytes[cursor + 1];
      const length = bytes.readUInt32BE(cursor + 4);
      frame.fields.push({ start: cursor, tag, wire, data: cursor + 8, length });
      cursor += 8 + length;
    }
    assert.equal(cursor, frameEnd);
    result.push(frame);
  }
  return result;
}

for (const name of [
  "complete", "partial", "type-error", "ownership", "duplicate", "cancelled",
]) {
  const events = read(name);
  assertDeepFrozen(events);
  const document = project(name);
  assertDeepFrozen(document);
  const encoded = encodeTypedSidecar(document);
  assert.equal(encoded.ok, true);
  const replay = readTypedSidecar(encoded.bytes);
  assert.equal(replay.ok, true);
  assert.deepEqual(replay.document, document);
}

const complete = project("complete");
assert.equal(complete.authoritative, false);
assert.equal(complete.completeness, "complete");
assert.equal(complete.source_status, "checked");
assert.equal(complete.generation.sequence, 41);
assert.ok(complete.nodes.some((node) => node.kind === "function.declaration"));
assert.ok(complete.nodes.some((node) => node.kind === "parameter.binding"));
assert.ok(complete.nodes.some((node) => node.kind === "local.binding"));
assert.ok(complete.nodes.some((node) => node.kind === "adt.declaration"));
assert.ok(complete.nodes.some((node) => node.kind === "constructor.declaration"));
assert.ok(complete.nodes.some((node) => node.type?.status === "validated"));
assert.ok(complete.nodes.some((node) => node.ownership?.status === "validated"));
assert.ok(complete.nodes.some((node) => node.depends_on.length > 0));
assert.ok(complete.nodes.flatMap((node) => node.identities)
  .some((identity) => identity.kind === "SymbolId"));
assert.ok(complete.references.length > 0);
assert.ok(complete.references.every((reference) =>
  reference.target.disclosure === "resolved"));

for (const name of ["partial", "type-error", "ownership"]) {
  const document = project(name);
  assert.equal(document.completeness, "partial");
  assert.equal(document.source_status, "failed");
  assert.ok(document.diagnostics.some((diagnostic) => diagnostic.severity === "error"));
}
const cancelled = project("cancelled");
assert.equal(cancelled.completeness, "partial");
assert.equal(cancelled.source_status, "cancelled");

const remapA = project("remap-a");
const remapB = project("remap-b");
assert.deepEqual(remapA, remapB);

const generationA = structuredClone(project("generation-a"));
const generationB = structuredClone(project("generation-b"));
assert.notEqual(generationA.generation.sequence, generationB.generation.sequence);
generationA.generation.sequence = 0;
generationB.generation.sequence = 0;
assert.deepEqual(generationA, generationB);

const emitted = path.join(work, "projected.kofun-semantic.json");
let emittedResult = await emitStage2TypedSidecar(
  stream("complete"), emitted, { sourcePath: source },
);
assert.equal(emittedResult.ok, true, emittedResult.error?.message);
assert.deepEqual(readTypedSidecar(fs.readFileSync(emitted)).document, complete);

const completeEvents = read("complete");

const numericRemedies = structuredClone(read("ownership"));
const numericRemedyDiagnostic = numericRemedies.find(
  (event) => event.kind === "diagnostic",
);
numericRemedyDiagnostic.remedy_ids = [2, 10];
numericRemedyDiagnostic.edits = [];
const numericRemedyProjection = projectStage2SemanticEvents(numericRemedies);
assert.equal(numericRemedyProjection.ok, true, numericRemedyProjection.error?.message);
assert.deepEqual(
  numericRemedyProjection.document.diagnostics[0].remedies.map(
    (remedy) => remedy.id,
  ),
  ["stage2-remedy-10", "stage2-remedy-2"],
);

const partialEvents = read("partial");
const partialDiagnostic = partialEvents.find((event) => event.kind === "diagnostic");
const factWithoutDirectDiagnostic = structuredClone(partialEvents);
const errorFact = factWithoutDirectDiagnostic.find((event) => event.kind === "fact");
const errorFactOwner = factWithoutDirectDiagnostic.find(
  (event) => event.kind === "node" && event.id === errorFact.owner_node_id,
);
errorFact.status = 3;
errorFact.diagnostic_ids = [];
errorFactOwner.status = 3;
errorFactOwner.diagnostic_ids = [partialDiagnostic.id];
let invalid = projectStage2SemanticEvents(factWithoutDirectDiagnostic);
assert.equal(invalid.ok, false);
assert.equal(invalid.error.code, "ETS03");

const nodeWithoutDirectDiagnostic = structuredClone(partialEvents);
const diagnosticViaFact = nodeWithoutDirectDiagnostic.find(
  (event) => event.kind === "diagnostic",
);
const linkedFact = nodeWithoutDirectDiagnostic.find((event) => event.kind === "fact");
const errorOwner = nodeWithoutDirectDiagnostic.find(
  (event) => event.kind === "node" && event.id === linkedFact.owner_node_id,
);
errorOwner.status = 3;
errorOwner.diagnostic_ids = [];
linkedFact.diagnostic_ids = [diagnosticViaFact.id];
invalid = projectStage2SemanticEvents(nodeWithoutDirectDiagnostic);
assert.equal(invalid.ok, false);
assert.equal(invalid.error.code, "ETS03");

const dangling = structuredClone(completeEvents);
const node = dangling.find((event) => event.kind === "node");
node.dependencies = ["f".repeat(64)];
invalid = projectStage2SemanticEvents(dangling);
assert.equal(invalid.ok, false);
assert.equal(invalid.error.code, "ETS03");

const leaked = structuredClone(completeEvents);
const reference = leaked.find((event) => event.kind === "reference");
reference.status = 4;
reference.target_shape = 2;
reference.target_value = "1".repeat(64);
reference.reason = "visibility-restricted";
leaked.at(-1).source_status = 3;
leaked.at(-1).completeness = 2;
invalid = projectStage2SemanticEvents(leaked);
assert.equal(invalid.ok, false);
assert.equal(invalid.error.code, "ETS03");

const referenceWithoutDirectDiagnostic = structuredClone(partialEvents);
const directReference = referenceWithoutDirectDiagnostic.find(
  (event) => event.kind === "reference",
);
if (directReference) {
  directReference.status = 3;
  directReference.target_shape = 3;
  directReference.target_value = "0".repeat(64);
  directReference.reason = "unresolved-current-stage2-reference";
  directReference.diagnostic_ids = [];
  const referenceOwner = referenceWithoutDirectDiagnostic.find(
    (event) => event.kind === "node" &&
      event.id === directReference.source_node_id,
  );
  referenceOwner.diagnostic_ids = [partialDiagnostic.id];
  invalid = projectStage2SemanticEvents(referenceWithoutDirectDiagnostic);
  assert.equal(invalid.ok, false);
  assert.equal(invalid.error.code, "ETS03");
}

const identityAffected = structuredClone(partialEvents);
const identityRecord = identityAffected.find((event) => event.kind === "identity");
identityAffected.find((event) => event.kind === "diagnostic").affected_ids = [
  identityRecord.value,
];
invalid = projectStage2SemanticEvents(identityAffected);
assert.equal(invalid.ok, false);
assert.equal(invalid.error.code, "ETS03");

const hidden = structuredClone(completeEvents);
const hiddenReference = hidden.find((event) => event.kind === "reference");
hiddenReference.status = 4;
hiddenReference.target_shape = 2;
hiddenReference.target_value = "0".repeat(64);
hiddenReference.reason = "visibility-restricted";
hidden.at(-1).source_status = 3;
hidden.at(-1).completeness = 2;
const hiddenProjection = projectStage2SemanticEvents(hidden);
assert.equal(hiddenProjection.ok, true, hiddenProjection.error?.message);
const hiddenTarget = hiddenProjection.document.references
  .find((item) => item.id === hiddenReference.id).target;
assert.deepEqual(Object.keys(hiddenTarget).sort(), [
  "disclosure", "identity_kind", "reason",
]);
assert.equal(hiddenTarget.disclosure, "hidden");
assert.equal(JSON.stringify(hiddenTarget).includes(hiddenReference.target_value), false);

const reordered = structuredClone(completeEvents);
const firstIdentity = reordered.findIndex((event) => event.kind === "identity");
[reordered[firstIdentity - 1], reordered[firstIdentity]] =
  [reordered[firstIdentity], reordered[firstIdentity - 1]];
invalid = projectStage2SemanticEvents(reordered);
assert.equal(invalid.ok, false);
assert.equal(invalid.error.code, "ETS03");

const relationOverflow = structuredClone(completeEvents);
relationOverflow.find((event) => event.kind === "node").dependencies =
  Array.from({ length: 65 }, (_, index) =>
    (index + 1).toString(16).padStart(64, "0"));
invalid = projectStage2SemanticEvents(relationOverflow);
assert.equal(invalid.ok, false);
assert.equal(invalid.error.code, "ETS04");

const corruptions = [];
const digest = Buffer.from(stream("complete"));
digest[digest.length - 1] ^= 1;
corruptions.push(["digest", digest, "ETS03"]);
const magic = Buffer.from(stream("complete"));
magic[0] ^= 1;
corruptions.push(["magic", magic, "ETS03"]);
const version = Buffer.from(stream("complete"));
version.writeUInt16BE(2, 4);
corruptions.push(["version", resign(version), "ETS03"]);
const count = Buffer.from(stream("complete"));
count.writeUInt32BE(count.readUInt32BE(8) + 1, 8);
corruptions.push(["event-count", resign(count), "ETS03"]);
const payloadLength = Buffer.from(stream("complete"));
payloadLength.writeUInt32BE(payloadLength.readUInt32BE(12) - 1, 12);
corruptions.push(["payload-length", payloadLength, "ETS04"]);
const flags = Buffer.from(stream("complete"));
flags[17] = 1;
corruptions.push(["flags", resign(flags), "ETS03"]);
const unknownKind = Buffer.from(stream("complete"));
unknownKind[16] = 99;
corruptions.push(["event-kind", resign(unknownKind), "ETS03"]);
const fieldReserved = Buffer.from(stream("complete"));
fieldReserved[eventFrames(fieldReserved)[0].fields[0].start + 2] = 1;
corruptions.push(["reserved", resign(fieldReserved), "ETS03"]);
const unknownWire = Buffer.from(stream("complete"));
unknownWire[eventFrames(unknownWire)[0].fields[0].start + 1] = 10;
corruptions.push(["wire", resign(unknownWire), "ETS03"]);
const unsafeGeneration = Buffer.from(stream("complete"));
const sourceFrame = eventFrames(unsafeGeneration)[0];
const generationField = sourceFrame.fields.find((field) => field.tag === 9);
unsafeGeneration.writeBigUInt64BE(BigInt(Number.MAX_SAFE_INTEGER) + 1n, generationField.data);
corruptions.push(["unsafe-generation", resign(unsafeGeneration), "ETS03"]);
const absolutePath = Buffer.from(stream("complete"));
const logicalPath = eventFrames(absolutePath)[0].fields
  .find((field) => field.tag === 4);
absolutePath[logicalPath.data] = 0x2f;
corruptions.push(["absolute-path", resign(absolutePath), "ETS03"]);
const invalidUtf8 = Buffer.from(stream("complete"));
invalidUtf8[logicalPath.data] = 0xc3;
invalidUtf8[logicalPath.data + 1] = 0x28;
corruptions.push(["invalid-utf8", resign(invalidUtf8), "ETS04"]);
const nonNfc = Buffer.from(stream("complete"));
nonNfc[logicalPath.data] = 0x65;
nonNfc[logicalPath.data + 1] = 0xcc;
nonNfc[logicalPath.data + 2] = 0x81;
corruptions.push(["non-nfc", resign(nonNfc), "ETS04"]);
const outOfOrder = Buffer.from(stream("complete"));
const nodeFrame = eventFrames(outOfOrder).find((frame) => frame.kind === 2);
nodeFrame.fields[1] && (outOfOrder[nodeFrame.fields[1].start] = 1);
corruptions.push(["duplicate-tag", resign(outOfOrder), "ETS03"]);
const enumValue = Buffer.from(stream("complete"));
const enumNode = eventFrames(enumValue).find((frame) => frame.kind === 2);
enumValue[enumNode.fields.find((field) => field.tag === 2).data] = 255;
corruptions.push(["enum", resign(enumValue), "ETS03"]);
const nested = Buffer.from(stream("duplicate"));
const relatedField = eventFrames(nested)
  .find((frame) => frame.kind === 6).fields
  .find((field) => field.tag === 12);
assert.ok(relatedField);
nested.writeUInt16BE(nested.readUInt16BE(relatedField.data) + 1, relatedField.data);
corruptions.push(["nested-related-count", resign(nested), "ETS03"]);
for (const [label, bytes, code] of corruptions) {
  const result = readStage2SemanticEvents(bytes);
  assert.equal(result.ok, false, label);
  assert.equal(result.error.code, code, label);
}

const oneOver = Buffer.alloc(STAGE2_SEMANTIC_EVENT_LIMITS.streamBytes + 1);
assert.equal(readStage2SemanticEvents(oneOver).error.code, "ETS04");
assert.equal(
  readStage2SemanticEvents(stream("complete").subarray(0, -1)).error.code,
  "ETS04",
);
assert.throws(() => readStage2SemanticEvents("not bytes"), TypeError);
assert.throws(() => projectStage2SemanticEvents(null), TypeError);

const editedSource = path.join(work, "edited.kofun");
fs.copyFileSync(source, editedSource);
fs.appendFileSync(editedSource, "\n");
emittedResult = await emitStage2TypedSidecar(
  stream("generation-b"),
  path.join(work, "stale.kofun-semantic.json"),
  { sourcePath: editedSource },
);
assert.equal(emittedResult.ok, false);
assert.equal(emittedResult.error.code, "ETS05");
emittedResult = await emitStage2TypedSidecar(
  stream("generation-b"),
  path.join(work, "missing-source.kofun-semantic.json"),
  { sourcePath: path.join(work, "does-not-exist.kofun") },
);
assert.equal(emittedResult.ok, false);
assert.equal(emittedResult.error.code, "ETS05");

console.log("PASS: Stage 2 KSE reader, exact projection, disclosure, and stale-source guard");
