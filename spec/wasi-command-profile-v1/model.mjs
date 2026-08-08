#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";

import {
  CAPABILITIES,
  DIAGNOSTICS,
  IMPORT_MODULE,
  IMPORTS,
  PROFILE_TARGET,
  PROFILE_VERSION,
  START_EXPORT,
  VERSION_EXPORT,
  importByField,
  sameSignature,
} from "./contract.mjs";
import {
  decodeModule,
  definedGlobal,
  definedMemory,
  exportedFunctionType,
  signed,
  unsigned,
} from "../wasm-host-abi-v1/wasm.mjs";

const VALUE_TYPE = { i32: 0x7f, i64: 0x7e, f32: 0x7d, f64: 0x7c };
const SECTION = { type: 1, import: 2, function: 3, memory: 5, global: 6, export: 7, start: 8, code: 10, data: 11 };
const MESSAGE = Buffer.from("ok\n", "utf8");
const IOVEC_ADDRESS = 32;
const WRITTEN_ADDRESS = 48;
const MESSAGE_ADDRESS = 64;
const CLOCK_MONOTONIC = 1;
const RIGHTS_FD_READ = 2n;
const U64_MAX = (1n << 64n) - 1n;

export class Refusal extends Error {
  constructor(diagnostic, detail) {
    super(`${diagnostic}: ${detail}`);
    this.diagnostic = diagnostic;
    this.detail = detail;
  }
}

export class ProcessExit extends Error {
  constructor(code) {
    super(`process exited with ${code}`);
    this.code = code;
  }
}

export class GuestTrap extends Error {
  constructor(category) {
    super(`guest trapped: ${category}`);
    this.category = category;
  }
}

function refuse(diagnostic, detail) {
  throw new Refusal(diagnostic, detail);
}

export function canonical(value) {
  return `${JSON.stringify(value)}\n`;
}

function encodedName(value) {
  const bytes = [...Buffer.from(value, "utf8")];
  return [...unsigned(bytes.length), ...bytes];
}

function vector(items) {
  return [...unsigned(items.length), ...items.flat()];
}

function section(id, bytes) {
  return [id, ...unsigned(bytes.length), ...bytes];
}

function functionType(params, results) {
  return [
    0x60,
    ...vector(params.map((value) => [VALUE_TYPE[value]])),
    ...vector(results.map((value) => [VALUE_TYPE[value]])),
  ];
}

function u32Bytes(value) {
  return [value & 0xff, (value >>> 8) & 0xff, (value >>> 16) & 0xff, (value >>> 24) & 0xff];
}

function body(instructions) {
  const bytes = [0x00, ...instructions, 0x0b];
  return [...unsigned(bytes.length), ...bytes];
}

/* Build one transparent core-Wasm fixture. It is hand-assembled from the
 * contract rather than emitted by a Kofun backend, so this gate cannot turn
 * the reserved target into an implementation claim. */
