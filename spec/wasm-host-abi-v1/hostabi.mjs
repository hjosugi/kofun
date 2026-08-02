#!/usr/bin/env node

/*
 * kofun-wasm-host-abi-v1 reference host and vector computer.
 *
 * Nothing about the boundary layout is written down here. Every size,
 * alignment, offset, reference width, and object image is recomputed by
 * running `spec/aggregate-layout-v1/layout.mjs` over
 * `spec/aggregate-layout-v1/targets/wasm32.json`, exactly as
 * `spec/aggregate-layout-v1/check.sh` recomputes its own descriptors instead
 * of reading them. A golden vector that was edited by hand, without the rule
 * that produces it changing, does not survive the comparison.
 *
 *   node hostabi.mjs schema                 print the schema and ABI version
 *   node hostabi.mjs vectors                recompute the golden vectors
 *   node hostabi.mjs derive TARGET.json     recompute against another target
 *   node hostabi.mjs compare GOLDEN.json    recompute and reject drift
 *   node hostabi.mjs case CASE.json         run one instantiation fixture
 *   node hostabi.mjs module MODULE.wasm     phase 1 over bytes from elsewhere
 *   node hostabi.mjs run                    the end-to-end boundary scenario
 *   node hostabi.mjs lifetime MODE          conforming | retained
 *   node hostabi.mjs self-test              the checks with no fixture
 */

import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";

import {
  ABI_REVISION,
  ABI_VERSION,
  ABORT_CODES,
  DIAGNOSTICS,
  EXPORTS,
  IMPORTS,
  IMPORT_MODULE,
  SCHEMA_CASE,
  SCHEMA_VECTORS,
  exportByName,
  importByField,
  sameSignature,
  signatureText,
} from "./contract.mjs";
import {
  buildGuest,
  buildShim,
  decodeModule,
  definedGlobal,
  definedMemory,
  exportedFunctionType,
  signed,
  unsigned,
} from "./wasm.mjs";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, "..", "..");
const LAYOUT = path.join(ROOT, "spec", "aggregate-layout-v1", "layout.mjs");
const PINNED_TARGET = path.join(ROOT, "spec", "aggregate-layout-v1", "targets", "wasm32.json");
const CORE_DOCUMENT = path.join(ROOT, "spec", "aggregate-layout-v1", "vectors", "core.json");
const CORE_EXAMPLE = path.join(ROOT, "spec", "aggregate-layout-v1", "examples", "core.wasm32.json");
const BOUNDARY = path.join(HERE, "boundary.json");

/* The single byte the fixture guest writes over a borrowed payload once the
 * host call it was lent to has returned. */
const SCRIBBLE_BYTE = 0x21;
const WRONG_IMPORT_MODULE = "kofun:host-abi-v2";
const WRONG_ABI_REVISION = 2;
const MUTATED_IMPORT_FIELD = "text_out";
const UNKNOWN_IMPORT_FIELD = "text_bytes_out";

class Refusal extends Error {
  constructor(diagnostic, detail) {
    super(`${diagnostic}: ${detail}`);
    this.diagnostic = diagnostic;
    this.detail = detail;
  }
}

class HostTrap extends Error {
  constructor(code, detail) {
    super(`guest aborted with code ${code}`);
    this.code = code;
    this.detail = detail;
  }
}

function refuse(diagnostic, detail) {
  throw new Refusal(diagnostic, detail);
}

function readJson(file) {
  let text;
  try {
    text = fs.readFileSync(file, "utf8");
  } catch (error) {
    refuse("input-unreadable", `${file}: ${error.message}`);
  }
  try {
    return JSON.parse(text);
  } catch (error) {
    return refuse("input-unreadable", `${file}: ${error.message}`);
  }
}

/* Canonical serialization, identical in shape to the aggregate-layout
 * contract's: BigInt becomes a decimal string and the document ends with
 * exactly one newline, so a vector file is byte-comparable across producers. */
function canonical(value) {
  return (
    JSON.stringify(value, (_key, inner) => (typeof inner === "bigint" ? inner.toString() : inner), 2) +
    "\n"
  );
}

function alignUp(value, align) {
  const remainder = value % align;
  return remainder === 0n ? value : value + (align - remainder);
}

/*
 * The normative computer is run as a process rather than reimplemented. A
 * second implementation of the layout rules would be a second place for them
 * to be wrong.
 */
function runLayout(targetFile, documentFile) {
  try {
    return execFileSync(process.execPath, [LAYOUT, "describe", targetFile, documentFile], {
      encoding: "utf8",
      stdio: ["ignore", "pipe", "pipe"],
    });
  } catch (error) {
    const reason = (error.stderr ?? "").toString().trim() || error.message;
    return refuse("layout-unavailable", `${path.relative(ROOT, documentFile)}: ${reason}`);
  }
}

function relative(file) {
  return path.relative(ROOT, file).split(path.sep).join("/");
}

function objectCount(object) {
  return object.kind === "text" ? object.byte_length : object.length;
}

/*
 * Agreement with the published AggregateLayout vectors, in both directions
 * that can go wrong.
 *
 * First the core document is recomputed and compared with the checked-in
 * `examples/core.wasm32.json` byte for byte, so this contract cannot be built
 * on a stale copy of the layout rules. Then every type and object this
 * boundary document shares with that example must agree exactly — a size,
 * alignment, or reference width that contradicted it would be a second,
 * quieter layout contract.
 */
