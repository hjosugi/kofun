#!/usr/bin/env node

import fs from "node:fs";

import {
  STAGE2_SEMANTIC_EVENT_LIMITS,
  emitStage2TypedSidecar,
} from "./from-stage2.mjs";

function stop(message) {
  process.stderr.write(`ETS03: ${message}\n`);
  process.exit(3);
}

if (process.argv.length !== 5) {
  stop("internal Stage 2 sidecar emitter usage error");
}

const [, , eventPath, destination, sourcePath] = process.argv;
let stat;
try {
  stat = fs.statSync(eventPath);
} catch {
  stop("semantic event stream is unavailable");
}
if (!stat.isFile() || stat.size > STAGE2_SEMANTIC_EVENT_LIMITS.streamBytes) {
  process.stderr.write("ETS04: semantic event stream exceeds the v1 byte cap\n");
  process.exit(3);
}

let eventBytes;
try {
  eventBytes = fs.readFileSync(eventPath);
} catch {
  stop("semantic event stream cannot be read");
}
const result = await emitStage2TypedSidecar(
  eventBytes,
  destination,
  { sourcePath },
);
if (!result.ok) {
  process.stderr.write(`${result.error.code}: ${result.error.message}\n`);
  process.exit(3);
}
