#!/usr/bin/env node

import { mkdirSync, writeFileSync } from "node:fs";
import { join } from "node:path";

const outputDirectory = process.argv[2];
const caseCount = Number.parseInt(process.argv[3] ?? "128", 10);
const initialSeed = Number.parseInt(process.argv[4] ?? "5572026", 10) >>> 0;

if (!outputDirectory || !Number.isInteger(caseCount) || caseCount < 1 || caseCount > 512) {
  process.stderr.write("usage: hm_levels_oracle.mjs OUTPUT_DIR CASES(1..512) [SEED]\n");
  process.exit(2);
}

mkdirSync(outputDirectory, { recursive: true });

let randomState = initialSeed;
function random(limit) {
  randomState = (Math.imul(randomState, 1664525) + 1013904223) >>> 0;
  return randomState % limit;
}

let nextTypeVariable = 0;
function variable() {
  return { kind: "variable", id: nextTypeVariable++, link: null };
}
function builtin(name) {
  return { kind: "builtin", name };
}
function callable(argument, result) {
  return { kind: "callable", argument, result };
}

const INT = builtin("Int");
const BOOL = builtin("Bool");
const TEXT = builtin("Text");

function prune(type) {
  if (type.kind === "variable" && type.link) {
    type.link = prune(type.link);
    return type.link;
  }
  return type;
}

function occurs(needle, type) {
  type = prune(type);
  if (type === needle) return true;
  return type.kind === "callable" &&
    (occurs(needle, type.argument) || occurs(needle, type.result));
}

class OracleError extends Error {}

function unify(left, right) {
  left = prune(left);
  right = prune(right);
  if (left === right) return;
  if (left.kind === "variable") {
    if (occurs(left, right)) throw new OracleError("occurs");
    left.link = right;
    return;
  }
  if (right.kind === "variable") {
    unify(right, left);
    return;
  }
  if (left.kind === "builtin" && right.kind === "builtin" && left.name === right.name) {
    return;
  }
  if (left.kind === "callable" && right.kind === "callable") {
    unify(left.argument, right.argument);
    unify(left.result, right.result);
    return;
  }
  throw new OracleError("mismatch");
}

function freeVariables(type, result = new Set()) {
  type = prune(type);
  if (type.kind === "variable") result.add(type);
  if (type.kind === "callable") {
    freeVariables(type.argument, result);
    freeVariables(type.result, result);
  }
  return result;
}

function freeEnvironment(environment) {
  const result = new Set();
  for (const scheme of environment.values()) {
    const quantified = new Set(scheme.variables);
    for (const item of freeVariables(scheme.type)) {
      if (!quantified.has(item)) result.add(item);
    }
  }
  return result;
}

function generalize(type, environment, allowed) {
  if (!allowed) return { variables: [], type };
  const environmentVariables = freeEnvironment(environment);
  const variables = [...freeVariables(type)].filter((item) => !environmentVariables.has(item));
  variables.sort((left, right) => left.id - right.id);
  return { variables, type };
}

function instantiate(scheme) {
  const replacements = new Map(scheme.variables.map((item) => [item, variable()]));
  function visit(type) {
    type = prune(type);
    if (type.kind === "variable") return replacements.get(type) ?? type;
    if (type.kind === "callable") return callable(visit(type.argument), visit(type.result));
    return type;
  }
  return visit(scheme.type);
}

function inferExpression(expression, environment) {
  switch (expression.kind) {
    case "int": return INT;
    case "bool": return BOOL;
    case "text": return TEXT;
    case "variable": {
      const scheme = environment.get(expression.name);
      if (!scheme) throw new OracleError("unknown");
      return instantiate(scheme);
    }
    case "lambda": {
      const argument = variable();
      const nested = new Map(environment);
      nested.set(expression.parameter, { variables: [], type: argument });
      const result = inferExpression(expression.body, nested);
      return callable(argument, result);
    }
    case "application": {
      const callee = inferExpression(expression.callee, environment);
      const argument = inferExpression(expression.argument, environment);
      const result = variable();
      unify(callee, callable(argument, result));
      return result;
    }
    default: throw new OracleError("unknown AST");
  }
}