function agreeWithCoreExample(targetFile, descriptors) {
  const recomputed = runLayout(targetFile, CORE_DOCUMENT);
  const published = fs.readFileSync(CORE_EXAMPLE, "utf8");
  if (recomputed !== published) {
    refuse(
      "vector-drift",
      `${relative(CORE_EXAMPLE)} is not what ${relative(LAYOUT)} computes for ${relative(targetFile)}`
    );
  }
  const core = JSON.parse(recomputed);
  let types = 0;
  for (const layout of descriptors.layouts) {
    const counterpart = core.layouts.find((candidate) => candidate.id === layout.id);
    if (counterpart === undefined) continue;
    for (const field of ["kind", "size", "align", "drop"]) {
      if (layout[field] !== counterpart[field]) {
        refuse(
          "vector-drift",
          `${layout.id} ${field} is ${layout[field]} here and ${counterpart[field]} in ${relative(CORE_EXAMPLE)}`
        );
      }
    }
    if (layout.pointers.join(",") !== counterpart.pointers.join(",")) {
      refuse("vector-drift", `${layout.id} pointer bitmap disagrees with ${relative(CORE_EXAMPLE)}`);
    }
    types += 1;
  }
  if (types < 4) {
    refuse("vector-drift", `only ${types} boundary types have a counterpart in ${relative(CORE_EXAMPLE)}`);
  }
  let objects = 0;
  for (const object of descriptors.objects) {
    const counterpart = core.objects.find(
      (candidate) => candidate.type === object.type && objectCount(candidate) === objectCount(object)
    );
    if (counterpart === undefined) continue;
    for (const field of ["align", "payload_offset", "payload_size", "size"]) {
      if (object[field] !== counterpart[field]) {
        refuse(
          "vector-drift",
          `${object.id} ${field} is ${object[field]} here and ${counterpart[field]} in ${relative(CORE_EXAMPLE)}`
        );
      }
    }
    objects += 1;
  }
  if (objects < 4) {
    refuse("vector-drift", `only ${objects} boundary objects have a counterpart in ${relative(CORE_EXAMPLE)}`);
  }
  return { types, objects };
}

function descriptorFor(descriptors, id) {
  const found = descriptors.objects.find((candidate) => candidate.id === id);
  if (found === undefined) refuse("vector-drift", `${relative(BOUNDARY)} declares no object ${id}`);
  return found;
}

function layoutFor(descriptors, id) {
  const found = descriptors.layouts.find((candidate) => candidate.id === id);
  if (found === undefined) refuse("vector-drift", `${relative(BOUNDARY)} declares no type ${id}`);
  return found;
}

/*
 * Places every boundary object in its arena in declaration order, aligned by
 * the alignment the descriptor computed — never by a constant written here.
 */
function place(descriptors, document) {
  const cursors = new Map();
  const placed = [];
  for (const object of document.objects) {
    const descriptor = descriptorFor(descriptors, object.id);
    const base = document.arenas[object.arena];
    if (base === undefined) refuse("vector-drift", `${object.id} names no arena`);
    const cursor = cursors.get(object.arena) ?? BigInt(base);
    const address = alignUp(cursor, BigInt(descriptor.align));
    cursors.set(object.arena, address + BigInt(descriptor.size));
    placed.push({ source: object, descriptor, address, arena: object.arena });
  }
  const arenas = {};
  for (const [name, base] of Object.entries(document.arenas)) {
    const end = cursors.get(name) ?? BigInt(base);
    arenas[name] = { address: BigInt(base), span: end - BigInt(base) };
  }
  return { placed, arenas };
}

/*
 * The byte image of one object: the u64 header the layout contract specifies,
 * then the payload at the payload offset it computed. Padding is zero, and
 * `Uint8Array` starts zeroed, so no padding is written by hand either.
 */
function imageFor(entry, addressOf) {
  const { descriptor, source } = entry;
  if (descriptor.header.length !== 1) {
    refuse("vector-drift", `${descriptor.id} has ${descriptor.header.length} header fields, v1 has exactly one`);
  }
  const image = new Uint8Array(Number(descriptor.size));
  const view = new DataView(image.buffer);
  const headerField = descriptor.header[0];
  const payloadAt = Number(descriptor.payload_offset);
  if (descriptor.kind === "text") {
    if (headerField.name !== "byte_length") {
      refuse("vector-drift", `${descriptor.id} header field is ${headerField.name}, expected byte_length`);
    }
    const bytes = Buffer.from(source.text, "utf8");
    if (BigInt(bytes.length) !== BigInt(descriptor.byte_length)) {
      refuse("vector-drift", `${descriptor.id} byte_length is ${descriptor.byte_length}, its text encodes to ${bytes.length}`);
    }
    view.setBigUint64(Number(headerField.offset), BigInt(descriptor.byte_length), true);
    image.set(bytes, payloadAt);
    return image;
  }
  if (headerField.name !== "length") {
    refuse("vector-drift", `${descriptor.id} header field is ${headerField.name}, expected length`);
  }
  const values = source.element_values ?? [];
  if (BigInt(values.length) !== BigInt(descriptor.length)) {
    refuse("vector-drift", `${descriptor.id} length is ${descriptor.length}, ${values.length} element values were given`);
  }
  view.setBigUint64(Number(headerField.offset), BigInt(descriptor.length), true);
  const stride = Number(descriptor.element_size);
  values.forEach((value, index) => {
    const at = payloadAt + index * stride;
    if (descriptor.element_type === "Int") {
      view.setBigInt64(at, BigInt(value), true);
    } else {
      view.setUint32(at, Number(addressOf(value)), true);
    }
  });
  return image;
}

