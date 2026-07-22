#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const HEX = /^[0-9a-f]{64}$/;
const NAME = /^[A-Za-z][A-Za-z0-9._-]*$/;
const STATUSES = new Set(["error", "provisional", "unavailable", "validated"]);
const IDENTITY_KINDS = new Set([
  "BindingId", "ExportBindingId", "FileId", "ImplementationId",
  "ImportBindingId", "LawEvidenceId", "ModuleId", "NamespaceId",
  "PackageId", "ScopeId", "SymbolId", "TypeId",
]);
const LIMITS = Object.freeze({
  documentBytes: 16 * 1024 * 1024,
  maxDepth: 128,
  nodes: 65_536,
  references: 131_072,
  diagnostics: 65_536,
  identities: 262_144,
  edges: 262_144,
  attachedTextBytes: 1024 * 1024,
  edits: 4096,
  replacementTextBytes: 1024 * 1024,
});

function fail(message) {
  throw new Error(message);
}

class JsonParser {
  constructor(text) {
    this.text = text;
    this.index = 0;
  }

  parse() {
    this.skipSpace();
    const value = this.value("$");
    this.skipSpace();
    if (this.index !== this.text.length) fail(`trailing JSON data at byte ${this.index}`);
    return value;
  }

  skipSpace() {
    while (/\s/.test(this.text[this.index] ?? "") &&
           " \t\r\n".includes(this.text[this.index])) this.index += 1;
  }

  value(where) {
    this.skipSpace();
    const ch = this.text[this.index];
    if (ch === "{") return this.object(where);
    if (ch === "[") return this.array(where);
    if (ch === '"') return this.string();
    if (ch === "t" && this.take("true")) return true;
    if (ch === "f" && this.take("false")) return false;
    if (ch === "n" && this.take("null")) return null;
    if (ch === "-" || /[0-9]/.test(ch ?? "")) return this.number();
    fail(`invalid JSON value at byte ${this.index}`);
  }

  take(token) {
    if (!this.text.startsWith(token, this.index)) return false;
    this.index += token.length;
    return true;
  }

  string() {
    const start = this.index;
    this.index += 1;
    let escaped = false;
    while (this.index < this.text.length) {
      const ch = this.text[this.index++];
      if (!escaped && ch === '"') {
        const raw = this.text.slice(start, this.index);
        try {
          return JSON.parse(raw);
        } catch {
          fail(`invalid JSON string at byte ${start}`);
        }
      }
      if (!escaped && ch === "\\") escaped = true;
      else escaped = false;
    }
    fail(`unterminated JSON string at byte ${start}`);
  }

  number() {
    const match = this.text.slice(this.index).match(/^-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?/);
    if (!match) fail(`invalid JSON number at byte ${this.index}`);
    this.index += match[0].length;
    return Number(match[0]);
  }

  object(where) {
    this.index += 1;
    const result = {};
    const keys = new Set();
    this.skipSpace();
    if (this.text[this.index] === "}") {
      this.index += 1;
      return result;
    }
    while (true) {
      this.skipSpace();
      if (this.text[this.index] !== '"') fail(`object key expected at byte ${this.index}`);
      const key = this.string();
      if (keys.has(key)) fail(`${where}: duplicate object key ${JSON.stringify(key)}`);
      keys.add(key);
      this.skipSpace();
      if (this.text[this.index++] !== ":") fail(`':' expected after ${where}.${key}`);
      result[key] = this.value(`${where}.${key}`);
      this.skipSpace();
      const separator = this.text[this.index++];
      if (separator === "}") return result;
      if (separator !== ",") fail(`',' or '}' expected at byte ${this.index - 1}`);
    }
  }

  array(where) {
    this.index += 1;
    const result = [];
    this.skipSpace();
    if (this.text[this.index] === "]") {
      this.index += 1;
      return result;
    }
    while (true) {
      result.push(this.value(`${where}[${result.length}]`));
      this.skipSpace();
      const separator = this.text[this.index++];
      if (separator === "]") return result;
      if (separator !== ",") fail(`',' or ']' expected at byte ${this.index - 1}`);
    }
  }
}

