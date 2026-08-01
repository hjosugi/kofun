import fs from "node:fs";

const LIMITS = Object.freeze({ operations: 64, branches: 8, captures: 8, transfers: 8 });
const KNOWN = new Set(["consume", "drop", "if", "transfer", "escape", "loop", "clone"]);

function fail(message) {
  throw new Error(message);
}
function exact(value, keys, label) {
  if (value === null || typeof value !== "object" || Array.isArray(value)) fail(`${label} must be an object`);
  const actual = Object.keys(value).sort();
  const wanted = [...keys].sort();
  if (actual.length !== wanted.length || actual.some((key, i) => key !== wanted[i])) {
    fail(`${label} has unknown or missing fields`);
  }
}

function sourceSpan(value, label) {
  exact(value, ["end", "file", "start"], label);
  if (typeof value.file !== "string" || value.file.length === 0 ||
      !Number.isSafeInteger(value.start) || !Number.isSafeInteger(value.end) ||
      value.start < 0 || value.start >= value.end) fail(`${label} is not a stable half-open span`);
  return value;
}

function diagnostic(code, primary, related) {
  return { code, primary, related };
}

function checkFixture(document) {
  exact(document, ["capture", "expect", "name", "program", "schema"], "fixture");
  if (document.schema !== "kofun.affine-resumption-model/v1") fail("fixture schema is incompatible");
  exact(document.expect, ["capture_span", "end_span", "primary_span", "related_span", "result"], "expect");
  if (!Array.isArray(document.capture) || document.capture.length > LIMITS.captures) fail("capture limit exceeded");
  if (!Array.isArray(document.program)) fail("program must be an array");
  const captureSpan = sourceSpan(document.expect.capture_span, "expect.capture_span");
  sourceSpan(document.expect.end_span, "expect.end_span");
  if (document.expect.primary_span !== null) sourceSpan(document.expect.primary_span, "expect.primary_span");
  if (document.expect.related_span !== null) sourceSpan(document.expect.related_span, "expect.related_span");
  for (const [index, capture] of document.capture.entries()) {
    exact(capture, ["cleanup_owner", "mode", "name", "span"], `capture[${index}]`);
    if (!["read", "edit", "take"].includes(capture.mode)) fail(`capture[${index}].mode is invalid`);
    sourceSpan(capture.span, `capture[${index}].span`);
    if (capture.mode === "take" && capture.cleanup_owner !== "handler-scope") {
      return diagnostic("EAF008", captureSpan, capture.span);
    }
  }
  const counter = { operations: 0, transfers: 0 };

  function walk(program, state, depth) {
    if (depth > LIMITS.branches) fail("branch limit exceeded");
    for (const [index, operation] of program.entries()) {
      counter.operations += 1;
      if (counter.operations > LIMITS.operations) fail("operation limit exceeded");
      if (operation === null || typeof operation !== "object" || Array.isArray(operation) || !KNOWN.has(operation.op)) {
        fail(`operation ${index} is invalid`);
      }
      if (operation.op === "consume" || operation.op === "drop") {
        exact(operation, ["op", "span"], `operation ${index}`);
        const current = sourceSpan(operation.span, `operation ${index}.span`);
        if (state.kind !== "available") return { error: diagnostic("EAF001", current, state.terminalSpan) };
        state = { kind: operation.op === "consume" ? "resumed" : "dropped", terminalSpan: current };
      } else if (operation.op === "if") {
        exact(operation, ["else", "op", "span", "then"], `operation ${index}`);
        sourceSpan(operation.span, `operation ${index}.span`);
        if (!Array.isArray(operation.then) || !Array.isArray(operation.else)) fail("if arms must be arrays");
        const left = walk(operation.then, { ...state }, depth + 1);
        if (left.error) return left;
        const right = walk(operation.else, { ...state }, depth + 1);
        if (right.error) return right;
        if (left.state.kind === "available" || right.state.kind === "available") {
          return { error: diagnostic("EAF006", operation.span, captureSpan) };
        }
        state = { kind: "terminal-alternative", terminalSpan: operation.span };
      } else if (operation.op === "transfer") {
        exact(operation, ["body", "known_affine_local", "op", "recursive", "span"], `operation ${index}`);
        const current = sourceSpan(operation.span, `operation ${index}.span`);
        counter.transfers += 1;
        if (counter.transfers > LIMITS.transfers) fail("transfer limit exceeded");
        if (operation.recursive) return { error: diagnostic("EAF004", current, captureSpan) };
        if (!operation.known_affine_local) return { error: diagnostic("EAF003", current, captureSpan) };
        if (state.kind !== "available") return { error: diagnostic("EAF001", current, state.terminalSpan) };
        if (!Array.isArray(operation.body)) fail("transfer body must be an array");
        const moved = walk(operation.body, { ...state }, depth + 1);
        if (moved.error) return moved;
        if (moved.state.kind === "available") return { error: diagnostic("EAF006", current, captureSpan) };
        state = moved.state;
      } else if (operation.op === "escape") {
        exact(operation, ["kind", "op", "span"], `operation ${index}`);
        const kinds = new Set(["closure", "store", "return", "quote", "container", "unknown-parameter", "erase"]);
        if (!kinds.has(operation.kind)) fail("escape kind is invalid");
        const code = operation.kind === "unknown-parameter" ? "EAF003" : "EAF002";
        return { error: diagnostic(code, sourceSpan(operation.span, `operation ${index}.span`), captureSpan) };
      } else if (operation.op === "loop") {
        exact(operation, ["body", "op", "span"], `operation ${index}`);
        return { error: diagnostic("EAF005", sourceSpan(operation.span, `operation ${index}.span`), captureSpan) };
      } else if (operation.op === "clone") {
        exact(operation, ["op", "span"], `operation ${index}`);
        return { error: diagnostic("EAF007", sourceSpan(operation.span, `operation ${index}.span`), captureSpan) };
      }
    }
    return { state };
  }

  const result = walk(document.program, { kind: "available", terminalSpan: null }, 0);
  if (result.error) return result.error;
  if (result.state.kind === "available") return diagnostic("EAF006", document.expect.end_span, captureSpan);
  return null;
}