function hex(bytes) {
  return Buffer.from(bytes).toString("hex");
}

/*
 * The golden host-ABI vectors. Everything in the returned document is either a
 * pinned contract constant or a recomputed layout value; nothing is a literal
 * byte quantity typed in by a human.
 */
function recompute(targetFile, { agree }) {
  const document = readJson(BOUNDARY);
  const descriptors = JSON.parse(runLayout(targetFile, BOUNDARY));
  const agreement = agree ? agreeWithCoreExample(targetFile, descriptors) : null;

  const text = layoutFor(descriptors, "Text");
  const listInt = layoutFor(descriptors, "List[Int]");
  const listText = layoutFor(descriptors, "List[Text]");
  const int = layoutFor(descriptors, "Int");
  const reference = descriptors.target.pointer_size;
  for (const [id, layout] of [["Text", text], ["List[Int]", listInt], ["List[Text]", listText]]) {
    if (layout.size !== reference || layout.align !== descriptors.target.pointer_align) {
      refuse("vector-drift", `${id} is not one reference wide on ${descriptors.target.name}`);
    }
    if (layout.pointers.length !== 1 || layout.pointers[0] !== "0") {
      refuse("vector-drift", `${id} does not carry exactly one reference at offset 0`);
    }
  }

  const { placed, arenas } = place(descriptors, document);
  const addresses = new Map(placed.map((entry) => [entry.source.id, entry.address]));
  const addressOf = (id) => {
    const address = addresses.get(id);
    if (address === undefined) refuse("vector-drift", `${id} is referenced before it is placed`);
    return address;
  };

  const sampleText = placed.find((entry) => entry.descriptor.kind === "text");
  const sampleList = placed.find((entry) => entry.descriptor.kind === "list");
  const objects = placed.map((entry) => {
    const image = imageFor(entry, addressOf);
    const common = {
      id: entry.source.id,
      type: entry.source.type,
      kind: entry.descriptor.kind,
      arena: entry.arena,
      address: entry.address,
      align: BigInt(entry.descriptor.align),
      size: BigInt(entry.descriptor.size),
      payload_offset: BigInt(entry.descriptor.payload_offset),
      payload_size: BigInt(entry.descriptor.payload_size),
    };
    if (entry.descriptor.kind === "text") {
      return { ...common, byte_length: BigInt(entry.descriptor.byte_length), image: hex(image) };
    }
    return {
      ...common,
      length: BigInt(entry.descriptor.length),
      element_type: entry.descriptor.element_type,
      element_size: BigInt(entry.descriptor.element_size),
      element_align: BigInt(entry.descriptor.element_align),
      image: hex(image),
    };
  });

  const vectors = {
    schema: SCHEMA_VECTORS,
    abi_version: ABI_VERSION,
    abi_revision: BigInt(ABI_REVISION),
    import_module: IMPORT_MODULE,
    derived_from: {
      layout_abi_version: descriptors.abi_version,
      computer: relative(LAYOUT),
      target: relative(targetFile),
      boundary_document: relative(BOUNDARY),
      agrees_with: agree ? relative(CORE_EXAMPLE) : null,
      shared_types: agreement === null ? null : BigInt(agreement.types),
      shared_objects: agreement === null ? null : BigInt(agreement.objects),
    },
    target: descriptors.target,
    boundary: {
      reference_width: BigInt(reference),
      reference_align: BigInt(descriptors.target.pointer_align),
      int_size: BigInt(int.size),
      text: {
        value_size: BigInt(text.size),
        value_align: BigInt(text.align),
        header: sampleText.descriptor.header,
        payload_offset: BigInt(sampleText.descriptor.payload_offset),
        encoding: "utf-8",
        capacity_field: false,
      },
      list: {
        value_size: BigInt(listInt.size),
        value_align: BigInt(listInt.align),
        header: sampleList.descriptor.header,
        payload_offset: BigInt(sampleList.descriptor.payload_offset),
        capacity_field: false,
        elements: [
          { type: "Int", size: BigInt(int.size), align: BigInt(int.align) },
          { type: "Text", size: BigInt(text.size), align: BigInt(text.align) },
        ],
      },
    },
    imports: IMPORTS.map((entry) => ({
      module: IMPORT_MODULE,
      field: entry.field,
      signature: signatureText(entry.params, entry.results),
    })),
    exports: EXPORTS.map((entry) => ({
      name: entry.name,
      kind: entry.kind,
      signature:
        entry.kind === "func"
          ? signatureText(entry.params, entry.results)
          : entry.kind === "global"
            ? `(global ${entry.type}${entry.mutable ? " mut" : ""} = ${entry.value})`
            : `(memory ${entry.minimum_pages})`,
    })),
    abort_codes: ABORT_CODES.map((entry) => ({ name: entry.name, code: BigInt(entry.code) })),
    arenas,
    objects,
  };
  vectors.fixture = { conforming_guest_sha256: sha256(buildGuest(planFrom(vectors), "none")) };
  return vectors;
}

