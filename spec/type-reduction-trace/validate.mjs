import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const DIRECTORY = path.dirname(fileURLToPath(import.meta.url));
const SCHEMA_PATH = path.join(
  DIRECTORY,
  "kofun.type-reduction-trace.v1.schema.json",
);
const EXAMPLES = path.join(DIRECTORY, "examples");

const SCHEMA = "kofun.type-reduction-trace/v1";
const PROFILE = "kofun.type-reduction/default-v1";
const LIMITS = Object.freeze({
  diagnostic_bytes: 4096,
  diagnostic_frames: 8,
  display_bytes: 4096,
  frames: 32,
  static_message_bytes: 4096,
  steps: 256,
  structured_bytes: 4 * 1024 * 1024,
  text_explain_bytes: 64 * 1024,
  trace_records: 256,
  type_nodes: 256,
});
const IDENTITIES = new Set(["SymbolId", "TypeId"]);
const FAILURE_KINDS = new Set([
  "cycle",
  "frame-limit",
  "invalid-declaration",
  "no-matching-arm",
  "node-limit",
  "step-limit",
]);
const ID = /^[0-9a-f]{64}$/;

function fail(message) {
  throw new Error(message);
}

function isRecord(value) {
  return (
    value !== null &&
    typeof value === "object" &&
    !Array.isArray(value) &&
    (Object.getPrototypeOf(value) === Object.prototype ||
      Object.getPrototypeOf(value) === null)
  );
}

function exactRecord(value, wantedKeys, label) {
  if (!isRecord(value)) fail(`${label} must be an object`);
  const actual = Object.keys(value).sort();
  const wanted = [...wantedKeys].sort();
  if (
    actual.length !== wanted.length ||
    actual.some((key, index) => key !== wanted[index])
  ) {
    fail(`${label} has unknown or missing fields`);
  }
}

function integer(value, minimum, maximum, label) {
  if (
    !Number.isSafeInteger(value) ||
    value < minimum ||
    value > maximum
  ) {
    fail(`${label} must be an integer from ${minimum} through ${maximum}`);
  }
}

function validUnicode(value, label) {
  for (let index = 0; index < value.length; index += 1) {
    const unit = value.charCodeAt(index);
    if (unit >= 0xd800 && unit <= 0xdbff) {
      const next = value.charCodeAt(index + 1);
      if (!(next >= 0xdc00 && next <= 0xdfff)) {
        fail(`${label} contains an unpaired UTF-16 surrogate`);
      }
      index += 1;
    } else if (unit >= 0xdc00 && unit <= 0xdfff) {
      fail(`${label} contains an unpaired UTF-16 surrogate`);
    }
  }
}

function display(value, label, maximum = LIMITS.display_bytes) {
  if (typeof value !== "string" || value.length === 0) {
    fail(`${label} must be a non-empty string`);
  }
  validUnicode(value, label);
  if (value.normalize("NFC") !== value) fail(`${label} must be NFC`);
  if (/[\u0000-\u001f\u007f]/u.test(value)) {
    fail(`${label} must not contain control characters`);
  }
  if (Buffer.byteLength(value, "utf8") > maximum) {
    fail(`${label} exceeds ${maximum} UTF-8 bytes`);
  }
}

function id(value, label) {
  if (typeof value !== "string" || !ID.test(value)) {
    fail(`${label} must be 64 lowercase hexadecimal characters`);
  }
}

function identity(value, label) {
  exactRecord(value, ["kind", "value"], label);
  if (!IDENTITIES.has(value.kind)) {
    fail(`${label}.kind must be SymbolId or TypeId`);
  }
  id(value.value, `${label}.value`);
}

function sameIdentity(left, right) {
  return left.kind === right.kind && left.value === right.value;
}

function observed(value, label, enforceLimits) {
  exactRecord(value, ["frames", "steps", "type_nodes"], label);
  integer(value.frames, 0, Number.MAX_SAFE_INTEGER, `${label}.frames`);
  integer(value.steps, 0, Number.MAX_SAFE_INTEGER, `${label}.steps`);
  integer(
    value.type_nodes,
    0,
    Number.MAX_SAFE_INTEGER,
    `${label}.type_nodes`,
  );
  if (
    enforceLimits &&
    (value.frames > LIMITS.frames ||
      value.steps > LIMITS.steps ||
      value.type_nodes > LIMITS.type_nodes)
  ) {
    fail(`${label} exceeds the completed-work limits`);
  }
}

