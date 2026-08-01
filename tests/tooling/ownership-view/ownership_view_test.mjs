import assert from "node:assert/strict";
import fs from "node:fs";

import { encodeTypedSidecar } from "../../../tooling/typed-sidecar/codec.mjs";
import {
  OWNERSHIP_VIEW_LIMITS,
  canonicalOwnershipViewBytes,
  projectOwnershipView,
} from "../../../tooling/typed-sidecar/ownership-view.mjs";

const id = (digit) => digit.repeat(64);

function completeDocument() {
  return {
    authoritative: false,
    compiler: {
      edition: "kofun-2026",
      semantic_compatibility: "bootstrap-0.3",
    },
    completeness: "complete",
    diagnostics: [],
    file: {
      byte_length: 200,
      content_sha256: id("a"),
      file_id: id("b"),
      logical_path: "src/main.kofun",
      module_id: id("c"),
      package_id: id("d"),
      path_remap_root_id: id("e"),
    },
    generation: { sequence: 17 },
    limits: {
      document_bytes: 16777216,
      max_depth: 128,
      profile: "default-v1",
    },
    nodes: [
      {
        depends_on: [],
        diagnostic_ids: [],
        id: id("1"),
        identities: [{ kind: "ScopeId", value: id("5") }],
        kind: "lexical.scope",
        origin: { display: "authored-source", status: "validated" },
        span: { end: 200, start: 0 },
        status: "validated",
      },
      {
        depends_on: [],
        diagnostic_ids: [],
        id: id("2"),
        identities: [{ kind: "BindingId", value: id("6") }],
        kind: "parameter.binding",
        origin: { display: "authored-source", status: "validated" },
        ownership: { display: "copy", status: "validated" },
        span: { end: 20, start: 10 },
        status: "validated",
        type: { display: "Int", status: "validated" },
      },
      {
        depends_on: [],
        diagnostic_ids: [],
        id: id("3"),
        identities: [{ kind: "ScopeId", value: id("7") }],
        kind: "lexical.scope",
        origin: { display: "authored-source", status: "validated" },
        span: { end: 150, start: 50 },
        status: "validated",
      },
      {
        depends_on: [id("2")],
        diagnostic_ids: [],
        id: id("4"),
        identities: [{ kind: "BindingId", value: id("8") }],
        kind: "local.binding",
        origin: { display: "authored-source", status: "validated" },
        ownership: { display: "owned", status: "validated" },
        span: { end: 70, start: 60 },
        status: "validated",
        type: { display: "Text", status: "validated" },
      },
    ],
    references: [],
    schema: "kofun.typed-sidecar/v1",
    source_status: "checked",
  };
}

function partialDocument() {
  const document = structuredClone(completeDocument());
  document.completeness = "partial";
  document.source_status = "failed";
  const local = document.nodes.find((node) => node.id === id("4"));
  local.status = "error";
  local.diagnostic_ids = [id("9")];
  local.ownership = {
    display: "moved",
    reason: "move-after-borrow",
    status: "error",
  };
  document.nodes.push(
    {
      depends_on: [],
      diagnostic_ids: [],
      id: id("a"),
      identities: [{ kind: "BindingId", value: id("0") }],
      kind: "local.binding",
      span: { end: 90, start: 80 },
      status: "validated",
      type: { display: "Int", status: "validated" },
    },
    {
      depends_on: [],
      diagnostic_ids: [],
      id: id("b"),
      identities: [{ kind: "BindingId", value: id("f") }],
      kind: "local.binding",
      ownership: {
        reason: "unsupported-current-stage2-feature",
        status: "unavailable",
      },
      span: { end: 110, start: 100 },
      status: "unavailable",
      type: {
        reason: "unsupported-current-stage2-feature",
        status: "unavailable",
      },
    },
  );
  document.diagnostics = [{
    affected_ids: [id("4")],
    category: "ownership",
    code: "E2S22",
    fallback_text: "value is unavailable after move",
    id: id("9"),
    primary: {
      file_id: document.file.file_id,
      span: { end: 70, start: 60 },
    },
    related: [],
    remedies: [],
    severity: "error",
    template_id: "move-after-borrow",
    truncated: false,
  }];
  return document;
}

function encode(document) {
  const result = encodeTypedSidecar(document);
  assert.equal(result.ok, true, result.error?.message);
  return result.bytes;
}

function project(document, extra = {}) {
  return projectOwnershipView(encode(document), {
    currentGeneration: document.generation.sequence,
    currentSourceDigest: document.file.content_sha256,
    ...extra,
  });
}

