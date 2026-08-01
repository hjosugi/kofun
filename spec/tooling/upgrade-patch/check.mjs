import assert from "node:assert/strict";
import fs from "node:fs";

import {
  PatchError,
  applyAtomically,
  canonical,
  classifyRelation,
  composePatches,
  computeFrontier,
  digest,
  graphIdentity,
  previewEdits,
  rollback,
  validatePatch,
} from "./model.mjs";

const sha = (letter) => letter.repeat(64);
const signature = (overrides = {}) => ({
  abi: "c-v1", effects: [], parameters: [{ mode: "read", type: "Int" }], result: "Int", ...overrides,
});

assert.equal(classifyRelation(signature(), signature()), "exact-type");
assert.equal(classifyRelation(signature({ parameters: [{ mode: "read", type: "PositiveInt" }] }),
  signature(), [["PositiveInt", "Int"]]), "safe-compatible");
assert.equal(classifyRelation(signature(), signature({ parameters: [{ mode: "take", type: "Int" }] })),
  "ownership-changing");
assert.equal(classifyRelation(signature(), signature({ effects: ["io"] })), "effect-changing");
assert.equal(classifyRelation(signature(), signature({ abi: "c-v2" })), "abi-changing");
assert.equal(classifyRelation(signature(), signature({ result: "Text" })), "incompatible");

function replacement(id = "r1", relation = "exact-type", rewrite = "rename-callee") {
  const oldSignature = signature();
  let newSignature = signature();
  if (relation === "effect-changing") newSignature = signature({ effects: ["io"] });
  if (relation === "ownership-changing") newSignature = signature({ parameters: [{ mode: "take", type: "Int" }] });
  if (relation === "abi-changing") newSignature = signature({ abi: "c-v2" });
  if (relation === "incompatible") newSignature = signature({ result: "Text" });
  if (relation === "safe-compatible") {
    oldSignature.parameters[0].type = "PositiveInt";
    newSignature.parameters[0].type = "Int";
  }
  return {
    dependencies: [], id, kind: "term-replacement",
    new: { symbolId: sha("b"), termId: sha("c") }, newSignature,
    old: { symbolId: sha("a"), termId: sha("d") }, oldSignature,
    provenance: "issue-741-fixture", relation, rewrite,
  };
}

function patch(rows = [replacement()], revision = 1) {
  return {
    conflicts: [], limits: {
      maxEdits: 64, maxGraphEdges: 1024, maxGraphNodes: 256,
      maxReplacements: 32, maxTextBytes: 1048576,
    }, provenance: "fixture",
    replacements: rows, revision, schema: "kofun.upgrade-patch/v1",
    subtypes: [["PositiveInt", "Int"]], workspace: { symbolId: sha("e"), termId: sha("f") },
  };
}
assert.equal(validatePatch(patch()).replacements[0].relation, "exact-type");
assert.equal(validatePatch(patch([replacement("effect", "effect-changing", "preview-only")]))
  .replacements[0].relation, "effect-changing");

const conflict = patch([replacement("r1")]);
conflict.replacements.push({ ...replacement("r2"), new: { symbolId: sha("9"), termId: sha("8") } });
assert.throws(() => validatePatch(conflict),
  (error) => error instanceof PatchError && error.code === "UP06");
const cycleA = replacement("cycle-a");
cycleA.old.termId = sha("1"); cycleA.new.termId = sha("2");
const cycleB = replacement("cycle-b");
cycleB.old.termId = sha("2"); cycleB.new.termId = sha("1");
assert.throws(() => validatePatch(patch([cycleA, cycleB])),
  (error) => error instanceof PatchError && error.code === "UP08");

const disjoint = replacement("r2");
disjoint.old = { symbolId: sha("3"), termId: sha("4") };
disjoint.new = { symbolId: sha("5"), termId: sha("6") };
const composed = composePatches(patch(), patch([disjoint], 2));
assert.deepEqual(composed.replacements.map((item) => item.id), ["r1", "r2"]);
const chain = replacement("r3");
chain.old = structuredClone(disjoint.new);
chain.new = { symbolId: sha("7"), termId: sha("8") };
assert.equal(composePatches(composed, patch([chain], 3)).replacements.length, 3);
const superseding = structuredClone(replacement("r1"));
superseding.supersedes = "r1";
superseding.provenance = "newer-review";
assert.equal(composePatches(patch(), patch([superseding], 4)).replacements[0].provenance,
  "newer-review");

const exact = replacement();
const graph = {
  nodes: [
    { complete: true, dependsOn: [], id: "direct-safe", references: [exact.old.termId], spans: [{ end: 8, path: "src/a.kofun", start: 3 }] },
    { complete: false, dependsOn: [], id: "direct-partial", references: [exact.old.termId], spans: [{ end: 8, path: "src/b.kofun", start: 3 }] },
    { complete: true, dependsOn: ["direct-partial"], id: "escaped", references: [], spans: [] },
    { complete: true, dependsOn: [], id: "done", references: [exact.new.termId], spans: [{ end: 8, path: "src/c.kofun", start: 3 }] },
  ],
  schema: "kofun.semantic-dependency-graph/v1",
};
const frontier = computeFrontier(exact, graph);
assert.deepEqual(frontier.totals,
  { alreadyMigrated: 1, blocked: 0, covered: 1, escaped: 1, unresolved: 1 });
