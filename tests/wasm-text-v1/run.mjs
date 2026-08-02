#!/usr/bin/env node

import { readFileSync } from "node:fs";

const [modulePath, vectorsPath, mode = "run"] = process.argv.slice(2);
if (!modulePath || !vectorsPath || !["run", "expect-abort"].includes(mode)) {
  throw new Error("usage: run.mjs MODULE.wasm VECTORS.json [run|expect-abort]");
}

const vectors = JSON.parse(readFileSync(vectorsPath, "utf8"));
const text = vectors.boundary.text;
const header = text.header.find(({ name }) => name === "byte_length");
const exemplar = vectors.objects.find(({ kind }) => kind === "text");
if (!header || !exemplar || vectors.target.endianness !== "little" || text.encoding !== "utf-8") {
  throw new Error("recomputed wasm32 vectors do not describe the v1 Text layout");
}
const headerOffset = Number(header.offset);
const headerSize = Number(header.size);
const payloadOffset = Number(text.payload_offset);
const objectAlign = Number(exemplar.align);
if (headerSize !== 8) throw new Error("this engine gate requires a u64 Text header");

const module = new WebAssembly.Module(readFileSync(modulePath));
let memory;
let abort = null;
const observations = [];
const decoder = new TextDecoder("utf-8", { fatal: true });

function readText(reference) {
  if (!Number.isInteger(reference) || reference === 0 || reference % objectAlign !== 0) {
    throw new Error(`text_out received invalid reference ${reference}`);
  }
  const bytes = memory.buffer.byteLength;
  if (reference < 0 || reference + headerOffset + headerSize > bytes) {
    throw new Error(`Text header at ${reference} leaves ${bytes}-byte memory`);
  }
  const view = new DataView(memory.buffer);
  const byteLength = view.getBigUint64(reference + headerOffset, true);
  const payload = BigInt(reference + payloadOffset);
  if (payload + byteLength > BigInt(bytes) || byteLength > BigInt(Number.MAX_SAFE_INTEGER)) {
    throw new Error(`Text payload at ${reference} is out of bounds`);
  }
  const raw = new Uint8Array(memory.buffer, Number(payload), Number(byteLength));
  const decoded = decoder.decode(raw);
  const expected = new TextEncoder().encode(decoded);
  if (expected.length !== raw.length || expected.some((value, index) => value !== raw[index])) {
    throw new Error(`Text payload at ${reference} is not canonical UTF-8`);
  }
  return decoded;
}

const imports = {
  "kofun:host-abi-v1": {
    abort(code, detail) {
      abort = { code, detail };
      throw new Error("expected guest abort");
    },
    text_out(reference) {
      observations.push(readText(reference));
    },
  },
};
const instance = new WebAssembly.Instance(module, imports);
memory = instance.exports.memory;
if (instance.exports.kofun_abi_version.value !== 1) throw new Error("wrong ABI revision");

const argv = instance.exports.kofun_alloc(8, 8);
if (argv === 0) throw new Error("cannot allocate empty argv fixture");
new DataView(memory.buffer).setBigUint64(argv, 0n, true);

try {
  instance.exports.kofun_start(argv);
  if (mode === "expect-abort") throw new Error("arena exhaustion did not abort");
} catch (error) {
  if (mode !== "expect-abort" || abort === null) throw error;
  if (abort.code !== 2 || abort.detail <= 8) {
    throw new Error(`wrong allocation abort ${JSON.stringify(abort)}`);
  }
}

if (mode === "run") process.stdout.write(`${observations.join("\n")}\n`);
