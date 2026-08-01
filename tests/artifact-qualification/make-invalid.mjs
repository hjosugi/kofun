#!/usr/bin/env node

import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");
const output = process.argv[2];
if (!output) throw new Error("usage: make-invalid.mjs OUTPUT_DIRECTORY");

const source = JSON.parse(readFileSync(
  path.join(ROOT, "spec/artifact-qualification/kif-sidecar-v1.json"), "utf8"));

const mutations = {
  "accept-future-version": (matrix) => matrix.artifacts[0].compatibility.accept.push(2),
  "drop-authority-consumer": (matrix) => matrix.artifacts[1].forbidden_authority_consumers.shift(),
  "drop-disclosure-evidence": (matrix) => delete matrix.artifacts[1].evidence.disclosure,
  "drop-integration-gate": (matrix) => matrix.artifacts[1].gates.shift(),
  "invent-migration": (matrix) => matrix.artifacts[0].compatibility.migrate.push(0),
  "relax-measurement-budget": (matrix) => matrix.artifacts[0].budgets.peak_rss_kib += 1,
  "relax-production-limit": (matrix) => matrix.artifacts[1].limits.document_bytes += 1,
  "widen-sidecar-authority": (matrix) => { matrix.artifacts[1].authority = "compiler"; },
};

mkdirSync(output, { recursive: true });
for (const [name, mutate] of Object.entries(mutations)) {
  const matrix = structuredClone(source);
  mutate(matrix);
  writeFileSync(path.join(output, `${name}.json`), `${JSON.stringify(matrix, null, 2)}\n`);
}
writeFileSync(path.join(output, "names.txt"), `${Object.keys(mutations).join("\n")}\n`);