function span(value, label) {
  exactRecord(value, ["end", "file_id", "start"], label);
  integer(value.start, 0, 0xffffffff, `${label}.start`);
  integer(value.end, 0, 0xffffffff, `${label}.end`);
  if (value.start > value.end) fail(`${label}.start must not exceed end`);
  id(value.file_id, `${label}.file_id`);
}

function limits(value) {
  exactRecord(value, Object.keys(LIMITS), "limits");
  for (const [name, expected] of Object.entries(LIMITS)) {
    if (value[name] !== expected) {
      fail(`limits.${name} must be ${expected}`);
    }
  }
}

function step(value, index, rootIdentity, previousNodes) {
  const label = `steps[${index}]`;
  exactRecord(
    value,
    [
      "arm_index",
      "cumulative_nodes",
      "cumulative_steps",
      "declaration_symbol_id",
      "depth",
      "input",
      "kind",
      "output",
      "root_identity",
      "span",
      "step",
    ],
    label,
  );
  const wantedStep = index + 1;
  integer(value.step, 1, LIMITS.steps, `${label}.step`);
  if (value.step !== wantedStep) {
    fail(`${label}.step must be the sequential index ${wantedStep}`);
  }
  integer(
    value.cumulative_steps,
    1,
    LIMITS.steps,
    `${label}.cumulative_steps`,
  );
  if (value.cumulative_steps !== wantedStep) {
    fail(`${label}.cumulative_steps must equal its logical step`);
  }
  integer(
    value.cumulative_nodes,
    1,
    LIMITS.type_nodes,
    `${label}.cumulative_nodes`,
  );
  if (value.cumulative_nodes < previousNodes) {
    fail(`${label}.cumulative_nodes must be monotonic`);
  }
  integer(value.depth, 1, LIMITS.frames, `${label}.depth`);
  if (value.kind === "alias-expansion") {
    if (value.arm_index !== null) {
      fail(`${label}.arm_index must be null for an alias expansion`);
    }
  } else if (value.kind === "type-function-arm") {
    integer(value.arm_index, 0, 0xffffffff, `${label}.arm_index`);
  } else {
    fail(`${label}.kind is unknown`);
  }
  id(value.declaration_symbol_id, `${label}.declaration_symbol_id`);
  display(value.input, `${label}.input`);
  display(value.output, `${label}.output`);
  identity(value.root_identity, `${label}.root_identity`);
  if (!sameIdentity(value.root_identity, rootIdentity)) {
    fail(`${label}.root_identity does not match root.identity`);
  }
  span(value.span, `${label}.span`);
  return value.cumulative_nodes;
}

function result(value) {
  exactRecord(value, ["display", "type_id"], "result");
  display(value.display, "result.display");
  id(value.type_id, "result.type_id");
}

function failure(value, completed) {
  exactRecord(
    value,
    [
      "arm_index",
      "declaration_symbol_id",
      "kind",
      "measured",
      "message",
      "span",
    ],
    "failure",
  );
  if (!FAILURE_KINDS.has(value.kind)) fail("failure.kind is unknown");
  if (value.arm_index !== null) {
    integer(value.arm_index, 0, 0xffffffff, "failure.arm_index");
  }
  id(value.declaration_symbol_id, "failure.declaration_symbol_id");
  observed(value.measured, "failure.measured", false);
  display(value.message, "failure.message", LIMITS.diagnostic_bytes);
  span(value.span, "failure.span");
  for (const name of ["frames", "steps", "type_nodes"]) {
    if (value.measured[name] < completed[name]) {
      fail(`failure.measured.${name} precedes completed work`);
    }
  }

  if (
    value.kind === "frame-limit" &&
    value.measured.frames !== LIMITS.frames + 1
  ) {
    fail("frame-limit must report attempted frame 33");
  }
  if (
    value.kind === "step-limit" &&
    value.measured.steps !== LIMITS.steps + 1
  ) {
    fail("step-limit must report attempted step 257");
  }
  if (
    value.kind === "node-limit" &&
    value.measured.type_nodes !== LIMITS.type_nodes + 1
  ) {
    fail("node-limit must report attempted node 257");
  }
  if (
    value.kind === "frame-limit" &&
    (value.measured.steps > LIMITS.steps ||
      value.measured.type_nodes > LIMITS.type_nodes)
  ) {
    fail("frame-limit cannot also exceed the step or node limit");
  }
  if (
    value.kind === "step-limit" &&
    (value.measured.frames > LIMITS.frames ||
      value.measured.type_nodes > LIMITS.type_nodes)
  ) {
    fail("step-limit cannot also exceed the frame or node limit");
  }
  if (
    value.kind === "node-limit" &&
    (value.measured.frames > LIMITS.frames ||
      value.measured.steps > LIMITS.steps)
  ) {
    fail("node-limit cannot also exceed the frame or step limit");
  }
  if (
    !value.kind.endsWith("-limit") &&
    (value.measured.frames > LIMITS.frames ||
      value.measured.steps > LIMITS.steps ||
      value.measured.type_nodes > LIMITS.type_nodes)
  ) {
    fail(`${value.kind} cannot exceed the fixed reduction limits`);
  }
}

