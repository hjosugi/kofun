# Kofun

Kofun is an experimental programming language. Source files use `.kofun`.

Kofun's product position is: **the language where you state an algebraic law
and the compiler hands you a counterexample.** The current implementation only
supports a bounded Monad-law checkpoint; this sentence is the direction used to
prioritize future work, not a claim that the general law system is complete.

The active compiler seed is written in Kofun itself:

- canonical source: `bootstrap/stage1/compiler.kofun`
- audited bootstrap artifact: `bootstrap/stage1/compiler.c`
- Python-free verification: `bootstrap/stage1/check.sh`

The repository contains no Python implementation or Python build dependency.

## Current bootstrap Core

Stage 1 intentionally accepts a small arithmetic Core:

```kofun
# expect: 42
fn main() {
    print((6 + 1) * 6)
}
```

Supported expressions use integer digits, whitespace, `+`, `-`, `*`, `/`, `%`,
and parentheses. Unsupported language features are rejected or explicitly
reported as skipped by the test runner.

This is not yet a semantic self-hosting fixed point. The checked-in C11 seed
starts the Kofun-written compiler, but Stage 1 cannot compile all constructs in
its own source yet.

Six Python-free checkpoints now exercise the path beyond that Core:

- `bootstrap/stage2/` lexes and structurally parses Kofun, emits a deterministic
  function IR, and reaches a byte-stable source/token/IR round trip;
- `bootstrap/native/` uses Kofun-authored bytes to build and execute a static
  Linux x86-64 ELF64 image without an assembler or linker; its active x86-64
  Core includes recursive user functions, local List bindings,
  `map`/`filter`/`fold`, and UTF-8 Text operations;
- `bootstrap/c_abi/` provides an explicit host-C/dynamic-linking profile for
  checked `extern "C"` declarations and `repr(C)` structs;
- `framework/http/` uses that explicit host-C profile to run a reusable Linux
  epoll HTTP/1.1 library configured with routes from Kofun source;
- `framework/cli/` compiles declarative command metadata into a direct-static
  Linux x86-64 application whose help and runtime dispatch share that metadata;
- `stdlib/` defines the raw syscall ABI, value-level errno conversion, affine
  resource wrappers, and the file round-trip acceptance fixture in Kofun.

All six have executable gates. The complete Stage 2 self-recompile and
general native lowering remain open.

## Measured project status