function inferProgram(program) {
  nextTypeVariable = 0;
  const environment = new Map();
  const schemes = [];
  for (const binding of program.bindings) {
    const type = inferExpression(binding.initializer, environment);
    if (binding.annotation) unify(type, binding.annotation);
    const scheme = generalize(type, environment, binding.initializer.kind === "lambda");
    environment.set(binding.name, scheme);
    schemes.push({ name: binding.name, scheme });
  }
  return { type: inferExpression(program.result, environment), schemes };
}

function typeText(type, quantified = []) {
  const quantifiedNames = new Map(
    quantified.map((item, index) => [item, `'${String.fromCharCode(97 + (index % 26))}${index >= 26 ? Math.floor(index / 26) : ""}`]),
  );
  const names = new Map();
  function visit(item) {
    item = prune(item);
    if (item.kind === "builtin") return item.name;
    if (item.kind === "callable") return `(${visit(item.argument)} -> ${visit(item.result)})`;
    if (quantifiedNames.has(item)) return quantifiedNames.get(item);
    if (!names.has(item)) names.set(item, `_${names.size}`);
    return names.get(item);
  }
  return visit(type);
}

function schemeText(scheme) {
  const body = typeText(scheme.type, scheme.variables);
  if (scheme.variables.length === 0) return body;
  const names = scheme.variables.map(
    (_, index) => `'${String.fromCharCode(97 + (index % 26))}${index >= 26 ? Math.floor(index / 26) : ""}`,
  );
  return `forall ${names.join(",")}. ${body}`;
}

const ast = {
  int: (value) => ({ kind: "int", value }),
  bool: (value) => ({ kind: "bool", value }),
  text: (value) => ({ kind: "text", value }),
  variable: (name) => ({ kind: "variable", name }),
  lambda: (parameter, body) => ({ kind: "lambda", parameter, body }),
  application: (callee, argument) => ({ kind: "application", callee, argument }),
};

function expressionText(expression) {
  switch (expression.kind) {
    case "int": return String(expression.value);
    case "bool": return expression.value ? "true" : "false";
    case "text": return JSON.stringify(expression.value);
    case "variable": return expression.name;
    case "lambda": return `fn(${expression.parameter}) => ${expressionText(expression.body)}`;
    case "application": return `${expressionText(expression.callee)}(${expressionText(expression.argument)})`;
    default: throw new Error("unknown AST");
  }
}

function annotationText(type) {
  if (!type) return "";
  return `: ${typeText(type)}`;
}

function programText(program) {
  const lines = ["fn main() {"];
  for (const binding of program.bindings) {
    lines.push(`    let ${binding.name}${annotationText(binding.annotation)} = ${expressionText(binding.initializer)}`);
  }
  lines.push(`    ${expressionText(program.result)}`);
  lines.push("}");
  return `${lines.join("\n")}\n`;
}

function literal() {
  switch (random(3)) {
    case 0: return ast.int(random(100));
    case 1: return ast.bool(random(2) === 1);
    default: return ast.text(`t${random(100)}`);
  }
}

function knownProgram(index) {
  if (index === 0) {
    return {
      bindings: [
        { name: "id", initializer: ast.lambda("x", ast.variable("x")) },
        { name: "number", initializer: ast.application(ast.variable("id"), ast.int(1)) },
        { name: "flag", initializer: ast.application(ast.variable("id"), ast.bool(true)) },
        { name: "word", initializer: ast.application(ast.variable("id"), ast.text("x")) },
      ],
      result: ast.variable("word"),
    };
  }
  if (index === 1) {
    return {
      bindings: [
        { name: "id", initializer: ast.lambda("x", ast.variable("x")) },
        {
          name: "mono",
          initializer: ast.application(
            ast.variable("id"),
            ast.lambda("value", ast.variable("value")),
          ),
        },
        { name: "first", initializer: ast.application(ast.variable("mono"), ast.int(1)) },
        { name: "second", initializer: ast.application(ast.variable("mono"), ast.bool(true)) },
      ],
      result: ast.variable("second"),
    };
  }
  if (index === 2) {
    return {
      bindings: [
        {
          name: "bad",
          initializer: ast.lambda(
            "x",
            ast.application(ast.variable("x"), ast.variable("x")),
          ),
        },
      ],
      result: ast.variable("bad"),
    };
  }
  return null;
}

