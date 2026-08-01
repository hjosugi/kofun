import crypto from "node:crypto";

export class PatchError extends Error {
  constructor(code, message) {
    super(`${code}: ${message}`);
    this.name = "PatchError";
    this.code = code;
  }
}

const fail = (code, message) => { throw new PatchError(code, message); };

function exactKeys(value, required, optional = [], code = "UP01") {
  const keys = Object.keys(value).sort();
  const allowed = [...required, ...optional].sort();
  if (canonical(keys) !== canonical(allowed.filter((key) => key in value).sort()) ||
      required.some((key) => !(key in value))) fail(code, "unexpected or missing field");
}

function validDigest(value) {
  return /^[0-9a-f]{64}$/.test(value ?? "");
}

export function canonical(value) {
  if (value === null || typeof value === "boolean" || typeof value === "string") return JSON.stringify(value);
  if (Number.isSafeInteger(value)) return String(value);
  if (Array.isArray(value)) return `[${value.map(canonical).join(",")}]`;
  if (typeof value !== "object" || value === undefined) fail("UP01", "unsupported canonical value");
  return `{${Object.keys(value).sort().map((key) => `${JSON.stringify(key)}:${canonical(value[key])}`).join(",")}}`;
}

export function digest(domain, value) {
  return crypto.createHash("sha256").update(domain).update("\0").update(canonical(value)).digest("hex");
}

function isSubtype(subtypes, child, parent) {
  if (child === parent) return true;
  const seen = new Set([child]);
  const queue = [child];
  while (queue.length > 0) {
    const current = queue.shift();
    for (const [from, to] of subtypes) {
      if (from !== current || seen.has(to)) continue;
      if (to === parent) return true;
      seen.add(to);
      queue.push(to);
    }
  }
  return false;
}

const arrayEqual = (left, right) => canonical(left) === canonical(right);

export function classifyRelation(oldSignature, newSignature, subtypes = []) {
  if (oldSignature.abi !== newSignature.abi) return "abi-changing";
  if (!arrayEqual(oldSignature.parameters.map((item) => item.mode),
    newSignature.parameters.map((item) => item.mode))) return "ownership-changing";
  if (!arrayEqual([...oldSignature.effects].sort(), [...newSignature.effects].sort())) return "effect-changing";
  const oldTypes = oldSignature.parameters.map((item) => item.type);
  const newTypes = newSignature.parameters.map((item) => item.type);
  if (arrayEqual(oldTypes, newTypes) && oldSignature.result === newSignature.result) return "exact-type";
  if (oldTypes.length === newTypes.length &&
      oldTypes.every((type, index) => isSubtype(subtypes, type, newTypes[index])) &&
      isSubtype(subtypes, newSignature.result, oldSignature.result)) return "safe-compatible";
  return "incompatible";
}

export function graphIdentity(graph) {
  const nodes = graph.nodes.map((node) => ({
    complete: node.complete,
    dependsOn: [...node.dependsOn].sort(),
    id: node.id,
    references: [...node.references].sort(),
    spans: [...node.spans].sort((a, b) => a.path.localeCompare(b.path) || a.start - b.start),
  })).sort((a, b) => a.id.localeCompare(b.id));
  return digest("kofun.semantic-dependency-graph/v1", { nodes, schema: graph.schema });
}