function runtimeBackstop() {
  function project(backend) {
    let state = "available";
    let cleanupRuns = 0;
    let ownershipTransfers = 0;
    let stdout = "";
    const resume = () => {
      if (state !== "available") {
        return {
          backend,
          stdout,
          stderr: "EAFR01: affine resumption already consumed\n",
          exit_category: "language-runtime-error",
          cleanup_owner: "handler-scope",
          cleanup_runs: cleanupRuns,
          ownership_transfers: ownershipTransfers,
        };
      }
      state = "consumed";
      ownershipTransfers += 1;
      stdout += "continued-once\n";
      cleanupRuns += 1;
      return null;
    };
    resume();
    return resume();
  }
  const interpreter = project("interpreter-model");
  const native = project("native-c11-model");
  const observation = ({ backend: _backend, ...rest }) => rest;
  if (JSON.stringify(observation(interpreter)) !== JSON.stringify(observation(native))) {
    fail("runtime backstop observations differ across model backends");
  }
  if (interpreter.cleanup_runs !== 1 || interpreter.ownership_transfers !== 1) {
    fail("runtime backstop duplicated cleanup or captured take authority");
  }
  return interpreter;
}

function load(file) {
  return JSON.parse(fs.readFileSync(file, "utf8"));
}

function main(args) {
  if (args[0] === "check" && args.length === 2) {
    const fixture = load(args[1]);
    const result = checkFixture(fixture);
    const wanted = fixture.expect.result;
    if (wanted === "accept") {
      if (result !== null) fail(`${fixture.name}: expected accept, got ${result.code}`);
    } else {
      if (result === null || result.code !== wanted) fail(`${fixture.name}: expected ${wanted}`);
      if (JSON.stringify(result.primary) !== JSON.stringify(fixture.expect.primary_span) ||
          JSON.stringify(result.related) !== JSON.stringify(fixture.expect.related_span)) {
        fail(`${fixture.name}: diagnostic spans drifted`);
      }
    }
    console.log(`${fixture.name}: ${wanted}`);
  } else if (args[0] === "runtime" && args.length === 1) {
    console.log(JSON.stringify(runtimeBackstop(), null, 2));
  } else {
    fail("usage: model.mjs check FIXTURE | runtime");
  }
}

try {
  main(process.argv.slice(2));
} catch (error) {
  console.error(`affine-resumption-model: ${error.message}`);
  process.exitCode = 1;
}