export function validateTrace(value) {
  exactRecord(
    value,
    [
      "authoritative",
      "failure",
      "limits",
      "observed",
      "profile",
      "result",
      "root",
      "schema",
      "status",
      "steps",
    ],
    "trace",
  );
  if (value.schema !== SCHEMA) fail(`schema must be ${SCHEMA}`);
  if (value.authoritative !== false) {
    fail("authoritative must be the JSON boolean false");
  }
  if (value.profile !== PROFILE) fail(`profile must be ${PROFILE}`);
  limits(value.limits);
  observed(value.observed, "observed", true);

  exactRecord(value.root, ["display", "identity"], "root");
  display(value.root.display, "root.display");
  identity(value.root.identity, "root.identity");

  if (!Array.isArray(value.steps)) fail("steps must be an array");
  if (value.steps.length > LIMITS.trace_records) {
    fail(`steps exceeds ${LIMITS.trace_records} records`);
  }
  let previousNodes = 0;
  let maximumDepth = 0;
  for (let index = 0; index < value.steps.length; index += 1) {
    previousNodes = step(
      value.steps[index],
      index,
      value.root.identity,
      previousNodes,
    );
    maximumDepth = Math.max(maximumDepth, value.steps[index].depth);
  }
  if (value.observed.steps !== value.steps.length) {
    fail("observed.steps must equal the retained step count");
  }
  if (value.observed.frames !== maximumDepth) {
    fail("observed.frames must equal the maximum completed depth");
  }
  if (value.observed.type_nodes !== previousNodes) {
    fail("observed.type_nodes must equal the final cumulative node count");
  }

  if (value.status === "reduced") {
    if (value.failure !== null || !isRecord(value.result)) {
      fail("reduced status requires result and forbids failure");
    }
    result(value.result);
  } else if (value.status === "failed") {
    if (value.result !== null || !isRecord(value.failure)) {
      fail("failed status requires failure and forbids result");
    }
    failure(value.failure, value.observed);
  } else {
    fail("status must be failed or reduced");
  }
  return value;
}

function sortValue(value) {
  if (Array.isArray(value)) return value.map(sortValue);
  if (isRecord(value)) {
    const sorted = Object.create(null);
    for (const key of Object.keys(value).sort()) {
      sorted[key] = sortValue(value[key]);
    }
    return sorted;
  }
  return value;
}

export function canonicalBytes(value) {
  return Buffer.from(`${JSON.stringify(sortValue(value), null, 2)}\n`, "utf8");
}

function parseCanonical(bytes, label) {
  if (bytes.length > LIMITS.structured_bytes) {
    fail(`${label} exceeds ${LIMITS.structured_bytes} bytes`);
  }
  if (
    bytes.length >= 3 &&
    bytes[0] === 0xef &&
    bytes[1] === 0xbb &&
    bytes[2] === 0xbf
  ) {
    fail(`${label} has a UTF-8 BOM`);
  }
  const text = bytes.toString("utf8");
  if (!Buffer.from(text, "utf8").equals(bytes)) {
    fail(`${label} is not valid UTF-8`);
  }
  if (!text.endsWith("\n") || text.endsWith("\n\n")) {
    fail(`${label} must contain one JSON value followed by one LF`);
  }
  let value;
  try {
    value = JSON.parse(text.slice(0, -1));
  } catch (error) {
    fail(`${label} is not JSON: ${error.message}`);
  }
  if (!canonicalBytes(value).equals(bytes)) {
    fail(`${label} is not canonical two-space JSON with sorted object keys`);
  }
  return value;
}

export function readCanonical(file) {
  return parseCanonical(fs.readFileSync(file), file);
}

function clone(value) {
  return JSON.parse(JSON.stringify(value));
}

