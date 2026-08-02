#!/usr/bin/env node

/*
 * The import and export surface of a WebAssembly module, as the engine itself
 * sees it, printed one entry per line and sorted so the output is comparable.
 *
 * The module is *compiled* and never instantiated. `WebAssembly.Module` runs
 * no guest code, links no import, and calls no export, which is the whole
 * point: which host binding a module carries has to be answerable before
 * anything of the guest's runs. The engine is asked rather than a decoder
 * written here, because a second decoder would be a second place to be wrong
 * about what the bytes say.
 *
 *   node surface.mjs MODULE.wasm
 */

import { readFileSync } from "node:fs";

const [file, ...rest] = process.argv.slice(2);
if (file === undefined || rest.length !== 0) {
  process.stderr.write("wasm-host-profile: usage: surface.mjs MODULE.wasm\n");
  process.exit(2);
}

let bytes;
try {
  bytes = readFileSync(file);
} catch (error) {
  process.stderr.write(`wasm-host-profile: cannot read ${file}: ${error.message}\n`);
  process.exit(1);
}

let compiled;
try {
  compiled = new WebAssembly.Module(bytes);
} catch (error) {
  process.stderr.write(
    `wasm-host-profile: ${file} is not a module this engine accepts: ${error.message}\n`
  );
  process.exit(1);
}

const lines = [
  ...WebAssembly.Module.imports(compiled).map(
    (entry) => `import ${entry.module}.${entry.name} ${entry.kind}`
  ),
  ...WebAssembly.Module.exports(compiled).map((entry) => `export ${entry.name} ${entry.kind}`),
].sort();

process.stdout.write(lines.length === 0 ? "" : `${lines.join("\n")}\n`);
