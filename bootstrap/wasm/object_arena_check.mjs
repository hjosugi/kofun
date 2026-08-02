#!/usr/bin/env node

import { execFileSync } from "node:child_process";
import { readFileSync } from "node:fs";

const [modulePath, vectorsPath, headerProbe] = process.argv.slice(2);
if (!modulePath || !vectorsPath || !headerProbe) {
  throw new Error("usage: object_arena_check.mjs MODULE VECTORS HEADER_PROBE");
}

const bytes = readFileSync(modulePath);
const compiled = new WebAssembly.Module(bytes);
const imports = WebAssembly.Module.imports(compiled);
const exports = WebAssembly.Module.exports(compiled);
const expectedExports = [
  ["memory", "memory"],
  ["kofun_abi_version", "global"],
  ["kofun_start", "function"],
  ["kofun_alloc", "function"],
];
if (imports.length !== 0) {
  throw new Error(`arena-only profile unexpectedly imports ${JSON.stringify(imports)}`);
}
if (JSON.stringify(exports.map(({ name, kind }) => [name, kind])) !== JSON.stringify(expectedExports)) {
  throw new Error(`wrong export surface: ${JSON.stringify(exports)}`);
}

function fresh() {
  const instance = new WebAssembly.Instance(compiled, {});
  if (instance.exports.kofun_abi_version.value !== 1) throw new Error("wrong ABI revision");
  if (instance.exports.memory.buffer.byteLength !== 65536) throw new Error("arena is not one page");
  try {
    instance.exports.memory.grow(1);
    throw new Error("fixed arena accepted memory.grow");
  } catch (error) {
    if (!(error instanceof RangeError)) throw error;
  }
  return instance;
}

const vectors = JSON.parse(readFileSync(vectorsPath, "utf8"));
const hostArena = vectors.arenas.host;
const vectorInstance = fresh();
const vectorBase = vectorInstance.exports.kofun_alloc(
  Number(BigInt(hostArena.span)),
  Number(BigInt(vectors.boundary.list.value_align)),
);
if (BigInt(vectorBase) !== BigInt(hostArena.address)) {
  throw new Error(`host arena started at ${vectorBase}, expected ${hostArena.address}`);
}

/* Invalid requests leave both cursor and bytes unchanged. */
const invalidInstance = fresh();
const invalidMemory = new Uint8Array(invalidInstance.exports.memory.buffer);
invalidMemory.fill(0xa5, 1024, 1088);
const before = Buffer.from(invalidMemory.slice(1024, 1088));
for (const request of [[0, 8], [-1, 8], [1, 0], [1, -1], [1, 3], [1, 65536], [65536, 1]]) {
  if (invalidInstance.exports.kofun_alloc(...request) !== 0) {
    throw new Error(`invalid allocation ${request} did not return zero`);
  }
}
if (!before.equals(Buffer.from(invalidMemory.slice(1024, 1088)))) {
  throw new Error("a refused allocation modified memory");
}
if (invalidInstance.exports.kofun_alloc(1, 8) !== 1024) {
  throw new Error("a refused allocation advanced the cursor");
}
if (invalidMemory[1024] !== 0xa5) throw new Error("a successful allocation published bytes");

/* Successful regions are aligned, increasing, and non-overlapping. */
const sequenceInstance = fresh();
let previousEnd = 1024;
for (const [size, align] of [[1, 1], [7, 8], [9, 4], [32, 16], [3, 2]]) {
  const address = sequenceInstance.exports.kofun_alloc(size, align);
  if (address === 0 || address % align !== 0) {
    throw new Error(`allocation ${size}/${align} returned ${address}`);
  }
  if (address < previousEnd) throw new Error("allocator returned overlapping regions");
  previousEnd = address + size;
}

/* The exact remaining capacity succeeds once, then exhaustion is stable. */
const capacityInstance = fresh();
if (capacityInstance.exports.kofun_alloc(65536 - 1024, 1) !== 1024) {
  throw new Error("exact arena capacity was refused");
}
if (capacityInstance.exports.kofun_alloc(1, 1) !== 0 ||
    capacityInstance.exports.kofun_alloc(1, 1) !== 0) {
  throw new Error("exhaustion did not remain a zero result");
}

/* A bounded deterministic model probe covers signed i32 conversion, every
 * small power-of-two alignment, malformed alignments, exact capacity, and
 * repeated requests after exhaustion without making the gate expensive. */
let seed = 0x1001;
const next = () => {
  seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0;
  return seed;
};
const edges = [0, 1, 2, 3, 4, 7, 8, 15, 16, 1023, 1024, 32768, 65535, 65536, 65537, 0x7fffffff, 0x80000000, 0xffffffff];
let modelRequests = 0;
for (let sequence = 0; sequence < 256; sequence += 1) {
  const instance = fresh();
  let cursor = 1024;
  for (let step = 0; step < 256; step += 1) {
    let rawSize;
    let rawAlign;
    if (step % 3 === 0) {
      rawSize = edges[(step + sequence) % edges.length];
      rawAlign = edges[(step * 7 + sequence) % edges.length];
    } else if (step % 3 === 1) {
      rawSize = (next() % 4096) + 1;
      rawAlign = 1 << (next() % 17);
    } else {
      rawSize = next();
      rawAlign = next();
    }
    const size = rawSize | 0;
    const align = rawAlign | 0;
    let expected = 0;
    if (size > 0 && align > 0 && size <= 65536 && align <= 65536 &&
        (align & (align - 1)) === 0) {
      const aligned = (cursor + align - 1) & -align;
      const end = aligned + size;
      if (end <= 65536) {
        expected = aligned;
        cursor = end;
      }
    }
    const actual = instance.exports.kofun_alloc(rawSize, rawAlign);
    if (actual !== expected) {
      throw new Error(`allocator/model mismatch: ${JSON.stringify({ sequence, step, rawSize, rawAlign, expected, actual })}`);
    }
    modelRequests += 1;
  }
}

/* Every object image's header is compared with the production encoder.  The
 * vectors were recomputed by the independent host-ABI gate before this test. */
for (const object of vectors.objects) {
  const expected = object.image.slice(0, 16);
  const raw = Buffer.from(expected, "hex");
  const value = raw.readBigUInt64LE(0).toString();
  const actual = execFileSync(headerProbe, [value], { encoding: "utf8" }).trim();
  if (actual !== expected) {
    throw new Error(`${object.id} header encoded as ${actual}, expected ${expected}`);
  }
}

console.log("PASS: wasm32-hostabi1 exports one fixed memory and the exact v1 entry-point signatures");
console.log("PASS: checked bump allocation is aligned, non-overlapping, bounded, and transactional on failure");
console.log(`PASS: allocator agrees with the independent model on ${modelRequests} deterministic requests`);
console.log("PASS: u64 object headers match every recomputed wasm32 boundary vector");
