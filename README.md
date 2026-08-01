# Kofun

![Kofun language icon: a keyhole-shaped burial mound](docs/kofun-mark.svg)

Kofun is an experimental programming language with a Kofun-written bootstrap
and direct ELF64 backends for x86-64 and AArch64. Source files use `.kofun`.

[Website](https://hjosugi.github.io/kofun/) ·
[Documentation](https://hjosugi.github.io/kofun/docs/) ·
[Playground](https://hjosugi.github.io/kofun/#playground) ·
[GitHub Project](https://github.com/users/hjosugi/projects/3) ·
[Releases](https://github.com/hjosugi/kofun/releases) ·
[Roadmap](https://hjosugi.github.io/kofun/docs/roadmap/)

> **Project status:** research compiler, not a production language. “Active”
> means an executable gate exists in this repository. Design documents and open
> issues are not implementation claims.

Kofun's distinguishing product direction is: **the language where you state an
algebraic law and the compiler hands you a counterexample.** This is product
direction, not current compiler behavior; general ownership and law checking
remain open work.

## Quick start

Use the repository launcher for the checked compiler paths:

```sh
./bin/kofun --version
./bin/kofun check bootstrap/fixtures/answer.kofun
mkdir -p build
./bin/kofun check bootstrap/fixtures/answer.kofun \
  --emit-typed-sidecar build/answer.kofun-semantic.json --generation 1
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
| Bootstrap | Kofun-written Stage 1 nested-block and looping Int/Bool/Text/List[Text] seed, audited C11 artifact, and reproducible generation |
| Self-hosting | Frozen compiler profile reaches a runnable compiler-produced compiler; the three-generation semantic fixed point is still open |
| Frontend | Stage 2 lexer/parser plus focused typed, diagnostic, same-package module-alias/public-re-export, ADT, and bounded-generic checkpoints |
| Native | Direct static ELF64 for bounded Int, function, `List[Int]`, and UTF-8 `Text` profiles on x86-64 and AArch64 |
| WebAssembly | Direct wasm32 checked-Int64 arithmetic Core and browser tour |
| Interop | Explicit C ABI profile and an audited Rust-crate shim example |
| Frameworks | Bounded HTTP, native CLI, TUI, package, build-system, and syscall/stdlib gates |
| Tooling | VS Code/TextMate support, stdio LSP, explicit Stage 2 typed-sidecar emission, and Tree-sitter parsing |
| Quality | Conformance corpora, stable-diagnostic registry, Unicode 17 gates, and deterministic semantic oracle fuzzing |

For exact claims and their gates, use the
[implemented-status matrix](https://hjosugi.github.io/kofun/docs/implemented-status/).
Every row there carries a stable claim id from
[`release/claims.json`](release/claims.json), which names the gate that proves
it, the boundary that fails outside it, and the command that reproduces both.
`task release-claims` fails when published wording and executable evidence
disagree.
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
| [`docs/`](docs/) | Language, compiler, roadmap, security, and status documents |

The official site, its docs renderer, the browser playground, and the delivery
planning snapshots live in
[`hjosugi/kofun-site`](https://github.com/hjosugi/kofun-site), which reads this
repository as a submodule. This repository carries no npm, Next.js, or
Cloudflare toolchain.

## Requirements and verification

The core gates require [go-task](https://taskfile.dev), a POSIX shell, a C11
compiler, `sha256sum`, Node.js, and Linux x86-64 tooling. `task` is only the
entry point — every gate is a shell script, and `Taskfile.yml` is where to read
the exact command a gate name stands for, so a gate can always be run directly.
Full verification also uses Rust/Cargo, binary inspection tools, and optionally
`qemu-aarch64` to execute AArch64 output.

```sh
task               # grouped contributor guide
task --list        # complete flat task inventory
task verify        # every active repository gate
task diagnostics   # diagnostic registry and exact fixtures
task fuzz          # deterministic grammar and semantic fuzzing
task native        # direct ELF64 backends
task tour          # no-install browser tour
```

CI runs `task verify`. Unsupported target behavior must fail explicitly or be
reported as unsupported; it must not silently fall back.

## Documentation

Start with:

- [Getting started](https://hjosugi.github.io/kofun/docs/getting-started/)
- [Repository guide](https://hjosugi.github.io/kofun/docs/repository-guide/)
- [Contributing](https://hjosugi.github.io/kofun/docs/contributing/)
- [Implemented status](https://hjosugi.github.io/kofun/docs/implemented-status/)
- [Language vision](https://hjosugi.github.io/kofun/docs/language-vision/)
- [Compiler architecture](https://hjosugi.github.io/kofun/docs/compiler-architecture/)
- [Native backends](https://hjosugi.github.io/kofun/docs/native-backends/)
- [Self-hosting boundary](https://hjosugi.github.io/kofun/docs/self-hosting/)
- [Specification](https://hjosugi.github.io/kofun/docs/specification/)
- [Security](https://hjosugi.github.io/kofun/docs/security/)
- [Release evidence](https://hjosugi.github.io/kofun/docs/release-evidence/)

The website renders selected Markdown files from this repository, so source and
published documentation share one authority. The renderer itself lives in
[`hjosugi/kofun-site`](https://github.com/hjosugi/kofun-site); the documents it
names stay here, under `docs/`.

## Contributing

New contributors should follow the
[setup guide](https://hjosugi.github.io/kofun/docs/getting-started/), use the
[repository map](https://hjosugi.github.io/kofun/docs/repository-guide/) to
find the owning source and gate, and read the full
[contribution workflow](https://hjosugi.github.io/kofun/docs/contributing/).
The issue tracker distinguishes planning records from independently refinable
work. Good starting points carry both
[`curated`](https://github.com/hjosugi/kofun/issues?q=is%3Aopen+label%3Acurated)
and [`ready`](https://github.com/hjosugi/kofun/issues?q=is%3Aopen+label%3Aready).
Keep capability claims tied to executable evidence and preserve explicit
unsupported cases.

## License

Licensed under [Apache-2.0](LICENSE-APACHE) or [MIT](LICENSE-MIT), at your
option.
