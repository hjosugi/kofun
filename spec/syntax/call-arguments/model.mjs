import crypto from "node:crypto";

export class CallModelError extends Error {
  constructor(code, message) {
    super(`${code}: ${message}`);
    this.name = "CallModelError";
    this.code = code;
  }
}

const fail = (code, message) => {
  throw new CallModelError(code, message);
};

function stable(value) {
  if (Array.isArray(value)) return `[${value.map(stable).join(",")}]`;
  if (value !== null && typeof value === "object") {
    return `{${Object.keys(value).sort().map((key) =>
      `${JSON.stringify(key)}:${stable(value[key])}`).join(",")}}`;
  }
  return JSON.stringify(value);
}

export function interfaceFingerprint(signature) {
  const publicShape = {
    effects: [...(signature.effects ?? [])].sort(),
    name: signature.name,
    parameters: signature.parameters.map((parameter) => ({
      external: parameter.external ?? "unlabelled",
      mode: parameter.mode ?? "read",
      type: parameter.type,
    })),
    result: signature.result,
  };
  return crypto.createHash("sha256")
    .update("kofun.call-interface/v1\0")
    .update(stable(publicShape))
    .digest("hex");
}

export function bindCall(signature, call) {
  const parameters = signature.parameters;
  const bound = Array(parameters.length).fill(null);
  const evaluation = [];
  let sawLabel = false;
  let nextPositional = 0;

  if (call.pipeline) {
    if (parameters.length === 0) fail("CALL13", "pipeline target has no parameter");
    bound[0] = { expression: call.pipeline, origin: "pipeline", temp: evaluation.length };
    evaluation.push(call.pipeline);
    nextPositional = 1;
  }

  for (const argument of call.arguments ?? []) {
    if (argument.label === null || argument.label === undefined) {
      if (sawLabel) fail("CALL05", "positional argument follows a labelled argument");
      while (nextPositional < parameters.length && bound[nextPositional] !== null) {
        nextPositional += 1;
      }
      if (nextPositional >= parameters.length) fail("CALL06", "too many positional arguments");
      if (parameters[nextPositional].external) {
        fail("CALL07", `parameter ${parameters[nextPositional].external} requires its label`);
      }
      bound[nextPositional] = {
        expression: argument.expression,
        origin: "positional",
        temp: evaluation.length,
      };
      nextPositional += 1;
    } else {
      sawLabel = true;
      const index = parameters.findIndex((parameter) => parameter.external === argument.label);
      if (index < 0) fail("CALL01", `unknown label ${argument.label}`);
      if (bound[index] !== null) fail("CALL02", `duplicate binding for ${argument.label}`);
      bound[index] = {
        expression: argument.expression,
        origin: `label:${argument.label}`,
        temp: evaluation.length,
      };
    }
    evaluation.push(argument.expression);
  }

  if (call.trailingLambda) {
    const index = parameters.length - 1;
    if (index < 0 || parameters[index].type !== "Function") {
      fail("CALL08", "trailing lambda requires a final Function parameter");
    }
    if (bound[index] !== null) fail("CALL09", "final Function parameter is already bound");
    bound[index] = {
      expression: call.trailingLambda,
      origin: "trailing-lambda",
      temp: evaluation.length,
    };
    evaluation.push(call.trailingLambda);
  }

  for (let index = 0; index < parameters.length; index += 1) {
    if (bound[index] === null) {
      const name = parameters[index].external ?? parameters[index].internal;
      fail("CALL03", `missing argument ${name}`);
    }
  }

  const lowering = evaluation.map((expression, index) => ({
    op: "eval-temp",
    expression,
    temp: index,
  }));
  for (let slot = 0; slot < bound.length; slot += 1) {
    lowering.push({
      op: "store-slot",
      slot,
      temp: bound[slot].temp,
    });
  }
  lowering.push({ op: "call", target: signature.name, slots: bound.length });

  return {
    abi: bound.map((entry, slot) => ({ expression: entry.expression, slot })),
    evaluation,
    lowering,
  };
}

export function classifySurface(source) {
  const normalized = source.replace(/#[^\n]*/g, " ").replace(/\s+/g, " ").trim();
  const fnCount = (normalized.match(/\bfn\s*\(/g) ?? []).length;
  if (/\)\s+fn\s+[A-Za-z_]/.test(normalized)) {
    return { kind: "separate-declaration", trailingCount: 0 };
  }
  if (fnCount === 0) return { kind: "ordinary-call", trailingCount: 0 };
  if (fnCount > 1 && /\)\s+fn\s*\([^)]*\)[^{=]*=>[^)]*\s+fn\s*\(/.test(normalized)) {
    fail("CALL10", "more than one trailing lambda");
  }
  return {
    kind: normalized.includes("|>") ? "pipeline-trailing-call" : "trailing-call",
    trailingCount: 1,
  };
}

export function assertNoRuntimeLabels(lowering) {
  const allowed = new Set(["eval-temp", "store-slot", "call"]);
  if (!lowering.every((instruction) => allowed.has(instruction.op))) {
    fail("CALL12", "lowering contains runtime label machinery");
  }
  return true;
}