function sortedValue(value) {
  if (Array.isArray(value)) return value.map(sortedValue);
  if (value !== null && typeof value === "object") {
    const result = {};
    for (const key of Object.keys(value).sort()) result[key] = sortedValue(value[key]);
    return result;
  }
  return value;
}

function canonicalBytes(value) {
  return `${JSON.stringify(sortedValue(value), null, 2)}\n`;
}

function ensureDocumentBytes(length) {
  if (length > LIMITS.documentBytes) fail("document exceeds default-v1 byte limit");
}

function loadDocument(filename, { canonical = true } = {}) {
  const bytes = fs.readFileSync(filename);
  ensureDocumentBytes(bytes.length);
  let text;
  try {
    text = new TextDecoder("utf-8", { fatal: true }).decode(bytes);
  } catch {
    fail("document is not valid UTF-8");
  }
  if (text.startsWith("\uFEFF")) fail("UTF-8 BOM is forbidden");
  const value = new JsonParser(text).parse();
  if (canonical && canonicalBytes(value) !== text) fail("document is not canonical JSON");
  return value;
}

function object(value, where, required, optional = []) {
  if (value === null || typeof value !== "object" || Array.isArray(value)) fail(`${where}: object required`);
  const allowed = new Set([...required, ...optional]);
  for (const key of required) if (!(key in value)) fail(`${where}: missing ${key}`);
  for (const key of Object.keys(value)) if (!allowed.has(key)) fail(`${where}: unknown field ${key}`);
  return value;
}

function array(value, where, maximum) {
  if (!Array.isArray(value)) fail(`${where}: array required`);
  if (value.length > maximum) fail(`${where}: item limit exceeded`);
  return value;
}

function string(value, where, maximum = Infinity) {
  if (typeof value !== "string") fail(`${where}: string required`);
  if (value.length > maximum) fail(`${where}: string limit exceeded`);
  if (value.normalize("NFC") !== value) fail(`${where}: string must be NFC`);
  return value;
}

function name(value, where) {
  string(value, where, 256);
  if (!NAME.test(value)) fail(`${where}: invalid name`);
  return value;
}

function hex(value, where) {
  if (typeof value !== "string" || !HEX.test(value)) fail(`${where}: lowercase 64-hex ID required`);
  return value;
}

function integer(value, where, minimum, maximum) {
  if (!Number.isSafeInteger(value) || value < minimum || value > maximum) {
    fail(`${where}: integer outside ${minimum}..${maximum}`);
  }
  return value;
}

function status(value, where) {
  if (!STATUSES.has(value)) fail(`${where}: invalid fact status`);
  return value;
}

function sortedUniqueStrings(values, where) {
  for (let i = 0; i < values.length; i += 1) {
    string(values[i], `${where}[${i}]`);
    if (i > 0 && values[i - 1] >= values[i]) fail(`${where}: values must be unique and sorted`);
  }
}

function compareTuple(a, b) {
  for (let i = 0; i < a.length; i += 1) {
    if (a[i] < b[i]) return -1;
    if (a[i] > b[i]) return 1;
  }
  return 0;
}

function assertSorted(items, tuple, where) {
  for (let i = 1; i < items.length; i += 1) {
    if (compareTuple(tuple(items[i - 1]), tuple(items[i])) >= 0) {
      fail(`${where}: records must be unique and canonically sorted`);
    }
  }
}

function validateSpan(value, where, byteLength) {
  object(value, where, ["end", "start"]);
  integer(value.start, `${where}.start`, 0, 0xffff_ffff);
  integer(value.end, `${where}.end`, value.start, 0xffff_ffff);
  if (byteLength !== null && value.end > byteLength) fail(`${where}: span exceeds source byte length`);
}

