# Kofun WASI command capability profile v1

Status: accepted target-profile contract. Owner: repository maintainer. Issue:
#1098. Parent: #26.

This document reserves `wasm32-wasi-command1` and fixes the capability and
module boundary a later backend must implement. The executable authority is
`spec/wasi-command-profile-v1/contract.mjs` plus
`spec/wasi-command-profile-v1/check.sh`, gated by `task
wasi-command-profile`.

It is not implementation evidence. The shipped CLI still refuses the target,
no capability row or release claim names it, and no compiler or backend is
changed by this contract.

## Decision in one line

**A Kofun WASI command is a core WebAssembly module selected only by
`wasm32-wasi-command1`, importing an allowlisted subset of
`wasi_snapshot_preview1` under an exact host capability manifest, exporting
`memory`, `_start`, and immutable `kofun_wasi_command_version = 1`, and gaining
no ambient authority.**

## Current executable boundary

Measured on exact
`origin/main@5c43d4999aefe4f6caeb15a19207bf41fa9935d8`:

```sh
./bin/kofun build examples/wasm_arithmetic.kofun \
  --target wasm32-wasi-command1 -o /tmp/out.wasm
# exit 2
# stdout: empty
# stderr: kofun: unsupported target: wasm32-wasi-command1
# /tmp/out.wasm: absent

task wasm
# pass: the legacy numeric/functions target and browser sample

task wasm-host-abi
task wasm-host-profile
# pass: the separate wasm32-hostabi1 Text/List contract and selector

task rfc-registry
# PASS: 31 recorded decisions (20 accepted, 5 implemented, 6 proposed)
```

`wasm32` and `wasm32-hostabi1` therefore remain the only shipped wasm target
profiles. Reserving another name does not select it by source shape and does
not make either existing target import WASI.

## Compatibility basis and version