function sha256(bytes) {
  return crypto.createHash("sha256").update(bytes).digest("hex");
}

function objectIn(vectors, id) {
  const found = vectors.objects.find((candidate) => candidate.id === id);
  if (found === undefined) refuse("vector-drift", `the vectors hold no object ${id}`);
  return found;
}

/*
 * The fixture plan: every address, offset, and capacity the guest module is
 * built from, taken from the recomputed vectors. Changing a layout rule moves
 * the fixture; it does not leave it pointing at the old bytes.
 */
function planFrom(vectors) {
  const argv = objectIn(vectors, "argv");
  const counts = objectIn(vectors, "counts");
  const emptyText = objectIn(vectors, "empty-text");
  const argvFirst = objectIn(vectors, "argv-0");
  const guestArena = vectors.arenas.guest;
  const hostArena = vectors.arenas.host;
  return {
    memoryPages: 1,
    importModule: IMPORT_MODULE,
    wrongImportModule: WRONG_IMPORT_MODULE,
    abiRevision: ABI_REVISION,
    wrongAbiRevision: WRONG_ABI_REVISION,
    imports: IMPORTS,
    mutatedImportField: MUTATED_IMPORT_FIELD,
    unknownImportField: UNKNOWN_IMPORT_FIELD,
    listLengthOffset: Number(vectors.boundary.list.header[0].offset),
    listPayloadOffset: Number(vectors.boundary.list.payload_offset),
    textPayloadOffset: Number(vectors.boundary.text.payload_offset),
    countsAddress: Number(counts.address),
    emptyTextAddress: Number(emptyText.address),
    emptyArgvAddress: Number(objectIn(vectors, "empty-argv").address),
    argvAddress: Number(argv.address),
    argvFirstAddress: Number(argvFirst.address),
    arenaAddress: Number(hostArena.address),
    arenaCapacity: Number(hostArena.span),
    guestArenaAddress: Number(guestArena.address),
    scribbleByte: SCRIBBLE_BYTE,
    boundsAbortCode: ABORT_CODES.find((entry) => entry.name === "bounds").code,
    data: vectors.objects
      .filter((entry) => entry.arena === "guest")
      .map((entry) => ({ address: Number(entry.address), bytes: Buffer.from(entry.image, "hex") })),
  };
}

/*
 * Instantiation, phase 1: the static contract check.
 *
 * Every refusal below happens against the module *bytes*, before the engine is
 * asked for an instance, so no guest code can have run when one fires. The
 * order is part of the contract: a module that is wrong in two ways is
 * reported by the earlier rule, and the earlier rule is always the one that
 * would otherwise let guest code run.
 */
function validateModule(bytes) {
  let module;
  try {
    module = decodeModule(bytes);
  } catch (error) {
    return refuse("malformed-module", error.message);
  }

  if (module.start !== null) {
    refuse(
      "start-section-forbidden",
      `the module declares a start function (index ${module.start}); all guest execution begins at kofun_start`
    );
  }

  for (const entry of module.imports) {
    if (entry.module !== IMPORT_MODULE) {
      refuse(
        "abi-version-mismatch",
        `import "${entry.module}"."${entry.field}" names ${entry.module}, this host publishes ${IMPORT_MODULE}`
      );
    }
  }
  const versionExport = module.exports.find((entry) => entry.name === "kofun_abi_version");
  if (versionExport === undefined) {
    refuse("abi-version-mismatch", "the module exports no kofun_abi_version global");
  }
  if (versionExport.kind !== "global") {
    refuse("abi-version-mismatch", `kofun_abi_version is exported as a ${versionExport.kind}, not a global`);
  }
  const version = definedGlobal(module, versionExport.index);
  if (version === null) {
    refuse("abi-version-mismatch", "kofun_abi_version is imported, so its value is not known before instantiation");
  }
  if (version.type !== "i32" || version.mutable) {
    refuse(
      "abi-version-mismatch",
      `kofun_abi_version is ${version.mutable ? "mutable " : ""}${version.type}, the contract pins an immutable i32`
    );
  }
  if (version.value === null || BigInt(version.value) !== BigInt(ABI_REVISION)) {
    refuse(
      "abi-version-mismatch",
      `kofun_abi_version is ${version.value}, this host implements revision ${ABI_REVISION} (${ABI_VERSION})`
    );
  }

  for (const entry of module.imports) {
    if (entry.kind !== "func") {
      refuse("missing-import", `"${entry.field}" is imported as a ${entry.kind}; the allowlist holds functions only`);
    }
    const allowed = importByField(entry.field);
    if (allowed === undefined) {
      refuse(
        "missing-import",
        `"${entry.field}" is not on the ${IMPORT_MODULE} allowlist, so this host cannot supply it`
      );
    }
    const declared = module.types[entry.typeIndex];
    if (declared === undefined) {
      refuse("import-signature-mismatch", `"${entry.field}" names type index ${entry.typeIndex}, which is absent`);
    }
    if (!sameSignature(declared, allowed)) {
      refuse(
        "import-signature-mismatch",
        `"${entry.field}" is imported as ${signatureText(declared.params, declared.results)}, ` +
          `the contract pins ${signatureText(allowed.params, allowed.results)}`
      );
    }
  }

  for (const required of EXPORTS) {
    if (required.name === "kofun_abi_version") continue;
    const found = module.exports.find((entry) => entry.name === required.name);
    if (found === undefined) {
      refuse("missing-export", `the module exports no ${required.name} (${required.kind})`);
    }
    if (found.kind !== required.kind) {
      refuse("missing-export", `${required.name} is exported as a ${found.kind}, the contract requires a ${required.kind}`);
    }
    if (required.kind === "func") {
      const type = exportedFunctionType(module, found.index);
      if (type === null || !sameSignature(type, required)) {
        refuse(
          "missing-export",
          `${required.name} is ${type === null ? "untyped" : signatureText(type.params, type.results)}, ` +
            `the contract requires ${signatureText(required.params, required.results)}`
        );
      }
    }
    if (required.kind === "memory") {
      const memory = definedMemory(module, found.index);
      if (memory === null || memory.minimum < required.minimum_pages) {
        refuse(
          "missing-export",
          `memory declares ${memory === null ? "no" : memory.minimum} pages, the contract requires at least ${required.minimum_pages}`
        );
      }
    }
  }
  return module;
}

