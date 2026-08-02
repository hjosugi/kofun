/*
 * A minimal, deterministic WebAssembly encoder and section decoder, used only
 * by this contract's fixtures and validator.
 *
 * This is not a code generator and it lowers no Kofun source. It exists so the
 * negative instantiation fixtures are *derived* — the guest module for each
 * case is built from the recomputed boundary vectors plus one named mutation,
 * rather than checked in as opaque bytes nobody can re-derive. Every offset it
 * writes comes from the layout descriptors, so a layout change moves the
 * fixture instead of leaving it silently stale.
 *
 * The decoder reads exactly the sections the contract has an opinion about:
 * types, imports, functions, memories, globals, exports, and the start
 * section. It is a validator's reader, not a general parser.
 */

const MAGIC = [0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00];

const VALUE_TYPE_CODES = { i32: 0x7f, i64: 0x7e, f32: 0x7d, f64: 0x7c };
const VALUE_TYPE_NAMES = { 0x7f: "i32", 0x7e: "i64", 0x7d: "f32", 0x7c: "f64" };

const SECTION = {
  type: 1,
  import: 2,
  function: 3,
  memory: 5,
  global: 6,
  export: 7,
  start: 8,
  code: 10,
  data: 11,
};

const EXTERN_KIND_NAMES = ["func", "table", "memory", "global"];

export function unsigned(value) {
  let n = BigInt(value);
  if (n < 0n) throw new Error(`unsigned LEB128 of a negative value: ${value}`);
  const out = [];
  do {
    let byte = Number(n & 0x7fn);
    n >>= 7n;
    if (n !== 0n) byte |= 0x80;
    out.push(byte);
  } while (n !== 0n);
  return out;
}

export function signed(value) {
  let n = BigInt(value);
  const out = [];
  for (;;) {
    const byte = Number(n & 0x7fn);
    n >>= 7n;
    const signBit = (byte & 0x40) !== 0;
    if ((n === 0n && !signBit) || (n === -1n && signBit)) {
      out.push(byte);
      return out;
    }
    out.push(byte | 0x80);
  }
}

function name(text) {
  const bytes = [...Buffer.from(text, "utf8")];
  return [...unsigned(bytes.length), ...bytes];
}

function vector(items) {
  return [...unsigned(items.length), ...items.flat()];
}

function section(id, body) {
  if (body.length === 0) return [];
  return [id, ...unsigned(body.length), ...body];
}

function functype(params, results) {
  return [
    0x60,
    ...vector(params.map((type) => [VALUE_TYPE_CODES[type]])),
    ...vector(results.map((type) => [VALUE_TYPE_CODES[type]])),
  ];
}

/* Instruction fragments. Only the opcodes the fixtures need exist here. */
const op = {
  localGet: (index) => [0x20, ...unsigned(index)],
  i32Const: (value) => [0x41, ...signed(value)],
  i64Const: (value) => [0x42, ...signed(value)],
  i32Load: (offset) => [0x28, ...unsigned(2), ...unsigned(offset)],
  i64Load: (offset) => [0x29, ...unsigned(3), ...unsigned(offset)],
  i32Store8: (offset) => [0x3a, ...unsigned(0), ...unsigned(offset)],
  i64Eq: () => [0x51],
  i32LeU: () => [0x4d],
  select: () => [0x1b],
  call: (index) => [0x10, ...unsigned(index)],
  ifVoid: () => [0x04, 0x40],
  return: () => [0x0f],
  end: () => [0x0b],
};

function body(instructions) {
  const code = [...instructions, ...op.end()];
  const withLocals = [...unsigned(0), ...code];
  return [...unsigned(withLocals.length), ...withLocals];
}

/*
 * Builds the fixture guest module.
 *
 * `plan` carries only recomputed values: every offset, address, and capacity
 * below arrives from the boundary vectors. `mutation` names the single thing
 * that is wrong, so each negative fixture differs from the conforming one in
 * exactly one respect and the diagnostic it earns cannot be ambiguous.
 */
