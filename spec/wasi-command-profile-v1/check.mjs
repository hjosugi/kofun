#!/usr/bin/env node

import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

import {
  CAPABILITIES,
  DIAGNOSTICS,
  IMPORTS,
  PROFILE_TARGET,
} from "./contract.mjs";
import {
  Refusal,
  ReferenceHost,
  GuestTrap,
  buildFixture,
  canonical,
  checkedGuestRange,
  checkedMemory32Size,
  compare,
  executeReferenceCommand,
  makeManifest,
  referenceVectors,
  requireCapability,
  runEngineFixture,
  runEngineTrapFixture,
  runReferenceHostScenarios,
  validateModule,
} from "./model.mjs";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, "..", "..");
const GOLDEN = path.join(HERE, "vectors", "canonical.json");
const SPEC = path.join(ROOT, "spec", "wasi-command-profile-v1.md");

function expectRefusal(name, diagnostic, operation) {
  try {
    operation();
  } catch (error) {
    assert.ok(error instanceof Refusal, `${name}: expected a profile refusal, got ${error}`);
    assert.equal(error.diagnostic, diagnostic, `${name}: wrong diagnostic`);
    return;
  }
  assert.fail(`${name}: expected ${diagnostic}`);
}

const stdoutManifest = makeManifest(["stdout", "exit"]);
assert.deepEqual(validateModule(buildFixture(), stdoutManifest).imports, ["fd_write"]);

for (const [mutation, diagnostic] of [
  ["invalid-opcode", "invalid-module"],
  ["wrong-module", "unknown-import-module"],
  ["unknown-import", "unknown-import"],
  ["duplicate-import", "duplicate-import"],
  ["wrong-signature", "import-signature-mismatch"],
  ["wrong-version", "profile-version-mismatch"],
  ["non-function-import", "non-function-import"],
  ["missing-export", "missing-export"],
  ["extra-memory-definition", "missing-export"],
  ["extra-memory-export", "missing-export"],
  ["start-section", "start-section-forbidden"],
]) {
  expectRefusal(mutation, diagnostic, () => validateModule(buildFixture(mutation), stdoutManifest));
}

expectRefusal("wrong profile", "profile-version-mismatch", () =>
  validateModule(buildFixture(), { ...stdoutManifest, profile: "wasm32-wasi-command2" }),
);
expectRefusal("missing manifest key", "undeclared-capability", () =>
  validateModule(buildFixture(), { profile: PROFILE_TARGET, capabilities: { stdout: true } }),
);
expectRefusal("unknown manifest key", "undeclared-capability", () =>
  validateModule(buildFixture(), {
    profile: PROFILE_TARGET,
    capabilities: { ...stdoutManifest.capabilities, network: false },
  }),
);
expectRefusal("unknown manifest field", "undeclared-capability", () =>
  validateModule(buildFixture(), { ...stdoutManifest, transport: "json" }),
);
expectRefusal("non-Boolean manifest value", "undeclared-capability", () =>
  validateModule(buildFixture(), {
    profile: PROFILE_TARGET,
    capabilities: { ...stdoutManifest.capabilities, stdout: "true" },
  }),
);
expectRefusal("stdout not granted", "undeclared-capability", () =>
  validateModule(buildFixture(), makeManifest([])),
);

assert.deepEqual(
  validateModule(buildFixture("larger-memory"), stdoutManifest).exports,
  ["memory", "kofun_wasi_command_version", "_start"],
  "the normative validator must not turn the fixture's one-page limits into profile policy",
);

for (const capability of CAPABILITIES) {
  assert.equal(requireCapability(makeManifest([capability]), capability), undefined);
  expectRefusal(`${capability} denied`, "undeclared-capability", () =>
    requireCapability(makeManifest([]), capability),
  );
}

const referenceBehavior = runReferenceHostScenarios();
assert.deepEqual(referenceBehavior, {
  arguments: ["kofun", "α"],
  argument_bytes: 9,
  environment: ["A=1", "Z=9"],
  environment_bytes: 8,
  stdin_hex: "fffe",
  stdout_hex: "6f7574",
  stderr_hex: "657272",
  monotonic_clock_ns: ["1000", "2000"],
  random_hex: "01020304",
  preopen: {
    name: "/fixtures",
    reads_hex: ["44ff", "54fe"],
    eof: 0,
    closed: true,
  },
  return_exit: { kind: "exit", code: 0 },
  proc_exit: { kind: "exit", code: 7 },
  trap: { kind: "trap", category: "unreachable" },
});

