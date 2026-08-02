#!/usr/bin/env node

import { readFileSync } from "node:fs";

const [modulePath, vectorsPath, mode = "run"] = process.argv.slice(2);
if (!modulePath || !vectorsPath ||
    !["run", "negative", "upper"].includes(mode)) {
  throw new Error("usage: run.mjs MODULE.wasm VECTORS.json [run|negative|upper]");
}

const vectors = JSON.parse(readFileSync(vectorsPath, "utf8"));
const text = vectors.boundary.text;
const list = vectors.boundary.list;
const textHeader = text.header.find(({ name }) => name === "byte_length");
const listHeader = list.header.find(({ name }) => name === "length");
const intElement = list.elements.find(({ type }) => type === "Int");
const textElement = list.elements.find(({ type }) => type === "Text");
if (!textHeader || !listHeader || !intElement || !textElement ||
    vectors.target.endianness !== "little" || text.encoding !== "utf-8") {
  throw new Error("recomputed wasm32 vectors do not describe Text/List v1");
}
if (Number(textHeader.size) !== 8 || Number(listHeader.size) !== 8 ||
    Number(intElement.size) !== 8 || Number(textElement.size) !== 4) {
  throw new Error("wasm32 List gate requires u64 headers and 8/4-byte strides");
}

const module = new WebAssembly.Module(readFileSync(modulePath));
let memory;
let abort = null;
let hostCalls = 0;
const observations = [];
const decoder = new TextDecoder("utf-8", { fatal: true });

function checkedReference(reference, align, label) {
  if (!Number.isInteger(reference) || reference === 0 || reference % align !== 0) {
    throw new Error(`${label} received invalid reference ${reference}`);
  }
  if (reference < 0 || reference + 8 > memory.buffer.byteLength) {
    throw new Error(`${label} header leaves guest memory`);
  }
}

function readText(reference) {
  checkedReference(reference, Number(text.value_align), "Text");
  const view = new DataView(memory.buffer);
  const length = view.getBigUint64(reference + Number(textHeader.offset), true);
  const start = BigInt(reference + Number(text.payload_offset));
  const end = start + length;
  if (end > BigInt(memory.buffer.byteLength) ||
      length > BigInt(Number.MAX_SAFE_INTEGER)) {
    throw new Error(`Text payload at ${reference} is out of bounds`);
  }
  const raw = new Uint8Array(memory.buffer, Number(start), Number(length));
  const decoded = decoder.decode(raw);
  const canonical = new TextEncoder().encode(decoded);
  if (canonical.length !== raw.length ||
      canonical.some((value, index) => value !== raw[index])) {
    throw new Error(`Text payload at ${reference} is not canonical UTF-8`);
  }
  return decoded;
}

function listBounds(reference, element) {
  checkedReference(reference, Number(list.value_align), "List");
  const view = new DataView(memory.buffer);
  const length = view.getBigUint64(reference + Number(listHeader.offset), true);
  const start = BigInt(reference + Number(list.payload_offset));
  const end = start + length * BigInt(element.size);
  if (end > BigInt(memory.buffer.byteLength) ||
      length > BigInt(Number.MAX_SAFE_INTEGER)) {
    throw new Error(`List payload at ${reference} is out of bounds`);
  }
  return { view, length: Number(length), start: Number(start) };
}

function readIntList(reference) {
  const { view, length, start } = listBounds(reference, intElement);
  return Array.from({ length }, (_, index) =>
    view.getBigInt64(start + index * Number(intElement.size), true).toString());
}

function readTextList(reference) {
  const { view, length, start } = listBounds(reference, textElement);
  return Array.from({ length }, (_, index) =>
    readText(view.getUint32(start + index * Number(textElement.size), true)));
}

const imports = {
  "kofun:host-abi-v1": {
    abort(code, detail) {
      hostCalls += 1;
      abort = { code, detail };
      throw new Error("guest abort");
    },
    text_out(reference) {
      hostCalls += 1;
      observations.push({ import: "text_out", value: readText(reference) });
    },
    list_int_out(reference) {
      hostCalls += 1;
      observations.push({ import: "list_int_out", value: readIntList(reference) });
    },
    list_text_out(reference) {
      hostCalls += 1;
      observations.push({ import: "list_text_out", value: readTextList(reference) });
    },
  },
};

const instance = new WebAssembly.Instance(module, imports);
memory = instance.exports.memory;
if (instance.exports.kofun_abi_version.value !== 1) {
  throw new Error("wrong ABI revision");
}
const argv = instance.exports.kofun_alloc(8, 8);
if (argv === 0) throw new Error("cannot allocate empty argv");
new DataView(memory.buffer).setBigUint64(argv, 0n, true);

try {
  instance.exports.kofun_start(argv);
  if (mode !== "run") throw new Error(`${mode} index did not abort`);
} catch (error) {
  if (mode === "run" || abort === null) throw error;
}

if (mode === "run") {
  process.stdout.write(`${JSON.stringify(observations)}\n`);
} else {
  const expectedDetail = mode === "negative" ? -1 : 1;
  if (abort.code !== 1 || abort.detail !== expectedDetail) {
    throw new Error(`wrong bounds abort ${JSON.stringify(abort)}`);
  }
  if (hostCalls !== 1 || observations.length !== 0) {
    throw new Error("bounds failure read or published a value");
  }
}
