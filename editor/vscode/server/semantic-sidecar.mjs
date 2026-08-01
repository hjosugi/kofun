import crypto from "node:crypto";
import { Worker } from "node:worker_threads";

import { readTypedSidecar } from "./generated/codec.mjs";

const FACT_FIELDS = Object.freeze(["type", "effect", "ownership", "origin"]);
const IDENTITY_KINDS = new Set([
  "BindingId", "ExportBindingId", "FileId", "ImplementationId",
  "ImportBindingId", "LawEvidenceId", "ModuleId", "NamespaceId",
  "PackageId", "ScopeId", "SymbolId", "TypeId",
]);
const internals = new WeakMap();
const textEncoder = new TextEncoder();

let worker;
let workerSequence = 0;
const pending = new Map();
const sourceBufferPool = [];
let pooledSourceBytes = 0;
const MAX_POOLED_SOURCE_BYTES = 8 * 1024 * 1024;

class SemanticSnapshot {
  constructor(metadata, state) {
    this.uri = metadata.uri;
    this.version = metadata.version;
    this.generation = metadata.generation;
    this.sessionEpoch = metadata.sessionEpoch;
    this.logicalPath = metadata.logicalPath;
    this.fileId = state.document.file.file_id;
    this.sourceDigest = state.document.file.content_sha256;
    this.sourceStatus = state.document.source_status;
    this.completeness = state.document.completeness;
    internals.set(this, state);
    Object.freeze(this);
  }
}

function boundedText(value, limit = 4096) {
  const clean = String(value ?? "")
    .replace(/[\u0000-\u0008\u000b\u000c\u000e-\u001f\u007f]/g, "\ufffd");
  const scalars = Array.from(clean);
  return scalars.length <= limit ? clean : `${scalars.slice(0, limit).join("")}\u2026`;
}

