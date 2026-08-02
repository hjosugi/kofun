import assert from "node:assert/strict";
import fs from "node:fs";

const [cPath, kofunPath, mode = "check"] = process.argv.slice(2);
if (!cPath || !kofunPath) {
  throw new Error("usage: check.mjs COMPILER_C COMPILER_KOFUN [check|self-test]");
}

const cSource = fs.readFileSync(cPath, "utf8");
const kofunSource = fs.readFileSync(kofunPath, "utf8");

function functionNames(source, language) {
  const expression = language === "c"
    ? /^static\s+[^\n(]*?\b([A-Za-z_][A-Za-z0-9_]*)\s*\(/gm
    : /^fn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(/gm;
  return new Set(
    [...source.matchAll(expression)]
      .map((match) => match[1])
      .filter((name) => name.includes("optional_int")),
  );
}

function callCount(source, name) {
  const escaped = name.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  return [...source.matchAll(new RegExp(`\\b${escaped}\\s*\\(`, "g"))].length;
}

const minimumCalls = new Map([
  ["optional_int_type_end", 8],
  ["source_uses_optional_int", 4],
  ["optional_int_result", 4],
  ["optional_int_binding", 5],
  ["optional_int_carrier_position", 2],
  ["optional_int_condition", 4],
  ["optional_int_block_returns", 2],
  ["optional_int_refined", 2],
  ["optional_int_null_context", 2],
  ["optional_int_declaration_count", 3],
  ["optional_int_value", 3],
  ["validate_optional_uses", 2],
  ["emit_optional_int_c_declarations", 2],
]);

const semanticAnchors = [
  {
    id: "pre-lowering-validation",
    c: "char *optional_use_check = validate_optional_uses(source);",
    kofun: "let optional_use_check = validate_optional_uses(source)",
  },
  {
    id: "whole-result-c-type",
    c: "if (optional_int_result(source, name)) { c_result = OPTIONAL_INT_C_TYPE;",
    kofun: "if optional_int_result(source, name) { c_result = \"KofunOptionalInt\"",
  },
  {
    id: "runtime-declarations",
    c: "if (source_uses_optional_int(source)) { char *optional_declarations = emit_optional_int_c_declarations();",
    kofun: "if source_uses_optional_int(source) { optional_declarations = emit_optional_int_c_declarations()",
  },
  {
    id: "proved-payload-projection",
    c: "optional_int_binding(source, cursor, name) && !optional_int_carrier_position(source, cursor)",
    kofun: "optional_int_binding(source, cursor, name) && !optional_int_carrier_position(source, cursor)",
  },
  {
    id: "null-is-contextual-not-a-binding",
    c: "!optional_int_null_context(source, cursor)",
    kofun: "!optional_int_null_context(source, cursor)",
  },
  {
    id: "condition-is-tag-test",
    c: "OptionalCondition optional = optional_int_condition(",
    kofun: "let optional = optional_int_condition(source, cursor, end)",
  },
  {
    id: "let-and-return-carry-whole-values",
    c: "char *value = optional_int_value(",
    kofun: "let value = optional_int_value(",
  },
];

function compact(source) {
  return source.replace(/\s+/g, " ");
}

function verifyPair(cText, kofunText) {
  const failures = [];
  const cFunctions = functionNames(cText, "c");
  const kofunFunctions = functionNames(kofunText, "kofun");

  // C returns the condition kind and binding byte through one out-parameter.
  // Kofun has no out-parameters, so this one helper is an intentional shape
  // difference rather than a semantic-family omission.
  kofunFunctions.delete("optional_int_condition_binding");

  for (const name of cFunctions) {
    if (!kofunFunctions.has(name)) {
      failures.push(`missing Kofun function: ${name}`);
    }
  }
  for (const name of kofunFunctions) {
    if (!cFunctions.has(name)) {
      failures.push(`missing C function: ${name}`);
    }
  }

  for (const [name, minimum] of minimumCalls) {
    for (const [side, source] of [["C", cText], ["Kofun", kofunText]]) {
      const observed = callCount(source, name);
      if (observed < minimum) {
        failures.push(
          `${side} dispatch ${name}: expected at least ${minimum}, saw ${observed}`,
        );
      }
    }
  }


  const compactC = compact(cText);
  const compactKofun = compact(kofunText);
  for (const anchor of semanticAnchors) {
    if (!compactC.includes(compact(anchor.c))) {
      failures.push(`C semantic dispatch missing: ${anchor.id}`);
    }
    if (!compactKofun.includes(compact(anchor.kofun))) {
      failures.push(`Kofun semantic dispatch missing: ${anchor.id}`);
    }
  }

  for (const [label, token] of [
    ["carrier representation", "KofunOptionalInt"],
    ["absence value", "KOFUN_OPTIONAL_INT_NONE"],
    ["present injection", "KOFUN_OPTIONAL_INT_SOME("],
    ["proved payload projection", ".payload"],
    ["stable refusal", "E2S147"],
  ]) {
    if (!cText.includes(token)) failures.push(`C missing ${label}: ${token}`);
    if (!kofunText.includes(token)) failures.push(`Kofun missing ${label}: ${token}`);
  }
  return failures;
}

function requireFailure(failures, expected) {
  assert.ok(
    failures.some((failure) => failure.includes(expected)),
    `mutation did not produce named failure ${expected}: ${failures.join("; ")}`,
  );
}

const failures = verifyPair(cSource, kofunSource);
if (failures.length > 0) {
  throw new Error(failures.join("\n"));
}

if (mode === "self-test") {
  const renamedKofun = kofunSource.replace(
    "fn optional_int_type_end(",
    "fn removed_type_end(",
  );
  assert.notEqual(renamedKofun, kofunSource, "Kofun member mutation did not apply");
  requireFailure(
    verifyPair(cSource, renamedKofun),
    "missing Kofun function: optional_int_type_end",
  );

  const renamedC = cSource.replace(
    "static char *optional_int_value(",
    "static char *removed_value(",
  );
  assert.notEqual(renamedC, cSource, "C member mutation did not apply");
  requireFailure(
    verifyPair(renamedC, kofunSource),
    "missing C function: optional_int_value",
  );

  const unvalidatedKofun = kofunSource.replace(
    "let optional_use_check = validate_optional_uses(source)",
    "let optional_use_check = \"ok\"",
  );
  assert.notEqual(
    unvalidatedKofun,
    kofunSource,
    "Kofun dispatch mutation did not apply",
  );
  requireFailure(
    verifyPair(cSource, unvalidatedKofun),
    "Kofun semantic dispatch missing: pre-lowering-validation",
  );

  const unvalidatedC = cSource.replace(
    "char *optional_use_check = validate_optional_uses(source);",
    "char *optional_use_check = owned_text(\"ok\");",
  );
  assert.notEqual(
    unvalidatedC,
    cSource,
    "C dispatch mutation did not apply",
  );
  requireFailure(
    verifyPair(unvalidatedC, kofunSource),
    "C semantic dispatch missing: pre-lowering-validation",
  );

  console.log("PASS: pair drift mutations fail by missing member and dispatch name");
}

console.log("PASS: Optional(Int) semantic family and load-bearing dispatches exist on both canonical surfaces");