export function buildFixture(mutation = "none") {
  let requested = [importByField("fd_write")];
  let moduleName = IMPORT_MODULE;
  let version = PROFILE_VERSION;
  if (mutation === "unknown-import") requested = [{ ...requested[0], field: "sock_send" }];
  if (mutation === "duplicate-import") requested = [requested[0], requested[0]];
  if (mutation === "wrong-module") moduleName = "wasi_unstable";
  if (mutation === "wrong-version") version += 1;

  const importTypes = requested.map((entry) =>
    functionType(
      mutation === "wrong-signature" ? entry.params.slice(0, -1) : entry.params,
      entry.results,
    ),
  );
  const startTypeIndex = importTypes.length;
  const types = [...importTypes, functionType([], [])];

  let imports;
  if (mutation === "non-function-import") {
    imports = [[...encodedName(moduleName), ...encodedName(requested[0].field), 0x02, 0x00, ...unsigned(1)]];
  } else {
    imports = requested.map((entry, index) => [
      ...encodedName(moduleName),
      ...encodedName(entry.field),
      0x00,
      ...unsigned(index),
    ]);
  }

  const importedFunctions = mutation === "non-function-import" ? 0 : requested.length;
  const startIndex = importedFunctions;
  let startInstructions = [
    ...(mutation === "non-function-import" ? [] : [
      0x41, ...signed(1),
      0x41, ...signed(IOVEC_ADDRESS),
      0x41, ...signed(1),
      ...(mutation === "wrong-signature" ? [] : [0x41, ...signed(WRITTEN_ADDRESS)]),
      0x10, ...unsigned(0),
      0x1a,
    ]),
    /* Mutate the guest bytes after fd_write returned. A host that retained a
     * view instead of copying would observe Xk rather than ok. */
    0x41, ...signed(MESSAGE_ADDRESS),
    0x41, ...signed("X".charCodeAt(0)),
    0x3a, 0x00, 0x00,
  ];
  if (mutation === "invalid-opcode") startInstructions = [0xff, ...startInstructions];
  if (mutation === "engine-trap") startInstructions = [0x00, ...startInstructions];

  const data = [
    ...u32Bytes(MESSAGE_ADDRESS),
    ...u32Bytes(MESSAGE.length),
    ...new Array(MESSAGE_ADDRESS - IOVEC_ADDRESS - 8).fill(0),
    ...MESSAGE,
  ];

  const exports = [
    [...encodedName("memory"), 0x02, ...unsigned(0)],
    [...encodedName(VERSION_EXPORT), 0x03, ...unsigned(0)],
    [...encodedName(START_EXPORT), 0x00, ...unsigned(startIndex)],
  ];
  if (mutation === "missing-export") exports.shift();
  if (mutation === "extra-memory-export") {
    exports.push([...encodedName("memory_alias"), 0x02, ...unsigned(0)]);
  }

  let memories = [[0x01, ...unsigned(1), ...unsigned(1)]];
  if (mutation === "larger-memory") {
    memories = [[0x01, ...unsigned(2), ...unsigned(3)]];
  }
  if (mutation === "extra-memory-definition") {
    memories.push([0x00, ...unsigned(1)]);
  }

  const bytes = [
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    ...section(SECTION.type, vector(types)),
    ...section(SECTION.import, vector(imports)),
    ...section(SECTION.function, vector([[...unsigned(startTypeIndex)]])),
    ...section(SECTION.memory, vector(memories)),
    ...section(SECTION.global, vector([[VALUE_TYPE.i32, 0x00, 0x41, ...signed(version), 0x0b]])),
    ...section(SECTION.export, vector(exports)),
    ...(mutation === "start-section" ? section(SECTION.start, [...unsigned(startIndex)]) : []),
    ...section(SECTION.code, vector([body(startInstructions)])),
    ...section(SECTION.data, vector([[0x00, 0x41, ...signed(IOVEC_ADDRESS), 0x0b, ...unsigned(data.length), ...data]])),
  ];
  return Uint8Array.from(bytes);
}

export function makeManifest(enabled = []) {
  const capabilities = Object.fromEntries(CAPABILITIES.map((name) => [name, enabled.includes(name)]));
  return { profile: PROFILE_TARGET, capabilities };
}

function validateManifest(manifest) {
  if (manifest?.profile !== PROFILE_TARGET) {
    refuse("profile-version-mismatch", `expected ${PROFILE_TARGET}, got ${manifest?.profile ?? "(missing)"}`);
  }
  const manifestKeys = Object.keys(manifest).sort();
  if (manifestKeys.join("\n") !== "capabilities\nprofile") {
    refuse("undeclared-capability", "the manifest record has only profile and capabilities fields");
  }
  const keys = Object.keys(manifest.capabilities ?? {}).sort();
  if (keys.join("\n") !== [...CAPABILITIES].sort().join("\n")) {
    refuse("undeclared-capability", "the capability manifest must name precisely the closed v1 vocabulary");
  }
  for (const name of CAPABILITIES) {
    if (typeof manifest.capabilities[name] !== "boolean") {
      refuse("undeclared-capability", `${name} must be true or false`);
    }
  }
}

export function requireCapability(manifest, name) {
  validateManifest(manifest);
  if (!CAPABILITIES.includes(name) || manifest.capabilities[name] !== true) {
    refuse("undeclared-capability", `${name} is not granted by ${PROFILE_TARGET}`);
  }
}

function importAuthorized(entry, manifest) {
  const alternatives = entry.capability.split("|");
  if (!alternatives.some((name) => manifest.capabilities[name] === true)) {
    refuse("undeclared-capability", `${entry.field} requires ${alternatives.join(" or ")}`);
  }
}

