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

Build the Kofun-authored browser sample and serve it without package
installation:

```sh
sh examples/wasm-browser/build.sh
node examples/wasm-browser/serve.mjs build/wasm-browser
# open http://127.0.0.1:8080/
```

`app.kofun` is the program. It is compiled directly to `app.wasm`; `main.mjs`
is only the generic browser host that maps `print_i64` to text content and
checked traps to diagnostics. The host waits for the sample element to enter
the viewport before it fetches or instantiates the module. Browsers without
`IntersectionObserver` load it immediately.

The module exports `main(): void` and imports two host functions:

- `kofun.print_i64(value: i64)` observes each Kofun `print`;
- `kofun.panic(code: i32)` reports a checked arithmetic failure.

These imports are ordinary WebAssembly host bindings. `run.mjs` is the
non-skipping Node host used by the differential gate. The browser sample
supplies the same bindings and renders the two Kofun-produced values into an
`aria-live` output element.

## Supported source slice

The current profile supports one zero-argument `fn main`, immutable `Int`
bindings, `print`, Int64 literals and variables, parentheses, unary `+`/`-`,
and checked `+`, `-`, `*`, `/`, `//`, and `%`. Division and modulo follow the
same truncating or floor semantics and stable runtime diagnostics as the C11
Stage 1 backend. Parenthesized expressions and unary operators share one
deterministic 256-level nesting limit: 128 nested parentheses combined with
128 unary operators are accepted, while any combined depth of 257 is rejected.
This makes hostile inputs fail with a compile diagnostic instead of exhausting
the C stack.

`tests/conformance/backends/wasm32-node.sh` registers the target against the
shared numeric corpus. All nine success and failure cases execute under the
Node WebAssembly engine and compare exact exit status, stdout, and stderr with
the C11 observations.

Run the mandatory gate:

```sh
make wasm
```

## Honest boundary

This remains a bounded arithmetic target. It has no linear-memory object
layout, Text or List lowering, general functions, WASI profile, general
JavaScript value conversion, direct DOM declarations in Kofun, or optimizer.
The standard module loads in both Node and browsers, the numeric differential
corpus is executable, and the sample now renders Kofun output in a page. Wider
language coverage should be tracked independently rather than implied here.