/*
 * The reference host.
 *
 * Every allowlisted import is reached through a wasm wrapper, never as a bare
 * JavaScript function: a JavaScript function is coerced to whatever signature
 * the guest declared, so an engine cannot reject a mismatch against one. The
 * wrapper carries a real function type and the engine refuses the mismatch
 * itself.
 */
class Host {
  constructor(vectors, options = {}) {
    this.vectors = vectors;
    this.retain = options.retain === true;
    this.memory = null;
    this.calls = [];
    this.hostCalls = 0;
    this.aborted = null;
    this.retained = null;
  }

  view() {
    if (this.memory === null) refuse("scenario", "a boundary call arrived before the memory was bound");
    return new DataView(this.memory.buffer);
  }

  /* A Text at the boundary is one reference. The host derives the borrowed
   * (pointer, length) pair from it and may not keep either past this call. */
  borrowText(reference) {
    const view = this.view();
    const bytes = this.memory.buffer.byteLength;
    const headerOffset = Number(this.vectors.boundary.text.header[0].offset);
    const headerSize = Number(this.vectors.boundary.text.header[0].size);
    if (reference < 0 || reference + headerOffset + headerSize > bytes) {
      refuse("boundary-out-of-range", `Text header at ${reference} lies outside ${bytes} bytes of memory`);
    }
    const byteLength = view.getBigUint64(reference + headerOffset, true);
    const pointer = reference + Number(this.vectors.boundary.text.payload_offset);
    if (BigInt(pointer) + byteLength > BigInt(bytes)) {
      refuse("boundary-out-of-range", `Text payload at ${pointer} for ${byteLength} bytes lies outside ${bytes} bytes of memory`);
    }
    return { pointer, byteLength: Number(byteLength) };
  }

  /* UTF-8 only: the decoder is fatal, so ill-formed bytes are refused rather
   * than replaced with U+FFFD and carried into a host string. */
  decodeText(reference) {
    const { pointer, byteLength } = this.borrowText(reference);
    const bytes = new Uint8Array(this.memory.buffer, pointer, byteLength);
    try {
      return new TextDecoder("utf-8", { fatal: true }).decode(bytes);
    } catch {
      return refuse("text-not-utf8", `the Text at ${reference} is not well-formed UTF-8`);
    }
  }

  borrowList(reference, elementType) {
    const view = this.view();
    const bytes = this.memory.buffer.byteLength;
    const headerOffset = Number(this.vectors.boundary.list.header[0].offset);
    const headerSize = Number(this.vectors.boundary.list.header[0].size);
    if (reference < 0 || reference + headerOffset + headerSize > bytes) {
      refuse("boundary-out-of-range", `List header at ${reference} lies outside ${bytes} bytes of memory`);
    }
    const length = view.getBigUint64(reference + headerOffset, true);
    const element = this.vectors.boundary.list.elements.find((entry) => entry.type === elementType);
    if (element === undefined) refuse("scenario", `no element description for ${elementType}`);
    const base = reference + Number(this.vectors.boundary.list.payload_offset);
    const span = length * BigInt(element.size);
    if (BigInt(base) + span > BigInt(bytes)) {
      refuse("boundary-out-of-range", `List of ${length} ${elementType} at ${base} lies outside ${bytes} bytes of memory`);
    }
    return { base, length, stride: Number(element.size) };
  }

  /* Bounds behaviour: the host reads indices 0 .. length-1 and no others.
   * There is no capacity field to consult and no slack past the last element. */
  readIntList(reference) {
    const { base, length, stride } = this.borrowList(reference, "Int");
    const view = this.view();
    const out = [];
    for (let index = 0n; index < length; index += 1n) {
      out.push(view.getBigInt64(base + Number(index) * stride, true).toString());
    }
    return out;
  }

  readTextList(reference) {
    const { base, length, stride } = this.borrowList(reference, "Text");
    const view = this.view();
    const out = [];
    for (let index = 0n; index < length; index += 1n) {
      out.push(this.decodeText(view.getUint32(base + Number(index) * stride, true)));
    }
    return out;
  }

