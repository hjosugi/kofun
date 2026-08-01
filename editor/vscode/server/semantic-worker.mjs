import { createRequire } from "node:module";
import { parentPort } from "node:worker_threads";

import {
  projectStage2SemanticEvents,
  readStage2SemanticEvents,
} from "./generated/from-stage2.mjs";
import { encodeTypedSidecar } from "./generated/codec.mjs";

const require = createRequire(import.meta.url);
const bridge = require("./generated/semantic-bridge.node");

function failure(code, detail) {
  return { ok: false, code: code || "ETS03", detail: String(detail || "semantic analysis failed").slice(0, 512) };
}

function project(produced) {
  if (!produced || produced.ok !== true || !produced.eventBytes) {
    return failure(produced?.error?.code, produced?.error?.detail);
  }
  const read = readStage2SemanticEvents(produced.eventBytes);
  if (!read.ok) return failure(read.error?.code, read.error?.message);
  const projected = projectStage2SemanticEvents(read.events);
  if (!projected.ok) return failure(projected.error?.code, projected.error?.message);
  const encoded = encodeTypedSidecar(projected.document);
  if (!encoded.ok) return failure(encoded.error?.code, encoded.error?.message);
  return {
    ok: true,
    sidecarBytes: encoded.bytes,
    compilerExitClass: produced.compilerExitClass,
    diagnosticCode: produced.diagnosticCode,
  };
}

function produce(message) {
  const source = Buffer.from(
    message.sourceBytes.buffer,
    message.sourceBytes.byteOffset,
    message.sourceBytes.byteLength,
  );
  const compile = bridge.produce(
    source,
    message.logicalPath,
    message.generation,
    0,
    message.cancelAfterCommit === true,
  );
  if (!compile.ok) return failure(compile.error?.code, compile.error?.detail);
  if (compile.compilerExitClass === 0) return project(compile);

  const ownership = bridge.produce(
    source,
    message.logicalPath,
    message.generation,
    1,
    message.cancelAfterCommit === true,
  );
  const ownershipIsFrozenAuthority = ownership.ok && (
    ownership.compilerExitClass === 0 ||
    ownership.diagnosticCode === "E007" ||
    ownership.diagnosticCode === "E2S21"
  );
  return project(ownershipIsFrozenAuthority ? ownership : compile);
}

parentPort.on("message", (message) => {
  if (!message || message.type !== "analyze") return;
  let result;
  try {
    result = produce(message);
  } catch (caught) {
    result = failure("ETS03", caught?.message);
  }
  const transfer = [message.sourceBytes.buffer];
  if (result.ok && result.sidecarBytes?.buffer) {
    transfer.push(result.sidecarBytes.buffer);
  }
  parentPort.postMessage({
    id: message.id,
    result,
    sourceBuffer: message.sourceBytes.buffer,
  }, transfer);
});