function validateIdentity(value, where) {
  object(value, where, ["kind", "value"]);
  if (!IDENTITY_KINDS.has(value.kind)) fail(`${where}.kind: unknown identity kind`);
  hex(value.value, `${where}.value`);
}

function validateFact(value, where, diagnosticIds) {
  object(value, where, ["status"], ["display", "reason"]);
  status(value.status, `${where}.status`);
  if (value.status === "unavailable") {
    if ("display" in value) fail(`${where}: unavailable fact must not carry display data`);
    name(value.reason, `${where}.reason`);
  } else {
    string(value.display, `${where}.display`, 4096);
    if ("reason" in value) name(value.reason, `${where}.reason`);
  }
  if (value.status === "error" && diagnosticIds.length === 0) {
    fail(`${where}: error fact requires a parent diagnostic link`);
  }
}

function validatePath(value, where) {
  string(value, where, 4096);
  if (value.length === 0 || value.startsWith("/") || value.includes("\\") ||
      /^[A-Za-z][A-Za-z0-9+.-]*:/.test(value)) fail(`${where}: relative logical POSIX path required`);
  const parts = value.split("/");
  if (parts.some((part) => part === "" || part === "." || part === "..")) {
    fail(`${where}: empty, '.' and '..' path components are forbidden`);
  }
}

function validateLocation(value, where, rootFileId, rootByteLength, requireRoot) {
  object(value, where, ["file_id", "span"]);
  hex(value.file_id, `${where}.file_id`);
  if (requireRoot && value.file_id !== rootFileId) fail(`${where}: primary location must use root FileId`);
  validateSpan(value.span, `${where}.span`, value.file_id === rootFileId ? rootByteLength : null);
}

function validateRemedy(value, where, byteLength, counters) {
  object(value, where, ["id"], ["replacement", "span"]);
  name(value.id, `${where}.id`);
  const hasEdit = "replacement" in value || "span" in value;
  if (hasEdit && !("replacement" in value && "span" in value)) fail(`${where}: edit needs both span and replacement`);
  if (hasEdit) {
    string(value.replacement, `${where}.replacement`, 65536);
    validateSpan(value.span, `${where}.span`, byteLength);
    counters.edits += 1;
    counters.replacementTextBytes += Buffer.byteLength(value.replacement);
  }
}

function validateSchemaFile() {
  const schemaPath = path.join(HERE, "kofun.typed-sidecar.v1.schema.json");
  const schema = loadDocument(schemaPath);
  if (schema.$schema !== "https://json-schema.org/draft/2020-12/schema" ||
      schema.properties?.authoritative?.const !== false ||
      schema.properties?.schema?.const !== "kofun.typed-sidecar/v1" ||
      schema.properties?.limits?.properties?.document_bytes?.const !== LIMITS.documentBytes ||
      schema.properties?.limits?.properties?.max_depth?.const !== LIMITS.maxDepth ||
      schema.properties?.nodes?.maxItems !== LIMITS.nodes ||
      schema.properties?.references?.maxItems !== LIMITS.references ||
      schema.properties?.diagnostics?.maxItems !== LIMITS.diagnostics) {
    fail("checked-in JSON Schema does not encode the default-v1 trust and limit constants");
  }
}

function structuralDepth(value, depth = 1) {
  if (value === null || typeof value !== "object") return depth;
  let maximum = depth;
  for (const child of Array.isArray(value) ? value : Object.values(value)) {
    maximum = Math.max(maximum, structuralDepth(child, depth + 1));
  }
  return maximum;
}