function randomProgram(index) {
  const known = knownProgram(index);
  if (known) return known;
  if (index % 4 === 0) {
    const stored = literal();
    return {
      bindings: [
        { name: "v0", initializer: ast.lambda("p0", ast.variable("p0")) },
        { name: "v1", initializer: stored },
        { name: "v2", initializer: ast.application(ast.variable("v0"), ast.variable("v1")) },
        { name: "v3", initializer: ast.lambda("p3", ast.variable("v2")) },
        { name: "v4", initializer: ast.application(ast.variable("v3"), literal()) },
      ],
      result: ast.variable("v4"),
    };
  }
  if (index % 4 === 1) {
    return {
      bindings: [
        { name: "v0", initializer: literal() },
        { name: "v1", initializer: ast.lambda("p1", ast.variable("v0")) },
        { name: "v2", initializer: ast.application(ast.variable("v1"), literal()) },
      ],
      result: ast.variable("v2"),
    };
  }
  const bindings = [];
  const names = [];
  const bindingCount = 2 + random(5);
  for (let slot = 0; slot < bindingCount; slot++) {
    const name = `v${slot}`;
    let initializer;
    const choice = random(7);
    if (choice === 0 || names.length === 0) {
      initializer = literal();
    } else if (choice === 1) {
      const parameter = `p${slot}`;
      initializer = ast.lambda(parameter, ast.variable(parameter));
    } else if (choice === 2) {
      const parameter = `p${slot}`;
      initializer = ast.lambda(parameter, ast.variable(names[random(names.length)]));
    } else if (choice === 3) {
      initializer = ast.application(
        ast.variable(names[random(names.length)]),
        literal(),
      );
    } else if (choice === 4) {
      const parameter = `p${slot}`;
      initializer = ast.application(
        ast.variable(names[random(names.length)]),
        ast.lambda(parameter, ast.variable(parameter)),
      );
    } else if (choice === 5) {
      const parameter = `p${slot}`;
      initializer = ast.lambda(
        parameter,
        ast.application(ast.variable(parameter), ast.variable(parameter)),
      );
    } else {
      initializer = ast.variable(names[random(names.length)]);
    }
    const annotationChoice = random(8);
    let annotation;
    if (annotationChoice === 0) annotation = INT;
    if (annotationChoice === 1) annotation = BOOL;
    if (annotationChoice === 2) annotation = TEXT;
    bindings.push({ name, initializer, annotation });
    names.push(name);
  }
  return { bindings, result: ast.variable(names[names.length - 1]) };
}

const manifest = [];
let accepted = 0;
let rejected = 0;
for (let index = 0; index < caseCount; index++) {
  const program = randomProgram(index);
  const file = `case-${String(index).padStart(4, "0")}.kofun`;
  let status = "accepted";
  let result = "-";
  let inferred;
  try {
    inferred = inferProgram(program);
    result = typeText(inferred.type);
    accepted++;
  } catch (error) {
    if (!(error instanceof OracleError)) throw error;
    status = "rejected";
    rejected++;
  }
  writeFileSync(join(outputDirectory, file), programText(program), "utf8");
  if (inferred) {
    const schemes = inferred.schemes
      .map(({ name, scheme }) => `${name}|${schemeText(scheme)}`)
      .sort();
    writeFileSync(
      join(outputDirectory, `${file}.schemes`),
      `${schemes.join("\n")}\n`,
      "utf8",
    );
  }
  manifest.push(`${file}\t${status}\t${result}`);
}

writeFileSync(join(outputDirectory, "expected.tsv"), `${manifest.join("\n")}\n`, "utf8");
process.stdout.write(`oracle cases=${caseCount} accepted=${accepted} rejected=${rejected} seed=${initialSeed}\n`);