The profile pins the core-module namespace used by WASI Preview 1:
`wasi_snapshot_preview1`. Preview 1 remains a distinct, widely deployed WASI
line; current WASI 0.2 and 0.3 use Component Model/WIT worlds and different
resource and async interfaces. The upstream relationship is documented by the
[WebAssembly/WASI repository](https://github.com/WebAssembly/WASI) and the
[WASI release index](https://wasi.dev/releases/).

This is deliberately a Kofun target profile, not a claim of implementing the
full WASI CLI world. The exact subset below is v1. Adding an import, changing a
signature, adding writable authority, or moving to a Component Model world
requires a new Kofun profile revision and target name.

Version identity has three independent parts:

1. the selector is `wasm32-wasi-command1`;
2. every import module is exactly `wasi_snapshot_preview1`; and
3. the module exports immutable `kofun_wasi_command_version: i32 = 1`.

A host receives the expected selector out of band and validates the other two
parts before instantiation. It never infers the profile from source or from a
single familiar import.

## Module surface

A conforming module:

- defines and exports exactly one core-Wasm memory for boundary pointers;
- exports `_start: () -> ()` as the only command entry point;
- exports immutable `kofun_wasi_command_version: i32 = 1`;
- has no core-Wasm start section, so validation and authority binding precede
  guest execution;
- imports functions only, all from `wasi_snapshot_preview1`; and
- imports each allowlisted field at most once and with the exact signature
  below.

The module may import a subset. An import is legal only when the manifest
grants its required authority.

| Import | Core-Wasm signature | Capability |
|---|---|---|
| `args_get` | `(i32, i32) -> i32` | `arguments` |
| `args_sizes_get` | `(i32, i32) -> i32` | `arguments` |
| `environ_get` | `(i32, i32) -> i32` | `environment` |
| `environ_sizes_get` | `(i32, i32) -> i32` | `environment` |
| `fd_read` | `(i32, i32, i32, i32) -> i32` | `stdin` or `preopen-read` |
| `fd_write` | `(i32, i32, i32, i32) -> i32` | `stdout` or `stderr` |
| `fd_close` | `(i32) -> i32` | `preopen-read` |
| `fd_prestat_get` | `(i32, i32) -> i32` | `preopen-read` |
| `fd_prestat_dir_name` | `(i32, i32, i32) -> i32` | `preopen-read` |
| `path_open` | `(i32, i32, i32, i32, i32, i64, i64, i32, i32) -> i32` | `preopen-read` |
| `clock_time_get` | `(i32, i64, i32) -> i32` | `monotonic-clock` |
| `random_get` | `(i32, i32) -> i32` | `random` |
| `proc_exit` | `(i32) -> ()` | `exit` |

No other import namespace, field, extern kind, overload, or signature belongs
to v1. In particular there is no socket, DNS, thread, poll, realtime-clock,
filesystem-write, or component interface.

## Capability manifest

Before module validation, the host receives one closed, already-decoded typed
record. The JSON below is an illustrative serialization, not input to a JSON
parser owned by this profile:

```json
{
  "profile": "wasm32-wasi-command1",
  "capabilities": {
    "arguments": true,
    "environment": false,
    "stdin": false,
    "stdout": true,
    "stderr": true,
    "monotonic-clock": false,
    "random": false,
    "preopen-read": false,
    "exit": true
  }
}
```

Every v1 capability key is present and has a Boolean value. A missing, unknown,
or non-Boolean key refuses the profile rather than inheriting a host default.
The reference model consumes the typed record and therefore makes no claim
that it can observe duplicate keys in raw serialized bytes. Duplicate-key
handling belongs to a transport decoder outside this typed-record interface.
Operational data is supplied beside the manifest by the host; the Boolean
controls whether that channel exists at all.

### Arguments and environment

`arguments` exposes an ordered sequence of host-owned UTF-8 byte strings.
`environment` exposes unique key/value byte strings sorted by unsigned UTF-8
key bytes for deterministic enumeration. Neither permits an embedded NUL; an
environment key also cannot contain `=`. The host computes sizes first and
copies bytes only into guest ranges supplied by `args_get` or `environ_get`.
The typed JavaScript reference input must be well-formed Unicode and round-trip
through UTF-8 without replacement; uniqueness is checked on encoded key bytes,
not only JavaScript string identity.

When the capability is false, importing either function in its pair is an
`undeclared-capability` refusal before instantiation. An empty granted vector
is different from an absent capability.

### Standard streams

Descriptors are fixed: stdin is 0, stdout is 1, stderr is 2. `fd_read` on 0
requires `stdin`; `fd_write` on 1 requires `stdout`; `fd_write` on 2 requires
`stderr`. A standard descriptor never denotes a preopen and cannot be closed
or rebound.

Stream payloads are bytes. A later Kofun `Text` adapter must validate UTF-8 at
its own language boundary; the host ABI does not silently repair bytes.
Reference results therefore publish stream hex, and the gate carries `fffe`
through stdin, stdout, and read-only file evidence without replacement text.

### Monotonic clock and random

`clock_time_get` accepts only the Preview 1 monotonic clock identifier. A
realtime clock is not v1 authority. The deterministic conformance host supplies
a scripted nondecreasing nanosecond sequence; a production host may supply a
real monotonic clock only when `monotonic-clock` is true.
Every scripted value is validated as `u64` before execution, and descending,
negative, or overflowing scripts refuse during host construction.

`random_get` copies explicit host-provided bytes. The conformance host uses a
scripted byte sequence so vectors are reproducible. A production entropy
source is authority granted by `random`; it is never inferred from the host's
ability to call an operating-system RNG.

### Read-only preopens

When the `preopen-read` Boolean is granted, its operational side-channel is an
ordered mapping from a guest absolute directory name to a host-selected
directory capability. Preopen descriptors begin at 3. `fd_prestat_get` and
`fd_prestat_dir_name` disclose only those guest names.

`path_open` is accepted only beneath one supplied preopen, with read rights,
no create/truncate/append flags, no absolute child path, no empty segment, and
no `.` or `..` segment. Opened file descriptors support sequential `fd_read`
and `fd_close` only. They cannot escape the preopen, follow authority outside
it, or become writable.

An empty granted preopen list is different from absent authority: enumeration
succeeds with no entries. No physical working directory is implicit.

### Exit and traps

`proc_exit` consumes an unsigned 32-bit status and terminates the command
without returning to guest code. It requires `exit`. Normal `_start` return is
status 0.

An engine validation error, guest `unreachable`, bounds trap, integer trap, or
host boundary refusal is a trap result, not a chosen process exit. The host
records the trap category and publishes no normal stdout/stderr/exit tuple for
that run.

## Guest memory and ownership

All Preview 1 pointers are unsigned 32-bit offsets into the single exported
memory. Before reading or writing, the host checks each pointer, length,
vector count, nested pointer, and multiplication/addition for overflow against
the current memory size. A zero length still requires a representable pointer
at or one byte past the end; wrapping arithmetic is never accepted.
JavaScript receives a Wasm `i32` as a signed Number, so the reference host
normalizes every guest pointer, length, and count to its `u32` bit pattern
before range checks or state changes. Computed table sizes and vector byte
totals are separately required to fit `u32`; they are never truncated by a
typed-array or `DataView` write.

The profile does not fix the memory's initial or maximum page count. The
reference fixture uses one fixed page only to keep its bytes and addresses
small and deterministic; conforming modules may declare other valid limits
while still defining and exporting exactly one memory.

The host borrows a guest range only for the dynamic extent of one import call.
It copies any byte sequence it must retain, completes all guest writes before
returning, and stores no `DataView`, typed-array view, pointer, iovec address,
or path slice in host state. Growing or mutating guest memory after the call
therefore cannot change captured stdout, an environment key, a path, or a
random result already delivered.

The engine fixture writes `ok\n`, returns from `fd_write`, then overwrites the
guest source byte with `X`. The captured host output must remain `ok\n`. That
mutation is the executable lifetime proof; a model that only states the rule
does not pass.

## Validation and refusal order

The host completes these phases before calling `_start`:

1. validate the manifest's profile and exact capability-key vocabulary;
2. require a core-valid WebAssembly module, then decode it and require the
   expected import module;
3. reject duplicate, unknown, non-function, or wrong-signature imports;
4. prove every imported field has at least one granted authority;
5. require exactly one defined memory, exactly one memory export named
   `memory`, `_start`, and the immutable version export; and
6. reject a core-Wasm start section.

The stable contract diagnostics are:

- `profile-version-mismatch`
- `invalid-module`
- `unknown-import-module`
- `unknown-import`
- `duplicate-import`
- `import-signature-mismatch`
- `non-function-import`
- `missing-export`
- `start-section-forbidden`
- `undeclared-capability`
- `boundary-out-of-range`
- `retained-guest-pointer`
- `vector-drift`

A refusal publishes no accepted validation report and executes no guest code.
The focused gate mutates every structural class independently, so one catch-all
failure cannot make the matrix green.

## Canonical vectors and engine evidence

`contract.mjs` is the single machine-readable vocabulary. `model.mjs vectors`
derives `vectors/canonical.json`; two runs must be byte-identical, and
`model.mjs compare` rejects any checked-in drift.

The vector's `reference_behavior` field is produced by executing the bounded
reference host, not by copying prose: argument/environment size and copy
pairs, byte-exact stdin/stdout/stderr iovecs, two validated monotonic readings,
scripted random bytes, preopen discovery, read-only `path_open`, two short
sequential `fd_read` calls followed by EOF, `fd_close`, normal return,
`proc_exit`, and an actual Node-executed `unreachable` trap all run against
guest memory.
The gate invokes every corresponding operation again with an all-false
manifest and requires `undeclared-capability`; it also refuses path traversal
and writable rights. A deliberately drifted vector must produce
`vector-drift`.

The positive fixture is assembled from the same contract into transparent
core-Wasm bytes. The model first validates its imports, signatures, manifest,
exports, and version, then Node's WebAssembly engine validates, instantiates,
and executes it through the same reference host's `fd_write`. A second
hand-assembled core-valid fixture executes `unreachable`; the gate catches a
real `WebAssembly.RuntimeError` and publishes only the stable trap category,
with no stream or exit tuple. Passing the static model without engine execution
is not sufficient.

This evidence proves the contract is implementable. It does not prove that
Kofun emits such a module, that another engine supports it, or that WASI is a
released capability.

## Why this is not in the RFC ledger

`rfcs/README.md` assigns the ledger public language semantics. This profile
changes no Kofun syntax, type, effect, ownership rule, or source diagnostic.
It selects an external target/host boundary, the same class of decision #1000
records in `spec/wasm-host-profile-v1.md` with an executable gate and no RFC
row.

Therefore `rfcs/index.json` is unchanged. `task rfc-registry` remains a
regression gate: a green ledger does not implement this profile, and this
profile does not consume an RFC identity or alter any proposal under review.

## Compatibility

The change is additive at the specification layer and operationally inert.
Exact current behavior is preserved:

- bare `wasm32` still emits the legacy numeric binding;
- `wasm32-hostabi1` still emits its bounded Text/List profile;
- `wasm32-wasi-command1` still exits 2 and writes no artifact; and
- release claims and capability rows do not mention the reserved profile.

A later implementation must use a separate issue and must change the CLI,
backend, capability matrix, and release evidence together. That change cannot
be smuggled into this decision gate.

## Non-goals

- **No sockets or DNS.** Network authority and name resolution are absent.
- **No ambient filesystem authority.** Only explicit read-only preopens exist.
- **No Component Model.** WASI 0.2/0.3 worlds, WIT bindings, resources, streams,
  futures, and component packaging require another target profile.
- **No backend or runtime activation.** No compiler emits this surface here.
- No writable preopens, current-directory inference, symlink escape, or host
  path disclosure.
- No realtime clock, threads, polling, async I/O, SIMD, GC types, or unstable
  WASI proposal.
- No engine/browser/WASI support matrix. #26 files that only after at least two
  hosts implement the same accepted ABI revision.
- No claim of full-language, full-WASI, or production sandbox support.

## Later implementation exit criteria

A future backend child may claim `wasm32-wasi-command1` only when:

1. a Kofun source fixture emits a module accepted by this validator;
2. every imported capability comes from an explicit invocation manifest;
3. the deterministic reference host and one separately maintained engine host
   agree on stdout, stderr, exit, traps, arguments, environment, clock/random,
   and read-only preopen observations;
4. every unsupported source and absent capability fails without an artifact;
5. capability and release rows name the exact target and gate; and
6. `wasm32` and `wasm32-hostabi1` bytes and observations remain unchanged.

Until then the profile is an accepted implementation input and an unsupported
target, not a shipped capability.

## Validation

| Check | Command | Expected result |
|---|---|---|
| Contract/model | `task wasi-command-profile` | canonical vectors, structural refusals, capability denial, pointer bounds, and engine lifetime proof pass |
| Existing wasm | `task wasm` | legacy numeric/functions/browser evidence remains green |
| Existing aggregate ABI | `task wasm-host-abi && task wasm-host-profile` | hostabi1 contract and activation remain green |
| RFC separation | `task rfc-registry` | existing rows pass unchanged |
| Repository | `task repository-check` | spec/docs/task registration obey repository rules |