function expectFailure(label, operation) {
  try {
    operation();
  } catch {
    return;
  }
  fail(`self-test ${label} unexpectedly succeeded`);
}

function validateSchema(file) {
  const schema = readCanonical(file);
  if (
    schema.$schema !== "https://json-schema.org/draft/2020-12/schema" ||
    schema.$id !==
      "https://kofun.dev/schema/kofun.type-reduction-trace.v1.schema.json"
  ) {
    fail("schema identity or dialect is incorrect");
  }
  if (schema.additionalProperties !== false) {
    fail("root JSON Schema must reject unknown fields");
  }
  if (schema.properties?.schema?.const !== SCHEMA) {
    fail("JSON Schema does not pin the trace schema name");
  }
  if (schema.properties?.profile?.const !== PROFILE) {
    fail("JSON Schema does not pin the reduction profile");
  }
  if (schema.properties?.authoritative?.const !== false) {
    fail("JSON Schema does not pin authoritative false");
  }
  if (schema.properties?.steps?.maxItems !== LIMITS.trace_records) {
    fail("JSON Schema does not pin the trace-record limit");
  }
  if (schema.$defs?.step?.properties?.depth?.maximum !== LIMITS.frames) {
    fail("JSON Schema does not pin the frame limit");
  }
  if (
    schema.$defs?.step?.properties?.cumulative_steps?.maximum !== LIMITS.steps
  ) {
    fail("JSON Schema does not pin the step limit");
  }
  if (
    schema.$defs?.step?.properties?.cumulative_nodes?.maximum !==
      LIMITS.type_nodes ||
    schema.$defs?.step?.properties?.cumulative_nodes?.minimum !== 1
  ) {
    fail("JSON Schema does not pin the node range");
  }
  if (
    !schema.$defs?.step?.properties?.kind?.enum?.includes("alias-expansion") ||
    !schema.$defs?.step?.properties?.kind?.enum?.includes("type-function-arm")
  ) {
    fail("JSON Schema does not distinguish alias and type-function steps");
  }
  if (schema.$defs?.failure?.properties?.span?.$ref !== "#/$defs/span") {
    fail("JSON Schema does not require a structured failure span");
  }

  const visit = (value, label) => {
    if (Array.isArray(value)) {
      value.forEach((item, index) => visit(item, `${label}[${index}]`));
      return;
    }
    if (!isRecord(value)) return;
    if (typeof value.$ref === "string") {
      const prefix = "#/$defs/";
      if (
        !value.$ref.startsWith(prefix) ||
        schema.$defs[value.$ref.slice(prefix.length)] === undefined
      ) {
        fail(`${label} contains an unresolved or external JSON Schema ref`);
      }
    }
    if (value.type === "object") {
      if (value.additionalProperties !== false) {
        fail(`${label} must reject unknown object fields`);
      }
      const properties = Object.keys(value.properties ?? {}).sort();
      const required = [...(value.required ?? [])].sort();
      if (
        properties.length !== required.length ||
        properties.some((key, index) => key !== required[index])
      ) {
        fail(`${label} must require every declared object field`);
      }
    }
    for (const [key, nested] of Object.entries(value)) {
      visit(nested, `${label}.${key}`);
    }
  };
  visit(schema, "schema");
  if (schema.allOf?.length !== 2 || schema.$defs?.step?.allOf?.length !== 2) {
    fail("JSON Schema must encode status and step discriminant pairing");
  }
}

function boundaryTrace(base, stepCount, maximumDepth, nodeCount) {
  const value = clone(base);
  const template = base.steps[0];
  value.steps = Array.from({ length: stepCount }, (_, index) => {
    const item = clone(template);
    item.cumulative_nodes = Math.max(
      1,
      Math.ceil(((index + 1) * nodeCount) / stepCount),
    );
    item.cumulative_steps = index + 1;
    item.depth = Math.min(index + 1, maximumDepth);
    item.input = `Boundary${index}`;
    item.output = `Boundary${index + 1}`;
    item.step = index + 1;
    return item;
  });
  value.observed.frames = maximumDepth;
  value.observed.steps = stepCount;
  value.observed.type_nodes = nodeCount;
  value.root.display = "Boundary0";
  value.result.display = `Boundary${stepCount}`;
  return value;
}