function validateDocument(doc) {
  object(doc, "$", [
    "authoritative", "compiler", "completeness", "diagnostics", "file",
    "generation", "limits", "nodes", "references", "schema", "source_status",
  ]);
  if (doc.authoritative !== false) fail("$.authoritative: must be false");
  if (doc.schema !== "kofun.typed-sidecar/v1") fail("$.schema: unsupported schema");
  if (structuralDepth(doc) > LIMITS.maxDepth) fail("document exceeds default-v1 structural depth");

  object(doc.compiler, "$.compiler", ["edition", "semantic_compatibility"]);
  name(doc.compiler.edition, "$.compiler.edition");
  name(doc.compiler.semantic_compatibility, "$.compiler.semantic_compatibility");

  object(doc.file, "$.file", [
    "byte_length", "content_sha256", "file_id", "logical_path", "module_id", "package_id",
  ], ["path_remap_root_id"]);
  integer(doc.file.byte_length, "$.file.byte_length", 0, 0xffff_ffff);
  hex(doc.file.content_sha256, "$.file.content_sha256");
  hex(doc.file.file_id, "$.file.file_id");
  hex(doc.file.module_id, "$.file.module_id");
  hex(doc.file.package_id, "$.file.package_id");
  if ("path_remap_root_id" in doc.file) hex(doc.file.path_remap_root_id, "$.file.path_remap_root_id");
  validatePath(doc.file.logical_path, "$.file.logical_path");

  object(doc.generation, "$.generation", ["sequence"]);
  integer(doc.generation.sequence, "$.generation.sequence", 0, Number.MAX_SAFE_INTEGER);
  object(doc.limits, "$.limits", ["document_bytes", "max_depth", "profile"]);
  if (doc.limits.document_bytes !== LIMITS.documentBytes ||
      doc.limits.max_depth !== LIMITS.maxDepth || doc.limits.profile !== "default-v1") {
    fail("$.limits: unknown or incorrect bounded profile");
  }

  const validPair = (doc.completeness === "complete" && doc.source_status === "checked") ||
    (doc.completeness === "partial" && ["failed", "cancelled"].includes(doc.source_status));
  if (!validPair) fail("completeness/source_status pairing is invalid");

  const nodes = array(doc.nodes, "$.nodes", LIMITS.nodes);
  const references = array(doc.references, "$.references", LIMITS.references);
  const diagnostics = array(doc.diagnostics, "$.diagnostics", LIMITS.diagnostics);
  const nodeIds = new Map();
  const referenceIds = new Set();
  const diagnosticIds = new Set();
  const allIds = new Set();
  const counters = { identities: 0, edges: 0, attachedTextBytes: 0, edits: 0, replacementTextBytes: 0 };

  for (let index = 0; index < nodes.length; index += 1) {
    const where = `$.nodes[${index}]`;
    const node = object(nodes[index], where, [
      "depends_on", "diagnostic_ids", "id", "identities", "kind", "span", "status",
    ], ["effect", "origin", "ownership", "type"]);
    hex(node.id, `${where}.id`);
    if (allIds.has(node.id)) fail(`${where}.id: duplicate sidecar-local ID`);
    allIds.add(node.id);
    nodeIds.set(node.id, node);
    name(node.kind, `${where}.kind`);
    validateSpan(node.span, `${where}.span`, doc.file.byte_length);
    status(node.status, `${where}.status`);
    array(node.depends_on, `${where}.depends_on`, LIMITS.nodes);
    array(node.diagnostic_ids, `${where}.diagnostic_ids`, 256);
    sortedUniqueStrings(node.depends_on, `${where}.depends_on`);
    sortedUniqueStrings(node.diagnostic_ids, `${where}.diagnostic_ids`);
    counters.edges += node.depends_on.length + node.diagnostic_ids.length;
    const identities = array(node.identities, `${where}.identities`, 64);
    for (let i = 0; i < identities.length; i += 1) validateIdentity(identities[i], `${where}.identities[${i}]`);
    assertSorted(identities, (identity) => [identity.kind, identity.value], `${where}.identities`);
    counters.identities += identities.length;
    for (const field of ["effect", "origin", "ownership", "type"]) {
      if (field in node) validateFact(node[field], `${where}.${field}`, node.diagnostic_ids);
    }
    if (node.status === "error" && node.diagnostic_ids.length === 0) fail(`${where}: error node needs a diagnostic`);
    if (node.status === "unavailable") {
      for (const field of ["effect", "origin", "ownership", "type"]) {
        if (field in node && node[field].status !== "unavailable") fail(`${where}: unavailable node cannot carry ${field} data`);
      }
    }
  }
  assertSorted(nodes, (node) => [node.span.start, node.span.end, node.kind, node.id], "$.nodes");

  for (let index = 0; index < references.length; index += 1) {
    const where = `$.references[${index}]`;
    const reference = object(references[index], where, [
      "diagnostic_ids", "from_node", "id", "namespace", "span", "status", "target",
    ]);
    hex(reference.id, `${where}.id`);
    if (allIds.has(reference.id)) fail(`${where}.id: duplicate sidecar-local ID`);
    allIds.add(reference.id);
    referenceIds.add(reference.id);
    hex(reference.from_node, `${where}.from_node`);
    validateSpan(reference.span, `${where}.span`, doc.file.byte_length);
    if (!["meta", "module", "type", "value"].includes(reference.namespace)) fail(`${where}.namespace: invalid namespace`);
    status(reference.status, `${where}.status`);
    array(reference.diagnostic_ids, `${where}.diagnostic_ids`, 256);
    sortedUniqueStrings(reference.diagnostic_ids, `${where}.diagnostic_ids`);
    counters.edges += reference.diagnostic_ids.length + 1;
    const target = object(reference.target, `${where}.target`, ["disclosure"], [
      "declaration_node", "identity", "identity_kind", "reason",
    ]);
    if (!["hidden", "provisional", "resolved", "unavailable"].includes(target.disclosure)) {
      fail(`${where}.target.disclosure: invalid disclosure`);
    }
    if (target.disclosure === "resolved") {
      if (!("identity" in target) || "identity_kind" in target || "reason" in target) fail(`${where}.target: malformed resolved target`);
      validateIdentity(target.identity, `${where}.target.identity`);
      counters.identities += 1;
    } else {
      if ("identity" in target || "declaration_node" in target) fail(`${where}.target: non-resolved target leaks identity or declaration`);
      if ("identity_kind" in target && !IDENTITY_KINDS.has(target.identity_kind)) fail(`${where}.target.identity_kind: unknown kind`);
      if (target.disclosure !== "hidden" && "identity_kind" in target) fail(`${where}.target: identity kind is permitted only for hidden disclosure`);
      if (target.disclosure !== "hidden" && !("reason" in target)) fail(`${where}.target: reason required`);
      if ("reason" in target) name(target.reason, `${where}.target.reason`);
    }
    const expectedDisclosure = {
      validated: "resolved", provisional: "provisional", error: "unavailable",
    }[reference.status];
    if (expectedDisclosure && target.disclosure !== expectedDisclosure) fail(`${where}: status and target disclosure disagree`);
    if (reference.status === "unavailable" && !["hidden", "unavailable"].includes(target.disclosure)) fail(`${where}: unavailable reference has unsafe target`);
    if (reference.status === "error" && reference.diagnostic_ids.length === 0) fail(`${where}: error reference needs a diagnostic`);
  }
  assertSorted(references, (reference) => [reference.span.start, reference.span.end, reference.id], "$.references");

  const severityRank = { error: 0, warning: 1, information: 2, hint: 3 };
  for (let index = 0; index < diagnostics.length; index += 1) {
    const where = `$.diagnostics[${index}]`;
    const diagnostic = object(diagnostics[index], where, [
      "affected_ids", "category", "code", "fallback_text", "id", "primary", "related",
      "remedies", "severity", "template_id", "truncated",
    ]);
    hex(diagnostic.id, `${where}.id`);
    if (allIds.has(diagnostic.id)) fail(`${where}.id: duplicate sidecar-local ID`);
    allIds.add(diagnostic.id);
    diagnosticIds.add(diagnostic.id);
    name(diagnostic.category, `${where}.category`);
    if (typeof diagnostic.code !== "string" || !/^[A-Z][A-Z0-9]{0,15}$/.test(diagnostic.code)) fail(`${where}.code: invalid diagnostic code`);
    string(diagnostic.fallback_text, `${where}.fallback_text`, 65536);
    counters.attachedTextBytes += Buffer.byteLength(diagnostic.fallback_text);
    if (!(diagnostic.severity in severityRank)) fail(`${where}.severity: invalid severity`);
    name(diagnostic.template_id, `${where}.template_id`);
    if (typeof diagnostic.truncated !== "boolean") fail(`${where}.truncated: boolean required`);
    validateLocation(diagnostic.primary, `${where}.primary`, doc.file.file_id, doc.file.byte_length, true);
    array(diagnostic.affected_ids, `${where}.affected_ids`, LIMITS.edges);
    sortedUniqueStrings(diagnostic.affected_ids, `${where}.affected_ids`);
    counters.edges += diagnostic.affected_ids.length;
    const related = array(diagnostic.related, `${where}.related`, 256);
    for (let i = 0; i < related.length; i += 1) {
      const itemWhere = `${where}.related[${i}]`;
      const item = object(related[i], itemWhere, ["relation"], ["identity", "location"]);
      name(item.relation, `${itemWhere}.relation`);
      if (!("identity" in item) && !("location" in item)) fail(`${itemWhere}: identity or location required`);
      if ("identity" in item) { validateIdentity(item.identity, `${itemWhere}.identity`); counters.identities += 1; }
      if ("location" in item) validateLocation(item.location, `${itemWhere}.location`, doc.file.file_id, doc.file.byte_length, false);
    }
    assertSorted(related, (item) => [
      item.relation, item.location?.file_id ?? "", item.location?.span.start ?? -1,
      item.location?.span.end ?? -1, item.identity?.kind ?? "", item.identity?.value ?? "",
    ], `${where}.related`);
    const remedies = array(diagnostic.remedies, `${where}.remedies`, 256);
    for (let i = 0; i < remedies.length; i += 1) validateRemedy(remedies[i], `${where}.remedies[${i}]`, doc.file.byte_length, counters);
    assertSorted(remedies, (remedy) => [remedy.id, remedy.span?.start ?? -1, remedy.span?.end ?? -1, remedy.replacement ?? ""], `${where}.remedies`);
  }
  assertSorted(diagnostics, (diagnostic) => [
    diagnostic.primary.file_id, diagnostic.primary.span.start, diagnostic.primary.span.end,
    severityRank[diagnostic.severity], diagnostic.code, diagnostic.id,
  ], "$.diagnostics");

  for (const [nodeId, node] of nodeIds) {
    for (const dependency of node.depends_on) {
      if (!nodeIds.has(dependency)) fail(`node ${nodeId}: dangling dependency ${dependency}`);
      if (node.status === "validated" && nodeIds.get(dependency).status !== "validated") {
        fail(`node ${nodeId}: validated node depends on non-validated node`);
      }
    }
    for (const diagnosticId of node.diagnostic_ids) if (!diagnosticIds.has(diagnosticId)) fail(`node ${nodeId}: dangling diagnostic ${diagnosticId}`);
  }
  for (const reference of references) {
    if (!nodeIds.has(reference.from_node)) fail(`reference ${reference.id}: dangling from_node`);
    if (reference.status === "validated" && nodeIds.get(reference.from_node).status !== "validated") fail(`reference ${reference.id}: validated reference has non-validated source node`);
    if (reference.target.declaration_node && !nodeIds.has(reference.target.declaration_node)) fail(`reference ${reference.id}: dangling declaration_node`);
    for (const diagnosticId of reference.diagnostic_ids) if (!diagnosticIds.has(diagnosticId)) fail(`reference ${reference.id}: dangling diagnostic ${diagnosticId}`);
  }
  for (const diagnostic of diagnostics) {
    for (const affected of diagnostic.affected_ids) {
      if (!nodeIds.has(affected) && !referenceIds.has(affected)) fail(`diagnostic ${diagnostic.id}: dangling affected ID`);
    }
  }

  if (doc.completeness === "complete") {
    if (diagnostics.some((item) => item.severity === "error")) fail("complete document contains an error diagnostic");
    if (nodes.some((item) => item.status !== "validated") || references.some((item) => item.status !== "validated")) fail("complete document contains a non-validated fact");
    for (const node of nodes) for (const field of ["effect", "origin", "ownership", "type"]) {
      if (field in node && node[field].status !== "validated") fail("complete document contains a non-validated nested fact");
    }
  }
  if (doc.source_status === "failed" && !diagnostics.some((item) => item.severity === "error")) fail("failed document requires an error diagnostic");
  if (counters.identities > LIMITS.identities) fail("stable identity record limit exceeded");
  if (counters.edges > LIMITS.edges) fail("dependency/affected edge limit exceeded");
  if (counters.attachedTextBytes > LIMITS.attachedTextBytes) fail("attached fallback text byte limit exceeded");
  if (counters.edits > LIMITS.edits) fail("edit limit exceeded");
  if (counters.replacementTextBytes > LIMITS.replacementTextBytes) fail("replacement text byte limit exceeded");
  return doc;
}