export function computeFrontier(replacement, graph, limits = {
  maxGraphEdges: 500000,
  maxGraphNodes: 100000,
}) {
  if (graph.schema !== "kofun.semantic-dependency-graph/v1") fail("UP10", "unsupported graph schema");
  if (graph.nodes.length > limits.maxGraphNodes) fail("UP13", "graph node limit exceeded");
  const byId = new Map(graph.nodes.map((node) => [node.id, node]));
  if (byId.size !== graph.nodes.length) fail("UP11", "duplicate graph node");
  const reverse = new Map(graph.nodes.map((node) => [node.id, []]));
  let edgeCount = 0;
  for (const node of graph.nodes) {
    for (const dependency of node.dependsOn) {
      edgeCount += 1;
      if (edgeCount > limits.maxGraphEdges) fail("UP14", "graph edge limit exceeded");
      if (!byId.has(dependency)) fail("UP12", `unknown graph dependency ${dependency}`);
      reverse.get(dependency).push(node.id);
    }
  }
  const statuses = new Map();
  const direct = [];
  for (const node of graph.nodes) {
    if (node.references.includes(replacement.new.termId) && !node.references.includes(replacement.old.termId)) {
      statuses.set(node.id, "already-migrated");
    }
    if (node.references.includes(replacement.old.termId)) direct.push(node.id);
  }
  for (const id of direct.sort()) {
    const node = byId.get(id);
    const safe = replacement.relation === "exact-type" && replacement.rewrite === "rename-callee" &&
      node.complete && node.spans.length > 0;
    statuses.set(id, safe ? "covered" : node.complete ? "blocked" : "unresolved");
  }
  const queue = [...direct];
  const seen = new Set(queue);
  while (queue.length > 0) {
    const current = queue.shift();
    for (const dependent of reverse.get(current)) {
      if (!statuses.has(dependent)) statuses.set(dependent, "escaped");
      if (!seen.has(dependent)) { seen.add(dependent); queue.push(dependent); }
    }
  }
  const rows = [...statuses].map(([id, status]) => ({ id, status }))
    .sort((a, b) => a.id.localeCompare(b.id));
  const count = (status) => rows.filter((row) => row.status === status).length;
  const denominator = rows.length;
  return {
    graphIdentity: graphIdentity(graph),
    progress: denominator === 0 ? 1 : (count("covered") + count("already-migrated")) / denominator,
    rows,
    totals: {
      alreadyMigrated: count("already-migrated"), blocked: count("blocked"),
      covered: count("covered"), escaped: count("escaped"), unresolved: count("unresolved"),
    },
  };
}

export function validatePatch(patch) {
  exactKeys(patch, [
    "conflicts", "limits", "provenance", "replacements", "revision", "schema",
    "subtypes", "workspace",
  ]);
  if (patch.schema !== "kofun.upgrade-patch/v1") fail("UP02", "unsupported patch schema");
  if (!Number.isSafeInteger(patch.revision) || patch.revision < 1) fail("UP03", "invalid revision");
  exactKeys(patch.limits, [
    "maxEdits", "maxGraphEdges", "maxGraphNodes", "maxReplacements", "maxTextBytes",
  ]);
  for (const [name, value] of Object.entries(patch.limits)) {
    if (!Number.isSafeInteger(value) || value < 0 || (name !== "maxGraphEdges" && value < 1)) {
      fail("UP03", `invalid limit ${name}`);
    }
  }
  if (patch.replacements.length > patch.limits.maxReplacements) fail("UP04", "replacement limit exceeded");
  if (!validDigest(patch.workspace.symbolId) || !validDigest(patch.workspace.termId)) {
    fail("UP03", "invalid workspace identity");
  }
  const validated = structuredClone(patch);
  const ids = new Set();
  const oldTargets = new Map();
  for (const replacement of validated.replacements) {
    exactKeys(replacement, [
      "dependencies", "id", "kind", "new", "newSignature", "old", "oldSignature",
      "provenance", "relation", "rewrite",
    ], ["supersedes"]);
    if (!validDigest(replacement.old.symbolId) || !validDigest(replacement.old.termId) ||
        !validDigest(replacement.new.symbolId) || !validDigest(replacement.new.termId)) {
      fail("UP03", `invalid replacement identity ${replacement.id}`);
    }
    if (ids.has(replacement.id)) fail("UP05", `duplicate replacement ${replacement.id}`);
    ids.add(replacement.id);
    const oldKey = canonical(replacement.old);
    if (oldTargets.has(oldKey) && oldTargets.get(oldKey) !== canonical(replacement.new)) {
      fail("UP06", "same-old replacement conflict");
    }
    oldTargets.set(oldKey, canonical(replacement.new));
    if (replacement.relation !== classifyRelation(replacement.oldSignature,
      replacement.newSignature, validated.subtypes)) fail("UP07", `relation evidence mismatch for ${replacement.id}`);
  }
  const edges = new Map(validated.replacements.map((item) => [item.old.termId, item.new.termId]));
  for (const start of edges.keys()) {
    const seen = new Set([start]);
    let current = start;
    while (edges.has(current)) {
      current = edges.get(current);
      if (seen.has(current)) fail("UP08", "replacement cycle");
      seen.add(current);
    }
  }
  return validated;
}

