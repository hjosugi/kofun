#!/usr/bin/env node

import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

import {
  PROVENANCES,
  REASONS,
  REMARKS,
  ReuseCandidateError,
  SCHEMA,
  STATES,
  canonical,
  validateRecord,
} from "./validate.mjs";

assert.equal(SCHEMA, "kofun.reuse-candidate/v1");
assert.deepEqual(STATES, [
  "ordinary_allocation",
  "reuse_candidate",
  "reuse_statically_guaranteed",
]);
assert.equal(new Set(STATES).size, 3);
assert.equal(REASONS.length, 11);
assert.equal(new Set(REASONS).size, REASONS.length);
assert.deepEqual(PROVENANCES, [
  "compile-time-ownership-escape-last-use",
  "owned-or-rc-specialized-representation",
  "runtime-uniqueness-check",
  "unproved",
]);
assert.equal(new Set(PROVENANCES).size, PROVENANCES.length);
assert.deepEqual(
  Object.keys(REMARKS).sort(),
  ["guaranteed", ...REASONS].sort(),
  "the unstable remark vocabulary must cover success and every closed reason exactly once",
);
for (const [kind, remark] of Object.entries(REMARKS)) {
  assert.equal(typeof remark, "string", `${kind} remark must be text`);
  assert.ok(remark.startsWith(kind === "guaranteed" ? "reuse guaranteed: " : "reuse refused: "));
}

const base = JSON.parse(readFileSync(new URL("./valid/same-size-unique.json", import.meta.url), "utf8"));
const baseBytes = JSON.stringify(base);
assert.equal(canonical(validateRecord(base)), `${JSON.stringify(base, null, 2)}\n`);
assert.equal(JSON.stringify(base), baseBytes, "validation must not mutate the producer record");

function expectRejected(code, name, mutate) {
  const candidate = structuredClone(base);
  mutate(candidate);
  assert.throws(
    () => validateRecord(candidate),
    (error) => error instanceof ReuseCandidateError && error.code === code && error.name === name,
    `${code} ${name}`,
  );
}

expectRejected("RCV004", "unknown-schema", (candidate) => {
  candidate.schema = "kofun.reuse-candidate/v2";
});
expectRejected("RCV002", "invalid-field", (candidate) => {
  candidate.ambient_path = "/tmp/checkout/source.kofun";
});
expectRejected("RCV109", "remark-mismatch", (candidate) => {
  candidate.remark = "reuse succeeded";
});
expectRejected("RCV108", "provenance-inconsistent", (candidate) => {
  candidate.uniqueness_evidence.proof = "conditional";
});
expectRejected("RCV110", "layout-incompatible", (candidate) => {
  candidate.layout_evidence.target_layout = {
    layout_id: "Shape",
    size: "16",
    align: "8",
    field_count: 1,
    scanning_shape: { pointers: ["8"], drop: "managed" },
  };
  candidate.constructed_target = {
    type_id: "Shape",
    constructor: "Handle",
    span: candidate.constructed_target.span,
  };
});
expectRejected("RCV111", "insufficient-evidence", (candidate) => {
  candidate.uniqueness_evidence.evidence_id = "compiler.ensure_move";
});

process.stdout.write("PASS: state, reason, provenance, and unstable remark vocabularies are closed\n");
process.stdout.write("PASS: schema, unknown-field, remark, provenance, layout-state, and proof mutations fail closed\n");
