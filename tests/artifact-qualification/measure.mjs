#!/usr/bin/env node

import { createHash } from "node:crypto";
import { execFileSync } from "node:child_process";
import { mkdirSync, readFileSync, statSync, writeFileSync } from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

import { encodeTypedSidecar, readTypedSidecar } from "../../tooling/typed-sidecar/codec.mjs";
import { projectOwnershipView } from "../../tooling/typed-sidecar/ownership-view.mjs";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");
const [output, helper, coldKif, warmKif] = process.argv.slice(2);
if (!output || !helper || !coldKif || !warmKif) {
  throw new Error("usage: measure.mjs OUTPUT.json KIF_HELPER COLD.kif WARM.kif");
}

const matrix = JSON.parse(readFileSync(
  path.join(ROOT, "spec/artifact-qualification/kif-sidecar-v1.json"), "utf8"));
const rows = Object.fromEntries(matrix.artifacts.map((row) => [row.id, row]));
const sha256 = (bytes) => createHash("sha256").update(bytes).digest("hex");
const timed = (operation) => {
  const start = process.hrtime.bigint();
  const value = operation();
  return { value, nanoseconds: Number(process.hrtime.bigint() - start) };
};
const p95 = (samples) => {
  const ordered = [...samples].sort((left, right) => left - right);
  return ordered[Math.ceil(ordered.length * 0.95) - 1];
};
const requireOk = (result, label) => {
  if (!result.ok) throw new Error(`${label}: ${result.error?.message ?? "operation failed"}`);
  return result;
};

const coldKifBytes = readFileSync(coldKif);
const warmKifBytes = readFileSync(warmKif);
const kif = JSON.parse(execFileSync(helper, [coldKif], { encoding: "utf8" }));

const sidecarBytes = readFileSync(path.join(ROOT, "spec/typed-sidecar/examples/complete.json"));
const coldDecode = timed(() => requireOk(readTypedSidecar(sidecarBytes), "cold sidecar decode"));
const decoded = coldDecode.value.document;
const coldEncode = requireOk(encodeTypedSidecar(decoded), "cold sidecar encode");
const warmEncode = requireOk(encodeTypedSidecar(decoded), "warm sidecar encode");
const context = {
  currentGeneration: decoded.generation.sequence,
  currentSourceDigest: decoded.file.content_sha256,
};
const coldProject = timed(() => requireOk(
  projectOwnershipView(sidecarBytes, context), "cold sidecar projection"));
const warmDecodeNs = [];
const warmProjectNs = [];
for (let index = 0; index < 31; index += 1) {
  warmDecodeNs.push(timed(() => requireOk(
    readTypedSidecar(sidecarBytes), "warm sidecar decode")).nanoseconds);
  warmProjectNs.push(timed(() => requireOk(
    projectOwnershipView(sidecarBytes, context), "warm sidecar projection")).nanoseconds);
}

const evidence = {
  schema: "kofun.artifact-qualification-evidence/v1",
  revision: execFileSync("git", ["-C", ROOT, "rev-parse", "HEAD"], { encoding: "utf8" }).trim(),
  environment: {
    arch: process.arch,
    cpu_count: os.cpus().length,
    node: process.version,
    platform: process.platform,
    rss_unit: process.platform === "darwin" ? "bytes" : "KiB",
  },
  artifacts: {
    "kif-v1": {
      budget: rows["kif-v1"].budgets,
      cold_artifact_bytes: coldKifBytes.length,
      warm_artifact_bytes: warmKifBytes.length,
      cold_sha256: sha256(coldKifBytes),
      warm_sha256: sha256(warmKifBytes),
      cold_decode_ns: kif.cold_decode_ns,
      warm_decode_ns: kif.warm_decode_ns,
      warm_decode_p95_ns: p95(kif.warm_decode_ns),
      peak_rss_kib: kif.peak_rss_kib,
    },
    "typed-sidecar-v1": {
      budget: rows["typed-sidecar-v1"].budgets,
      cold_artifact_bytes: Buffer.byteLength(coldEncode.bytes),
      warm_artifact_bytes: Buffer.byteLength(warmEncode.bytes),
      cold_sha256: sha256(coldEncode.bytes),
      warm_sha256: sha256(warmEncode.bytes),
      cold_decode_ns: coldDecode.nanoseconds,
      warm_decode_ns: warmDecodeNs,
      warm_decode_p95_ns: p95(warmDecodeNs),
      cold_project_ns: coldProject.nanoseconds,
      warm_project_ns: warmProjectNs,
      warm_project_p95_ns: p95(warmProjectNs),
      peak_rss_kib: process.resourceUsage().maxRSS,
    },
  },
};

const violations = [];
for (const [id, result] of Object.entries(evidence.artifacts)) {
  const budget = result.budget;
  if (result.cold_artifact_bytes !== result.warm_artifact_bytes ||
      result.cold_sha256 !== result.warm_sha256) {
    violations.push(`${id}: cold and warm artifact bytes differ`);
  }
  if (result.cold_artifact_bytes > budget.fixture_bytes) {
    violations.push(`${id}: artifact bytes ${result.cold_artifact_bytes} exceed ${budget.fixture_bytes}`);
  }
  for (const field of ["cold_decode_ns", "warm_decode_p95_ns", "peak_rss_kib"]) {
    if (result[field] > budget[field]) {
      violations.push(`${id}: ${field} ${result[field]} exceeds ${budget[field]}`);
    }
  }
  if (id === "typed-sidecar-v1") {
    for (const field of ["cold_project_ns", "warm_project_p95_ns"]) {
      if (result[field] > budget[field]) {
        violations.push(`${id}: ${field} ${result[field]} exceeds ${budget[field]}`);
      }
    }
  }
}

mkdirSync(path.dirname(output), { recursive: true });
writeFileSync(output, `${JSON.stringify(evidence, null, 2)}\n`);
if (statSync(output).size === 0) violations.push("raw evidence file is empty");
if (violations.length > 0) {
  for (const violation of violations) process.stderr.write(`artifact-qualification: ${violation}\n`);
  process.exit(1);
}
process.stdout.write(`PASS: raw cold/warm bytes, decode/project samples, and peak RSS satisfy reviewed budgets (${output})\n`);