export function validateModule(bytes, manifest) {
  validateManifest(manifest);
  if (!WebAssembly.validate(bytes)) {
    refuse("invalid-module", "core WebAssembly validation failed");
  }
  let module;
  try {
    module = decodeModule(bytes);
  } catch (error) {
    refuse("profile-version-mismatch", error.message);
  }
  const seen = new Set();
  for (const imported of module.imports) {
    if (imported.module !== IMPORT_MODULE) {
      refuse("unknown-import-module", `${imported.module}.${imported.field}`);
    }
    if (seen.has(imported.field)) refuse("duplicate-import", imported.field);
    seen.add(imported.field);
    if (imported.kind !== "func") refuse("non-function-import", `${imported.field} is ${imported.kind}`);
    const expected = importByField(imported.field);
    if (expected === undefined) refuse("unknown-import", imported.field);
    const actual = module.types[imported.typeIndex];
    if (actual === undefined || !sameSignature(actual, expected)) {
      refuse("import-signature-mismatch", imported.field);
    }
    importAuthorized(expected, manifest);
  }
  if (module.start !== null) refuse("start-section-forbidden", "use the exported _start function");

  const memoryExports = module.exports.filter((entry) => entry.kind === "memory");
  const memoryExport = memoryExports.find((entry) => entry.name === "memory");
  const versionExport = module.exports.find((entry) => entry.name === VERSION_EXPORT && entry.kind === "global");
  const startExport = module.exports.find((entry) => entry.name === START_EXPORT && entry.kind === "func");
  if (memoryExport === undefined || versionExport === undefined || startExport === undefined) {
    refuse("missing-export", "memory, kofun_wasi_command_version, and _start are required");
  }
  if (module.memories.length !== 1 || memoryExports.length !== 1) {
    refuse("missing-export", "the module must define and export exactly one memory named memory");
  }
  const memory = definedMemory(module, memoryExport.index);
  if (memory === null || memory !== module.memories[0]) {
    refuse("missing-export", "memory must resolve to the module's single defined memory");
  }
  const version = definedGlobal(module, versionExport.index);
  if (version === null || version.type !== "i32" || version.mutable || version.value !== BigInt(PROFILE_VERSION)) {
    refuse("profile-version-mismatch", `${VERSION_EXPORT} must be immutable i32 ${PROFILE_VERSION}`);
  }
  const startType = exportedFunctionType(module, startExport.index);
  if (startType === null || startType.params.length !== 0 || startType.results.length !== 0) {
    refuse("missing-export", "_start must have type () -> ()");
  }
  return { imports: module.imports.map((entry) => entry.field), exports: ["memory", VERSION_EXPORT, START_EXPORT] };
}

export function memory32U32(value, label = "memory32 value") {
  if (
    !Number.isInteger(value) ||
    value < -0x80000000 ||
    value > 0xffffffff
  ) {
    refuse("boundary-out-of-range", `${label} is not an i32/u32 value: ${value}`);
  }
  return value >>> 0;
}

export function checkedMemory32Size(value, label = "memory32 size") {
  if (!Number.isInteger(value) || value < 0 || value > 0xffffffff) {
    refuse("boundary-out-of-range", `${label} is outside u32: ${value}`);
  }
  return value;
}

export function checkedGuestRange(memory, pointer, length) {
  const normalizedPointer = memory32U32(pointer, "memory32 pointer");
  const normalizedLength = memory32U32(length, "memory32 length");
  if (normalizedPointer + normalizedLength > memory.buffer.byteLength) {
    refuse(
      "boundary-out-of-range",
      `${normalizedPointer} + ${normalizedLength} exceeds ${memory.buffer.byteLength}`,
    );
  }
  return { pointer: normalizedPointer, length: normalizedLength };
}

function checkedBytes(values, label) {
  return values.map((value) => {
    if (typeof value !== "string") refuse("boundary-out-of-range", `${label} is not Text`);
    for (let index = 0; index < value.length; index += 1) {
      const unit = value.charCodeAt(index);
      if (unit >= 0xd800 && unit <= 0xdbff) {
        const next = value.charCodeAt(index + 1);
        if (!(next >= 0xdc00 && next <= 0xdfff)) {
          refuse("boundary-out-of-range", `${label} contains an unpaired UTF-16 surrogate`);
        }
        index += 1;
      } else if (unit >= 0xdc00 && unit <= 0xdfff) {
        refuse("boundary-out-of-range", `${label} contains an unpaired UTF-16 surrogate`);
      }
    }
    const bytes = Buffer.from(value, "utf8");
    if (bytes.includes(0)) refuse("boundary-out-of-range", `${label} contains NUL`);
    if (bytes.toString("utf8") !== value) {
      refuse("boundary-out-of-range", `${label} does not round-trip through UTF-8`);
    }
    return bytes;
  });
}

