import crypto from "node:crypto";

export const LIMITS = Object.freeze({
  maxBytes: 1024 * 1024,
  maxDefinitions: 1024,
  maxDepth: 128,
  maxNodes: 10000,
  maxScc: 6,
});

export class IdentityError extends Error {
  constructor(code, message) {
    super(`${code}: ${message}`);
    this.name = "IdentityError";
    this.code = code;
  }
}

const fail = (code, message) => { throw new IdentityError(code, message); };

function allowedKeys(value, allowed, code = "SID13") {
  const extras = Object.keys(value).filter((key) => !allowed.includes(key));
  if (extras.length > 0) fail(code, `unknown semantic field ${extras.sort()[0]}`);
}

export function canonical(value) {
  if (value === null || typeof value === "boolean" || typeof value === "string") {
    return JSON.stringify(value);
  }
  if (Number.isSafeInteger(value)) return String(value);
  if (Array.isArray(value)) return `[${value.map(canonical).join(",")}]`;
  if (typeof value !== "object" || value === undefined) fail("SID01", "unsupported canonical value");
  return `{${Object.keys(value).sort().map((key) =>
    `${JSON.stringify(key)}:${canonical(value[key])}`).join(",")}}`;
}

export function hashDomain(domain, value) {
  return crypto.createHash("sha256")
    .update(domain)
    .update("\0")
    .update(typeof value === "string" ? value : canonical(value))
    .digest("hex");
}

function permutations(values) {
  if (values.length <= 1) return [values.slice()];
  const result = [];
  for (let index = 0; index < values.length; index += 1) {
    const head = values[index];
    const rest = values.slice(0, index).concat(values.slice(index + 1));
    for (const tail of permutations(rest)) result.push([head, ...tail]);
  }
  return result;
}

function collectInternalCalls(expression, keys, calls = new Set()) {
  if (expression === null || typeof expression !== "object") return calls;
  if (expression.kind === "call" && keys.has(expression.target)) calls.add(expression.target);
  for (const [key, value] of Object.entries(expression)) {
    if (key === "span" || key === "comments" || key === "trivia") continue;
    if (Array.isArray(value)) value.forEach((item) => collectInternalCalls(item, keys, calls));
    else collectInternalCalls(value, keys, calls);
  }
  return calls;
}

function stronglyConnected(definitions) {
  const keys = new Set(definitions.map((definition) => definition.key));
  const graph = new Map(definitions.map((definition) => [
    definition.key,
    [...collectInternalCalls(definition.body, keys)].sort(),
  ]));
  let nextIndex = 0;
  const indices = new Map();
  const low = new Map();
  const stack = [];
  const onStack = new Set();
  const components = [];

  function visit(key) {
    indices.set(key, nextIndex);
    low.set(key, nextIndex);
    nextIndex += 1;
    stack.push(key);
    onStack.add(key);
    for (const target of graph.get(key)) {
      if (!indices.has(target)) {
        visit(target);
        low.set(key, Math.min(low.get(key), low.get(target)));
      } else if (onStack.has(target)) {
        low.set(key, Math.min(low.get(key), indices.get(target)));
      }
    }
    if (low.get(key) === indices.get(key)) {
      const members = [];
      while (true) {
        const member = stack.pop();
        onStack.delete(member);
        members.push(member);
        if (member === key) break;
      }
      components.push(members);
    }
  }
  for (const key of [...keys].sort()) if (!indices.has(key)) visit(key);
  return components;
}

function countExpression(expression, depth, state) {
  if (depth > LIMITS.maxDepth) fail("SID05", "Core depth limit exceeded");
  if (expression === null || typeof expression !== "object") return;
  state.nodes += 1;
  if (state.nodes > LIMITS.maxNodes) fail("SID04", "Core node limit exceeded");
  for (const [key, value] of Object.entries(expression)) {
    if (["span", "comments", "trivia"].includes(key)) continue;
    if (Array.isArray(value)) value.forEach((item) => countExpression(item, depth + 1, state));
    else countExpression(value, depth + 1, state);
  }
}

