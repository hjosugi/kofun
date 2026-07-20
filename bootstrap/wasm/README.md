# wasm32 arithmetic Core

This directory is an executable first slice of issue #26. The seed compiler
parses Kofun source and writes a standard WebAssembly binary module directly;
it does not invoke Clang, LLVM, a C backend, or a text-to-Wasm assembler.

Build and run the sample:

```sh
./bin/kofun build examples/wasm_arithmetic.kofun \
  --target wasm32 -o build/arithmetic.wasm
node bootstrap/wasm/run.mjs build/arithmetic.wasm
```

The module exports `main(): void` and imports two host functions:

- `kofun.print_i64(value: i64)` observes each Kofun `print`;
- `kofun.panic(code: i32)` reports a checked arithmetic failure.

These imports are ordinary WebAssembly host bindings. `run.mjs` is the
non-skipping Node host used by the executable gate; a browser can instantiate
the same module by supplying equivalent functions.

## Supported source slice

The current profile supports one zero-argument `fn main`, immutable `Int`
bindings, `print`, Int64 literals and variables, parentheses, unary `+`/`-`,
and checked `+`, `-`, `*`, `/`, `//`, and `%`. Division and modulo follow the
same truncating or floor semantics and stable runtime diagnostics as the C11
Stage 1 backend.

`tests/conformance/backends/wasm32-node.sh` registers the target against the
shared numeric corpus. All nine success and failure cases execute under the
Node WebAssembly engine and compare exact exit status, stdout, and stderr with
the C11 observations.

Run the mandatory gate:

```sh
make wasm
```

## Honest boundary

This is not the complete issue #26 target. It has no linear-memory object
layout, Text or List lowering, general functions, WASI profile, JavaScript
value conversion, DOM API, browser UI sample, or optimizer. The module format
and CLI target are real, but issue #26 remains open until the general
differential suite and a browser-rendered sample are implemented.