function escapeMarkdown(value) {
  return boundedText(value, 2048)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/([\\`*_{}\[\]()#+.!|~-])/g, "\\$1");
}

function digest(bytes) {
  return crypto.createHash("sha256").update(bytes).digest("hex");
}

function buildUtf8Index(sourceText, sourceBytes) {
  const encoded = Buffer.from(sourceText, "utf8");
  if (!encoded.equals(sourceBytes)) throw new Error("source bytes do not match the LSP text snapshot");

  const utf16ToByte = new Array(sourceText.length + 1).fill(-1);
  const byteToUtf16 = new Array(sourceBytes.length + 1).fill(-1);
  let utf16 = 0;
  let byte = 0;
  utf16ToByte[0] = 0;
  byteToUtf16[0] = 0;
  while (utf16 < sourceText.length) {
    const codePoint = sourceText.codePointAt(utf16);
    const width = codePoint > 0xffff ? 2 : 1;
    const byteWidth = Buffer.byteLength(String.fromCodePoint(codePoint), "utf8");
    utf16 += width;
    byte += byteWidth;
    utf16ToByte[utf16] = byte;
    byteToUtf16[byte] = utf16;
  }
  if (byte !== sourceBytes.length) throw new Error("UTF-8 index length mismatch");

  const lines = [];
  let start = 0;
  for (let at = 0; at < sourceText.length; at += 1) {
    if (sourceText.charCodeAt(at) !== 10) continue;
    const contentEnd = at > start && sourceText.charCodeAt(at - 1) === 13
      ? at - 1 : at;
    lines.push(Object.freeze({
      utf16Start: start,
      utf16End: contentEnd,
      byteStart: utf16ToByte[start],
      byteEnd: utf16ToByte[contentEnd],
    }));
    start = at + 1;
  }
  lines.push(Object.freeze({
    utf16Start: start,
    utf16End: sourceText.length,
    byteStart: utf16ToByte[start],
    byteEnd: sourceBytes.length,
  }));

  function positionToByte(position) {
    if (!position || !Number.isInteger(position.line) ||
        !Number.isInteger(position.character) ||
        position.line < 0 || position.line >= lines.length ||
        position.character < 0) return null;
    const line = lines[position.line];
    if (position.character > line.utf16End - line.utf16Start) return null;
    const result = utf16ToByte[line.utf16Start + position.character];
    return result < 0 ? null : result;
  }

  function byteToPosition(offset) {
    if (!Number.isInteger(offset) || offset < 0 || offset > sourceBytes.length ||
        byteToUtf16[offset] < 0) return null;
    let low = 0;
    let high = lines.length;
    while (low + 1 < high) {
      const middle = (low + high) >> 1;
      if (lines[middle].byteStart <= offset) low = middle;
      else high = middle;
    }
    const line = lines[low];
    if (offset > line.byteEnd) return null;
    const global = byteToUtf16[offset];
    if (global < line.utf16Start || global > line.utf16End) return null;
    return { line: low, character: global - line.utf16Start };
  }

  function spanToRange(span) {
    const startPosition = byteToPosition(span.start);
    const endPosition = byteToPosition(span.end);
    return startPosition && endPosition
      ? { start: startPosition, end: endPosition } : null;
  }

  return Object.freeze({
    positionToByte,
    byteToPosition,
    spanToRange,
    lineCount: lines.length,
  });
}

function intervalIndex(records) {
  const maximumEnds = new Array(records.length);
  let maximum = -1;
  for (let index = 0; index < records.length; index += 1) {
    maximum = Math.max(maximum, records[index].span.end);
    maximumEnds[index] = maximum;
  }
  return Object.freeze({ records, maximumEnds: Object.freeze(maximumEnds) });
}

function containing(index, offset, compare) {
  let low = 0;
  let high = index.records.length;
  while (low < high) {
    const middle = (low + high) >> 1;
    if (index.records[middle].span.start <= offset) low = middle + 1;
    else high = middle;
  }
  let best = null;
  for (let at = low - 1; at >= 0; at -= 1) {
    if (index.maximumEnds[at] < offset) break;
    const candidate = index.records[at];
    const includes = candidate.span.start === candidate.span.end
      ? candidate.span.start === offset
      : candidate.span.start <= offset && offset < candidate.span.end;
    if (includes && (best === null || compare(candidate, best) < 0)) best = candidate;
  }
  return best;
}

function nodeOrder(left, right) {
  const length = (left.span.end - left.span.start) -
    (right.span.end - right.span.start);
  if (length !== 0) return length;
  const kind = left.kind.localeCompare(right.kind, "en");
  return kind !== 0 ? kind : left.id.localeCompare(right.id, "en");
}

function referenceOrder(left, right) {
  const length = (left.span.end - left.span.start) -
    (right.span.end - right.span.start);
  return length !== 0 ? length : left.id.localeCompare(right.id, "en");
}

function snapshotFailure(code, detail) {
  return Object.freeze({
    ok: false,
    code: boundedText(code || "TS003", 32),
    detail: boundedText(detail || "typed sidecar was rejected", 512),
  });
}

function recycleSourceBuffer(buffer) {
  if (!(buffer instanceof ArrayBuffer) || buffer.byteLength === 0 ||
      buffer.byteLength > 4 * 1024 * 1024 ||
      pooledSourceBytes + buffer.byteLength > MAX_POOLED_SOURCE_BYTES) return;
  sourceBufferPool.push(buffer);
  pooledSourceBytes += buffer.byteLength;
}

function encodedSource(sourceText) {
  const required = Buffer.byteLength(sourceText, "utf8");
  let selected = -1;
  for (let index = 0; index < sourceBufferPool.length; index += 1) {
    if (sourceBufferPool[index].byteLength >= required &&
        (selected < 0 || sourceBufferPool[index].byteLength <
          sourceBufferPool[selected].byteLength)) selected = index;
  }
  let buffer;
  if (selected >= 0) {
    buffer = sourceBufferPool.splice(selected, 1)[0];
    pooledSourceBytes -= buffer.byteLength;
  } else {
    buffer = new ArrayBuffer(required);
  }
  const bytes = new Uint8Array(buffer, 0, required);
  const encoded = textEncoder.encodeInto(sourceText, bytes);
  if (encoded.read !== sourceText.length || encoded.written !== required) {
    throw new Error("UTF-8 source encoding failed");
  }
  return bytes;
}

function linkedDiagnosticCodes(state, root) {
  const codes = new Set();
  const visited = new Set();
  const pendingNodes = [root];
  while (pendingNodes.length > 0) {
    const node = pendingNodes.pop();
    if (!node || visited.has(node.id)) continue;
    visited.add(node.id);
    for (const id of node.diagnostic_ids) {
      const code = state.diagnosticsById.get(id)?.code;
      if (code) codes.add(code);
    }
    for (const id of node.depends_on) pendingNodes.push(state.nodesById.get(id));
  }
  return [...codes].sort();
}

export function semanticSnapshotFromBytes(metadata, sidecarBytes) {
  try {
    if (!metadata || typeof metadata !== "object" ||
        typeof metadata.uri !== "string" ||
        !Number.isInteger(metadata.version) ||
        !Number.isSafeInteger(metadata.generation) || metadata.generation < 0 ||
        !Number.isSafeInteger(metadata.sessionEpoch) || metadata.sessionEpoch < 0 ||
        typeof metadata.logicalPath !== "string" ||
        typeof metadata.sourceText !== "string") {
      return snapshotFailure("TS003", "invalid LSP snapshot metadata");
    }
    const sourceBytes = Buffer.from(metadata.sourceText, "utf8");
    const sourceDigest = digest(sourceBytes);
    const read = readTypedSidecar(sidecarBytes);
    if (!read.ok) return snapshotFailure(read.error?.code, read.error?.message);
    const document = read.document;
    if (document.source_status === "cancelled") {
      return Object.freeze({
        ok: false,
        cancelled: true,
        code: "TS005",
        detail: "cancelled semantic result was discarded",
      });
    }
    if (document.file.logical_path !== metadata.logicalPath ||
        document.file.byte_length !== sourceBytes.length ||
        document.file.content_sha256 !== sourceDigest ||
        document.generation.sequence !== metadata.generation ||
        (metadata.expectedFileId && document.file.file_id !== metadata.expectedFileId)) {
      return snapshotFailure("TS005", "URI/version/FileId/digest/generation guard rejected the result");
    }
    const utf8 = buildUtf8Index(metadata.sourceText, sourceBytes);
    const nodesById = new Map(document.nodes.map((node) => [node.id, node]));
    const diagnosticsById = new Map(
      document.diagnostics.map((diagnostic) => [diagnostic.id, diagnostic]),
    );
    const state = Object.freeze({
      document,
      utf8,
      nodesById,
      diagnosticsById,
      nodes: intervalIndex(document.nodes),
      references: intervalIndex(document.references),
    });
    return Object.freeze({
      ok: true,
      snapshot: new SemanticSnapshot(metadata, state),
      decodeMilliseconds: metadata.decodeStarted === undefined ? null :
        Number(process.hrtime.bigint() - metadata.decodeStarted) / 1e6,
    });
  } catch (caught) {
    return snapshotFailure("TS003", caught?.message);
  }
}

function ensureWorker() {
  if (worker) return worker;
  worker = new Worker(new URL("./semantic-worker.mjs", import.meta.url), {
    // The server wrapper uses V8 process flags that Worker rejects. The
    // analyzer needs no inherited command-line flags.
    execArgv: [],
    // The bounded C producer keeps its fixed-capacity event staging records on
    // the stack. Node workers default to a smaller stack than the main thread.
    resourceLimits: { stackSizeMb: 32 },
  });
  worker.on("message", (message) => {
    recycleSourceBuffer(message?.sourceBuffer);
    const operation = pending.get(message?.id);
    if (!operation) return;
    pending.delete(message.id);
    operation.cleanup();
    operation.resolve(message.result);
  });
  worker.on("error", (caught) => {
    const operations = [...pending.values()];
    pending.clear();
    worker = undefined;
    for (const operation of operations) {
      operation.cleanup();
      operation.resolve(snapshotFailure("ETS03", caught?.message));
    }
  });
  worker.on("exit", (code) => {
    if (worker) worker = undefined;
    if (code === 0) return;
    const operations = [...pending.values()];
    pending.clear();
    for (const operation of operations) {
      operation.cleanup();
      operation.resolve(snapshotFailure("ETS03", `semantic worker exited with ${code}`));
    }
  });
  return worker;
}

function produce(metadata, signal) {
  if (signal?.aborted) return Promise.resolve(Object.freeze({ ok: false, cancelled: true }));
  const currentWorker = ensureWorker();
  const id = ++workerSequence;
  return new Promise((resolve) => {
    const abort = () => {
      const operation = pending.get(id);
      if (!operation) return;
      pending.delete(id);
      operation.cleanup();
      resolve(Object.freeze({ ok: false, cancelled: true }));
    };
    const cleanup = () => signal?.removeEventListener("abort", abort);
    signal?.addEventListener("abort", abort, { once: true });
    pending.set(id, { resolve, cleanup });
    let sourceBytes;
    try {
      sourceBytes = encodedSource(metadata.sourceText);
    } catch (caught) {
      pending.delete(id);
      cleanup();
      resolve(snapshotFailure("ETS04", caught?.message));
      return;
    }
    currentWorker.postMessage({
      type: "analyze",
      id,
      sourceBytes,
      logicalPath: metadata.logicalPath,
      generation: metadata.generation,
      cancelAfterCommit: metadata.cancelAfterCommit === true,
    }, [sourceBytes.buffer]);
  });
}

export async function analyzeDocument(metadata, cancellation) {
  const produced = await produce(metadata, cancellation);
  if (!produced.ok) return produced;
  if (cancellation?.aborted) return Object.freeze({ ok: false, cancelled: true });
  return semanticSnapshotFromBytes(
    { ...metadata, decodeStarted: process.hrtime.bigint() },
    produced.sidecarBytes,
  );
}

export function publishDiagnostics(snapshot) {
  const state = internals.get(snapshot);
  if (!state) return [];
  const severity = { error: 1, warning: 2, information: 3, hint: 4 };
  const diagnostics = [];
  for (const item of state.document.diagnostics) {
    const primary = state.utf8.spanToRange(item.primary.span);
    if (!primary || item.primary.file_id !== snapshot.fileId) return [];
    const relatedInformation = [];
    for (const related of item.related) {
      if (!related.location || related.location.file_id !== snapshot.fileId) continue;
      const relatedRange = state.utf8.spanToRange(related.location.span);
      if (!relatedRange) return [];
      relatedInformation.push({
        location: { uri: snapshot.uri, range: relatedRange },
        message: boundedText(related.relation, 256),
      });
    }
    const value = {
      range: primary,
      severity: severity[item.severity],
      code: item.code,
      source: "kofun",
      message: boundedText(item.fallback_text),
      data: {
        category: item.category,
        templateId: item.template_id,
        remedyIds: item.remedies.map((remedy) => remedy.id),
        truncated: item.truncated,
      },
    };
    if (relatedInformation.length > 0) value.relatedInformation = relatedInformation;
    diagnostics.push(value);
  }
  return diagnostics;
}

export function hoverAt(snapshot, utf16Position) {
  const state = internals.get(snapshot);
  if (!state) return null;
  const offset = state.utf8.positionToByte(utf16Position);
  if (offset === null) return null;
  const node = containing(state.nodes, offset, nodeOrder);
  if (!node || !["validated", "provisional", "error", "unavailable"].includes(node.status)) {
    return null;
  }
  const codes = linkedDiagnosticCodes(state, node);
  const lines = [];
  for (const field of FACT_FIELDS) {
    const fact = node[field];
    if (!fact || !["validated", "provisional", "error", "unavailable"].includes(fact.status)) {
      continue;
    }
    if (fact.status === "error" || fact.status === "unavailable") continue;
    let line = `${field}: ${escapeMarkdown(fact.display)}`;
    if (fact.status === "provisional") {
      line += ` **provisional**${codes.length > 0 ? ` (${codes.map(escapeMarkdown).join(", ")})` : ""}`;
    }
    lines.push(line);
  }
  if (lines.length === 1 && node.origin &&
      (node.kind === "module.root" || node.kind === "lexical.scope")) {
    return null;
  }
  if (lines.length === 0) return null;
  const hoverRange = state.utf8.spanToRange(node.span);
  return hoverRange ? {
    contents: { kind: "markdown", value: lines.join("  \n") },
    range: hoverRange,
  } : null;
}

export function definitionAt(snapshot, utf16Position) {
  const state = internals.get(snapshot);
  if (!state) return null;
  const offset = state.utf8.positionToByte(utf16Position);
  if (offset === null) return null;
  const reference = containing(state.references, offset, referenceOrder);
  if (!reference || reference.status !== "validated" ||
      reference.target.disclosure !== "resolved" ||
      !reference.target.identity ||
      !IDENTITY_KINDS.has(reference.target.identity.kind) ||
      !reference.target.declaration_node) return null;
  const sourceNode = state.nodesById.get(reference.from_node);
  const declaration = state.nodesById.get(reference.target.declaration_node);
  if (!sourceNode || sourceNode.status !== "validated" ||
      !declaration || declaration.status !== "validated" ||
      !declaration.identities.some((identity) =>
        identity.kind === reference.target.identity.kind &&
        identity.value === reference.target.identity.value)) return null;
  const targetRange = state.utf8.spanToRange(declaration.span);
  return targetRange ? { uri: snapshot.uri, range: targetRange } : null;
}

export function positionToByte(snapshot, position) {
  return internals.get(snapshot)?.utf8.positionToByte(position) ?? null;
}

export function byteToPosition(snapshot, offset) {
  return internals.get(snapshot)?.utf8.byteToPosition(offset) ?? null;
}

export async function shutdownSemanticAnalysis() {
  const current = worker;
  worker = undefined;
  const operations = [...pending.values()];
  pending.clear();
  sourceBufferPool.length = 0;
  pooledSourceBytes = 0;
  for (const operation of operations) {
    operation.cleanup();
    operation.resolve(Object.freeze({ ok: false, cancelled: true }));
  }
  if (current) await current.terminate();
}