function normalizeExpression(expression, environment, sameComponent, resolvedMembers) {
  if (expression === null || typeof expression !== "object") fail("SID02", "expression must be an object");
  switch (expression.kind) {
    case "lit":
      allowedKeys(expression, ["comments", "kind", "span", "trivia", "type", "value"]);
      return { kind: "lit", type: expression.type, value: expression.value };
    case "var": {
      allowedKeys(expression, ["comments", "kind", "name", "span", "trivia", "type"]);
      const reverse = environment.slice().reverse();
      const index = reverse.indexOf(expression.name);
      if (index < 0) fail("SID03", `unbound local ${expression.name}`);
      return { index, kind: "var" };
    }
    case "binary":
      allowedKeys(expression, ["comments", "kind", "left", "op", "right", "span", "trivia", "type"]);
      return {
        kind: "binary",
        left: normalizeExpression(expression.left, environment, sameComponent, resolvedMembers),
        op: expression.op,
        right: normalizeExpression(expression.right, environment, sameComponent, resolvedMembers),
        type: expression.type,
      };
    case "if":
      allowedKeys(expression, ["comments", "condition", "else", "kind", "span", "then", "trivia", "type"]);
      return {
        condition: normalizeExpression(expression.condition, environment, sameComponent, resolvedMembers),
        else: normalizeExpression(expression.else, environment, sameComponent, resolvedMembers),
        kind: "if",
        then: normalizeExpression(expression.then, environment, sameComponent, resolvedMembers),
        type: expression.type,
      };
    case "let":
      allowedKeys(expression, ["body", "comments", "kind", "name", "span", "trivia", "type", "value"]);
      return {
        body: normalizeExpression(expression.body, [...environment, expression.name], sameComponent, resolvedMembers),
        kind: "let",
        type: expression.type,
        value: normalizeExpression(expression.value, environment, sameComponent, resolvedMembers),
      };
    case "call": {
      allowedKeys(expression, ["args", "comments", "kind", "span", "target", "trivia", "type"]);
      let target;
      if (sameComponent.has(expression.target)) {
        target = { recursive: sameComponent.get(expression.target) };
      } else if (resolvedMembers.has(expression.target)) {
        const identity = resolvedMembers.get(expression.target);
        if (identity === null) fail("SID09", "cross-component reference targets an ambiguous member");
        target = { term: identity };
      } else {
        fail("SID03", `unknown call target ${expression.target}`);
      }
      return {
        args: (expression.args ?? []).map((argument) =>
          normalizeExpression(argument, environment, sameComponent, resolvedMembers)),
        kind: "call",
        target,
        type: expression.type,
      };
    }
    case "external":
      allowedKeys(expression, ["comments", "kind", "semanticId", "span", "trivia", "type"]);
      if (!/^[0-9a-f]{64}$/.test(expression.semanticId ?? "")) {
        fail("SID07", "external dependency identity must be lowercase SHA-256");
      }
      return { kind: "external", semanticId: expression.semanticId, type: expression.type };
    default:
      fail("SID02", `unsupported Core node ${expression.kind}`);
  }
}

function normalizeDefinition(definition, sameComponent, resolvedMembers) {
  allowedKeys(definition, [
    "body", "comments", "displayName", "effects", "key", "parameters", "result",
    "span", "symbol", "trivia",
  ]);
  for (const parameter of definition.parameters) {
    allowedKeys(parameter, ["comments", "mode", "name", "span", "trivia", "type"]);
  }
  const environment = definition.parameters.map((parameter) => parameter.name);
  return {
    body: normalizeExpression(definition.body, environment, sameComponent, resolvedMembers),
    effects: [...new Set(definition.effects ?? [])].sort(),
    parameters: definition.parameters.map((parameter) => ({
      mode: parameter.mode,
      type: parameter.type,
    })),
    result: definition.result,
  };
}

