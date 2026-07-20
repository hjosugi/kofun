import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import {
  instantiateKofun,
  mountKofunPrograms,
} from "./main.mjs";

if (process.argv.length !== 3) {
  console.error("usage: node check.mjs PROGRAM.wasm");
  process.exit(2);
}

const moduleBytes = await readFile(process.argv[2]);
const responseBytes = moduleBytes.buffer.slice(
  moduleBytes.byteOffset,
  moduleBytes.byteOffset + moduleBytes.byteLength,
);
let fetchCount = 0;
const fetchImpl = async () => {
  fetchCount += 1;
  return {
    ok: true,
    status: 200,
    async arrayBuffer() {
      return responseBytes;
    },
  };
};

const directLines = [];
const directInstance = await instantiateKofun("app.wasm", {
  appendLine(line) {
    directLines.push(line);
  },
  fetchImpl,
});
directInstance.exports.main();
assert.deepEqual(directLines, ["42", "-4"]);

const status = { textContent: "" };
const output = { textContent: "" };
const attributes = new Map();
const root = {
  dataset: { kofunWasm: "app.wasm" },
  querySelector(selector) {
    if (selector === "[data-kofun-status]") return status;
    if (selector === "[data-kofun-output]") return output;
    return null;
  },
  setAttribute(name, value) {
    attributes.set(name, value);
  },
};
const documentObject = {
  querySelectorAll(selector) {
    assert.equal(selector, "[data-kofun-wasm]");
    return [root];
  },
};

let observerCallback;
let observedRoot;
class Observer {
  constructor(callback) {
    observerCallback = callback;
  }

  observe(candidate) {
    observedRoot = candidate;
  }

  unobserve(candidate) {
    assert.equal(candidate, observedRoot);
    observedRoot = null;
  }
}

const mounted = mountKofunPrograms({
  documentObject,
  Observer,
  fetchImpl,
});
assert.equal(fetchCount, 1, "mount must not fetch before intersection");
assert.equal(observedRoot, root);
assert.equal(mounted.loadFor(root), undefined);

observerCallback([{ isIntersecting: true, target: root }]);
await mounted.loadFor(root);

assert.equal(fetchCount, 2);
assert.equal(observedRoot, null);
assert.equal(root.dataset.kofunState, "complete");
assert.equal(attributes.get("aria-busy"), "false");
assert.equal(status.textContent, "Executed by the browser WebAssembly runtime.");
assert.equal(output.textContent, "42\n-4\n");

console.log("PASS: browser host loaded and rendered Kofun WebAssembly");
console.log("PASS: viewport lazy loading deferred the wasm fetch");
