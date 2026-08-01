#!/usr/bin/env node

import { existsSync, readFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

import { taskfileTasks } from "../lib/taskfile.mjs";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");
const DEFAULT_MATRIX = "spec/artifact-qualification/kif-sidecar-v1.json";
const REQUIRED_EVIDENCE = [
  "authority-boundary",
  "atomic-newest-winner",
  "cold-warm-artifact-bytes",
  "decode-project-latency",
  "determinism-source-order-path-remap",
  "disclosure",
  "malformed-corrupt-oversized-deep",
  "peak-memory",
  "version-accept-migrate-reject",
];

const POLICY = Object.freeze({
  "kif-v1": Object.freeze({
    media_schema: "kofun.kif/v1",
    authority: "compiler",
    limits: Object.freeze({ document_bytes: 16 * 1024 * 1024, max_depth: 128 }),
    budgets: Object.freeze({
      fixture_bytes: 1024 * 1024,
      cold_decode_ns: 250_000_000,
      warm_decode_p95_ns: 100_000_000,
      peak_rss_kib: 524_288,
    }),
    gates: Object.freeze(["incremental", "kif-v1", "re-exports", "stage2-kif-producer"]),
  }),
  "typed-sidecar-v1": Object.freeze({
    media_schema: "kofun.typed-sidecar/v1",
    authority: "tooling-only",
    limits: Object.freeze({ document_bytes: 16 * 1024 * 1024, max_depth: 128 }),
    budgets: Object.freeze({
      fixture_bytes: 1024 * 1024,
      cold_decode_ns: 250_000_000,
      warm_decode_p95_ns: 100_000_000,
      cold_project_ns: 250_000_000,
      warm_project_p95_ns: 100_000_000,
      peak_rss_kib: 524_288,
    }),
    gates: Object.freeze([
      "lsp", "ownership-view", "typed-sidecar-codec", "typed-sidecar-projector",
      "typed-sidecar-spec",
    ]),
    forbidden_authority_consumers: Object.freeze([
      "build", "cache", "compiler", "kif", "linker", "package",
    ]),
  }),
});

class Report {
  constructor() {
    this.errors = [];
  }

  fail(subject, message, repair) {
    this.errors.push(`artifact-qualification: ${subject}: ${message}. Repair: ${repair}`);
  }
}

function same(left, right) {
  return JSON.stringify(left) === JSON.stringify(right);
}

function sortedUnique(values) {
  return Array.isArray(values) && values.length > 0 &&
    same(values, [...new Set(values)].sort());
}

function checkExactObject(report, subject, actual, expected, label) {
  if (!same(actual, expected)) {
    report.fail(subject, `${label} differs from the pinned release policy`,
      `restore ${label}; changing a release bound requires changing and reviewing the validator policy too`);
  }
}

function checkEvidencePath(report, subject, value) {
  if (typeof value !== "string" || value.startsWith("/") || value.includes("..") ||
      value.includes("//") || !existsSync(path.join(ROOT, value))) {
    report.fail(subject, `evidence path ${JSON.stringify(value)} is not a repository file`,
      "name an existing normalized repository-relative evidence file");
  }
}

function validateProductionConstants(report) {
  const kif = readFileSync(path.join(ROOT, "bootstrap/stage2/kif_v1.h"), "utf8");
  if (!/#define KOFUN_KIF_MAX_ENVELOPE \(16u \* 1024u \* 1024u\)/.test(kif) ||
      !/#define KOFUN_KIF_MAX_DEPTH 128u/.test(kif)) {
    report.fail("kif-v1", "matrix policy no longer matches the KIF production constants",
      "review the KIF compatibility row and update the policy with the production limit change");
  }

  const sidecar = readFileSync(path.join(ROOT, "tooling/typed-sidecar/codec.mjs"), "utf8");
  if (!/documentBytes: 16 \* 1024 \* 1024/.test(sidecar) ||
      !/maxDepth: 128/.test(sidecar)) {
    report.fail("typed-sidecar-v1", "matrix policy no longer matches the sidecar production constants",
      "review the sidecar compatibility row and update the policy with the production limit change");
  }
}

export function validateMatrix(matrix) {
  const report = new Report();
  if (matrix === null || typeof matrix !== "object" || Array.isArray(matrix)) {
    report.fail("matrix", "root is not an object", "write a JSON object with schema and artifacts");
    return report;
  }
  if (matrix.schema !== "kofun.artifact-qualification/v1") {
    report.fail("matrix", "schema is not kofun.artifact-qualification/v1",
      "use the checked v1 qualification schema name");
  }
  if (!Array.isArray(matrix.artifacts)) {
    report.fail("matrix", "artifacts is not an array", "declare the two artifact rows");
    return report;
  }
  if (!same(matrix.artifacts.map((row) => row?.id), Object.keys(POLICY).sort())) {
    report.fail("matrix", "artifact rows are missing, duplicated, or out of order",
      "declare exactly kif-v1 and typed-sidecar-v1 in ascending order");
    return report;
  }

  const tasks = taskfileTasks(ROOT);
  for (const row of matrix.artifacts) {
    const subject = `artifact ${JSON.stringify(row.id)}`;
    const policy = POLICY[row.id];
    if (row.media_schema !== policy.media_schema) {
      report.fail(subject, `media schema ${JSON.stringify(row.media_schema)} is not ${policy.media_schema}`,
        "restore the reviewed media schema");
    }
    if (row.authority !== policy.authority) {
      report.fail(subject, `authority ${JSON.stringify(row.authority)} widens or changes trust`,
        `restore authority ${JSON.stringify(policy.authority)}`);
    }
    if (!same(row.compatibility, { accept: [1], migrate: [], unknown_future: "bounded-reject" })) {
      report.fail(subject, "compatibility must accept v1, define no implicit migration, and bounded-reject future versions",
        "restore the explicit v1 accept/no-migration/future-reject decision");
    }
    checkExactObject(report, subject, row.limits, policy.limits, "production limits");
    checkExactObject(report, subject, row.budgets, policy.budgets, "measurement budgets");

    const categories = row.evidence && typeof row.evidence === "object" &&
      !Array.isArray(row.evidence) ? Object.keys(row.evidence) : [];
    if (!same(categories, REQUIRED_EVIDENCE)) {
      report.fail(subject, "evidence categories are missing, extra, or out of order",
        `declare exactly ${REQUIRED_EVIDENCE.join(", ")}`);
    } else {
      for (const category of categories) {
        const paths = row.evidence[category];
        if (!sortedUnique(paths)) {
          report.fail(subject, `${category} evidence is empty, duplicated, or not sorted`,
            "list one or more unique evidence files in ascending order");
          continue;
        }
        for (const evidencePath of paths) checkEvidencePath(report, subject, evidencePath);
      }
    }

    checkExactObject(report, subject, row.gates, policy.gates, "integration gates");
    if (Array.isArray(row.gates)) {
      for (const gate of row.gates) {
        if (!tasks.has(gate)) {
          report.fail(subject, `integration gate ${JSON.stringify(gate)} is absent from Taskfile.yml`,
            "restore the task or update the reviewed policy");
        }
      }
    }
    if (policy.forbidden_authority_consumers) {
      checkExactObject(report, subject, row.forbidden_authority_consumers,
        policy.forbidden_authority_consumers, "forbidden authority consumers");
    } else if (Object.hasOwn(row, "forbidden_authority_consumers")) {
      report.fail(subject, "compiler-authoritative KIF declares tooling-style forbidden consumers",
        "remove forbidden_authority_consumers from the KIF row");
    }
  }
  validateProductionConstants(report);
  return report;
}

function main() {
  const relative = process.argv[2] ?? DEFAULT_MATRIX;
  const absolute = path.resolve(ROOT, relative);
  let text;
  let matrix;
  try {
    text = readFileSync(absolute, "utf8");
    matrix = JSON.parse(text);
  } catch (error) {
    process.stderr.write(`artifact-qualification: matrix: cannot read JSON: ${error.message}\n`);
    process.exit(1);
  }
  const report = validateMatrix(matrix);
  for (const error of report.errors) process.stderr.write(`${error}\n`);
  if (report.errors.length > 0) process.exit(1);
  if (relative === DEFAULT_MATRIX && `${JSON.stringify(matrix, null, 2)}\n` !== text) {
    process.stderr.write("artifact-qualification: matrix: JSON is not canonical two-space JSON with a final newline\n");
    process.exit(1);
  }
  process.stdout.write(`PASS: ${matrix.artifacts.length} artifact rows retain compatibility, evidence, authority, and budget policy\n`);
}

if (process.argv[1] === fileURLToPath(import.meta.url)) main();