const deniedMemory = new WebAssembly.Memory({ initial: 1 });
const deniedHost = new ReferenceHost({
  manifest: makeManifest([]),
  memory: deniedMemory,
  arguments: ["denied"],
  environment: [["DENIED", "1"]],
  stdin: Buffer.from("denied"),
  monotonicClock: [1n],
  random: Buffer.from([1]),
  preopens: [{ guest: "/denied", files: { "file.txt": Buffer.from("denied") } }],
});
for (const [name, operation] of [
  ["args_sizes_get denied", () => deniedHost.args_sizes_get(0, 4)],
  ["args_get denied", () => deniedHost.args_get(8, 16)],
  ["environ_sizes_get denied", () => deniedHost.environ_sizes_get(0, 4)],
  ["environ_get denied", () => deniedHost.environ_get(8, 16)],
  ["stdin fd_read denied", () => deniedHost.fd_read(0, 0, 0, 0)],
  ["stdout fd_write denied", () => deniedHost.fd_write(1, 0, 0, 0)],
  ["stderr fd_write denied", () => deniedHost.fd_write(2, 0, 0, 0)],
  ["fd_prestat_get denied", () => deniedHost.fd_prestat_get(3, 0)],
  ["fd_prestat_dir_name denied", () => deniedHost.fd_prestat_dir_name(3, 0, 7)],
  ["path_open denied", () => deniedHost.path_open(3, 0, 0, 0, 0, 2n, 0n, 0, 0)],
  ["preopen fd_read denied", () => deniedHost.fd_read(3, 0, 0, 0)],
  ["fd_close denied", () => deniedHost.fd_close(3)],
  ["clock_time_get denied", () => deniedHost.clock_time_get(1, 0n, 0)],
  ["random_get denied", () => deniedHost.random_get(0, 1)],
  ["proc_exit denied", () => deniedHost.proc_exit(1)],
]) {
  expectRefusal(name, "undeclared-capability", operation);
}
assert.deepEqual(
  executeReferenceCommand(deniedHost, (host) => host.fd_write(1, 0, 0, 0)),
  { kind: "trap", category: "undeclared-capability" },
  "a host authority refusal must be a trap without a normal stream/exit tuple",
);
assert.deepEqual(
  executeReferenceCommand(deniedHost, () => {
    throw new GuestTrap("integer-overflow");
  }),
  { kind: "trap", category: "integer-overflow" },
  "an engine trap must not publish a normal stream/exit tuple",
);

const clockOptions = {
  manifest: makeManifest(["monotonic-clock"]),
  memory: new WebAssembly.Memory({ initial: 1 }),
};
expectRefusal("descending monotonic script", "boundary-out-of-range", () =>
  new ReferenceHost({ ...clockOptions, monotonicClock: [2n, 1n] }),
);
expectRefusal("negative monotonic value", "boundary-out-of-range", () =>
  new ReferenceHost({ ...clockOptions, monotonicClock: [-1n] }),
);
expectRefusal("overflowing monotonic value", "boundary-out-of-range", () =>
  new ReferenceHost({ ...clockOptions, monotonicClock: [1n << 64n] }),
);
expectRefusal("argument lone surrogate", "boundary-out-of-range", () =>
  new ReferenceHost({
    manifest: makeManifest(["arguments"]),
    memory: new WebAssembly.Memory({ initial: 1 }),
    arguments: ["\ud800"],
  }),
);
expectRefusal("environment key lone surrogate", "boundary-out-of-range", () =>
  new ReferenceHost({
    manifest: makeManifest(["environment"]),
    memory: new WebAssembly.Memory({ initial: 1 }),
    environment: [["\ud800", "x"], ["\ufffd", "y"]],
  }),
);
expectRefusal("preopen guest lone surrogate", "boundary-out-of-range", () =>
  new ReferenceHost({
    manifest: makeManifest(["preopen-read"]),
    memory: new WebAssembly.Memory({ initial: 1 }),
    preopens: [{ guest: "/\ud800", files: {} }],
  }),
);

const preopenMemory = new WebAssembly.Memory({ initial: 1 });
const preopenHost = new ReferenceHost({
  manifest: makeManifest(["preopen-read"]),
  memory: preopenMemory,
  preopens: [{ guest: "/fixtures", files: { "data.txt": Buffer.from("DATA") } }],
});
preopenHost.writeBytes(64, Buffer.from("../data"));
expectRefusal("preopen traversal", "undeclared-capability", () =>
  preopenHost.path_open(3, 0, 64, 7, 0, 2n, 0n, 0, 32),
);
preopenHost.writeBytes(64, Buffer.from("data.txt"));
expectRefusal("writable preopen rights", "undeclared-capability", () =>
  preopenHost.path_open(3, 0, 64, 8, 0, 64n, 0n, 0, 32),
);

const memory = new WebAssembly.Memory({ initial: 1, maximum: 1 });
checkedGuestRange(memory, 0, memory.buffer.byteLength);
assert.deepEqual(
  checkedGuestRange({ buffer: { byteLength: 0x100000000 } }, -0x80000000, 1),
  { pointer: 0x80000000, length: 1 },
  "a signed JS i32 pointer must normalize to its memory32 u32 offset",
);
expectRefusal("computed vector byte overflow", "boundary-out-of-range", () =>
  checkedMemory32Size(0x100000000, "vector byte count"),
);
expectRefusal("negative pointer", "boundary-out-of-range", () => checkedGuestRange(memory, -1, 1));
expectRefusal("wrapped pointer", "boundary-out-of-range", () =>
  checkedGuestRange(memory, memory.buffer.byteLength - 1, 2),
);

