import {
  canonicalTypedSidecarBytes,
  readTypedSidecar,
} from "./codec.mjs";

export const OWNERSHIP_VIEW_LIMITS = Object.freeze({
  nodes: 4096,
  edges: 65536,
  depth: 128,
  documentBytes: 4 * 1024 * 1024,
});

const HEX_ID = /^[0-9a-f]{64}$/;
const RELEVANT_IDENTITIES = new Set(["ScopeId", "BindingId"]);

class OwnershipViewFailure extends Error {
  constructor(code, message, reason) {
    super(message);
    this.name = "OwnershipViewFailure";
    this.code = code;
    this.reason = reason;
  }
}

function fail(code, message, reason) {
  throw new OwnershipViewFailure(code, message, reason);
}

function errorResult(error) {
  const code = error instanceof OwnershipViewFailure ? error.code : "TOV03";
  const message = error instanceof OwnershipViewFailure ?
    error.message : "ownership view projection failed";
  const reason = error instanceof OwnershipViewFailure ?
    error.reason : "projection-failed";
  return Object.freeze({
    ok: false,
    error: Object.freeze({
      code,
      message: message.length <= 256 ? message : message.slice(0, 253) + "...",
      reason,
    }),
  });
}

function deepFreeze(root) {
  const pending = [root];
  const seen = new WeakSet();
  while (pending.length > 0) {
    const value = pending.pop();
    if (value === null || typeof value !== "object" || seen.has(value)) continue;
    seen.add(value);
    for (const child of Array.isArray(value) ? value : Object.values(value)) {
      pending.push(child);
    }
    Object.freeze(value);
  }
  return root;
}

function configuredLimits(overrides = {}) {
  if (overrides === null || typeof overrides !== "object" ||
      Array.isArray(overrides)) {
    fail("TOV01", "ownership view limits must be an object", "invalid-limits");
  }
  const result = {};
  for (const [name, maximum] of Object.entries(OWNERSHIP_VIEW_LIMITS)) {
    const value = overrides[name] ?? maximum;
    if (!Number.isSafeInteger(value) || value <= 0 || value > maximum) {
      fail(
        "TOV01",
        "ownership view limit " + name + " must be a positive integer at most " +
          maximum,
        "invalid-limits",
      );
    }
    result[name] = value;
  }
  return Object.freeze(result);
}

function factView(fact, missingReason) {
  if (fact === undefined) {
    return Object.freeze({ status: "unknown", reason: missingReason });
  }
  const result = {};
  if (fact.status === "validated") {
    result.status = "validated";
  } else if (fact.status === "unavailable" &&
      typeof fact.reason === "string" && fact.reason.startsWith("unsupported")) {
    result.status = "unsupported";
  } else if (fact.status === "unavailable") {
    result.status = "unknown";
  } else {
    result.status = "partial";
    result.source_status = fact.status;
  }
  if (typeof fact.display === "string") result.display = fact.display;
  if (typeof fact.reason === "string") result.reason = fact.reason;
  if (result.status === "unknown" && !result.reason) result.reason = missingReason;
  return Object.freeze(result);
}

function nodeStatus(node) {
  if (node.status === "validated") return "validated";
  if (node.status === "unavailable" &&
      node.ownership?.reason?.startsWith("unsupported")) {
    return "unsupported";
  }
  if (node.status === "unavailable") return "unknown";
  return "partial";
}

function contains(outer, inner) {
  return outer.start <= inner.start && outer.end >= inner.end &&
    (outer.start < inner.start || outer.end > inner.end);
}

function identityFor(node) {
  const identities = node.identities.filter((identity) =>
    RELEVANT_IDENTITIES.has(identity.kind));
  if (identities.length === 0) return undefined;
  if (identities.length !== 1) {
    fail(
      "TOV03",
      "one sidecar node carries multiple ScopeId or BindingId identities",
      "ambiguous-identity",
    );
  }
  return identities[0];
}

function projectedEntities(document, limits) {
  const entities = [];
  for (const node of document.nodes) {
    const identity = identityFor(node);
    if (!identity) continue;
    entities.push({
      id: identity.value,
      identity_kind: identity.kind,
      sidecar_node_id: node.id,
      kind: node.kind,
      span: { start: node.span.start, end: node.span.end },
      status: nodeStatus(node),
      ownership: factView(node.ownership, "ownership-not-emitted"),
      type: factView(node.type, "type-not-emitted"),
      origin: factView(node.origin, "origin-not-emitted"),
      effect: factView(node.effect, "effect-not-emitted"),
      diagnostic_ids: [...node.diagnostic_ids],
      dependency_node_ids: [...node.depends_on],
      parent_scope_id: null,
    });
  }
  if (entities.length > limits.nodes) {
    fail("TOV04", "ownership view node limit exceeded", "node-limit");
  }
  entities.sort((left, right) =>
    left.span.start - right.span.start ||
    right.span.end - left.span.end ||
    left.identity_kind.localeCompare(right.identity_kind) ||
    left.id.localeCompare(right.id));

  const scopes = entities.filter((entity) => entity.identity_kind === "ScopeId");
  for (const entity of entities) {
    const parents = scopes.filter((scope) =>
      scope.id !== entity.id && contains(scope.span, entity.span));
    parents.sort((left, right) =>
      (left.span.end - left.span.start) - (right.span.end - right.span.start) ||
      left.span.start - right.span.start ||
      left.id.localeCompare(right.id));
    if (parents.length > 0) entity.parent_scope_id = parents[0].id;
  }
  return entities;
}

