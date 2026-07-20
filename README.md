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

Three Python-free checkpoints now exercise the path beyond that Core:

- `bootstrap/stage2/` lexes and structurally parses Kofun, emits a deterministic
  function IR, and reaches a byte-stable source/token/IR round trip;
- `bootstrap/native/` uses Kofun-authored bytes to build and execute a static
  Linux x86-64 ELF64 image without an assembler or linker;
- `stdlib/` defines the raw syscall ABI, value-level errno conversion, affine
  resource wrappers, and the file round-trip acceptance fixture in Kofun.

All three have executable gates. The complete Stage 2 self-recompile and
general native lowering remain open.

## Requirements

- POSIX shell
- C11 compiler (`cc`, or set `CC=clang`)
- `sha256sum`
- Linux x86-64 for the native executable gate

Python is not required or used.

## Quick start

```sh
./bin/kofun --version
./bin/kofun check bootstrap/fixtures/answer.kofun
./bin/kofun run bootstrap/fixtures/answer.kofun
./bin/kofun build bootstrap/fixtures/answer.kofun -o build/answer
./build/answer
```

Generated C11 can be inspected with:

```sh
./bin/kofun emit-c bootstrap/fixtures/answer.kofun build/answer.c
```

Run every active gate:

```sh
make verify
```

## CLI

```text
kofun build INPUT.kofun [-o OUTPUT] [--emit-c OUTPUT.c]
kofun run INPUT.kofun
kofun check INPUT.kofun
kofun test [PATH]
kofun bootstrap
kofun emit-c INPUT.kofun OUTPUT.c
```

## Semantic corpus

`tests/conformance/numeric/` defines the backend contract for floor division,
division by zero, Int64 boundaries, and overflow. All nine active cases execute
through Stage 1; the runner compares stdout, stderr, and exit status and reports
backend coverage.

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