function limitFailure(base, cycle, kind, measured) {
  const value = clone(base);
  value.failure = clone(cycle.failure);
  value.failure.kind = kind;
  value.failure.measured = measured;
  value.failure.message = `${kind} at the fixed v1 boundary`;
  value.result = null;
  value.status = "failed";
  return value;
}

function selfTest() {
  const alias = validateTrace(readCanonical(path.join(EXAMPLES, "alias.json")));
  const reduced = validateTrace(
    readCanonical(path.join(EXAMPLES, "reduced.json")),
  );
  const cycle = validateTrace(readCanonical(path.join(EXAMPLES, "cycle.json")));

  const mutations = [
    ["authoritative", (value) => { value.authoritative = true; }],
    ["profile", (value) => { value.profile = "other"; }],
    ["unknown-field", (value) => { value.extra = true; }],
    ["status-pair", (value) => { value.status = "failed"; }],
    ["display-bytes", (value) => { value.root.display = "é".repeat(2049); }],
    ["step-sequence", (value) => { value.steps[0].step = 2; }],
    ["step-depth", (value) => { value.steps[0].depth = 33; }],
    ["observed-steps", (value) => { value.observed.steps = 1; }],
    [
      "root-identity",
      (value) => {
        value.steps[0].root_identity.value =
          "9999999999999999999999999999999999999999999999999999999999999999";
      },
    ],
    ["span-order", (value) => { value.steps[0].span.start = 141; }],
    ["fixed-limit", (value) => { value.limits.steps = 257; }],
  ];
  for (const [label, mutate] of mutations) {
    const value = clone(reduced);
    mutate(value);
    expectFailure(label, () => validateTrace(value));
  }

  const tooMany = clone(reduced);
  tooMany.steps = Array.from({ length: 257 }, (_, index) => {
    const item = clone(reduced.steps[0]);
    item.cumulative_nodes = 1;
    item.cumulative_steps = index + 1;
    item.step = index + 1;
    return item;
  });
  tooMany.observed.frames = 1;
  tooMany.observed.steps = 257;
  tooMany.observed.type_nodes = 1;
  expectFailure("trace-record-limit", () => validateTrace(tooMany));

  const badLimit = clone(cycle);
  badLimit.failure.kind = "step-limit";
  expectFailure("attempted-step", () => validateTrace(badLimit));

  const badAlias = clone(alias);
  badAlias.steps[0].arm_index = 0;
  expectFailure("alias-arm", () => validateTrace(badAlias));

  const exact = boundaryTrace(reduced, 256, 32, 256);
  validateTrace(exact);
  const frameLimit = limitFailure(
    boundaryTrace(reduced, 32, 32, 32),
    cycle,
    "frame-limit",
    { frames: 33, steps: 32, type_nodes: 32 },
  );
  const stepLimit = limitFailure(
    exact,
    cycle,
    "step-limit",
    { frames: 32, steps: 257, type_nodes: 256 },
  );
  const nodeLimit = limitFailure(
    boundaryTrace(reduced, 1, 1, 256),
    cycle,
    "node-limit",
    { frames: 1, steps: 1, type_nodes: 257 },
  );
  validateTrace(frameLimit);
  validateTrace(stepLimit);
  validateTrace(nodeLimit);

  const ambiguousLimit = clone(stepLimit);
  ambiguousLimit.failure.measured.frames = 33;
  expectFailure("cross-limit-precedence", () => validateTrace(ambiguousLimit));

  expectFailure("noncanonical-json", () =>
    parseCanonical(
      Buffer.from('{"schema":"x","authoritative":false}\n', "utf8"),
      "self-test noncanonical",
    ),
  );
  expectFailure("invalid-utf8", () =>
    parseCanonical(
      Buffer.from([0xc3, 0x28, 0x0a]),
      "self-test invalid UTF-8",
    ),
  );
}

function main(argv) {
  const [command, ...arguments_] = argv;
  if (command === "schema") {
    validateSchema(arguments_[0] ?? SCHEMA_PATH);
    return;
  }
  if (command === "validate" && arguments_.length > 0) {
    for (const file of arguments_) validateTrace(readCanonical(file));
    return;
  }
  if (command === "self-test" && arguments_.length === 0) {
    selfTest();
    return;
  }
  fail(
    "usage: validate.mjs schema [schema] | validate <trace>... | self-test",
  );
}

if (
  process.argv[1] &&
  path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)
) {
  try {
    main(process.argv.slice(2));
  } catch (error) {
    process.stderr.write(`type-reduction-trace: ${error.message}\n`);
    process.exitCode = 1;
  }
}