const complete = completeDocument();
let result = project(complete);
assert.equal(result.ok, true, result.error?.message);
const view = result.view;
assert.equal(view.schema, "kofun.ownership-view/v1");
assert.equal(view.authoritative, false);
assert.equal(view.current, true);
assert.equal(view.status, "complete");
assert.equal(view.source_status, "checked");
assert.equal(view.nodes.length, 4);
assert.deepEqual(view.roots, [id("5")]);
const parameter = view.nodes.find((node) => node.id === id("6"));
const innerScope = view.nodes.find((node) => node.id === id("7"));
const local = view.nodes.find((node) => node.id === id("8"));
assert.equal(parameter.parent_scope_id, id("5"));
assert.equal(innerScope.parent_scope_id, id("5"));
assert.equal(local.parent_scope_id, id("7"));
assert.equal(local.ownership.status, "validated");
assert.ok(view.edges.some((edge) =>
  edge.from === id("8") && edge.relation === "depends-on" &&
  edge.to === id("6")));
assert.throws(() => { view.nodes.push({}); }, TypeError);

const firstBytes = canonicalOwnershipViewBytes(view);
const second = project(complete);
assert.equal(second.ok, true);
assert.equal(canonicalOwnershipViewBytes(second.view), firstBytes);

const remapped = structuredClone(complete);
remapped.file.logical_path = "different-checkout/src/main.kofun";
remapped.file.path_remap_root_id = id("f");
result = project(remapped);
assert.equal(result.ok, true);
assert.equal(canonicalOwnershipViewBytes(result.view), firstBytes);
assert.equal("logical_path" in result.view.file, false);
assert.equal("path_remap_root_id" in result.view.file, false);

const partial = partialDocument();
result = project(partial);
assert.equal(result.ok, true, result.error?.message);
assert.equal(result.view.current, true);
assert.equal(result.view.status, "partial");
assert.equal(result.view.reason, "justified-prefix-only");
assert.equal(result.view.nodes.length, 6);
const moved = result.view.nodes.find((node) => node.id === id("8"));
const unknown = result.view.nodes.find((node) => node.id === id("0"));
const unsupported = result.view.nodes.find((node) => node.id === id("f"));
assert.equal(moved.status, "partial");
assert.equal(moved.ownership.status, "partial");
assert.equal(moved.ownership.reason, "move-after-borrow");
assert.equal(unknown.ownership.status, "unknown");
assert.equal(unknown.ownership.reason, "ownership-not-emitted");
assert.equal(unsupported.status, "unsupported");
assert.equal(unsupported.ownership.status, "unsupported");

result = projectOwnershipView(encode(complete), {
  currentGeneration: complete.generation.sequence,
  currentSourceDigest: id("0"),
});
assert.equal(result.ok, true);
assert.equal(result.view.current, false);
assert.equal(result.view.status, "stale");
assert.deepEqual(result.view.nodes, []);

result = projectOwnershipView(encode(complete), {
  currentGeneration: 18,
  currentSourceDigest: complete.file.content_sha256,
});
assert.equal(result.ok, true);
assert.equal(result.view.status, "stale");

const cancelled = partialDocument();
cancelled.source_status = "cancelled";
result = project(cancelled);
assert.equal(result.ok, true);
assert.equal(result.view.current, false);
assert.equal(result.view.status, "cancelled");
assert.deepEqual(result.view.nodes, []);

result = projectOwnershipView(Buffer.from("{"), {
  currentSourceDigest: complete.file.content_sha256,
});
assert.equal(result.ok, false);
assert.equal(result.error.code, "TOV03");
assert.equal(result.error.reason, "invalid-sidecar");
assert.equal("view" in result, false);

result = projectOwnershipView(encode(complete), {});
assert.equal(result.ok, false);
assert.equal(result.error.code, "TOV01");
assert.equal("view" in result, false);

for (const [limits, reason] of [
  [{ nodes: 2 }, "node-limit"],
  [{ edges: 1 }, "edge-limit"],
  [{ depth: 1 }, "depth-limit"],
  [{ documentBytes: 512 }, "byte-limit"],
]) {
  result = project(complete, { limits });
  assert.equal(result.ok, false);
  assert.equal(result.error.code, "TOV04");
  assert.equal(result.error.reason, reason);
  assert.equal("view" in result, false);
}

result = project(complete, {
  limits: { nodes: OWNERSHIP_VIEW_LIMITS.nodes + 1 },
});
assert.equal(result.ok, false);
assert.equal(result.error.code, "TOV01");

const implementation = fs.readFileSync(
  new URL("../../../tooling/typed-sidecar/ownership-view.mjs", import.meta.url),
  "utf8",
);
for (const forbidden of [
  "node:child_process",
  "node:http",
  "node:https",
  "node:net",
  "kif_v1",
  "compiler.c",
]) {
  assert.equal(implementation.includes(forbidden), false);
}

console.log(
  "PASS: validated ownership views preserve bounded ScopeId/BindingId facts",
);