function sortedEnvironment(entries) {
  const seen = new Set();
  const normalized = entries.map(([key, value]) => {
    const [keyBytes] = checkedBytes([key], "environment key");
    const [valueBytes] = checkedBytes([value], "environment value");
    if (key.includes("=")) refuse("boundary-out-of-range", "environment key contains =");
    const encodedKey = keyBytes.toString("hex");
    if (seen.has(encodedKey)) refuse("boundary-out-of-range", `duplicate environment key bytes ${encodedKey}`);
    seen.add(encodedKey);
    return { key, keyBytes, encoded: Buffer.concat([keyBytes, Buffer.from("="), valueBytes]) };
  });
  normalized.sort((left, right) => Buffer.compare(left.keyBytes, right.keyBytes));
  return normalized.map((entry) => entry.encoded);
}

function preopenEntries(entries) {
  const seen = new Set();
  return entries.map((entry, index) => {
    const [guestBytes] = checkedBytes([entry.guest], "preopen guest name");
    const encodedGuest = guestBytes.toString("hex");
    if (!entry.guest.startsWith("/") || entry.guest === "/" || seen.has(encodedGuest)) {
      refuse("undeclared-capability", `invalid or duplicate preopen name ${entry.guest}`);
    }
    seen.add(encodedGuest);
    const files = new Map();
    for (const [name, contents] of Object.entries(entry.files ?? {})) {
      checkedBytes([name], "preopen file path");
      const segments = name.split("/");
      if (name.startsWith("/") || segments.some((segment) => segment === "" || segment === "." || segment === "..")) {
        refuse("undeclared-capability", `invalid preopen file path ${name}`);
      }
      files.set(name, Buffer.from(contents));
    }
    return { fd: index + 3, guest: entry.guest, files };
  });
}

function monotonicScript(values) {
  const result = [];
  let previous = null;
  for (const raw of values) {
    let value;
    try {
      value = BigInt(raw);
    } catch {
      refuse("boundary-out-of-range", `monotonic clock value is not u64: ${raw}`);
    }
    if (value < 0n || value > U64_MAX) {
      refuse("boundary-out-of-range", `monotonic clock value is outside u64: ${value}`);
    }
    if (previous !== null && value < previous) {
      refuse("boundary-out-of-range", `monotonic clock descends from ${previous} to ${value}`);
    }
    result.push(value);
    previous = value;
  }
  return result;
}

export class ReferenceHost {
  constructor({
    manifest,
    memory = null,
    arguments: argumentValues = [],
    environment = [],
    stdin = Buffer.alloc(0),
    monotonicClock = [],
    random = Buffer.alloc(0),
    preopens = [],
  }) {
    validateManifest(manifest);
    this.manifest = manifest;
    this.memory = memory;
    this.arguments = checkedBytes(argumentValues, "argument");
    this.environment = sortedEnvironment(environment);
    this.stdin = Buffer.from(stdin);
    this.stdinOffset = 0;
    this.monotonicClock = monotonicScript(monotonicClock);
    this.clockIndex = 0;
    this.random = Buffer.from(random);
    this.randomOffset = 0;
    this.stdout = [];
    this.stderr = [];
    this.descriptors = new Map();
    const prepared = preopenEntries(preopens);
    for (const entry of prepared) this.descriptors.set(entry.fd, { kind: "preopen", ...entry });
    this.nextDescriptor = 3 + prepared.length;
  }

  setMemory(memory) {
    this.memory = memory;
  }

  view() {
    if (!(this.memory instanceof WebAssembly.Memory)) {
      refuse("boundary-out-of-range", "guest memory is not attached");
    }
    return new DataView(this.memory.buffer);
  }

  writeU32(pointer, value) {
    const range = checkedGuestRange(this.memory, pointer, 4);
    this.view().setUint32(range.pointer, value, true);
  }

  writeU64(pointer, value) {
    const range = checkedGuestRange(this.memory, pointer, 8);
    this.view().setBigUint64(range.pointer, BigInt(value), true);
  }

  readBytes(pointer, length) {
    const range = checkedGuestRange(this.memory, pointer, length);
    return Buffer.from(new Uint8Array(this.memory.buffer, range.pointer, range.length));
  }

  writeBytes(pointer, bytes) {
    const copied = Buffer.from(bytes);
    const range = checkedGuestRange(this.memory, pointer, copied.length);
    new Uint8Array(this.memory.buffer, range.pointer, range.length).set(copied);
  }

