#!/usr/bin/env node

import fs from "node:fs";

function fail(message) {
  process.stderr.write(`pure/io report: ${message}\n`);
  process.exit(1);
}

const inventory = process.argv[2] === "--inventory";
const offset = inventory ? 1 : 0;
if (process.argv.length !== 4 + offset) {
  fail("usage: report.mjs [--inventory] SIDECAR SOURCE");
}

const sidecar = JSON.parse(fs.readFileSync(process.argv[2 + offset], "utf8"));
const source = fs.readFileSync(process.argv[3 + offset], "utf8");
const functions = sidecar.nodes.filter((node) =>
  node.kind === "function.declaration");
const byId = new Map(functions.map((node) => [node.id, node]));

function functionName(node) {
  const declaration = source.slice(node.span.start, node.span.end);
  const match = /\bfn\s+([\p{L}_][\p{L}\p{N}_]*)/u.exec(declaration);
  if (!match) fail(`function span ${node.span.start}..${node.span.end} has no name`);
  return match[1];
}

const names = new Map(functions.map((node) => [node.id, functionName(node)]));
const rows = functions.map((node) => {
  if (!node.effect || node.effect.status !== "validated" ||
      !["pure", "io"].includes(node.effect.display)) {
    fail(`function ${names.get(node.id)} has no unique validated effect`);
  }
  const calleeNames = node.depends_on
    .filter((id) => byId.has(id))
    .map((id) => names.get(id))
    .sort((left, right) => left.localeCompare(right, "en"));
  const reason = node.effect.reason ?? "-";
  let forcing = "-";
  if (reason === "effect-io-callee") {
    if (calleeNames.length !== 1) {
      fail(`${names.get(node.id)} must name exactly one forcing callee`);
    }
    [forcing] = calleeNames;
  } else if (reason === "effect-io-root-print") {
    if (calleeNames.length !== 0) {
      fail(`${names.get(node.id)} direct print root has a forcing callee`);
    }
    forcing = "print";
  } else if (node.effect.display === "io" || reason !== "-") {
    fail(`${names.get(node.id)} has inconsistent effect reason ${reason}`);
  }
  return { effect: node.effect.display, forcing, name: names.get(node.id), reason };
}).sort((left, right) => left.name.localeCompare(right.name, "en"));

if (new Set(rows.map((row) => row.name)).size !== rows.length) {
  fail("function names are not unique");
}

if (inventory) {
  const pure = rows.filter((row) => row.effect === "pure").length;
  const io = rows.length - pure;
  if (rows.length === 0 || pure * 100 < rows.length * 80) {
    fail(`tracked corpus is below 80% pure (${pure}/${rows.length})`);
  }
  const basisPoints = Math.floor((pure * 10000) / rows.length);
  process.stdout.write(
    `pure|${pure}\nio|${io}\ntotal|${rows.length}\n` +
    `pure-percent-basis-points|${basisPoints}\n`,
  );
} else {
  process.stdout.write(rows.map((row) =>
    `${row.name}|${row.effect}|${row.reason}|${row.forcing}`).join("\n") + "\n");
}