const hostileMemory = new WebAssembly.Memory({ initial: 1 });
const hostileRandom = new ReferenceHost({
  manifest: makeManifest(["random"]),
  memory: hostileMemory,
  random: Buffer.from([1, 2, 3, 4]),
});
expectRefusal("signed random length 0xffffffff", "boundary-out-of-range", () =>
  hostileRandom.random_get(0, -1),
);
assert.equal(hostileRandom.randomOffset, 0, "a refused unsigned random length changed host state");
assert.deepEqual(
  [...new Uint8Array(hostileMemory.buffer, 0, 4)],
  [0, 0, 0, 0],
  "a refused unsigned random length wrote guest bytes",
);

const hostileStdout = new ReferenceHost({
  manifest: makeManifest(["stdout"]),
  memory: hostileMemory,
});
expectRefusal("signed iovec count 0xffffffff", "boundary-out-of-range", () =>
  hostileStdout.fd_write(1, 0, -1, 8),
);
assert.equal(hostileStdout.captured("stdout").length, 0, "a refused iovec count published bytes");

const byteMemory = new WebAssembly.Memory({ initial: 1 });
const byteHost = new ReferenceHost({
  manifest: makeManifest(["stdout"]),
  memory: byteMemory,
});
byteHost.writeBytes(16, [0xff, 0xfe]);
new DataView(byteMemory.buffer).setUint32(0, 16, true);
new DataView(byteMemory.buffer).setUint32(4, 2, true);
byteHost.fd_write(1, 0, 1, 8);
const byteExactResult = executeReferenceCommand(byteHost, () => {});
assert.deepEqual(
  byteExactResult,
  { kind: "exit", code: 0, stdout_hex: "fffe", stderr_hex: "" },
  "invalid UTF-8 stream bytes must remain byte-exact",
);
assert.ok(!("stdout" in byteExactResult), "command results must not publish a repaired UTF-8 stream");

const engine = await runEngineFixture();
assert.equal(engine.engine, "node-webassembly");
assert.deepEqual(engine.result, {
  kind: "exit",
  code: 0,
  stdout_hex: "6f6b0a",
  stderr_hex: "",
});
assert.equal(engine.guest_byte_after_return, "X");
assert.equal(engine.host_retained_guest_pointer, false);
assert.deepEqual(
  runEngineTrapFixture(),
  { kind: "trap", category: "unreachable" },
  "Node must execute an actual Wasm unreachable trap without a normal tuple",
);

assert.equal(fs.readFileSync(GOLDEN, "utf8"), canonical(referenceVectors()), "canonical vector drift");
const driftDirectory = fs.mkdtempSync(path.join(os.tmpdir(), "kofun-wasi-command-drift."));
try {
  const drifted = path.join(driftDirectory, "canonical.json");
  fs.writeFileSync(drifted, canonical({ ...referenceVectors(), version: 2 }));
  expectRefusal("drifted canonical vector", "vector-drift", () => compare(drifted));
} finally {
  fs.rmSync(driftDirectory, { recursive: true, force: true });
}

const spec = fs.readFileSync(SPEC, "utf8");
for (const value of [PROFILE_TARGET, "wasi_snapshot_preview1", ...CAPABILITIES, ...IMPORTS.map((entry) => entry.field)]) {
  assert.ok(spec.includes(`\`${value}\``) || spec.includes(value), `spec does not name ${value}`);
}
for (const phrase of [
  "No sockets or DNS",
  "No ambient filesystem authority",
  "No Component Model",
  "No backend or runtime activation",
]) {
  assert.ok(spec.includes(phrase), `spec does not pin non-goal: ${phrase}`);
}

const ledger = JSON.parse(fs.readFileSync(path.join(ROOT, "rfcs", "index.json"), "utf8"));
for (const entry of ledger.rfcs) {
  assert.ok(
    !(entry.normative_spec ?? []).includes("spec/wasi-command-profile-v1.md"),
    "the target profile must not be recorded as a language RFC",
  );
}
const claims = fs.readFileSync(path.join(ROOT, "release", "claims.json"), "utf8");
assert.ok(!claims.includes(PROFILE_TARGET), "the reserved profile must not appear in release claims");

assert.equal(IMPORTS.length, 13, "the import allowlist changed without updating the contract review");
assert.equal(CAPABILITIES.length, 9, "the capability vocabulary changed without updating the contract review");
assert.equal(new Set(DIAGNOSTICS).size, DIAGNOSTICS.length, "duplicate profile diagnostics");

process.stdout.write(
  [
    "PASS: the 13-import Preview 1 allowlist and nine-capability manifest are closed and versioned",
    "PASS: invalid core modules, wrong/unknown/duplicate imports, signatures, version, memory surface, exports, and start section fail before execution",
    "PASS: arg/env, stdio, clock, random, read-only preopen, exit, and trap behaviors have deterministic granted and denied evidence",
    "PASS: Node executed the minimal module and an actual unreachable trap; byte-exact stdout survived guest mutation",
    "PASS: canonical vectors and adversarial drift, normative prose, RFC separation, and absent release claims agree",
  ].join("\n") + "\n",
);