  javascript() {
    return {
      abort: (code, detail) => {
        this.hostCalls += 1;
        this.aborted = { code, detail };
        throw new HostTrap(code, detail);
      },
      text_out: (reference) => {
        this.hostCalls += 1;
        const borrowed = this.borrowText(reference);
        const text = this.decodeText(reference);
        this.calls.push({ import: "text_out", text });
        /* The negative lifetime fixture: keeping the pair past the call is
         * exactly what the rule forbids, so the violating host does it here
         * and the checker measures what it then reads. */
        if (this.retain && this.retained === null) {
          this.retained = { reference, ...borrowed, textDuringCall: text };
        }
      },
      list_int_out: (reference) => {
        this.hostCalls += 1;
        this.calls.push({ import: "list_int_out", values: this.readIntList(reference) });
      },
      list_text_out: (reference) => {
        this.hostCalls += 1;
        this.calls.push({ import: "list_text_out", values: this.readTextList(reference) });
      },
    };
  }
}

function link(host, guestBytes) {
  const shim = new WebAssembly.Instance(
    new WebAssembly.Module(buildShim(IMPORTS)),
    { js: host.javascript() }
  );
  const provided = {};
  for (const entry of IMPORTS) provided[entry.field] = shim.exports[entry.field];
  return new WebAssembly.Instance(new WebAssembly.Module(guestBytes), { [IMPORT_MODULE]: provided });
}

/* Instantiation as the contract defines it: validate, then link, then bind.
 * Phase 1 refuses on the bytes; phase 2 is the engine's own link check. */
function instantiate(host, guestBytes) {
  validateModule(guestBytes);
  const instance = link(host, guestBytes);
  host.memory = instance.exports.memory;
  if (instance.exports.kofun_abi_version.value !== ABI_REVISION) {
    refuse(
      "abi-version-mismatch",
      `the instantiated kofun_abi_version is ${instance.exports.kofun_abi_version.value}, expected ${ABI_REVISION}`
    );
  }
  return instance;
}

function runCase(caseFile) {
  const description = readJson(caseFile);
  if (description.schema !== SCHEMA_CASE) {
    refuse("input-unreadable", `${relative(caseFile)}: schema must be ${SCHEMA_CASE}`);
  }
  const vectors = recompute(PINNED_TARGET, { agree: true });
  const guest = buildGuest(planFrom(vectors), description.mutation);
  const host = new Host(vectors);

  let observed = "accepted";
  let diagnostic = null;
  try {
    validateModule(guest);
  } catch (error) {
    if (!(error instanceof Refusal)) throw error;
    observed = "refused";
    diagnostic = error.diagnostic;
  }

  /* The engine is asked for its own opinion wherever asking cannot run guest
   * code. A module with a start section is never handed over: instantiating it
   * is the very thing the rule exists to prevent. */
  let engine = "not-attempted";
  let engineError = null;
  if (description.engine !== "not-attempted") {
    try {
      link(host, guest);
      engine = "accepted";
    } catch (error) {
      engine = "refused";
      engineError = `${error.constructor.name}`;
    }
  }

  const report = {
    schema: "kofun.wasm-host-abi-case-report/v1",
    id: description.id,
    mutation: description.mutation,
    contract: observed,
    diagnostic,
    engine,
    engine_error: engineError,
    instantiated: observed === "accepted",
    host_calls: host.hostCalls,
    guest_ran: host.hostCalls > 0,
  };

  if (observed !== description.expect) {
    refuse("case-outcome", `${description.id}: the contract ${observed} the module, the fixture expects ${description.expect}`);
  }
  if (diagnostic !== (description.diagnostic ?? null)) {
    refuse(
      "case-outcome",
      `${description.id}: the diagnostic is ${diagnostic ?? "none"}, the fixture expects ${description.diagnostic ?? "none"}`
    );
  }
  if (diagnostic !== null && !DIAGNOSTICS.includes(diagnostic)) {
    refuse("case-outcome", `${description.id}: ${diagnostic} is not one of the contract's named diagnostics`);
  }
  if (engine !== description.engine) {
    refuse("case-outcome", `${description.id}: the engine ${engine} the module, the fixture expects ${description.engine}`);
  }
  if (engineError !== (description.engine_error ?? null)) {
    refuse(
      "case-outcome",
      `${description.id}: the engine raised ${engineError ?? "nothing"}, the fixture expects ${description.engine_error ?? "nothing"}`
    );
  }
  if (host.hostCalls !== 0) {
    refuse("case-outcome", `${description.id}: ${host.hostCalls} guest call(s) reached the host during instantiation`);
  }
  return report;
}

function expectedObservations(vectors) {
  const document = readJson(BOUNDARY);
  const textOf = (id) => document.objects.find((object) => object.id === id).text;
  const argv = document.objects.find((object) => object.id === "argv");
  const counts = document.objects.find((object) => object.id === "counts");
  return [
    { import: "text_out", text: textOf("argv-0") },
    { import: "list_text_out", values: argv.element_values.map((id) => textOf(id)) },
    { import: "list_int_out", values: counts.element_values.map((value) => BigInt(value).toString()) },
    { import: "text_out", text: textOf("empty-text") },
  ];
}

/* Writes the host-owned arena into guest memory, at the recomputed addresses
 * and from the recomputed images. */
function handOverArena(instance, host, vectors) {
  const arena = vectors.arenas.host;
  const base = instance.exports.kofun_alloc(Number(arena.span), Number(vectors.boundary.list.value_align));
  if (base === 0) refuse("scenario", `kofun_alloc refused ${arena.span} bytes`);
  if (BigInt(base) !== arena.address) {
    refuse("scenario", `kofun_alloc returned ${base}, the vectors place the host arena at ${arena.address}`);
  }
  const bytes = new Uint8Array(host.memory.buffer);
  for (const object of vectors.objects) {
    if (object.arena !== "host") continue;
    bytes.set(Buffer.from(object.image, "hex"), Number(object.address));
  }
}

