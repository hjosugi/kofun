# wasm32 arithmetic and bounded Int function Core

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

The current profile supports a zero-argument `fn main`, immutable `Int`
bindings, `print`, Int64 literals and variables, parentheses, unary `+`/`-`,
and checked `+`, `-`, `*`, `//`, and `%`. `/` is not defined on `Int` and is
refused with a diagnostic (#687), exactly as both native targets refuse it.
Modulo and the integer quotient follow the same floor semantics and stable
runtime diagnostics as the C11 Stage 1 backend. Parenthesized expressions,
unary operators, and call argument lists share one deterministic 256-level
nesting limit: 128 nested parentheses combined with 128 unary operators are
accepted, while any combined depth of 257 is rejected. This makes hostile
inputs fail with a compile diagnostic instead of exhausting the C stack.

On top of that arithmetic the profile lowers a bounded Int function Core:
zero to six `Int` parameters and an `Int` result, direct calls in expression
position, forward calls, recursion and mutual recursion, comparison-guarded
early returns with `==`, `!=`, `<`, `<=`, `>`, and `>=`, and `let` bindings
inside a function body. Every top-level signature is collected before any body
is lowered, so a call resolves the same whether its callee is declared above or
below it. A call evaluates its arguments left to right and exactly once. This
is ordinary direct-call recursion — no tail-call proposal instruction, host
callback, or JavaScript trampoline participates — so recursion depth is
whatever the engine stack allows and the language makes no promise about it.

### Encoding

Module function indices start after the two host imports, and the declaration
table fixes them before any body is emitted. The module keeps exporting
`main(): void`, so hosts need no ABI change: when the source `main` declares no
result it *is* the export, and a program that is one `fn main` still emits the
same bytes it did before functions existed. Only when `main` declares
`-> Int` does the module gain a generated wrapper that calls the internal
`main` and drops its `i64`. Parameters occupy WebAssembly locals in source
order; expression temporaries follow them deterministically. A Kofun `Bool`
is never a value here — a comparison exists only as the `i32` branch condition
of one `if`.

### Refused, with no artifact written

Unknown functions, duplicate declarations, duplicate parameters, wrong call
arity, a seventh parameter or argument, non-`Int` parameters, a helper without
an `-> Int` result, an `-> Int` body that does not end in `return`, function
values, and calls through a binding are all compile failures. Each writes a
stable source-located diagnostic to stderr, nothing to stdout, and no `.wasm`
file. `bootstrap/wasm/fixtures/` holds one fixture per refusal and
`check.sh` asserts every one of them.

`tests/conformance/backends/wasm32-node.sh` registers the target against the
shared numeric and functions corpora. All nine numeric cases and all twelve
function cases execute under the Node WebAssembly engine and compare exact exit
status, stdout, and stderr with the C11 observations.

Run the mandatory gate:

```sh
make wasm
```

## Honest boundary

This remains a bounded Int target. It has no linear-memory object layout, no
Text or List lowering, no `else`, no loops, no mutation, no tables or indirect
calls, no closures or function values, no user-declared imports, no WASI
profile, no general JavaScript value conversion, no direct DOM declarations in
Kofun, no debug information, and no optimizer. Functions are bounded at six
`Int` parameters and one `Int` result; anything wider is refused rather than
silently narrowed. The standard module loads in both Node and browsers, the
numeric and function differential corpora are executable, and the sample
renders Kofun output in a page. Wider language coverage should be tracked
independently rather than implied here.

When linear-memory objects do arrive, their byte layout is already decided:
`spec/aggregate-layout-v1.md` defines the `wasm32` target with 4-byte
references and 8-byte `u64` object headers, with golden vectors in
`spec/aggregate-layout-v1/examples/core.wasm32.json`. That contract exists
specifically so this target is not given 64-bit references to match the
native backend's bytes; `Int` stays signed 64-bit here as everywhere.