export function composePatches(left, right) {
  if (left.schema !== right.schema) fail("UP20", "schema conflict");
  const combined = structuredClone(left);
  combined.revision = Math.max(left.revision, right.revision);
  const byId = new Map(combined.replacements.map((item) => [item.id, item]));
  for (const replacement of right.replacements) {
    if (byId.has(replacement.id)) {
      const existing = byId.get(replacement.id);
      if (canonical(existing) === canonical(replacement)) continue;
      if (replacement.supersedes === existing.id && right.revision > left.revision) {
        const index = combined.replacements.findIndex((item) => item.id === existing.id);
        combined.replacements[index] = structuredClone(replacement);
        byId.set(replacement.id, combined.replacements[index]);
        continue;
      }
      fail("UP21", `replacement id conflict ${replacement.id}`);
    }
    combined.replacements.push(structuredClone(replacement));
  }
  combined.replacements.sort((a, b) => a.id.localeCompare(b.id));
  return validatePatch(combined);
}

export function previewEdits(workspace, graph, replacement, edits) {
  if (workspace.graphIdentity !== graphIdentity(graph)) fail("UP30", "stale semantic graph");
  if (edits.length > workspace.limits.maxEdits) fail("UP31", "edit limit exceeded");
  if (workspace.files.length > workspace.limits.maxFiles) fail("UP31", "file limit exceeded");
  const textBytes = workspace.files.reduce((sum, file) => sum + Buffer.byteLength(file.text), 0);
  if (textBytes > workspace.limits.maxTextBytes) fail("UP31", "workspace text limit exceeded");
  const files = new Map(workspace.files.map((file) => [file.path, file]));
  const normalized = edits.map((edit) => {
    if (Buffer.byteLength(edit.path) > workspace.limits.maxPathBytes) fail("UP32", "path limit exceeded");
    if (edit.path.startsWith("/") || edit.path.includes("..") || edit.path.includes("\\")) {
      fail("UP32", "path must be normalized and repository-relative");
    }
    const file = files.get(edit.path);
    if (!file) fail("UP33", `unknown file ${edit.path}`);
    if (digest("kofun.workspace-file/v1", file.text) !== file.digest) fail("UP34", `stale file ${edit.path}`);
    if (file.text.slice(edit.start, edit.end) !== edit.expectedText) fail("UP35", `source text mismatch ${edit.path}`);
    return { ...edit };
  }).sort((a, b) => a.path.localeCompare(b.path) || a.start - b.start || a.end - b.end);
  for (let index = 1; index < normalized.length; index += 1) {
    if (normalized[index - 1].path === normalized[index].path &&
        normalized[index - 1].end > normalized[index].start) fail("UP36", "overlapping edits");
  }
  const autoApply = replacement.relation === "exact-type" && replacement.rewrite === "rename-callee";
  return {
    autoApply,
    edits: normalized,
    graphIdentity: workspace.graphIdentity,
    patchText: normalized.map((edit) =>
      `--- a/${edit.path}\n+++ b/${edit.path}\n@@ bytes ${edit.start}..${edit.end} @@\n-${edit.expectedText}\n+${edit.replacement}`)
      .join("\n"),
    schema: "kofun.upgrade-preview/v1",
  };
}

export function applyAtomically(workspace, preview, currentGraphIdentity, validator) {
  if (preview.schema !== "kofun.upgrade-preview/v1") fail("UP40", "unsupported preview schema");
  if (!preview.autoApply) fail("UP41", "preview requires manual review");
  if (preview.graphIdentity !== currentGraphIdentity || workspace.graphIdentity !== currentGraphIdentity) {
    fail("UP30", "stale semantic graph");
  }
  const original = structuredClone(workspace);
  const candidate = structuredClone(workspace);
  const grouped = new Map();
  for (const edit of preview.edits) {
    if (!grouped.has(edit.path)) grouped.set(edit.path, []);
    grouped.get(edit.path).push(edit);
  }
  for (const [filePath, edits] of grouped) {
    const file = candidate.files.find((item) => item.path === filePath);
    if (!file) fail("UP33", `unknown file ${filePath}`);
    if (digest("kofun.workspace-file/v1", file.text) !== file.digest) fail("UP34", `stale file ${filePath}`);
    for (const edit of edits.slice().sort((a, b) => b.start - a.start)) {
      if (file.text.slice(edit.start, edit.end) !== edit.expectedText) fail("UP35", `source text mismatch ${filePath}`);
      file.text = file.text.slice(0, edit.start) + edit.replacement + file.text.slice(edit.end);
    }
    file.digest = digest("kofun.workspace-file/v1", file.text);
  }
  if (!validator(candidate)) fail("UP42", "target-branch validation failed");
  candidate.graphIdentity = null;
  return { undo: original, workspace: candidate };
}

export function rollback(result) {
  if (!result?.undo) fail("UP43", "undo snapshot is absent");
  return structuredClone(result.undo);
}