  iovecs(pointer, count) {
    const normalizedCount = memory32U32(count, "iovec count");
    const tableLength = checkedMemory32Size(normalizedCount * 8, "iovec table bytes");
    const table = checkedGuestRange(this.memory, pointer, tableLength);
    const view = this.view();
    const result = [];
    for (let index = 0; index < normalizedCount; index += 1) {
      const at = table.pointer + index * 8;
      const base = view.getUint32(at, true);
      const length = view.getUint32(at + 4, true);
      checkedGuestRange(this.memory, base, length);
      result.push({ base, length });
    }
    return result;
  }

  vectorSizes(capability, values, countPointer, bytesPointer) {
    requireCapability(this.manifest, capability);
    const count = checkedMemory32Size(values.length, "vector element count");
    const byteCount = checkedMemory32Size(
      values.reduce((total, value) => total + value.length + 1, 0),
      "vector byte count",
    );
    checkedGuestRange(this.memory, countPointer, 4);
    checkedGuestRange(this.memory, bytesPointer, 4);
    this.writeU32(countPointer, count);
    this.writeU32(bytesPointer, byteCount);
    return 0;
  }

  writeVector(capability, values, pointers, bytes) {
    requireCapability(this.manifest, capability);
    const pointerTableLength = checkedMemory32Size(
      values.length * 4,
      "vector pointer table bytes",
    );
    const byteCount = checkedMemory32Size(
      values.reduce((total, value) => total + value.length + 1, 0),
      "vector byte count",
    );
    const pointerTable = checkedGuestRange(this.memory, pointers, pointerTableLength);
    const byteStorage = checkedGuestRange(
      this.memory,
      bytes,
      byteCount,
    );
    let cursor = byteStorage.pointer;
    for (let index = 0; index < values.length; index += 1) {
      this.writeU32(pointerTable.pointer + index * 4, cursor);
      this.writeBytes(cursor, values[index]);
      this.writeBytes(cursor + values[index].length, [0]);
      cursor += values[index].length + 1;
    }
    return 0;
  }

  args_sizes_get(countPointer, bytesPointer) {
    return this.vectorSizes("arguments", this.arguments, countPointer, bytesPointer);
  }

  args_get(pointers, bytes) {
    return this.writeVector("arguments", this.arguments, pointers, bytes);
  }

  environ_sizes_get(countPointer, bytesPointer) {
    return this.vectorSizes("environment", this.environment, countPointer, bytesPointer);
  }

  environ_get(pointers, bytes) {
    return this.writeVector("environment", this.environment, pointers, bytes);
  }

  fd_read(fd, iovecsPointer, count, readPointer) {
    let source;
    let advance;
    if (fd === 0) {
      requireCapability(this.manifest, "stdin");
      source = this.stdin.subarray(this.stdinOffset);
      advance = (length) => {
        this.stdinOffset += length;
      };
    } else {
      requireCapability(this.manifest, "preopen-read");
      const descriptor = this.descriptors.get(fd);
      if (descriptor?.kind !== "file") refuse("undeclared-capability", `descriptor ${fd} is not a readable file`);
      source = descriptor.bytes.subarray(descriptor.offset);
      advance = (length) => {
        descriptor.offset += length;
      };
    }
    checkedGuestRange(this.memory, readPointer, 4);
    let consumed = 0;
    for (const iovec of this.iovecs(iovecsPointer, count)) {
      const available = Math.min(iovec.length, source.length - consumed);
      if (available <= 0) break;
      this.writeBytes(iovec.base, source.subarray(consumed, consumed + available));
      consumed = checkedMemory32Size(consumed + available, "fd_read byte count");
    }
    advance(consumed);
    this.writeU32(readPointer, consumed);
    return 0;
  }

  fd_write(fd, iovecsPointer, count, writtenPointer) {
    let sink;
    if (fd === 1) {
      requireCapability(this.manifest, "stdout");
      sink = this.stdout;
    } else if (fd === 2) {
      requireCapability(this.manifest, "stderr");
      sink = this.stderr;
    } else {
      refuse("undeclared-capability", `descriptor ${fd} is not writable`);
    }
    checkedGuestRange(this.memory, writtenPointer, 4);
    const vectors = this.iovecs(iovecsPointer, count);
    let written = 0;
    for (const iovec of vectors) {
      written = checkedMemory32Size(written + iovec.length, "fd_write byte count");
    }
    for (const iovec of vectors) {
      sink.push(this.readBytes(iovec.base, iovec.length));
    }
    this.writeU32(writtenPointer, written);
    return 0;
  }