export function analyzeProgram(program) {
  allowedKeys(program, [
    "comments", "compilerSemantics", "definitions", "edition", "numericPolicy",
    "schema", "trivia", "unicodeVersion",
  ]);
  const inputBytes = Buffer.byteLength(JSON.stringify(program));
  if (inputBytes > LIMITS.maxBytes) fail("SID06", "input byte limit exceeded");
  if (program.schema !== "kofun.semantic-core-model/v1") fail("SID08", "unsupported model schema");
  if (!Array.isArray(program.definitions) || program.definitions.length > LIMITS.maxDefinitions) {
    fail("SID06", "definition limit exceeded");
  }
  const byKey = new Map();
  const symbols = new Set();
  const state = { nodes: 0 };
  for (const definition of program.definitions) {
    if (byKey.has(definition.key)) fail("SID10", `duplicate definition key ${definition.key}`);
    if (symbols.has(definition.symbol)) fail("SID10", `duplicate nominal symbol ${definition.symbol}`);
    byKey.set(definition.key, definition);
    symbols.add(definition.symbol);
    countExpression(definition.body, 0, state);
  }
  const definitionKeys = new Set(byKey.keys());

  const components = stronglyConnected(program.definitions);
  const componentOf = new Map();
  components.forEach((component, index) => component.forEach((key) => componentOf.set(key, index)));
  const resolvedMembers = new Map();
  const resolvedComponents = new Map();
  const pending = new Set(components.map((_, index) => index));

  while (pending.size > 0) {
    let progressed = false;
    for (const componentIndex of [...pending]) {
      const members = components[componentIndex];
      if (members.length > LIMITS.maxScc) fail("SID11", "recursive component limit exceeded");
      const dependencies = new Set();
      for (const key of members) {
        for (const target of collectInternalCalls(byKey.get(key).body, definitionKeys)) {
          const targetComponent = componentOf.get(target);
          if (targetComponent !== componentIndex) dependencies.add(targetComponent);
        }
      }
      if ([...dependencies].some((dependency) => !resolvedComponents.has(dependency))) continue;

      let minimum = null;
      let winners = [];
      for (const order of permutations(members)) {
        const same = new Map(order.map((key, index) => [key, index]));
        const value = {
          members: order.map((key) => normalizeDefinition(byKey.get(key), same, resolvedMembers)),
          schema: "kofun.semantic-component/v1",
        };
        const bytes = canonical(value);
        if (minimum === null || bytes < minimum) {
          minimum = bytes;
          winners = [order];
        } else if (bytes === minimum) {
          winners.push(order);
        }
      }
      const componentIdentity = hashDomain("kofun.semantic-component/v1", minimum);
      const ambiguous = winners.length > 1;
      const selected = winners[0];
      selected.forEach((key, index) => {
        resolvedMembers.set(key, ambiguous ? null :
          hashDomain("kofun.semantic-member/v1", { componentIdentity, index }));
      });
      resolvedComponents.set(componentIndex, {
        ambiguous,
        identity: componentIdentity,
        members: selected.map((key) => byKey.get(key).symbol).sort(),
      });
      pending.delete(componentIndex);
      progressed = true;
    }
    if (!progressed) fail("SID12", "component dependency ordering failed");
  }

  const semanticInputs = {
    compilerSemantics: program.compilerSemantics,
    edition: program.edition,
    numericPolicy: program.numericPolicy,
    unicodeVersion: program.unicodeVersion,
  };
  const componentIdentities = [...resolvedComponents.values()].map((item) => item.identity).sort();
  const identity = hashDomain("kofun.semantic-program/v1", {
    componentIdentities,
    schema: "kofun.semantic-program/v1",
    semanticInputs,
  });
  const membersBySymbol = {};
  for (const definition of program.definitions) {
    membersBySymbol[definition.symbol] = resolvedMembers.get(definition.key);
  }
  return {
    canonical: { componentIdentities, schema: "kofun.semantic-program/v1", semanticInputs },
    components: [...resolvedComponents.values()].sort((left, right) =>
      left.identity.localeCompare(right.identity)),
    identity,
    inputBytes,
    membersBySymbol,
    nodes: state.nodes,
  };
}

function exactKeys(object, keys, code) {
  if (canonical(Object.keys(object).sort()) !== canonical([...keys].sort())) {
    fail(code, "unexpected or missing field");
  }
}