function readAndValidate(filename) {
  return validateDocument(loadDocument(filename));
}

function canReplace(oldDoc, newDoc, currentDigest) {
  return oldDoc.file.file_id === newDoc.file.file_id &&
    newDoc.generation.sequence > oldDoc.generation.sequence &&
    newDoc.file.content_sha256 === currentDigest;
}

function selfTestLimits() {
  ensureDocumentBytes(LIMITS.documentBytes);
  let rejected = false;
  try { ensureDocumentBytes(LIMITS.documentBytes + 1); } catch { rejected = true; }
  if (!rejected) fail("document byte one-over self-test failed");
  if (LIMITS.nodes !== 65_536 || LIMITS.references !== 131_072 ||
      LIMITS.identities !== 262_144 || LIMITS.edges !== 262_144 ||
      LIMITS.edits !== 4096) fail("default-v1 exact-boundary self-test failed");
}

function usage() {
  fail("usage: validate.mjs validate FILE | canonicalize FILE | project FILE | replace OLD NEW CURRENT_SHA256 | schema | self-test-limits");
}

try {
  const [command, ...args] = process.argv.slice(2);
  if (command === "validate" && args.length === 1) {
    readAndValidate(args[0]);
    console.log(`PASS: ${args[0]}`);
  } else if (command === "canonicalize" && args.length === 1) {
    process.stdout.write(canonicalBytes(loadDocument(args[0], { canonical: false })));
  } else if (command === "project" && args.length === 1) {
    const doc = structuredClone(readAndValidate(args[0]));
    delete doc.file.path_remap_root_id;
    process.stdout.write(canonicalBytes(doc));
  } else if (command === "replace" && args.length === 3) {
    hex(args[2], "current source digest");
    const oldDoc = readAndValidate(args[0]);
    const newDoc = readAndValidate(args[1]);
    if (!canReplace(oldDoc, newDoc, args[2])) fail("replacement denied");
    console.log("ALLOW: sidecar replacement");
  } else if (command === "schema" && args.length === 0) {
    validateSchemaFile();
    console.log("PASS: typed-sidecar JSON Schema");
  } else if (command === "self-test-limits" && args.length === 0) {
    selfTestLimits();
    console.log("PASS: default-v1 exact/one-over limit model");
  } else {
    usage();
  }
} catch (error) {
  console.error(`typed-sidecar: ${error.message}`);
  process.exitCode = 1;
}