  fd_prestat_get(fd, pointer) {
    requireCapability(this.manifest, "preopen-read");
    const descriptor = this.descriptors.get(fd);
    if (descriptor?.kind !== "preopen") refuse("undeclared-capability", `descriptor ${fd} is not a preopen`);
    const name = Buffer.from(descriptor.guest, "utf8");
    checkedGuestRange(this.memory, pointer, 8);
    this.writeBytes(pointer, [0, 0, 0, 0]);
    this.writeU32(pointer + 4, name.length);
    return 0;
  }

  fd_prestat_dir_name(fd, pointer, length) {
    requireCapability(this.manifest, "preopen-read");
    const descriptor = this.descriptors.get(fd);
    if (descriptor?.kind !== "preopen") refuse("undeclared-capability", `descriptor ${fd} is not a preopen`);
    const name = Buffer.from(descriptor.guest, "utf8");
    const normalizedLength = memory32U32(length, "preopen name length");
    if (normalizedLength !== name.length) refuse("boundary-out-of-range", `preopen name needs ${name.length} bytes`);
    this.writeBytes(pointer, name);
    return 0;
  }

  path_open(fd, dirflags, pathPointer, pathLength, oflags, rightsBase, rightsInheriting, fdflags, openedPointer) {
    requireCapability(this.manifest, "preopen-read");
    const descriptor = this.descriptors.get(fd);
    if (descriptor?.kind !== "preopen") refuse("undeclared-capability", `descriptor ${fd} is not a preopen`);
    if (dirflags !== 0 || oflags !== 0 || BigInt(rightsBase) !== RIGHTS_FD_READ || BigInt(rightsInheriting) !== 0n || fdflags !== 0) {
      refuse("undeclared-capability", "path_open requested authority outside read-only v1");
    }
    const pathBytes = this.readBytes(pathPointer, pathLength);
    let requested;
    try {
      requested = new TextDecoder("utf-8", { fatal: true }).decode(pathBytes);
    } catch {
      refuse("undeclared-capability", "path_open path is not UTF-8");
    }
    const segments = requested.split("/");
    if (requested.startsWith("/") || segments.some((segment) => segment === "" || segment === "." || segment === "..")) {
      refuse("undeclared-capability", `path_open path escapes its preopen: ${requested}`);
    }
    const contents = descriptor.files.get(requested);
    if (contents === undefined) refuse("undeclared-capability", `path_open path is not supplied: ${requested}`);
    checkedGuestRange(this.memory, openedPointer, 4);
    const opened = this.nextDescriptor++;
    this.descriptors.set(opened, { kind: "file", bytes: contents, offset: 0 });
    this.writeU32(openedPointer, opened);
    return 0;
  }

  fd_close(fd) {
    requireCapability(this.manifest, "preopen-read");
    const descriptor = this.descriptors.get(fd);
    if (descriptor?.kind !== "file") refuse("undeclared-capability", `descriptor ${fd} cannot be closed`);
    this.descriptors.delete(fd);
    return 0;
  }

  clock_time_get(clockId, _precision, timePointer) {
    requireCapability(this.manifest, "monotonic-clock");
    if (clockId !== CLOCK_MONOTONIC) refuse("undeclared-capability", `clock ${clockId} is not monotonic`);
    if (this.clockIndex >= this.monotonicClock.length) {
      refuse("undeclared-capability", "the monotonic clock script is exhausted");
    }
    this.writeU64(timePointer, this.monotonicClock[this.clockIndex]);
    this.clockIndex += 1;
    return 0;
  }

  random_get(pointer, length) {
    requireCapability(this.manifest, "random");
    const normalizedLength = memory32U32(length, "random length");
    const nextOffset = checkedMemory32Size(
      this.randomOffset + normalizedLength,
      "random script offset",
    );
    if (nextOffset > this.random.length) {
      refuse("boundary-out-of-range", "the random byte script is exhausted");
    }
    this.writeBytes(
      pointer,
      this.random.subarray(this.randomOffset, this.randomOffset + normalizedLength),
    );
    this.randomOffset = nextOffset;
    return 0;
  }

  proc_exit(code) {
    requireCapability(this.manifest, "exit");
    throw new ProcessExit(code >>> 0);
  }

  captured(stream) {
    return Buffer.concat(stream === "stdout" ? this.stdout : this.stderr);
  }
}