assert.equal(frontier.progress, 0.5);
const unsafeFrontier = computeFrontier(replacement("unsafe", "effect-changing", "preview-only"), graph);
assert.equal(unsafeFrontier.totals.blocked, 1);
assert.equal(unsafeFrontier.totals.unresolved, 1);
assert.ok(unsafeFrontier.progress < frontier.progress);
assert.throws(() => computeFrontier(exact, graph, { maxGraphEdges: 0, maxGraphNodes: 10 }),
  (error) => error instanceof PatchError && error.code === "UP14");
assert.throws(() => computeFrontier(exact, graph, { maxGraphEdges: 10, maxGraphNodes: 1 }),
  (error) => error instanceof PatchError && error.code === "UP13");

const text = "let value = old_name(41)\n";
const workspace = {
  files: [{ digest: digest("kofun.workspace-file/v1", text), path: "src/main.kofun", text }],
  graphIdentity: graphIdentity(graph),
  limits: { maxEdits: 8, maxFiles: 8, maxPathBytes: 256, maxTextBytes: 4096 },
  schema: "kofun.workspace-model/v1",
};
const start = text.indexOf("old_name");
const preview = previewEdits(workspace, graph, exact, [{
  end: start + "old_name".length, expectedText: "old_name", path: "src/main.kofun",
  replacement: "new_name", start,
}]);
assert.equal(preview.autoApply, true);
assert.ok(preview.patchText.includes("--- a/src/main.kofun"));
assert.ok(preview.patchText.includes("@@ bytes"));

const before = structuredClone(workspace);
const applied = applyAtomically(workspace, preview, workspace.graphIdentity,
  (candidate) => candidate.files[0].text.includes("new_name"));
assert.ok(applied.workspace.files[0].text.includes("new_name"));
assert.equal(applied.workspace.graphIdentity, null);
assert.deepEqual(workspace, before, "input workspace must not be mutated");
assert.deepEqual(rollback(applied), before);

assert.throws(() => applyAtomically(workspace, preview, workspace.graphIdentity, () => false),
  (error) => error instanceof PatchError && error.code === "UP42");
assert.deepEqual(workspace, before, "validation failure must leave workspace unchanged");
assert.throws(() => applyAtomically(workspace, preview, sha("0"), () => true),
  (error) => error instanceof PatchError && error.code === "UP30");
assert.deepEqual(workspace, before, "stale apply must leave workspace unchanged");

const reviewOnly = previewEdits(workspace, graph,
  replacement("unsafe", "ownership-changing", "preview-only"), [{
    end: start + "old_name".length, expectedText: "old_name", path: "src/main.kofun",
    replacement: "new_name", start,
  }]);
assert.equal(reviewOnly.autoApply, false);
assert.throws(() => applyAtomically(workspace, reviewOnly, workspace.graphIdentity, () => true),
  (error) => error instanceof PatchError && error.code === "UP41");
const compatible = replacement("compatible", "safe-compatible", "preview-only");
assert.equal(validatePatch(patch([compatible])).replacements[0].relation, "safe-compatible");
const compatiblePreview = previewEdits(workspace, graph, compatible, [{
  end: start + "old_name".length, expectedText: "old_name", path: "src/main.kofun",
  replacement: "new_name", start,
}]);
assert.equal(compatiblePreview.autoApply, false);

const afterPartialGraph = structuredClone(graph);
afterPartialGraph.nodes[0].references = [exact.new.termId];
const afterPartial = computeFrontier(exact, afterPartialGraph);
assert.equal(afterPartial.totals.alreadyMigrated, 2);
assert.equal(afterPartial.totals.unresolved, 1);
assert.equal(afterPartial.totals.escaped, 1);
assert.notEqual(afterPartial.graphIdentity, frontier.graphIdentity);

const schema = JSON.parse(fs.readFileSync(new URL("./schema.json", import.meta.url), "utf8"));
assert.equal(schema.properties.schema.const, "kofun.upgrade-patch/v1");
for (const required of ["term-replacement", "type-replacement", "deprecation"])
  assert.ok(schema.$defs.replacement.properties.kind.enum.includes(required));
assert.ok(canonical(schema).includes("maxGraphNodes"));

console.log("upgrade-patch: schema, relation matrix, composition, cycle, and supersession: PASS");
console.log("upgrade-patch: complete frontier keeps covered, blocked, escaped, migrated, and unresolved visible: PASS");
console.log("upgrade-patch: deterministic preview, staleness, atomic validation, and undo: PASS");
