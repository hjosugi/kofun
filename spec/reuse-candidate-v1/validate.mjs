#!/usr/bin/env node

import { readFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

export const SCHEMA = "kofun.reuse-candidate/v1";

export const STATES = Object.freeze([
  "ordinary_allocation",
  "reuse_candidate",
  "reuse_statically_guaranteed",
]);

export const REASONS = Object.freeze([
  "incompatible-size",
  "incompatible-alignment",
  "incompatible-layout",
  "pinned",
  "ffi-exposed",
  "weakly-referenced",
  "borrowed-view",
  "closure-capture",
  "possible-alias",
  "owned-field-hazard",
  "backend-limitation",
]);

export const PROVENANCES = Object.freeze([
  "compile-time-ownership-escape-last-use",
  "owned-or-rc-specialized-representation",
  "runtime-uniqueness-check",
  "unproved",
]);

export const REMARKS = Object.freeze({
  guaranteed: "reuse guaranteed: compatible constructor storage and proved uniqueness",
  "incompatible-size": "reuse refused: source and target allocation sizes differ",
  "incompatible-alignment": "reuse refused: source and target alignments differ",
  "incompatible-layout": "reuse refused: source and target field or scanning layouts differ",
  pinned: "reuse refused: source storage is pinned",
  "ffi-exposed": "reuse refused: source storage is exposed through FFI",
  "weakly-referenced": "reuse refused: source storage is weakly referenced",
  "borrowed-view": "reuse refused: a borrowed view can observe source storage",
  "closure-capture": "reuse refused: a closure captures source storage",
  "possible-alias": "reuse refused: source storage may have another live alias",
  "owned-field-hazard": "reuse refused: owned fields lack a proven transfer/drop plan",
  "backend-limitation": "reuse refused: selected backend cannot implement this candidate",
});

const HERE = dirname(fileURLToPath(import.meta.url));
const AGGREGATE_ROOT = resolve(HERE, "../aggregate-layout-v1");
const LIMITS = Object.freeze({ inputBytes: 65_536, textBytes: 128, pointers: 64 });
const IDENTIFIER = /^[A-Za-z][A-Za-z0-9_.:\[\]-]*$/;
const QUANTITY = /^(0|[1-9][0-9]*)$/;
const TARGETS = new Set(["wasm32", "x86_64-linux"]);
const PROOFS = new Set(["proved", "conditional", "unproved"]);

export class ReuseCandidateError extends Error {
  constructor(code, name, detail) {
    super(detail);
    this.code = code;
    this.name = name;
  }
}

function reject(code, name, detail) {
  throw new ReuseCandidateError(code, name, detail);
}

function isObject(value) {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

function object(value, path) {
  if (!isObject(value)) reject("RCV002", "invalid-field", `${path} must be an object`);
  return value;
}

function exactKeys(value, allowed, path) {
  for (const key of Object.keys(value)) {
    if (!allowed.has(key)) {
      reject("RCV002", "invalid-field", `${path}.${key} is not a v1 field`);
    }
  }
}

function requiredKeys(value, required, path) {
  for (const key of required) {
    if (!Object.hasOwn(value, key)) {
      reject("RCV002", "invalid-field", `${path}.${key} is required`);
    }
  }
}

function text(value, path, { stable = false } = {}) {
  if (typeof value !== "string" || value.length === 0) {
    reject("RCV002", "invalid-field", `${path} must be non-empty text`);
  }
  if (Buffer.byteLength(value, "utf8") > LIMITS.textBytes) {
    reject("RCV003", "limit-exceeded", `${path} exceeds ${LIMITS.textBytes} UTF-8 bytes`);
  }
  if (stable && !IDENTIFIER.test(value)) {
    reject("RCV002", "invalid-field", `${path} must be a stable identifier`);
  }
  return value;
}

function integer(value, path) {
  if (!Number.isSafeInteger(value) || value < 0) {
    reject("RCV002", "invalid-field", `${path} must be a non-negative safe integer`);
  }
  return value;
}

function quantity(value, path) {
  if (typeof value !== "string" || !QUANTITY.test(value)) {
    reject("RCV002", "invalid-field", `${path} must be a canonical unsigned decimal string`);
  }
  return value;
}

function span(value, path) {
  const source = object(value, path);
  exactKeys(source, new Set(["start", "end"]), path);
  requiredKeys(source, ["start", "end"], path);
  const start = integer(source.start, `${path}.start`);
  const end = integer(source.end, `${path}.end`);
  if (end <= start) reject("RCV002", "invalid-field", `${path}.end must be greater than start`);
  return Object.freeze({ start, end });
}

function constructor(value, path) {
  const source = object(value, path);
  exactKeys(source, new Set(["type_id", "constructor", "span"]), path);
  requiredKeys(source, ["type_id", "constructor", "span"], path);
  return Object.freeze({
    type_id: text(source.type_id, `${path}.type_id`, { stable: true }),
    constructor: text(source.constructor, `${path}.constructor`, { stable: true }),
    span: span(source.span, `${path}.span`),
  });
}

function scanningShape(value, path) {
  const source = object(value, path);
  exactKeys(source, new Set(["pointers", "drop"]), path);
  requiredKeys(source, ["pointers", "drop"], path);
  if (!Array.isArray(source.pointers)) {
    reject("RCV002", "invalid-field", `${path}.pointers must be an array`);
  }
  if (source.pointers.length > LIMITS.pointers) {
    reject("RCV003", "limit-exceeded", `${path}.pointers exceeds ${LIMITS.pointers} entries`);
  }
  const pointers = source.pointers.map((pointer, index) =>
    quantity(pointer, `${path}.pointers[${index}]`));
  const sorted = [...pointers].sort((left, right) => {
    const a = BigInt(left);
    const b = BigInt(right);
    return a < b ? -1 : a > b ? 1 : 0;
  });
  if (new Set(pointers).size !== pointers.length ||
      pointers.some((pointer, index) => pointer !== sorted[index])) {
    reject("RCV002", "invalid-field", `${path}.pointers must be unique and numerically sorted`);
  }
  if (source.drop !== "trivial" && source.drop !== "managed") {
    reject("RCV002", "invalid-field", `${path}.drop must be trivial or managed`);
  }
  return Object.freeze({ pointers: Object.freeze(pointers), drop: source.drop });
}

function layoutSide(value, path) {
  const source = object(value, path);
  exactKeys(source, new Set([
    "layout_id", "size", "align", "field_count", "scanning_shape",
  ]), path);
  requiredKeys(source, [
    "layout_id", "size", "align", "field_count", "scanning_shape",
  ], path);
  return Object.freeze({
    layout_id: text(source.layout_id, `${path}.layout_id`, { stable: true }),
    size: quantity(source.size, `${path}.size`),
    align: quantity(source.align, `${path}.align`),
    field_count: integer(source.field_count, `${path}.field_count`),
    scanning_shape: scanningShape(source.scanning_shape, `${path}.scanning_shape`),
  });
}

function layoutEvidence(value) {
  const source = object(value, "$input.layout_evidence");
  exactKeys(source, new Set([
    "contract", "target_data_layout", "source_layout", "target_layout",
  ]), "$input.layout_evidence");
  requiredKeys(source, [
    "contract", "target_data_layout", "source_layout", "target_layout",
  ], "$input.layout_evidence");
  if (source.contract !== "kofun.aggregate-layout/v1") {
    reject("RCV106", "layout-contradiction", "layout_evidence.contract must name AggregateLayout v1");
  }
  if (!TARGETS.has(source.target_data_layout)) {
    reject("RCV106", "layout-contradiction", "target_data_layout has no committed AggregateLayout v1 descriptor");
  }
  return Object.freeze({
    contract: source.contract,
    target_data_layout: source.target_data_layout,
    source_layout: layoutSide(source.source_layout, "$input.layout_evidence.source_layout"),
    target_layout: layoutSide(source.target_layout, "$input.layout_evidence.target_layout"),
  });
}

function uniquenessEvidence(value) {
  const source = object(value, "$input.uniqueness_evidence");
  exactKeys(source, new Set(["proof", "provenance", "evidence_id"]), "$input.uniqueness_evidence");
  requiredKeys(source, ["proof", "provenance", "evidence_id"], "$input.uniqueness_evidence");
  if (!PROOFS.has(source.proof)) {
    reject("RCV002", "invalid-field", "uniqueness_evidence.proof is not a v1 proof state");
  }
  if (!PROVENANCES.includes(source.provenance)) {
    reject("RCV002", "invalid-field", "uniqueness_evidence.provenance is not a v1 provenance");
  }
  const expectedProof = source.provenance === "unproved"
    ? "unproved"
    : source.provenance === "runtime-uniqueness-check" ? "conditional" : "proved";
  if (source.proof !== expectedProof) {
    reject("RCV108", "provenance-inconsistent",
      `provenance ${source.provenance} requires proof ${expectedProof}`);
  }
  let evidenceId = null;
  if (source.provenance === "unproved") {
    if (source.evidence_id !== null) {
      reject("RCV108", "provenance-inconsistent", "unproved provenance requires a null evidence_id");
    }
  } else {
    evidenceId = text(source.evidence_id, "$input.uniqueness_evidence.evidence_id", { stable: true });
  }
  if (source.provenance === "compile-time-ownership-escape-last-use" &&
      evidenceId === "compiler.ensure_move") {
    reject("RCV111", "insufficient-evidence",
      "compiler.ensure_move alone does not prove constructor-storage uniqueness");
  }
  return Object.freeze({ proof: source.proof, provenance: source.provenance, evidence_id: evidenceId });
}

function eligibility(value) {
  const source = object(value, "$input.eligibility");
  exactKeys(source, new Set([...STATES, "reason"]), "$input.eligibility");
  requiredKeys(source, STATES, "$input.eligibility");
  const flags = {};
  for (const state of STATES) {
    if (typeof source[state] !== "boolean") {
      reject("RCV002", "invalid-field", `eligibility.${state} must be boolean`);
    }
    flags[state] = source[state];
  }
  const selected = STATES.filter((state) => flags[state]);
  if (selected.length === 0) {
    reject("RCV101", "state-required", "exactly one eligibility state must be true; found none");
  }
  if (selected.length > 1) {
    reject("RCV102", "state-exclusive", "exactly one eligibility state must be true; found multiple");
  }
  const guaranteed = selected[0] === "reuse_statically_guaranteed";
  if (!Object.hasOwn(source, "reason") || (!guaranteed && source.reason === null)) {
    reject("RCV103", "reason-required", "a non-guaranteed candidate requires one reason");
  }
  let reason = null;
  if (guaranteed) {
    if (source.reason !== null) {
      reject("RCV107", "reason-forbidden", "a statically guaranteed candidate must have reason null");
    }
  } else {
    if (typeof source.reason !== "string" || !REASONS.includes(source.reason)) {
      reject("RCV104", "reason-closed", "reason is outside the closed v1 vocabulary");
    }
    reason = source.reason;
  }
  return Object.freeze({
    ordinary_allocation: flags.ordinary_allocation,
    reuse_candidate: flags.reuse_candidate,
    reuse_statically_guaranteed: flags.reuse_statically_guaranteed,
    reason,
  });
}

function loadAggregate(target) {
  const path = resolve(AGGREGATE_ROOT, `examples/core.${target}.json`);
  let descriptor;
  try {
    descriptor = JSON.parse(readFileSync(path, "utf8"));
  } catch {
    reject("RCV106", "layout-contradiction", "AggregateLayout v1 descriptor is unavailable or invalid");
  }
  if (descriptor.schema !== "kofun.aggregate-layout/v1" || descriptor.target?.name !== target ||
      !Array.isArray(descriptor.layouts)) {
    reject("RCV106", "layout-contradiction", "AggregateLayout v1 descriptor identity is invalid");
  }
  return descriptor;
}

function expectedLayout(descriptor, endpoint, label) {
  const layout = descriptor.layouts.find((candidate) => candidate.id === endpoint.type_id);
  if (layout === undefined || (layout.kind !== "adt" && layout.kind !== "optional")) {
    reject("RCV106", "layout-contradiction", `${label} does not name an AggregateLayout v1 constructor type`);
  }
  const item = layout.constructors?.find((candidate) => candidate.name === endpoint.constructor);
  if (item === undefined) {
    reject("RCV106", "layout-contradiction", `${label} constructor is absent from AggregateLayout v1`);
  }
  return Object.freeze({
    layout_id: layout.id,
    size: layout.size,
    align: layout.align,
    field_count: item.payload === null ? 0 : 1,
    scanning_shape: Object.freeze({ pointers: Object.freeze([...layout.pointers]), drop: layout.drop }),
  });
}

function sameJson(left, right) {
  return JSON.stringify(left) === JSON.stringify(right);
}

function validateLayout(evidence, matchedSource, constructedTarget) {
  const descriptor = loadAggregate(evidence.target_data_layout);
  const expectedSource = expectedLayout(descriptor, matchedSource, "matched_source");
  const expectedTarget = expectedLayout(descriptor, constructedTarget, "constructed_target");
  if (!sameJson(evidence.source_layout, expectedSource)) {
    reject("RCV106", "layout-contradiction", "source_layout differs from AggregateLayout v1");
  }
  if (!sameJson(evidence.target_layout, expectedTarget)) {
    reject("RCV106", "layout-contradiction", "target_layout differs from AggregateLayout v1");
  }
}

function structuralReason(source, target) {
  if (source.size !== target.size) return "incompatible-size";
  if (source.align !== target.align) return "incompatible-alignment";
  if (source.field_count !== target.field_count ||
      !sameJson(source.scanning_shape, target.scanning_shape)) return "incompatible-layout";
  return null;
}

function validateDecision(layout, uniqueness, decision, remark) {
  const structural = structuralReason(layout.source_layout, layout.target_layout);
  if (decision.reuse_statically_guaranteed && uniqueness.proof !== "proved") {
    reject("RCV105", "uniqueness-unproved", "static reuse guarantee requires proved constructor-storage uniqueness");
  }
  if ((decision.reuse_candidate || decision.reuse_statically_guaranteed) && structural !== null) {
    reject("RCV110", "layout-incompatible", `${structural} cannot carry a reuse state`);
  }
  if (decision.ordinary_allocation && structural !== null && decision.reason !== structural) {
    reject("RCV110", "layout-incompatible", `structural mismatch requires reason ${structural}`);
  }
  if (structural === null && decision.reason !== null &&
      decision.reason.startsWith("incompatible-")) {
    reject("RCV110", "layout-incompatible", `${decision.reason} is not supported by the layout evidence`);
  }
  if (decision.reason === "possible-alias" && uniqueness.proof === "proved") {
    reject("RCV108", "provenance-inconsistent", "possible-alias contradicts proved uniqueness");
  }
  const expectedRemark = decision.reuse_statically_guaranteed
    ? REMARKS.guaranteed
    : REMARKS[decision.reason];
  if (remark !== expectedRemark) {
    reject("RCV109", "remark-mismatch", "remark does not match the selected state and reason");
  }
}

export function validateRecord(input) {
  const root = object(input, "$input");
  exactKeys(root, new Set([
    "schema", "candidate_id", "file_id", "matched_source", "constructed_target",
    "layout_evidence", "uniqueness_evidence", "eligibility", "remark",
  ]), "$input");
  requiredKeys(root, [
    "schema", "candidate_id", "file_id", "matched_source", "constructed_target",
    "layout_evidence", "uniqueness_evidence", "eligibility", "remark",
  ], "$input");
  if (root.schema !== SCHEMA) {
    reject("RCV004", "unknown-schema", `schema must be ${SCHEMA}`);
  }
  const matchedSource = constructor(root.matched_source, "$input.matched_source");
  const constructedTarget = constructor(root.constructed_target, "$input.constructed_target");
  const layout = layoutEvidence(root.layout_evidence);
  const uniqueness = uniquenessEvidence(root.uniqueness_evidence);
  const decision = eligibility(root.eligibility);
  const remark = text(root.remark, "$input.remark");
  validateLayout(layout, matchedSource, constructedTarget);
  validateDecision(layout, uniqueness, decision, remark);
  return Object.freeze({
    schema: SCHEMA,
    candidate_id: text(root.candidate_id, "$input.candidate_id", { stable: true }),
    file_id: text(root.file_id, "$input.file_id", { stable: true }),
    matched_source: matchedSource,
    constructed_target: constructedTarget,
    layout_evidence: layout,
    uniqueness_evidence: uniqueness,
    eligibility: decision,
    remark,
  });
}

export function canonical(record) {
  return `${JSON.stringify(record, null, 2)}\n`;
}

function readRecord(path) {
  let bytes;
  try {
    bytes = readFileSync(path);
  } catch {
    reject("RCV001", "invalid-input", "record is unreadable");
  }
  if (bytes.length > LIMITS.inputBytes) {
    reject("RCV003", "limit-exceeded", `record exceeds ${LIMITS.inputBytes} bytes`);
  }
  try {
    return JSON.parse(bytes.toString("utf8"));
  } catch {
    reject("RCV001", "invalid-input", "record is not valid JSON");
  }
}

function main(argv) {
  if (argv.length !== 1) {
    process.stderr.write("usage: validate.mjs RECORD.json\n");
    process.exit(2);
  }
  try {
    process.stdout.write(canonical(validateRecord(readRecord(argv[0]))));
  } catch (error) {
    if (error instanceof ReuseCandidateError) {
      process.stderr.write(`reuse-candidate: ${error.code} ${error.name}: ${error.message}\n`);
      process.exit(1);
    }
    throw error;
  }
}

if (process.argv[1] !== undefined && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main(process.argv.slice(2));
}