export function executeReferenceCommand(host, operation) {
  try {
    operation(host);
    return {
      kind: "exit",
      code: 0,
      stdout_hex: host.captured("stdout").toString("hex"),
      stderr_hex: host.captured("stderr").toString("hex"),
    };
  } catch (error) {
    if (error instanceof ProcessExit) {
      return {
        kind: "exit",
        code: error.code,
        stdout_hex: host.captured("stdout").toString("hex"),
        stderr_hex: host.captured("stderr").toString("hex"),
      };
    }
    if (error instanceof GuestTrap) return { kind: "trap", category: error.category };
    if (error instanceof Refusal) return { kind: "trap", category: error.diagnostic };
    throw error;
  }
}

function writeIovec(memory, pointer, base, length) {
  const view = new DataView(memory.buffer);
  view.setUint32(pointer, base, true);
  view.setUint32(pointer + 4, length, true);
}

function readCStringVector(memory, pointers, count) {
  const view = new DataView(memory.buffer);
  const bytes = new Uint8Array(memory.buffer);
  const values = [];
  for (let index = 0; index < count; index += 1) {
    const start = view.getUint32(pointers + index * 4, true);
    let end = start;
    while (end < bytes.length && bytes[end] !== 0) end += 1;
    values.push(Buffer.from(bytes.subarray(start, end)).toString("utf8"));
  }
  return values;
}

export function runReferenceHostScenarios() {
  const memory = new WebAssembly.Memory({ initial: 1 });
  const manifest = makeManifest(CAPABILITIES);
  const host = new ReferenceHost({
    manifest,
    memory,
    arguments: ["kofun", "α"],
    environment: [["Z", "9"], ["A", "1"]],
    stdin: Buffer.from([0xff, 0xfe]),
    monotonicClock: [1000n, 2000n],
    random: Buffer.from([1, 2, 3, 4]),
    preopens: [{
      guest: "/fixtures",
      files: { "data.txt": Buffer.from([0x44, 0xff, 0x54, 0xfe]) },
    }],
  });
  const view = new DataView(memory.buffer);

  host.args_sizes_get(0, 4);
  host.args_get(16, 32);
  const argumentsResult = readCStringVector(memory, 16, view.getUint32(0, true));
  const argumentBytes = view.getUint32(4, true);

  host.environ_sizes_get(96, 100);
  host.environ_get(112, 128);
  const environmentResult = readCStringVector(memory, 112, view.getUint32(96, true));
  const environmentBytes = view.getUint32(100, true);

  writeIovec(memory, 192, 208, 2);
  host.fd_read(0, 192, 1, 200);
  const stdinResult = host.readBytes(208, view.getUint32(200, true)).toString("hex");

  host.writeBytes(256, Buffer.from("out"));
  writeIovec(memory, 240, 256, 3);
  host.fd_write(1, 240, 1, 248);
  host.writeBytes(288, Buffer.from("err"));
  writeIovec(memory, 272, 288, 3);
  host.fd_write(2, 272, 1, 280);

  host.clock_time_get(CLOCK_MONOTONIC, 0n, 320);
  host.clock_time_get(CLOCK_MONOTONIC, 0n, 328);
  const monotonic = [view.getBigUint64(320, true).toString(), view.getBigUint64(328, true).toString()];
  host.random_get(336, 4);
  const random = host.readBytes(336, 4).toString("hex");

  host.fd_prestat_get(3, 352);
  const preopenNameLength = view.getUint32(356, true);
  host.fd_prestat_dir_name(3, 368, preopenNameLength);
  const preopenName = host.readBytes(368, preopenNameLength).toString("utf8");
  host.writeBytes(400, Buffer.from("data.txt"));
  host.path_open(3, 0, 400, 8, 0, RIGHTS_FD_READ, 0n, 0, 392);
  const opened = view.getUint32(392, true);
  writeIovec(memory, 432, 448, 2);
  host.fd_read(opened, 432, 1, 440);
  const firstRead = host.readBytes(448, view.getUint32(440, true)).toString("hex");
  writeIovec(memory, 464, 480, 2);
  host.fd_read(opened, 464, 1, 472);
  const secondRead = host.readBytes(480, view.getUint32(472, true)).toString("hex");
  writeIovec(memory, 496, 512, 2);
  host.fd_read(opened, 496, 1, 504);
  const eofRead = view.getUint32(504, true);
  host.fd_close(opened);

  const returned = executeReferenceCommand(host, () => {});
  const exited = executeReferenceCommand(host, (active) => active.proc_exit(7));
  const trapped = runEngineTrapFixture();

  return {
    arguments: argumentsResult,
    argument_bytes: argumentBytes,
    environment: environmentResult,
    environment_bytes: environmentBytes,
    stdin_hex: stdinResult,
    stdout_hex: host.captured("stdout").toString("hex"),
    stderr_hex: host.captured("stderr").toString("hex"),
    monotonic_clock_ns: monotonic,
    random_hex: random,
    preopen: {
      name: preopenName,
      reads_hex: [firstRead, secondRead],
      eof: eofRead,
      closed: !host.descriptors.has(opened),
    },
    return_exit: { kind: returned.kind, code: returned.code },
    proc_exit: { kind: exited.kind, code: exited.code },
    trap: trapped,
  };
}

