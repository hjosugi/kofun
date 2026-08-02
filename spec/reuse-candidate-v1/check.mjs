#!/usr/bin/env node

import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

import {
  DISPOSITIONS,
  OBSERVERS,
  OBSERVER_REASONS,
  PROVENANCES,
  REASONS,
  REMARKS,
  ReuseCandidateError,
  SCHEMA,
  STATES,
  WRITE_ORDERINGS,
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
assert.deepEqual(OBSERVERS, [
  "borrowed_view",
  "closure_capture",
  "weak_reference",
  "foreign_pointer",
  "alias",
]);
assert.equal(new Set(OBSERVERS).size, OBSERVERS.length);
assert.deepEqual(
  Object.values(OBSERVER_REASONS).sort(),
  [...OBSERVERS].sort(),
  "every observer must be reachable from exactly one closed disqualifying reason",
);
for (const reason of Object.keys(OBSERVER_REASONS)) {
  assert.ok(REASONS.includes(reason), `${reason} must be a closed v1 reason`);
}
assert.deepEqual(WRITE_ORDERINGS, ["evaluate-all-then-write", "interleaved", "unknown"]);
assert.deepEqual(DISPOSITIONS, ["honour", "reject-program", "weaken-to-allocation", "not-applicable"]);

const backends = JSON.parse(readFileSync(new URL("./backends.json", import.meta.url), "utf8"));
assert.equal(backends.schema, "kofun.reuse-backend-support/v1");
assert.ok(backends.backends.length > 0, "the backend support table must not be empty");
assert.equal(
  backends.backends.filter((row) => row.reuse_support === "honours-guarantee").length,
  0,
  "no registered backend honours a reuse guarantee at the audited commit; a row that claims one must arrive with the rewrite that implements it",
);
// The table claims to be keyed by the registered backends. That claim is only
// worth making if it is joined: without this, a phantom backend can be added
// here and cited by a valid vector, and every assertion below still passes.
// The join is an exact set equality in both directions -- an id here that is
// not registered is a backend that does not exist, and a registered backend
// missing here is a backend whose reuse disposition nobody has stated.
const capabilities = readFileSync(
  new URL("../../tests/conformance/capabilities.tsv", import.meta.url),
  "utf8",
);
const registered = new Set(
  capabilities
    .split("\n")
    .slice(1)
    .filter((line) => line.trim() !== "" && !line.startsWith("#"))
    .map((line) => line.split("\t")[0]),
);
// Factored out so the two halves can be exercised against synthetic sets
// below. Asserting only the committed green case would let a later change
// weaken or delete either half without CI noticing.
function joinBackendSets(tabled, registered) {
  const phantom = [...tabled].filter((id) => !registered.has(id)).sort();
  const unstated = [...registered].filter((id) => !tabled.has(id)).sort();
  assert.deepEqual(
    phantom,
    [],
    `backends.json names ${phantom.join(", ")}, which tests/conformance/capabilities.tsv does not register; a record may not cite a backend that does not exist`,
  );
  assert.deepEqual(
    unstated,
    [],
    `tests/conformance/capabilities.tsv registers ${unstated.join(", ")}, which backends.json does not state a reuse disposition for`,
  );
}

const tabled = new Set(backends.backends.map((row) => row.id));
joinBackendSets(tabled, registered);

// Both halves, proved to fire. A phantom row and an omitted registered row are
// distinct failures with distinct messages, so each is pinned separately.
assert.throws(
  () => joinBackendSets(new Set([...tabled, "phantom-backend"]), registered),
  /backends\.json names phantom-backend, which tests\/conformance\/capabilities\.tsv does not register/,
  "an extra backend id must be refused; without this the join can be weakened to a one-way check",
);
const omitted = [...registered][0];
assert.throws(
  () => joinBackendSets(new Set([...tabled].filter((id) => id !== omitted)), registered),
  new RegExp(`capabilities\\.tsv registers ${omitted}, which backends\\.json does not state`),
  "an omitted registered backend must be refused; the no-backend-honours-a-guarantee claim is only as strong as the set it ranges over",
);

assert.equal(
  new Set(backends.backends.map((row) => row.id)).size,
  backends.backends.length,
  "backend ids must be unique",
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

// Structural sub-cases of RCV110. The committed AggregateLayout v1 corpus has no
// two constructor types that differ in size or alignment, so those two branches
// are exercised here rather than by a committed vector; the scanning-shape and
// field-count branches are reachable from the corpus and the first has a vector.
expectRejected("RCV106", "layout-contradiction", (candidate) => {
  candidate.layout_evidence.target_layout.size = "24";
});
expectRejected("RCV106", "layout-contradiction", (candidate) => {
  candidate.layout_evidence.target_layout.align = "16";
});
expectRejected("RCV110", "layout-incompatible", (candidate) => {
  candidate.constructed_target.constructor = "Missing";
  candidate.layout_evidence.target_layout.field_count = 0;
});

// Backend honourability and observer conditions.
expectRejected("RCV112", "backend-unknown", (candidate) => {
  candidate.backend_support.backend_id = "backend-that-is-not-registered";
});
expectRejected("RCV112", "backend-unknown", (candidate) => {
  candidate.backend_support.backend_id = "native-aarch64";
});
expectRejected("RCV116", "disposition-inapplicable", (candidate) => {
  candidate.backend_support.guarantee_disposition = "not-applicable";
});
expectRejected("RCV116", "disposition-inapplicable", (candidate) => {
  candidate.eligibility = {
    ordinary_allocation: true,
    reuse_candidate: false,
    reuse_statically_guaranteed: false,
    reason: "pinned",
  };
  candidate.remark = REMARKS.pinned;
});
for (const observer of OBSERVERS) {
  for (const state of ["live", "unknown"]) {
    expectRejected("RCV117", "observer-live", (candidate) => {
      candidate.observers[observer] = state;
    });
  }
}
for (const ordering of ["interleaved", "unknown"]) {
  expectRejected("RCV119", "write-ordering-unsafe", (candidate) => {
    candidate.write_ordering = ordering;
  });
}
for (const [reason, observer] of Object.entries(OBSERVER_REASONS)) {
  expectRejected("RCV118", "observer-reason-contradiction", (candidate) => {
    candidate.uniqueness_evidence = { proof: "unproved", provenance: "unproved", evidence_id: null };
    candidate.eligibility = {
      ordinary_allocation: false,
      reuse_candidate: true,
      reuse_statically_guaranteed: false,
      reason,
    };
    candidate.remark = REMARKS[reason];
    candidate.observers[observer] = "absent";
    candidate.backend_support.guarantee_disposition = "not-applicable";
  });
}

process.stdout.write("PASS: state, reason, provenance, observer, ordering, and unstable remark vocabularies are closed\n");
process.stdout.write("PASS: schema, unknown-field, remark, provenance, layout-state, and proof mutations fail closed\n");
process.stdout.write("PASS: every observer, every unsafe ordering, and every unhonourable backend disposition refuses a static guarantee\n");