function projectedEdges(entities, limits) {
  const bySidecarNode = new Map(
    entities.map((entity) => [entity.sidecar_node_id, entity]),
  );
  const edges = [];
  for (const entity of entities) {
    if (entity.parent_scope_id !== null) {
      edges.push({
        from: entity.parent_scope_id,
        relation: "contains",
        to: entity.id,
      });
    }
    for (const dependency of entity.dependency_node_ids) {
      const target = bySidecarNode.get(dependency);
      if (target) {
        edges.push({
          from: entity.id,
          relation: "depends-on",
          to: target.id,
        });
      }
    }
  }
  if (edges.length > limits.edges) {
    fail("TOV04", "ownership view edge limit exceeded", "edge-limit");
  }
  edges.sort((left, right) =>
    left.from.localeCompare(right.from) ||
    left.relation.localeCompare(right.relation) ||
    left.to.localeCompare(right.to));
  return edges;
}

function requireBoundedDepth(entities, limits) {
  const byId = new Map(entities.map((entity) => [entity.id, entity]));
  for (const entity of entities) {
    let depth = 0;
    let cursor = entity;
    const seen = new Set([entity.id]);
    while (cursor.parent_scope_id !== null) {
      depth += 1;
      if (depth > limits.depth) {
        fail("TOV04", "ownership view depth limit exceeded", "depth-limit");
      }
      if (seen.has(cursor.parent_scope_id)) {
        fail("TOV03", "ownership view scope cycle detected", "scope-cycle");
      }
      seen.add(cursor.parent_scope_id);
      cursor = byId.get(cursor.parent_scope_id);
      if (!cursor) {
        fail("TOV03", "ownership view parent scope is absent", "missing-scope");
      }
    }
  }
}

function publicEntity(entity) {
  return Object.freeze({
    diagnostic_ids: Object.freeze(entity.diagnostic_ids),
    effect: entity.effect,
    id: entity.id,
    identity_kind: entity.identity_kind,
    kind: entity.kind,
    origin: entity.origin,
    ownership: entity.ownership,
    parent_scope_id: entity.parent_scope_id,
    span: Object.freeze(entity.span),
    status: entity.status,
    type: entity.type,
  });
}

function emptyView(document, limits, status, reason) {
  return {
    authoritative: false,
    current: false,
    edges: [],
    file: {
      content_sha256: document.file.content_sha256,
      file_id: document.file.file_id,
      module_id: document.file.module_id,
      package_id: document.file.package_id,
    },
    generation: { sequence: document.generation.sequence },
    limits: {
      depth: limits.depth,
      document_bytes: limits.documentBytes,
      edges: limits.edges,
      nodes: limits.nodes,
      profile: "ownership-view-v1",
    },
    nodes: [],
    reason,
    roots: [],
    schema: "kofun.ownership-view/v1",
    source_status: document.source_status,
    status,
  };
}

function buildView(document, context, limits) {
  if (typeof context.currentSourceDigest !== "string" ||
      !HEX_ID.test(context.currentSourceDigest)) {
    fail(
      "TOV01",
      "currentSourceDigest must be a lowercase SHA-256 digest",
      "missing-current-source",
    );
  }
  if (context.currentGeneration !== undefined &&
      (!Number.isSafeInteger(context.currentGeneration) ||
       context.currentGeneration < 0)) {
    fail(
      "TOV01",
      "currentGeneration must be a non-negative safe integer",
      "invalid-generation",
    );
  }

  if (document.file.content_sha256 !== context.currentSourceDigest ||
      (context.currentGeneration !== undefined &&
       document.generation.sequence !== context.currentGeneration)) {
    return emptyView(document, limits, "stale", "source-or-generation-mismatch");
  }
  if (document.source_status === "cancelled") {
    return emptyView(document, limits, "cancelled", "cancelled-input");
  }

  const entities = projectedEntities(document, limits);
  requireBoundedDepth(entities, limits);
  const edges = projectedEdges(entities, limits);
  const roots = entities
    .filter((entity) =>
      entity.identity_kind === "ScopeId" && entity.parent_scope_id === null)
    .map((entity) => entity.id)
    .sort();
  const view = {
    authoritative: false,
    current: true,
    edges,
    file: {
      content_sha256: document.file.content_sha256,
      file_id: document.file.file_id,
      module_id: document.file.module_id,
      package_id: document.file.package_id,
    },
    generation: { sequence: document.generation.sequence },
    limits: {
      depth: limits.depth,
      document_bytes: limits.documentBytes,
      edges: limits.edges,
      nodes: limits.nodes,
      profile: "ownership-view-v1",
    },
    nodes: entities.map(publicEntity),
    reason: document.completeness === "complete" ?
      "complete-sidecar" : "justified-prefix-only",
    roots,
    schema: "kofun.ownership-view/v1",
    source_status: document.source_status,
    status: document.completeness === "complete" ? "complete" : "partial",
  };
  return view;
}

export function canonicalOwnershipViewBytes(view) {
  return canonicalTypedSidecarBytes(view);
}

export function projectOwnershipView(input, context = {}) {
  try {
    const decoded = readTypedSidecar(input);
    if (!decoded.ok) {
      fail(
        "TOV03",
        "typed sidecar is invalid: " + decoded.error.message,
        "invalid-sidecar",
      );
    }
    const limits = configuredLimits(context.limits);
    const view = buildView(decoded.document, context, limits);
    const bytes = Buffer.byteLength(canonicalOwnershipViewBytes(view), "utf8");
    if (bytes > limits.documentBytes) {
      fail("TOV04", "ownership view byte limit exceeded", "byte-limit");
    }
    return Object.freeze({ ok: true, view: deepFreeze(view) });
  } catch (error) {
    if (error instanceof TypeError) throw error;
    return errorResult(error);
  }
}