function runScenario(options = {}) {
  const vectors = recompute(PINNED_TARGET, { agree: true });
  const plan = planFrom(vectors);
  const guest = buildGuest(plan, "none");
  const host = new Host(vectors, options);
  const instance = instantiate(host, guest);
  handOverArena(instance, host, vectors);

  instance.exports.kofun_start(plan.argvAddress);

  const expected = expectedObservations(vectors);
  if (JSON.stringify(host.calls) !== JSON.stringify(expected)) {
    refuse(
      "scenario",
      `the boundary observations were ${JSON.stringify(host.calls)}, expected ${JSON.stringify(expected)}`
    );
  }
  return { vectors, plan, host, instance };
}

/* The bounds rule, exercised: an empty List has no element 0, so the guest
 * aborts with the pinned code instead of reading a header-sized hole. */
function runBoundsScenario() {
  const vectors = recompute(PINNED_TARGET, { agree: true });
  const plan = planFrom(vectors);
  const host = new Host(vectors);
  const instance = instantiate(host, buildGuest(plan, "none"));
  const bounds = ABORT_CODES.find((entry) => entry.name === "bounds").code;
  let trapped = null;
  try {
    instance.exports.kofun_start(plan.emptyArgvAddress);
  } catch (error) {
    trapped = error;
  }
  if (!(trapped instanceof HostTrap)) {
    refuse("scenario", `an empty argv did not abort: ${trapped === null ? "the call returned" : trapped.message}`);
  }
  if (trapped.code !== bounds) {
    refuse("scenario", `the empty argv aborted with code ${trapped.code}, the contract pins ${bounds} for bounds`);
  }
  if (host.calls.length !== 0) {
    refuse("scenario", `${host.calls.length} boundary value(s) crossed after an out-of-range index`);
  }
  return { code: trapped.code, detail: trapped.detail };
}

function lifetime(mode) {
  if (mode !== "conforming" && mode !== "retained") {
    refuse("input-unreadable", `lifetime takes conforming or retained, got ${mode ?? "(none)"}`);
  }
  const { host, vectors } = runScenario({ retain: mode === "retained" });
  const borrowedDuringCall = host.calls[0].text;
  if (mode === "conforming") {
    /* The conforming host copied the bytes out while the call was live, so
     * what it holds cannot be disturbed by anything the guest does later. */
    return canonical({
      schema: "kofun.wasm-host-abi-lifetime/v1",
      mode,
      borrowed_during_call: borrowedDuringCall,
      held_after_return: borrowedDuringCall,
      rule: "the host copied the borrowed payload before returning",
    });
  }
  const bytes = new Uint8Array(host.memory.buffer, host.retained.pointer, host.retained.byteLength);
  const afterReturn = new TextDecoder("utf-8", { fatal: false }).decode(bytes);
  if (afterReturn === borrowedDuringCall) {
    refuse(
      "lifetime-fixture-inert",
      `the retained pointer still reads ${JSON.stringify(afterReturn)}, so this fixture no longer demonstrates the rule`
    );
  }
  return refuse(
    "retained-guest-pointer",
    `the host kept (${host.retained.pointer}, ${host.retained.byteLength}) past text_out and now reads ` +
      `${JSON.stringify(afterReturn)} where the call lent it ${JSON.stringify(borrowedDuringCall)}`
  );
}

function expectRefusal(label, diagnostic, run) {
  try {
    run();
  } catch (error) {
    if (error instanceof Refusal && error.diagnostic === diagnostic) return;
    refuse("self-test", `${label} failed with ${error.message}, expected ${diagnostic}`);
  }
  refuse("self-test", `${label} was accepted but must be refused with ${diagnostic}`);
}

function selfTest() {
  for (const value of [0, 1, 63, 64, 127, 128, 1024, 65535, 4294967295]) {
    const encoded = unsigned(value);
    if (encoded.length === 0) refuse("self-test", `unsigned(${value}) encoded to nothing`);
  }
  for (const value of [0, -1, 63, -64, 64, -65, 2147483647, -2147483648]) {
    if (signed(value).length === 0) refuse("self-test", `signed(${value}) encoded to nothing`);
  }
  if (new Set(DIAGNOSTICS).size !== DIAGNOSTICS.length) {
    refuse("self-test", "two diagnostics share a name");
  }
  if (signatureText(["i32", "i32"], []) !== "(func (param i32 i32))") {
    refuse("self-test", `signatureText disagrees: ${signatureText(["i32", "i32"], [])}`);
  }
  if (sameSignature({ params: ["i32"], results: [] }, { params: ["i32", "i32"], results: [] })) {
    refuse("self-test", "sameSignature accepted a different arity");
  }
  if (importByField(UNKNOWN_IMPORT_FIELD) !== undefined) {
    refuse("self-test", `${UNKNOWN_IMPORT_FIELD} is on the allowlist, so it cannot stand for a missing import`);
  }
  if (exportByName("kofun_start") === undefined) refuse("self-test", "kofun_start left the export set");

  /* The two boundary reads that have no instantiation fixture, because they
   * are refusals against memory rather than against a module. */
  const vectors = recompute(PINNED_TARGET, { agree: true });
  const host = new Host(vectors);
  host.memory = new WebAssembly.Memory({ initial: 1 });
  const view = new DataView(host.memory.buffer);
  const bytes = new Uint8Array(host.memory.buffer);
  const payload = Number(vectors.boundary.text.payload_offset);

  view.setBigUint64(0, 1n, true);
  bytes[payload] = 0xff;
  expectRefusal("an ill-formed UTF-8 Text", "text-not-utf8", () => host.decodeText(0));

  view.setBigUint64(0, 0xffffffffn, true);
  expectRefusal("a Text longer than memory", "boundary-out-of-range", () => host.decodeText(0));

  view.setBigUint64(0, 0xffffffffn, true);
  expectRefusal("a List longer than memory", "boundary-out-of-range", () => host.readIntList(0));

  return canonical({
    schema: "kofun.wasm-host-abi-self-test/v1",
    diagnostics: DIAGNOSTICS.length,
    imports: IMPORTS.length,
    exports: EXPORTS.length,
  });
}

