# wasm32 arithmetic and bounded Int function Core

This directory is an executable first slice of issue #26. The seed compiler
parses Kofun source and writes a standard WebAssembly binary module directly;
it does not invoke Clang, LLVM, a C backend, or a text-to-Wasm assembler.

`--target wasm32` names an architecture *and* a host binding, and the name says
only the first half: it selects the bounded numeric binding described below —
`main(): void` against `kofun.print_i64` and `kofun.panic` — and it will keep
selecting it. The accepted `kofun-wasm-host-abi-v1` aggregate binding is a
different target name, `wasm32-hostabi1`. That profile now emits its checked
object arena, but still refuses every non-empty program until the Text and List
lowering slices land.
`spec/wasm-host-profile-v1.md` decides that split and
`sh spec/wasm-host-profile-v1/check.sh` holds this target to it.

## Aggregate arena profile

The first executable aggregate slice builds an empty entry point:

```sh
./bin/kofun build bootstrap/wasm/fixtures/hostabi1_empty.kofun \
  --target wasm32-hostabi1 -o build/hostabi1-empty.wasm
node spec/wasm-host-abi-v1/hostabi.mjs module \
  build/hostabi1-empty.wasm
```

The module exports one fixed 64-KiB linear memory, immutable
`kofun_abi_version = 1`, `kofun_start(i32)`, and
`kofun_alloc(i32, i32) -> i32`. The arena begins at offset 1024, preserving
zero as the failure/null reference and the lower region for compiler-owned
objects. Allocation is a deterministic bump: positive power-of-two alignment
is checked before rounding, size/address arithmetic is bounded before the
cursor changes, and capacity exhaustion returns zero. Memory never grows and
an allocation never writes or publishes an object by itself. A later lowering
that requires an allocation is responsible for calling the v1 `abort(2,
detail)` import when the zero result cannot be handled.

`object_arena.h` contains the single little-endian u64 header encoder for both
`Text.byte_length` and `List.length`. `object_arena_check.sh` compares it with
every header in the recomputed wasm32 boundary vectors, exercises invalid and
exhausting allocations under a real engine, and proves failures leave the
cursor and memory unchanged. This slice lowers no Text or List value and makes
no conformance capability claim.

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
task wasm
```

## Honest boundary

The legacy `wasm32` binding remains a bounded Int target and its module bytes
are unchanged. The separate `wasm32-hostabi1` binding has the checked
linear-memory arena described above, but no Text or List lowering. Neither
profile has `else`, loops, mutation, tables or indirect
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

The host boundary those objects will cross is decided too:
`spec/wasm-host-abi-v1.md` pins `kofun-wasm-host-abi-v1` — the import
allowlist with exact wasm signatures, the required exports, the `Text` and
`List` representations recomputed from the layout target, and the rule that
a host may not retain a guest pointer past the call it was passed to. That is
a different, later host binding than the two imports above: this target still
exports `main(): void` and imports `kofun.print_i64` and `kofun.panic`, and
nothing in that contract changes what this directory emits today.

How a build reaches it is decided too, and is no longer left to the reader:
`spec/wasm-host-profile-v1.md` puts the host ABI in the target name. The
aggregate profile is `--target wasm32-hostabi1`; bare `--target wasm32` stays
on the numeric binding and is not deprecated, so nothing here has to migrate.
A toolchain predating the arena slice refuses the profile name with
`kofun: unsupported target:` and writes no module. Current builds emit the
arena-only module and refuse non-empty source until its value slices land. The
two bindings are told apart on the module bytes before
anything is instantiated — this one imports from `kofun` and exports one
function, `main`; every import a v1 module has comes from
`kofun:host-abi-v1`, while its immutable `kofun_abi_version` global identifies
even the arena-only module that needs no import yet. A module is never both.
