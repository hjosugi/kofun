# Getting started

Kofun is an experimental research compiler. The repository launcher is the
supported entry point; it builds the checked compiler artifacts as needed and
does not require Python.

## Requirements

The common path needs:

- a POSIX shell
- a C11 compiler (`cc`, or `CC=clang`)
- `sha256sum`
- Node.js for wasm32 execution
- Linux x86-64 tools for direct native output

The complete repository gate also uses Rust/Cargo, `ar`, `ld`, `readelf`,
`file`, `ldd`, and `script`. `qemu-aarch64` is optional locally; CI installs it
and executes the AArch64 corpus.

## First program

From the repository root:

```sh
./bin/kofun --version
./bin/kofun check bootstrap/fixtures/answer.kofun
./bin/kofun run bootstrap/fixtures/answer.kofun
./bin/kofun build bootstrap/fixtures/answer.kofun -o build/answer
./build/answer
```

The checked arithmetic Core accepts integer expressions and `print`:

```kofun
# expect: 42
fn main() {
    print((6 + 1) * 6)
}
```

Source files use `.kofun`. Unsupported syntax or target behavior must be
reported explicitly; the launcher does not silently switch backends.

## Choose a checked path

Direct native x86-64:

```sh
./bin/kofun build bootstrap/fixtures/answer.kofun \
  --target x86_64-linux -o build/answer
```

Direct native AArch64:

```sh
./bin/kofun build bootstrap/fixtures/answer.kofun \
  --target aarch64-linux -o build/answer-aarch64
```

WebAssembly:

```sh
./bin/kofun build examples/wasm_arithmetic.kofun \
  --target wasm32 -o build/arithmetic.wasm
node bootstrap/wasm/run.mjs build/arithmetic.wasm
```

Declarative native CLI:

```sh
./bin/kofun build examples/cli_tool.kofun \
  --framework cli -o build/kofun-tool
./build/kofun-tool greet Ada --prefix Welcome
```

These are separate bounded profiles, not one general language surface. See the
[implemented-status matrix](MVP_IMPLEMENTED.md) before relying on a feature.

## Verify the checkout

```sh
make verify        # every active repository gate
make diagnostics   # stable diagnostic registry and exact fixtures
make fuzz          # deterministic grammar and semantic oracle fuzzing
make native        # direct x86-64 and AArch64 ELF checkpoints
make tour          # no-install browser tour
```

CI runs `make verify`. For architecture-specific limits and trust boundaries,
continue with [native backends](NATIVE_BACKEND.md), [compiler architecture](COMPILER_ARCHITECTURE.md),
and [self-hosting](../bootstrap/selfhost/README.md).