export function buildGuest(plan, mutation = "none") {
  const importModule =
    mutation === "abi-version-namespace" ? plan.wrongImportModule : plan.importModule;
  const abiRevision = mutation === "abi-version-global" ? plan.wrongAbiRevision : plan.abiRevision;
  const withStart = mutation === "start-section";

  /* t0 (i32 i32) -> (), t1 (i32) -> (), t2 (i32 i32) -> i32, t3 () -> ().
   * All four are always declared so a mutation changes one index and nothing
   * else about the module's shape. */
  const types = [functype(["i32", "i32"], []), functype(["i32"], []), functype(["i32", "i32"], ["i32"]), functype([], [])];

  const imported = plan.imports.map((entry) => {
    let field = entry.field;
    let typeIndex = entry.params.length === 2 ? 0 : 1;
    if (mutation === "missing-import" && entry.field === plan.mutatedImportField) {
      field = plan.unknownImportField;
    }
    if (mutation === "import-signature-mismatch" && entry.field === plan.mutatedImportField) {
      typeIndex = 0;
    }
    return [...name(importModule), ...name(field), 0x00, ...unsigned(typeIndex)];
  });

  const importedFunctionCount = imported.length;
  const startIndex = importedFunctionCount;
  const allocIndex = importedFunctionCount + 1;
  const probeIndex = importedFunctionCount + 2;

  const abortIndex = 0;
  const textOutIndex = 1;
  const listIntOutIndex = 2;
  const listTextOutIndex = 3;

  /* The wrong-signature fixture must be a *well-formed* module whose only
   * fault is at the boundary. If its call sites still passed one argument the
   * module would not validate at all, and the engine would reject it for being
   * internally inconsistent rather than for disagreeing with the host. */
  const callTextOut =
    mutation === "import-signature-mismatch" && plan.mutatedImportField === "text_out"
      ? [...op.i32Const(0), ...op.call(textOutIndex)]
      : op.call(textOutIndex);

  const functions = withStart ? [[...unsigned(1)], [...unsigned(2)], [...unsigned(3)]] : [[...unsigned(1)], [...unsigned(2)]];

  const exports = [];
  if (mutation !== "missing-export") {
    exports.push([...name("memory"), 0x02, ...unsigned(0)]);
  }
  exports.push([...name("kofun_abi_version"), 0x03, ...unsigned(0)]);
  exports.push([...name("kofun_start"), 0x00, ...unsigned(startIndex)]);
  exports.push([...name("kofun_alloc"), 0x00, ...unsigned(allocIndex)]);

  /* kofun_start(argv):
   *   an empty List has no element 0, so the bounds rule aborts before the
   *   load rather than reading a header-sized hole;
   *   then one Text, one List[Text], one List[Int], and one empty Text cross
   *   the boundary by reference;
   *   then the first Text's payload is overwritten *after* text_out returned,
   *   which is what a host that retained the pointer would observe. */
  const startBody = body([
    ...op.localGet(0),
    ...op.i64Load(plan.listLengthOffset),
    ...op.i64Const(0),
    ...op.i64Eq(),
    ...op.ifVoid(),
    ...op.i32Const(plan.boundsAbortCode),
    ...op.i32Const(0),
    ...op.call(abortIndex),
    ...op.return(),
    ...op.end(),
    ...op.localGet(0),
    ...op.i32Load(plan.listPayloadOffset),
    ...callTextOut,
    ...op.localGet(0),
    ...op.call(listTextOutIndex),
    ...op.i32Const(plan.countsAddress),
    ...op.call(listIntOutIndex),
    ...op.i32Const(plan.emptyTextAddress),
    ...callTextOut,
    ...op.localGet(0),
    ...op.i32Load(plan.listPayloadOffset),
    ...op.i32Const(plan.scribbleByte),
    ...op.i32Store8(plan.textPayloadOffset),
  ]);

  /* kofun_alloc(size, align): a fixed arena, refused above its capacity.
   * `select` keeps the fixture free of control flow — the allocator is not
   * what is being specified here, the ownership rule around it is. */
  const allocBody = body([
    ...op.i32Const(plan.arenaAddress),
    ...op.i32Const(0),
    ...op.localGet(0),
    ...op.i32Const(plan.arenaCapacity),
    ...op.i32LeU(),
    ...op.select(),
  ]);

  /* Only the start-section fixture carries this. If it ever ran it would put a
   * Text across the boundary before the host had validated anything, which is
   * the whole reason a start section is refused. */
  const probeBody = body([...op.i32Const(plan.emptyTextAddress), ...callTextOut]);

  const code = withStart ? [startBody, allocBody, probeBody] : [startBody, allocBody];

  const data = plan.data.map((segment) => [
    0x00,
    ...op.i32Const(segment.address),
    ...op.end(),
    ...vector([...segment.bytes].map((byte) => [byte])),
  ]);

  return Uint8Array.from([
    ...MAGIC,
    ...section(SECTION.type, vector(types)),
    ...section(SECTION.import, vector(imported)),
    ...section(SECTION.function, vector(functions)),
    ...section(SECTION.memory, vector([[0x00, ...unsigned(plan.memoryPages)]])),
    ...section(SECTION.global, vector([[VALUE_TYPE_CODES.i32, 0x00, ...op.i32Const(abiRevision), ...op.end()]])),
    ...section(SECTION.export, vector(exports)),
    ...(withStart ? section(SECTION.start, unsigned(probeIndex)) : []),
    ...section(SECTION.code, vector(code)),
    ...section(SECTION.data, vector(data)),
  ]);
}