function compare(goldenFile) {
  const recomputed = canonical(recompute(PINNED_TARGET, { agree: true }));
  let golden;
  try {
    golden = fs.readFileSync(goldenFile, "utf8");
  } catch (error) {
    return refuse("vector-drift", `${relative(goldenFile)}: ${error.message}`);
  }
  if (golden === recomputed) return `${relative(goldenFile)} is what the layout rules compute\n`;
  const goldenLines = golden.split("\n");
  const freshLines = recomputed.split("\n");
  let line = 0;
  while (line < goldenLines.length && goldenLines[line] === freshLines[line]) line += 1;
  return refuse(
    "vector-drift",
    `${relative(goldenFile)} line ${line + 1} reads ${JSON.stringify(goldenLines[line] ?? "(end of file)")}, ` +
      `recomputing gives ${JSON.stringify(freshLines[line] ?? "(end of file)")}`
  );
}

function main(argv) {
  const [command, ...rest] = argv;
  switch (command) {
    case "schema":
      return `${SCHEMA_VECTORS} ${ABI_VERSION} revision ${ABI_REVISION}\n`;
    case "vectors":
      return canonical(recompute(PINNED_TARGET, { agree: true }));
    /* Every name the contract pins, one per line, so the gate can hold the
     * document to the allowlist instead of the document and the code drifting
     * apart with nobody noticing. */
    case "names":
      return `${[
        ...IMPORTS.map((entry) => entry.field),
        ...EXPORTS.map((entry) => entry.name),
        ...DIAGNOSTICS,
      ].join("\n")}\n`;
    case "derive": {
      if (rest.length !== 1) refuse("input-unreadable", "usage: hostabi.mjs derive TARGET.json");
      return canonical(recompute(path.resolve(rest[0]), { agree: false }));
    }
    case "compare": {
      if (rest.length !== 1) refuse("input-unreadable", "usage: hostabi.mjs compare GOLDEN.json");
      return compare(path.resolve(rest[0]));
    }
    case "case": {
      if (rest.length !== 1) refuse("input-unreadable", "usage: hostabi.mjs case CASE.json");
      return canonical(runCase(path.resolve(rest[0])));
    }
    /* The same phase-1 validator the fixtures run, pointed at bytes this file
     * did not assemble -- a compiler artifact, say. It answers one question:
     * is this module `kofun-wasm-host-abi-v1`? Nothing is linked and no engine
     * is asked for an instance, so a verdict costs no guest execution, which
     * is what lets a host decide which ABI it is holding before calling into
     * it. A module that is not v1 leaves through the usual refusal path with
     * the contract's own diagnostic name. */
    case "module": {
      if (rest.length !== 1) refuse("input-unreadable", "usage: hostabi.mjs module MODULE.wasm");
      const file = path.resolve(rest[0]);
      let bytes;
      try {
        bytes = fs.readFileSync(file);
      } catch (error) {
        return refuse("input-unreadable", `${relative(file)}: ${error.message}`);
      }
      validateModule(bytes);
      return canonical({
        schema: "kofun.wasm-host-abi-module/v1",
        abi_version: ABI_VERSION,
        module: relative(file),
        bytes: BigInt(bytes.length),
        contract: "accepted",
        instantiated: false,
        host_calls: BigInt(0),
        guest_ran: false,
      });
    }
    case "run": {
      const { host, vectors } = runScenario();
      const bounds = runBoundsScenario();
      return canonical({
        schema: "kofun.wasm-host-abi-run/v1",
        abi_version: ABI_VERSION,
        boundary_calls: host.calls,
        bounds_abort: { code: BigInt(bounds.code), detail: BigInt(bounds.detail) },
        objects: BigInt(vectors.objects.length),
      });
    }
    case "lifetime":
      return lifetime(rest[0]);
    case "self-test":
      return selfTest();
    default:
      return refuse("input-unreadable", `unknown command ${command ?? "(none)"}`);
  }
}

try {
  process.stdout.write(main(process.argv.slice(2)));
} catch (error) {
  if (error instanceof Refusal) {
    process.stderr.write(`wasm-host-abi: ${error.diagnostic}: ${error.detail}\n`);
  } else {
    process.stderr.write(`wasm-host-abi: internal: ${error.message}\n`);
  }
  process.exit(1);
}
