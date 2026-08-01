#!/usr/bin/env node

import fs from "node:fs";

import {
  projectDocumentationIndex,
  readKifVisibilityProjection,
  writeDocumentationIndexAtomic,
} from "./documentation-index.mjs";

function usage() {
  return [
    "usage: documentation-index-cli.mjs --sidecar FILE --kif FILE --kif-reader FILE",
    "       --output FILE --view public|package-internal --current-source SHA256",
    "       [--current-generation N] [--requesting-package PACKAGE_ID]",
  ].join("\n");
}

function parseArguments(argv) {
  const values = {};
  for (let index = 0; index < argv.length; index += 2) {
    const option = argv[index];
    const value = argv[index + 1];
    if (!option?.startsWith("--") || value === undefined || value.startsWith("--")) {
      throw new Error(usage());
    }
    const key = option.slice(2);
    if (key in values) throw new Error(`duplicate option --${key}\n${usage()}`);
    values[key] = value;
  }
  for (const key of ["sidecar", "kif", "kif-reader", "output", "view", "current-source"]) {
    if (!(key in values)) throw new Error(`missing --${key}\n${usage()}`);
  }
  const allowed = new Set([
    "current-generation", "current-source", "kif", "kif-reader", "output",
    "requesting-package", "sidecar", "view",
  ]);
  for (const key of Object.keys(values)) {
    if (!allowed.has(key)) throw new Error(`unknown option --${key}\n${usage()}`);
  }
  if ("current-generation" in values && !/^(?:0|[1-9][0-9]*)$/.test(values["current-generation"])) {
    throw new Error("--current-generation must be a non-negative decimal integer");
  }
  return values;
}

function report(result) {
  process.stderr.write(`${result.error.code}: ${result.error.message} (${result.error.reason})\n`);
}

try {
  const options = parseArguments(process.argv.slice(2));
  const visibility = readKifVisibilityProjection(options.kif, options["kif-reader"]);
  if (!visibility.ok) {
    report(visibility);
    process.exitCode = 1;
  } else {
    const projected = projectDocumentationIndex(
      fs.readFileSync(options.sidecar),
      visibility.projection,
      {
        currentGeneration: options["current-generation"] === undefined ?
          undefined : Number(options["current-generation"]),
        currentSourceDigest: options["current-source"],
        requestingPackageId: options["requesting-package"],
        view: options.view,
      },
    );
    if (!projected.ok) {
      report(projected);
      process.exitCode = 1;
    } else {
      const written = await writeDocumentationIndexAtomic(
        options.output,
        projected.index,
      );
      if (!written.ok) {
        report(written);
        process.exitCode = 1;
      }
    }
  }
} catch (error) {
  process.stderr.write(`${error.message}\n`);
  process.exitCode = 2;
}
