# Kofun

![Kofun language icon: a keyhole-shaped burial mound](public/kofun-mark.svg)

Kofun is an experimental programming language with a Kofun-written,
Python-free bootstrap and direct ELF64 backends for x86-64 and AArch64. Source
files use `.kofun`.

[Website](https://hjosugi.github.io/kofun/) ·
[Documentation](https://hjosugi.github.io/kofun/docs/) ·
[Playground](https://hjosugi.github.io/kofun/#playground) ·
[Releases](https://github.com/hjosugi/kofun/releases) ·
[Roadmap](https://hjosugi.github.io/kofun/docs/roadmap/)

> **Project status:** research compiler, not a production language. “Active”
> means an executable gate exists in this repository. Design documents and open
> issues are not implementation claims.

Kofun's product direction combines readable ownership (`read` / `edit` /
`take`), functional programming, native code generation, and compiler-checked
algebraic laws. General ownership and law checking remain open work.

## Quick start

The repository launcher needs no Python installation:

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

Run `./bin/kofun --help` for the current CLI. The
[getting-started documentation](https://hjosugi.github.io/kofun/docs/getting-started/)
contains the complete setup, target, and verification guide.

## What works now

| Area | Current executable boundary |
|---|---|
| Bootstrap | Kofun-written Stage 1 seed, audited C11 artifact, and Python-free reproduction |
| Self-hosting | Frozen compiler profile reaches a runnable compiler-produced compiler; the three-generation semantic fixed point is still open |
| Frontend | Stage 2 lexer, structural parser, typed checkpoints, diagnostics, modules, ADTs, and bounded generics |
| Native | Direct static ELF64 for bounded Int, function, `List[Int]`, and UTF-8 `Text` profiles on x86-64 and AArch64 |
| WebAssembly | Direct wasm32 checked-Int64 arithmetic Core and browser tour |
| Interop | Explicit C ABI profile and an audited Rust-crate shim example |
| Frameworks | Bounded HTTP, native CLI, TUI, package, build-system, and syscall/stdlib gates |
| Tooling | VS Code/TextMate support, stdio LSP, typed-sidecar contracts, and Tree-sitter parsing |
| Quality | Conformance corpora, stable-diagnostic registry, Unicode 17 gates, and deterministic semantic oracle fuzzing |

For exact claims and their gates, use the
[implemented-status matrix](https://hjosugi.github.io/kofun/docs/implemented-status/).
The active compiler is not yet a general parser/type checker, a complete
memory-safe runtime, or a semantically self-hosting fixed point.

## Common workflows

Build a direct native program:

```sh
./bin/kofun build bootstrap/fixtures/answer.kofun \
  --target x86_64-linux -o build/answer
```

Build the declarative CLI example:

```sh
./bin/kofun build examples/cli_tool.kofun \
  --framework cli -o build/kofun-tool
./build/kofun-tool greet Ada --prefix Welcome
```

Build the WebAssembly example:

```sh
./bin/kofun build examples/wasm_arithmetic.kofun \
  --target wasm32 -o build/arithmetic.wasm
node bootstrap/wasm/run.mjs build/arithmetic.wasm
```

Create a locked external C ABI package cache:

```sh
./bin/kofun package lock
./bin/kofun build app.kofun --backend c --c-abi \
  --package example --offline -o build/app
```

Each path is deliberately bounded. Follow the subsystem documentation before
using it as a compatibility or security promise.

## Repository map

| Path | Purpose |
|---|---|
| [`bootstrap/`](bootstrap/) | Stage 1/2, self-hosting, native, wasm32, and C ABI checkpoints |
| [`spec/`](spec/) | Normative contracts and executable specification examples |
| [`tests/`](tests/) | Conformance, diagnostics, fuzzing, CLI, and integration gates |
| [`framework/`](framework/) | HTTP, CLI, and terminal UI libraries |
| [`stdlib/`](stdlib/) | Kofun syscall and standard-library contracts |
| [`editor/`](editor/) | Editor integrations and structural grammar |
| [`docs/`](docs/) | Language, compiler, roadmap, security, and status documents |
| [`app/`](app/) | Official website, docs renderer, and browser playground |

## Requirements and verification

The core gates require a POSIX shell, a C11 compiler, `sha256sum`, Node.js, and
Linux x86-64 tooling. Full verification also uses Rust/Cargo, binary inspection
tools, and optionally `qemu-aarch64` to execute AArch64 output.

```sh
make verify        # every active repository gate
make diagnostics   # diagnostic registry and exact fixtures
make fuzz          # deterministic grammar and semantic fuzzing
make native        # direct ELF64 backends
make tour          # no-install browser tour
```

CI runs `make verify`. Unsupported target behavior must fail explicitly or be
reported as unsupported; it must not silently fall back.

## Documentation

Start with:

- [Implemented status](https://hjosugi.github.io/kofun/docs/implemented-status/)
- [Language vision](https://hjosugi.github.io/kofun/docs/language-vision/)
- [Compiler architecture](https://hjosugi.github.io/kofun/docs/compiler-architecture/)
- [Native backends](https://hjosugi.github.io/kofun/docs/native-backends/)
- [Self-hosting boundary](https://hjosugi.github.io/kofun/docs/self-hosting/)
- [Specification](https://hjosugi.github.io/kofun/docs/specification/)
- [Security](https://hjosugi.github.io/kofun/docs/security/)

The website renders selected Markdown files from this repository, so source and
published documentation share one authority.

## Contributing

The issue tracker distinguishes planning records from independently refinable
work. Good starting points carry both
[`curated`](https://github.com/hjosugi/kofun/issues?q=is%3Aopen+label%3Acurated)
and [`ready`](https://github.com/hjosugi/kofun/issues?q=is%3Aopen+label%3Aready).
Keep capability claims tied to executable evidence and preserve explicit
unsupported cases.

## License

Licensed under [Apache-2.0](LICENSE-APACHE) or [MIT](LICENSE-MIT), at your
option.