export function makeCacheKey(inputs) {
  exactKeys(inputs, [
    "compilerSemantics", "configDigest", "dependencyIdentities", "edition",
    "environmentDigest", "targetSemantics", "termIdentity", "testIdentity",
  ], "CACHE01");
  return hashDomain("kofun.test-cache-key/v1", {
    ...inputs,
    dependencyIdentities: [...inputs.dependencyIdentities].sort(),
    schema: "kofun.test-cache-key/v1",
  });
}

export function makeCacheEntry(key, observation) {
  const forbidden = /command|executable|token|capability|authority|path/i;
  if (Object.keys(observation).some((name) => forbidden.test(name))) {
    fail("CACHE03", "observation attempts to carry execution authority");
  }
  exactKeys(observation, ["exitCategory", "ownershipEvent", "stderr", "stdout"], "CACHE02");
  return {
    key,
    observation,
    observationDigest: hashDomain("kofun.test-observation/v1", observation),
    schema: "kofun.test-cache-entry/v1",
  };
}

export function readCacheEntry(entry, expectedKey) {
  exactKeys(entry, ["key", "observation", "observationDigest", "schema"], "CACHE02");
  if (entry.schema !== "kofun.test-cache-entry/v1") fail("CACHE04", "unsupported cache schema");
  if (entry.key !== expectedKey) return { hit: false, reason: "stale-key" };
  exactKeys(entry.observation, ["exitCategory", "ownershipEvent", "stderr", "stdout"], "CACHE02");
  const digest = hashDomain("kofun.test-observation/v1", entry.observation);
  if (digest !== entry.observationDigest) fail("CACHE05", "corrupt cache observation");
  return { hit: true, observation: structuredClone(entry.observation) };
}

export function insertContentObject(store, identity, canonicalBytes) {
  if (!(store instanceof Map)) fail("SID14", "content store model must be a Map");
  if (store.has(identity) && store.get(identity) !== canonicalBytes) {
    fail("SID15", "identity collision has different canonical bytes");
  }
  store.set(identity, canonicalBytes);
  return identity;
}

export function structuralDiff(before, after) {
  const beforeResult = analyzeProgram(before);
  const afterResult = analyzeProgram(after);
  const beforeBySymbol = new Map(before.definitions.map((item) => [item.symbol, item]));
  const afterBySymbol = new Map(after.definitions.map((item) => [item.symbol, item]));
  const symbols = [...new Set([...beforeBySymbol.keys(), ...afterBySymbol.keys()])].sort();
  return symbols.map((symbol) => {
    const oldDefinition = beforeBySymbol.get(symbol);
    const newDefinition = afterBySymbol.get(symbol);
    if (!oldDefinition) return { kind: "added", span: newDefinition.span ?? null, symbol };
    if (!newDefinition) return { kind: "removed", span: oldDefinition.span ?? null, symbol };
    const oldIdentity = beforeResult.membersBySymbol[symbol];
    const newIdentity = afterResult.membersBySymbol[symbol];
    if (oldIdentity === newIdentity) {
      const renamed = oldDefinition.displayName !== newDefinition.displayName ||
        canonical(oldDefinition.parameters.map((item) => item.name)) !==
          canonical(newDefinition.parameters.map((item) => item.name));
      return { kind: renamed ? "source-rename" : "unchanged", span: newDefinition.span ?? null, symbol };
    }
    const categories = [];
    if (oldDefinition.result !== newDefinition.result ||
        canonical(oldDefinition.parameters.map((item) => item.type)) !==
          canonical(newDefinition.parameters.map((item) => item.type))) categories.push("type");
    if (canonical((oldDefinition.effects ?? []).sort()) !==
        canonical((newDefinition.effects ?? []).sort())) categories.push("effect");
    if (canonical(oldDefinition.parameters.map((item) => item.mode)) !==
        canonical(newDefinition.parameters.map((item) => item.mode))) categories.push("ownership");
    if (categories.length === 0) categories.push("body");
    return { categories, kind: "modified", span: newDefinition.span ?? null, symbol };
  });
}