This table is the position-paper checklist tracked by
[issue #281](https://github.com/hjosugi/kofun/issues/281). `Active` means that
the linked executable gate exercises the feature. A design document alone is
never enough to move a row to `Active`.

| Requirement | Status | Evidence or blocker |
|---|---|---|
| Direct native machine code | **Active, bounded Core** | [`bootstrap/native/check.sh`](bootstrap/native/check.sh) builds and executes direct x86-64 ELF and audits AArch64 output |
| Static, dependency-free binaries | **Active, bounded Core** | The [native gate](bootstrap/native/check.sh) rejects an interpreter, dynamic section, and dynamic dependencies |
| Algebraic-law counterexamples | **Active, Monad only** | [`docs/LAW_SYSTEM.md`](docs/LAW_SYSTEM.md) records the executable bounded and finite-model gates; general declarations remain [#551](https://github.com/hjosugi/kofun/issues/551) |
| Memory safety without GC | **Design only** | [`docs/MEMORY_MODEL.md`](docs/MEMORY_MODEL.md) is target design; the complete checker and reclamation path are not implemented |
| Runtime performance parity | **Not established** | [`benchmarks/README.md`](benchmarks/README.md) limits current results to smoke and bounded HTTP measurements |
| Heap allocation | **Active, narrow; no reclamation** | [`bootstrap/native/README.md`](bootstrap/native/README.md) documents the x86-64 `mmap` runtime used by List and Text |
| Text and homogeneous List values | **Active, bounded x86-64 Core** | [`tests/conformance/`](tests/conformance/) runs the registered Text and List corpora |
| Heterogeneous records | **Missing** | [#546](https://github.com/hjosugi/kofun/issues/546) blocks a useful token and AST representation |
| User-defined function calls | **Active, bounded Int Core** | [`tests/conformance/functions`](tests/conformance/functions) executes arguments, results, forward/mutual recursion, and six-argument calls under both C11 and direct x86-64 |
| C ABI interop | **Active, bounded host-C profile** | [`bootstrap/c_abi/check.sh`](bootstrap/c_abi/check.sh) verifies calls and `repr(C)` layout; it is separate from direct native code |
| Embedded / freestanding profile | **Missing** | The current direct backend targets Linux syscalls |
| Semantic self-hosting fixed point | **Missing** | [`docs/SELF_HOSTING.md`](docs/SELF_HOSTING.md) and the bootstrap gates keep seed, checkpoint, and fixed-point claims distinct |
| Stable language specification | **Missing** | [`spec/README.md`](spec/README.md) distinguishes normative contracts from roadmap material |
| Package ecosystem | **Missing** | Package and registry work remains a later roadmap milestone |

The first P0 execution blocker now has a bounded C11/direct-x86-64 gate
([#549](https://github.com/hjosugi/kofun/issues/549)). Heterogeneous records
([#546](https://github.com/hjosugi/kofun/issues/546)) are the next critical
path before frontier type, effect, concurrency, or backend work.

The research decisions supporting that order are:

| Direction | Decision | Evidence |
|---|---|---|
| Native backend / MLIR | Keep the direct self-hosted backend; retain a second-backend interface | [#554](https://github.com/hjosugi/kofun/issues/554) |
| Ownership and data-race prevention | Keep `read` / `edit` / `take`; defer concurrency and begin with scoped parallelism | [#555](https://github.com/hjosugi/kofun/issues/555) |
| Effects | Defer advanced handlers; introduce a pure/impure boundary first | [#556](https://github.com/hjosugi/kofun/issues/556) |
| Type checker | Copy small, proven mechanisms before adding frontier features | [#557](https://github.com/hjosugi/kofun/issues/557) |
| Dependent types | Reject full dependent types; investigate refinement types instead | [#558](https://github.com/hjosugi/kofun/issues/558) |

## Requirements

- POSIX shell
- C11 compiler (`cc`, or set `CC=clang`)
- `sha256sum`
- Linux x86-64 for the native executable gate
- `rustc` for the required Rust `cdylib` C ABI acceptance gate
- `cargo` for the required offline vendored-crate shim gate
- `ar` for the required HTTP framework static-library gate
- `ld`, `readelf`, `file`, `ldd`, and `script` for native CLI prefix auditing

Python is not required or used.

## Quick start

```sh
./bin/kofun --version
./bin/kofun check bootstrap/fixtures/answer.kofun
./bin/kofun run bootstrap/fixtures/answer.kofun
./bin/kofun build bootstrap/fixtures/answer.kofun -o build/answer
./build/answer
```

Single-file build is always the direct path: it does not inspect a manifest,
look up an incremental cache, contact a daemon, or require frost-build. In a
directory containing `kofun.toml`, the argument-free form upgrades the same
command to the frost-build incremental engine:

```sh
cd examples/project
kofun build
```

See `docs/BUILD_SYSTEM.md` for the manifest contract, artifacts, and the
compiler-internal latency gate.

Build the runnable HTTP/API sample through the explicit C ABI profile:

```sh
sh framework/http/build.sh examples/api_server.kofun build/api-server
./build/api-server
```

See `framework/http/README.md` for its bounded HTTP/JSON contract, host-C trust
boundary, shutdown behavior, and benchmark methodology.

Build the dependency-free native CLI sample:

```sh
./bin/kofun build examples/cli_tool.kofun \
  --framework cli -o build/kofun-tool
./build/kofun-tool greet Ada --prefix Welcome
```

Or scaffold a clean project with
`./bin/kofun new demo-cli --template cli`. See
`framework/cli/README.md` for the bounded declaration profile, generated help,
TTY/`NO_COLOR` behavior, security boundary, and Linux x86-64 limitations.

Generated C11 can be inspected with:

```sh
./bin/kofun emit-c bootstrap/fixtures/answer.kofun build/answer.c
```

The foreign-function path is deliberately explicit and separate from the
static direct-native backend:

```sh
./bin/kofun build tests/ffi/c_abi.kofun \
  --backend c --c-abi --link-library /path/to/libexample.so \
  --emit-c build/ffi-demo.c -o build/ffi-demo
```

See `bootstrap/c_abi/README.md` for its bounded type/source slice and trust
boundary.

The worked Rust crate example wraps vendored `unicode-segmentation 1.13.3`
behind buffer/length/status and `repr(C)` values. It does not expose Rust
`String`, `Vec`, `Result`, traits, or unwinding:

```sh
make rust-shim
```

See `docs/RUST_CRATE_SHIMS.md` for the offline recipe, ownership/error mapping,
unsupported Rust surfaces, and measured build-cost definitions.

Run every active gate:

```sh
make verify
```

Run the compiler-quality gates independently:

```sh
make diagnostics
make fuzz
```

The Stage 2 must-fail corpus pins each active diagnostic code, exact message,
and declared source span. Its paired `.stderr` files can be regenerated with
`sh tests/diagnostics/stage2/bless.sh`. The deterministic fuzz smoke tests
exercise 128 arbitrary token streams with a watchdog and compare 48 generated
arithmetic programs across an independent expected value, C11, and direct
x86-64 output. See `tests/diagnostics/stage2/README.md` and
`tests/fuzz/README.md` for the exact scope and remaining coverage boundaries.

## CLI

```text
kofun build INPUT.kofun [-o OUTPUT] [--emit-c OUTPUT.c]
            [--backend c --c-abi [--link-library FILE]...]
            [--framework cli]
            [--target x86_64-linux|aarch64-linux] [-g]
kofun build [TARGET...] [FROST_OPTIONS]  # with ./kofun.toml
kofun new DIRECTORY --template cli
kofun run INPUT.kofun
kofun check INPUT.kofun
kofun test [PATH]
kofun bootstrap
kofun emit-c INPUT.kofun OUTPUT.c
```

## Semantic corpus

`tests/conformance/numeric/` defines the Stage 1 contract for floor division,
division by zero, Int64 boundaries, and overflow. `tests/conformance/text/`
defines the direct x86-64 contract for UTF-8 concatenation, equality,
codepoint length, `chars`, and indexing. Each active corpus executes 9/9 cases
through its registered backend; the runner compares stdout, stderr, and exit
status and reports backend coverage.

`tests/diagnostics/stage2/` is the first-class must-fail corpus. It currently
covers all 22 diagnostic codes emitted by the active Stage 2 checkpoint:
19 cases assert precise source locations and 3 explicitly track missing spans
as debt. Runtime, C ABI, native-backend, and host-I/O diagnostics remain
separate coverage work.

## Source extension

`.kofun` replaces `.kf` because `.kf` already exists as binary/data formats.
The editor grammar, CLI, tests, examples, and CI all use `.kofun`, and the
repository gate rejects legacy `.kf` source files.

## Status discipline

Documents under `docs/` include target design. A feature is active only when it
has Kofun source and an executable verification gate. The current truthful
label is **Kofun-written Python-free bootstrap seed**, not “fully self-hosting
compiler.”

Licensed under Apache-2.0 or MIT, at your option.