/*
 * The host shim.
 *
 * The allowlisted imports are handed to the guest as *wasm-exported* functions
 * rather than plain JavaScript functions. That is not decoration: a JavaScript
 * function is coerced to whatever type the importer declared, so an engine
 * cannot reject a wrong-signature import against one. A wasm export carries a
 * real function type, and the engine refuses the mismatch itself at
 * instantiation. Each wrapper forwards to the JavaScript host underneath.
 */
export function buildShim(imports) {
  const types = [];
  const typeIndexOf = (params, results) => {
    const encoded = functype(params, results).join(",");
    const existing = types.findIndex((candidate) => candidate.join(",") === encoded);
    if (existing >= 0) return existing;
    types.push(functype(params, results));
    return types.length - 1;
  };
  const signatures = imports.map((entry) => typeIndexOf(entry.params, entry.results));
  const imported = imports.map((entry, index) => [
    ...name("js"),
    ...name(entry.field),
    0x00,
    ...unsigned(signatures[index]),
  ]);
  const functions = signatures.map((index) => [...unsigned(index)]);
  const exports = imports.map((entry, index) => [
    ...name(entry.field),
    0x00,
    ...unsigned(imports.length + index),
  ]);
  const code = imports.map((entry, index) =>
    body([...entry.params.flatMap((_type, slot) => op.localGet(slot)), ...op.call(index)])
  );
  return Uint8Array.from([
    ...MAGIC,
    ...section(SECTION.type, vector(types)),
    ...section(SECTION.import, vector(imported)),
    ...section(SECTION.function, vector(functions)),
    ...section(SECTION.export, vector(exports)),
    ...section(SECTION.code, vector(code)),
  ]);
}

class Reader {
  constructor(bytes) {
    this.bytes = bytes;
    this.at = 0;
  }

  byte() {
    if (this.at >= this.bytes.length) throw new Error("module ends inside a section");
    return this.bytes[this.at++];
  }

  unsigned() {
    let result = 0n;
    let shift = 0n;
    for (;;) {
      const byte = this.byte();
      result |= BigInt(byte & 0x7f) << shift;
      if ((byte & 0x80) === 0) return Number(result);
      shift += 7n;
      if (shift > 63n) throw new Error("unsigned LEB128 is too long");
    }
  }

  signed() {
    let result = 0n;
    let shift = 0n;
    for (;;) {
      const byte = this.byte();
      result |= BigInt(byte & 0x7f) << shift;
      shift += 7n;
      if ((byte & 0x80) === 0) {
        if ((byte & 0x40) !== 0) result -= 1n << shift;
        return result;
      }
      if (shift > 70n) throw new Error("signed LEB128 is too long");
    }
  }

  name() {
    const length = this.unsigned();
    const slice = this.bytes.slice(this.at, this.at + length);
    if (slice.length !== length) throw new Error("module ends inside a name");
    this.at += length;
    return Buffer.from(slice).toString("utf8");
  }

  valueType() {
    const code = this.byte();
    const named = VALUE_TYPE_NAMES[code];
    if (named === undefined) throw new Error(`unknown value type 0x${code.toString(16)}`);
    return named;
  }
}