export function runEngineTrapFixture() {
  const bytes = buildFixture("engine-trap");
  const manifest = makeManifest(["stdout"]);
  validateModule(bytes, manifest);
  const referenceHost = new ReferenceHost({ manifest });
  const imports = {
    fd_write: (...arguments_) => referenceHost.fd_write(...arguments_),
  };
  const instance = new WebAssembly.Instance(
    new WebAssembly.Module(bytes),
    { [IMPORT_MODULE]: imports },
  );
  referenceHost.setMemory(instance.exports.memory);
  try {
    instance.exports[START_EXPORT]();
  } catch (error) {
    if (!(error instanceof WebAssembly.RuntimeError)) throw error;
    if (referenceHost.captured("stdout").length !== 0 || referenceHost.captured("stderr").length !== 0) {
      refuse("retained-guest-pointer", "an engine trap published stream bytes");
    }
    return { kind: "trap", category: "unreachable" };
  }
  refuse("invalid-module", "the unreachable engine fixture completed normally");
}

export async function runEngineFixture() {
  const bytes = buildFixture();
  const manifest = makeManifest(["stdout", "exit"]);
  validateModule(bytes, manifest);
  const referenceHost = new ReferenceHost({ manifest });
  const imports = {
    fd_write: (...arguments_) => referenceHost.fd_write(...arguments_),
  };
  const { instance } = await WebAssembly.instantiate(bytes, { [IMPORT_MODULE]: imports });
  const memory = instance.exports.memory;
  referenceHost.setMemory(memory);
  const result = executeReferenceCommand(referenceHost, () => {
    instance.exports[START_EXPORT]();
  });
  const stdout = referenceHost.captured("stdout");
  const guestAfterReturn = new Uint8Array(memory.buffer)[MESSAGE_ADDRESS];
  if (!stdout.equals(MESSAGE) || guestAfterReturn !== "X".charCodeAt(0)) {
    refuse("retained-guest-pointer", "stdout changed with guest memory after fd_write returned");
  }
  return {
    engine: "node-webassembly",
    result,
    guest_byte_after_return: "X",
    host_retained_guest_pointer: false,
  };
}

export function referenceVectors() {
  return {
    schema: "kofun.wasi-command-profile/v1",
    profile: PROFILE_TARGET,
    version: PROFILE_VERSION,
    import_module: IMPORT_MODULE,
    imports: IMPORTS,
    capabilities: CAPABILITIES.map((name) => ({
      name,
      granted: requireCapability(makeManifest([name]), name) ?? true,
      denied_diagnostic: "undeclared-capability",
    })),
    reference_behavior: runReferenceHostScenarios(),
    diagnostics: DIAGNOSTICS,
    exclusions: ["sockets", "ambient-filesystem", "writable-preopens", "threads", "component-model", "unstable-wasi"],
  };
}

export function compare(file) {
  const expected = canonical(referenceVectors());
  const actual = fs.readFileSync(path.resolve(file), "utf8");
  if (actual !== expected) refuse("vector-drift", file);
  return `${file} is canonical\n`;
}

async function main(argv) {
  const [command, argument] = argv;
  if (command === "vectors") return canonical(referenceVectors());
  if (command === "compare" && argument !== undefined) return compare(argument);
  if (command === "engine") return canonical(await runEngineFixture());
  refuse("vector-drift", `usage: model.mjs vectors | compare FILE | engine`);
}

if (path.resolve(process.argv[1] ?? "") === path.resolve(new URL(import.meta.url).pathname)) {
  try {
    process.stdout.write(await main(process.argv.slice(2)));
  } catch (error) {
    if (error instanceof Refusal) process.stderr.write(`wasi-command-profile: ${error.diagnostic}: ${error.detail}\n`);
    else process.stderr.write(`wasi-command-profile: internal: ${error.message}\n`);
    process.exitCode = 1;
  }
}
