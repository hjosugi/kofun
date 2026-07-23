# Kofun

Kofun is an experimental programming language. Source files use `.kofun`.

Kofun's product position is: **the language where you state an algebraic law
and the compiler hands you a counterexample.** This is product direction, not
current compiler behavior. The active CLI does not yet accept `law monad`;
bounded Monad examples and evidence in the repository are retained
historical/design artifacts for [#551](https://github.com/hjosugi/kofun/issues/551).

The active compiler seed is written in Kofun itself:

- canonical source: `bootstrap/stage1/compiler.kofun`
- audited bootstrap artifact: `bootstrap/stage1/compiler.c`
- Python-free verification: `bootstrap/stage1/check.sh`
- frozen first self-host profile: `bootstrap/selfhost/profile.tsv`

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

Eight Python-free checkpoints now exercise the path beyond that Core:

- `bootstrap/stage2/` lexes and structurally parses Kofun, emits a deterministic
  function IR, and reaches a byte-stable source/token/IR round trip;
- `bootstrap/native/` uses Kofun-authored bytes to build and execute a static
  Linux ELF64 image without an assembler or linker; its active x86-64 and
  AArch64 Cores include recursive user functions, local List bindings, and
  `map`/`filter`/`fold`; UTF-8 Text operations are currently x86-64-only;
- `bootstrap/wasm/` directly emits a standard wasm32 module for the checked
  Int64 arithmetic Core and differentially executes it in a WebAssembly
  engine;
- `bootstrap/c_abi/` provides an explicit host-C/dynamic-linking profile for
  checked `extern "C"` declarations and `repr(C)` structs;
- `framework/http/` uses that explicit host-C profile to run a reusable Linux
  epoll HTTP/1.1 library configured with routes from Kofun source;
- `framework/cli/` compiles declarative command metadata into a direct-static
  Linux x86-64 application whose help and runtime dispatch share that metadata;
- `framework/tui/` exposes bounded, Unicode-width-aware progress, table, tree,
  and log rendering to Kofun programs with terminal capability degradation;
- `stdlib/` defines the raw syscall ABI, value-level errno conversion, affine
  resource wrappers, and the file round-trip acceptance fixture in Kofun.

All eight have executable gates. The complete Stage 2 self-recompile and
general native lowering remain open.

## Measured project status

This table is the position-paper checklist tracked by
[issue #281](https://github.com/hjosugi/kofun/issues/281). `Active` means that
the linked executable gate exercises the feature. A design document alone is
never enough to move a row to `Active`.

| Requirement | Status | Evidence or blocker |
|---|---|---|
| Direct native machine code | **Active, bounded Core** | [`bootstrap/native/check.sh`](bootstrap/native/check.sh) builds and executes direct x86-64 and AArch64 ELF (AArch64 under `qemu-aarch64`) |
| Static, dependency-free binaries | **Active, bounded Core** | The [native gate](bootstrap/native/check.sh) rejects an interpreter, dynamic section, and dynamic dependencies |
| Algebraic-law counterexamples | **Design / historical evidence** | The active CLI rejects `law monad`; [`docs/LAW_SYSTEM.md`](docs/LAW_SYSTEM.md) preserves the bounded model and [#551](https://github.com/hjosugi/kofun/issues/551) tracks a concrete-first executable replacement |
| Memory safety without GC | **Design only** | [`docs/MEMORY_MODEL.md`](docs/MEMORY_MODEL.md) is target design; the complete checker and reclamation path are not implemented |
| Runtime performance parity | **Not established** | [`benchmarks/README.md`](benchmarks/README.md) limits current results to smoke and bounded HTTP measurements |
| Heap allocation | **Active, narrow; no reclamation** | [`bootstrap/native/README.md`](bootstrap/native/README.md) documents the x86-64/AArch64 `mmap` runtimes used by List and Text |
| Text and homogeneous List values | **Active, bounded target profiles** | [`tests/conformance/`](tests/conformance/) runs List and Text on x86-64/AArch64 |
| Heterogeneous records | **Missing** | [#546](https://github.com/hjosugi/kofun/issues/546) tracks structured compiler/application data, but does not block the first string-scanning C11 fixed point |
| User-defined function calls | **Active, bounded Int Core** | [`tests/conformance/functions`](tests/conformance/functions) executes arguments, results, forward/mutual recursion, and six-argument calls under both C11 and direct x86-64; the [native gate](bootstrap/native/check.sh) also runs the function Core on AArch64 under `qemu-aarch64` |
| C ABI interop | **Active, bounded host-C profile** | [`bootstrap/c_abi/check.sh`](bootstrap/c_abi/check.sh) verifies calls and `repr(C)` layout; it is separate from direct native code |
| Embedded / freestanding profile | **Missing** | The current direct backend targets Linux syscalls |
| Semantic self-hosting fixed point | **Missing** | [`docs/SELF_HOSTING.md`](docs/SELF_HOSTING.md) and the bootstrap gates keep seed, checkpoint, and fixed-point claims distinct |
| Stable language specification | **Missing** | [`spec/README.md`](spec/README.md) distinguishes normative contracts from roadmap material |
| Package ecosystem | **Missing** | Package and registry work remains a later roadmap milestone |

The P0 path now freezes the smallest compiler source profile
([#618](https://github.com/hjosugi/kofun/issues/618)), fills its frontend and
C11 coverage in [#619](https://github.com/hjosugi/kofun/issues/619) through
[#622](https://github.com/hjosugi/kofun/issues/622), then produces and compares
three compiler generations in [#271](https://github.com/hjosugi/kofun/issues/271)
and [#272](https://github.com/hjosugi/kofun/issues/272). Direct-native and
AArch64 parity continue in parallel without blocking that first fixed point.

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
- optional `qemu-aarch64` to execute the AArch64 native gate; without it the
  AArch64 images are still built and audited, and execution is explicitly
  skipped
- `rustc` for the required Rust `cdylib` C ABI acceptance gate
- `cargo` for the required offline vendored-crate shim gate
- `ar` for the required HTTP framework static-library gate
- `ld`, `readelf`, `file`, `ldd`, and `script` for native CLI prefix auditing
- `node` for the required wasm32 engine and differential gate

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

Build the shared terminal UI sample through the host-C profile:

```sh
framework/tui/build.sh examples/tui_dashboard.kofun build/tui-dashboard
./build/tui-dashboard
```

See `framework/tui/README.md` for Unicode width, component, resize, capability,
and frame-budget contracts.

Build the WebAssembly arithmetic sample:

```sh
./bin/kofun build examples/wasm_arithmetic.kofun \
  --target wasm32 -o build/arithmetic.wasm
node bootstrap/wasm/run.mjs build/arithmetic.wasm
```

Or build the Kofun-authored, viewport-lazy browser sample:

```sh
sh examples/wasm-browser/build.sh
node examples/wasm-browser/serve.mjs build/wasm-browser
```

See `bootstrap/wasm/README.md` for the host imports, browser URL, executable
checks, and exact arithmetic Core boundary.

Or open the no-install interactive learning tour. Every step is editable and
runnable in the page, and the source can be shared in a URL fragment:

```sh
node examples/wasm-browser/serve.mjs docs/tour
# open http://127.0.0.1:8080/
```

The browser compiler is byte-checked against the current bounded wasm32 seed.
See `docs/tour/README.md` for its exact limits and the Python, TypeScript, Go,
and Rust coming-from guides.

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

For direct external C ABI artifacts, `kofun package lock` records an exact
SHA-256 and `--package NAME --offline` builds from the shared content-addressed
cache. See `package/README.md` for the intentionally registry-free v1 scope.

Run every active gate:

```sh
make verify
```

Run the compiler-quality gates independently:

```sh
make diagnostics
make fuzz
make unicode
```

The Stage 2 must-fail corpus pins each active diagnostic code, exact message,
and declared source span. Its paired `.stderr` files can be regenerated with
`sh tests/diagnostics/stage2/bless.sh`. The deterministic fuzz smoke tests
exercise 128 arbitrary token streams with a watchdog and compare 48 generated
arithmetic programs across an independent expected value, C11, and direct
x86-64 output. A further 32 generated Int-valued `if` programs prove that
Stage 2 evaluates only the selected branch, and 32 guarded Bool matches prove
source-order fallthrough and selected-only guard evaluation. Another 32
Int-valued Bool matches cover nested value control and selected-only arm
evaluation, while 32 invalid value matches exercise four stable diagnostic
codes without emitting C. See
`tests/diagnostics/stage2/README.md` and
`tests/fuzz/README.md` for the exact scope and remaining coverage boundaries.

## CLI

```text
kofun build INPUT.kofun [-o OUTPUT] [--emit-c OUTPUT.c]
            [--backend c --c-abi [--link-library FILE]...]
            [--framework cli]
            [--target x86_64-linux|aarch64-linux|wasm32] [-g]
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
defines the direct x86-64 contract for UTF-8 concatenation, equality, extended
grapheme-cluster length, `chars`, indexing, and explicit byte/codepoint views.
It covers Arabic, Hebrew, Hindi, Thai, Japanese, Hangul/Jamo, and complex emoji.
The runner compares stdout, stderr, and exit status and reports backend
coverage.

`tests/diagnostics/stage2/` is the first-class parser/lowerer/ownership
must-fail corpus. It covers all 33 codes in that Stage 2 inventory:
30 cases assert precise source locations and 3 explicitly track missing spans
as debt. The localized `EUNICODE` source-validation diagnostics have their own
coverage in `tests/unicode/`. Runtime, C ABI, native-backend, and host-I/O
diagnostics remain separate coverage work.

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
