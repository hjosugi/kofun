# Kofun

Kofun is an experimental programming language. Source files use `.kofun`.

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

Four Python-free checkpoints now exercise the path beyond that Core:

- `bootstrap/stage2/` lexes and structurally parses Kofun, emits a deterministic
  function IR, and reaches a byte-stable source/token/IR round trip;
- `bootstrap/native/` uses Kofun-authored bytes to build and execute a static
  Linux x86-64 ELF64 image without an assembler or linker; its active x86-64
  Core includes local List bindings, `map`/`filter`/`fold`, and UTF-8 Text
  operations;
- `bootstrap/c_abi/` provides an explicit host-C/dynamic-linking profile for
  checked `extern "C"` declarations and `repr(C)` structs;
- `stdlib/` defines the raw syscall ABI, value-level errno conversion, affine
  resource wrappers, and the file round-trip acceptance fixture in Kofun.

All four have executable gates. The complete Stage 2 self-recompile and
general native lowering remain open.

## Requirements

- POSIX shell
- C11 compiler (`cc`, or set `CC=clang`)
- `sha256sum`
- Linux x86-64 for the native executable gate
- `rustc` for the required Rust `cdylib` C ABI acceptance gate

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

Run every active gate:

```sh
make verify
```

## CLI

```text
kofun build INPUT.kofun [-o OUTPUT] [--emit-c OUTPUT.c]
            [--backend c --c-abi [--link-library FILE]...]
            [--target x86_64-linux|aarch64-linux] [-g]
kofun build [TARGET...] [FROST_OPTIONS]  # with ./kofun.toml
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