/* Reads the sections the contract has an opinion about, and nothing else. */
export function decodeModule(bytes) {
  const reader = new Reader(bytes);
  for (const expected of MAGIC) {
    if (reader.byte() !== expected) throw new Error("not a WebAssembly module");
  }
  const module = {
    types: [],
    imports: [],
    functionTypes: [],
    memories: [],
    globals: [],
    exports: [],
    start: null,
  };
  while (reader.at < bytes.length) {
    const id = reader.byte();
    const size = reader.unsigned();
    const end = reader.at + size;
    if (id === SECTION.type) {
      const count = reader.unsigned();
      for (let index = 0; index < count; index += 1) {
        if (reader.byte() !== 0x60) throw new Error("type section holds a non-function type");
        const params = [];
        const paramCount = reader.unsigned();
        for (let slot = 0; slot < paramCount; slot += 1) params.push(reader.valueType());
        const results = [];
        const resultCount = reader.unsigned();
        for (let slot = 0; slot < resultCount; slot += 1) results.push(reader.valueType());
        module.types.push({ params, results });
      }
    } else if (id === SECTION.import) {
      const count = reader.unsigned();
      for (let index = 0; index < count; index += 1) {
        const from = reader.name();
        const field = reader.name();
        const kind = reader.byte();
        const entry = { module: from, field, kind: EXTERN_KIND_NAMES[kind] ?? `0x${kind.toString(16)}` };
        if (kind === 0x00) {
          entry.typeIndex = reader.unsigned();
        } else if (kind === 0x01) {
          reader.byte();
          const limits = reader.byte();
          entry.minimum = reader.unsigned();
          if (limits === 0x01) entry.maximum = reader.unsigned();
        } else if (kind === 0x02) {
          const limits = reader.byte();
          entry.minimum = reader.unsigned();
          if (limits === 0x01) entry.maximum = reader.unsigned();
        } else if (kind === 0x03) {
          entry.type = reader.valueType();
          entry.mutable = reader.byte() === 0x01;
        } else {
          throw new Error(`unknown import kind 0x${kind.toString(16)}`);
        }
        module.imports.push(entry);
      }
    } else if (id === SECTION.function) {
      const count = reader.unsigned();
      for (let index = 0; index < count; index += 1) module.functionTypes.push(reader.unsigned());
    } else if (id === SECTION.memory) {
      const count = reader.unsigned();
      for (let index = 0; index < count; index += 1) {
        const limits = reader.byte();
        const minimum = reader.unsigned();
        const memory = { minimum };
        if (limits === 0x01) memory.maximum = reader.unsigned();
        module.memories.push(memory);
      }
    } else if (id === SECTION.global) {
      const count = reader.unsigned();
      for (let index = 0; index < count; index += 1) {
        const type = reader.valueType();
        const mutable = reader.byte() === 0x01;
        const opcode = reader.byte();
        let value = null;
        if (opcode === 0x41) value = reader.signed();
        else if (opcode === 0x42) value = reader.signed();
        else value = null;
        /* Skip to the end of the initializer expression. */
        while (reader.at < end && reader.bytes[reader.at] !== 0x0b) reader.at += 1;
        reader.byte();
        module.globals.push({ type, mutable, value });
      }
    } else if (id === SECTION.export) {
      const count = reader.unsigned();
      for (let index = 0; index < count; index += 1) {
        const exported = reader.name();
        const kind = reader.byte();
        const target = reader.unsigned();
        module.exports.push({
          name: exported,
          kind: EXTERN_KIND_NAMES[kind] ?? `0x${kind.toString(16)}`,
          index: target,
        });
      }
    } else if (id === SECTION.start) {
      module.start = reader.unsigned();
    }
    if (end > bytes.length) throw new Error("section runs past the end of the module");
    reader.at = end;
  }
  return module;
}

/* Resolves an exported function's declared type across the import/definition
 * index space, which is the only place the two halves of a module meet. */
export function exportedFunctionType(module, index) {
  const importedFunctions = module.imports.filter((entry) => entry.kind === "func");
  if (index < importedFunctions.length) {
    return module.types[importedFunctions[index].typeIndex] ?? null;
  }
  const typeIndex = module.functionTypes[index - importedFunctions.length];
  if (typeIndex === undefined) return null;
  return module.types[typeIndex] ?? null;
}

export function definedGlobal(module, index) {
  const importedGlobals = module.imports.filter((entry) => entry.kind === "global");
  if (index < importedGlobals.length) return null;
  return module.globals[index - importedGlobals.length] ?? null;
}

export function definedMemory(module, index) {
  const importedMemories = module.imports.filter((entry) => entry.kind === "memory");
  if (index < importedMemories.length) return importedMemories[index];
  return module.memories[index - importedMemories.length] ?? null;
}
