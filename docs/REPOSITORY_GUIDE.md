# Repository guide

This guide is the map from a development task to the files and verification
gates that own it. Read it after [Getting started](GETTING_STARTED.md) and keep
it open during your first changes.

## The five ideas that make the repository understandable

1. **The repository is the toolchain.** `./bin/kofun` builds checked local
   compiler artifacts on demand; `build/` is disposable output.
2. **Kofun has multiple bounded compiler paths.** Stage 1, Stage 2, direct
   native, wasm32, C ABI, and framework paths do not expose one identical
   language surface.
3. **Executable evidence is the implementation boundary.** A design document
   or open issue is not an implementation claim. Active claims name a fixture
   and gate.
4. **Canonical sources and checked artifacts can coexist.** For example,
   Kofun-authored compiler source and an audited C seed are both committed.
   The nearest README and `check.sh` define their relationship.
5. **Specifications, implementation, and explanation are separate.**
   `spec/` defines normative contracts, implementation lives primarily under
   `bootstrap/`, and `docs/` explains design and project state.

## Request flow: from command to evidence

```text
./bin/kofun
    |
    +-- check/run/build (host C path)
    |      +-- Stage 2 bounded C11 Core
    |      `-- explicit Stage 1 compatibility path for unsupported lowering
    |
    +-- build --target x86_64-linux|aarch64-linux
    |      `-- direct native ELF64 compilers
    |
    +-- build --target wasm32
    |      `-- direct wasm32 arithmetic Core
    |
    +-- build --backend c --c-abi
    |      `-- explicit foreign-code and host-linker boundary
    |
    +-- build --framework cli
    |      `-- declarative native CLI compiler/runtime
    |
    `-- package / project build
           +-- locked external native artifacts
           `-- optional Frost project engine

Each path -> focused check.sh or test runner -> Makefile target -> make verify
```

The launcher source is readable shell. When command routing is surprising,
start at [`bin/kofun`](../bin/kofun), find the public subcommand, and follow the
`ensure_*_compiler` or build function it invokes.

## Top-level directory map

| Path | What it owns | Start here | Typical gate |
|---|---|---|---|
| `bin/` | repository-local public launcher | `bin/kofun` | `make test` |
| `bootstrap/` | compiler seeds, frontends, direct backends, self-host evidence | `bootstrap/README.md` | `make bootstrap`, `make stage2`, `make native` |
| `spec/` | normative language and tooling contracts plus executable examples | `spec/README.md` | the matching `make *-spec` target |
| `tests/` | public behavior, conformance, diagnostics, fuzzing, tooling, integration | nearest runner or README | `make test`, `make diagnostics`, `make fuzz` |
| `stdlib/` | Kofun-authored standard-library contracts and focused projections | `stdlib/README.md` | `make stdlib` |
| `framework/` | bounded HTTP, CLI, and terminal UI surfaces | subsystem README | `make http`, `make cli-framework`, `make tui-framework` |
| `tooling/` | language server and typed-sidecar codec/projector | subsystem README | `make lsp`, `make typed-sidecar-codec` |
| `editor/` | VS Code metadata/LSP bundle and Tree-sitter grammar | subsystem README | `make tree-sitter`, `make lsp` |
| `unicode/` | Unicode tables, generator, provenance, and C boundary | `unicode/README.md` | `make unicode` |
| `vendor/` | reviewed third-party source copied into the tree | `vendor/*/README.kofun.md` | integrity owner named by subsystem |
| `package/` | locked external native-artifact package manager | `package/README.md` | `make packages` |
| `examples/` | user-facing and interoperability examples | example plus its nearest check | relevant build or `check.sh` |
| `docs/` | authored guides, designs, status, and browser-tour source | this guide | `npm run test:docs` |
| `app/` | Next.js project site, docs renderer, roadmap, playground | `site/README.md` | `npm run verify:site` |
| `site/` | site build, synchronization, and static-export scripts/tests | `site/README.md` | `npm run verify:pages` |
| `public/` | static site assets; generated tour output is ignored | `public/kofun-mark.svg` | site verification |
| `backlog/` | historical/planning inventory, not active implementation | `backlog/README.md` | `scripts/verify_backlog.kofun` owner |
| `benchmarks/` | reproducible benchmark programs, harnesses, and recorded results | `benchmarks/README.md` | subsystem benchmark script |
| `artifacts/` | checked evidence summaries and cost/law artifacts | inspect producer named in the artifact | producer-specific gate |
| `scripts/` | repository policy verification written in Kofun | script source | caller in repository gates |
| `.github/workflows/` | CI, docs publication, and project-roadmap automation | workflow YAML | GitHub Actions |

Root files are also part of the architecture:

| File | Purpose |
|---|---|
| `Makefile` | contributor-facing index of executable gates |
| `README.md` | concise public project entrypoint |
| `DESIGN.md` | early high-level language design context |
| `package.json` / `package-lock.json` | locked official-site toolchain |
| `next.config.ts` | Next.js static-export and base-path behavior |
| `wrangler.jsonc` / `open-next.config.ts` | Cloudflare-compatible site build configuration |
| `LICENSE-*` / `NOTICE` | dual-license and attribution terms |

## `bootstrap/`: the compiler is several checkpoints

### `bootstrap/stage1/`

Stage 1 is the Python-free bootstrap seed.

- `compiler.kofun` is the canonical Kofun source.
- `compiler.c` is the checked-in audited C11 seed.
- `SHA256SUMS` pins the source/artifact relationship.
- `check.sh` builds the seed and verifies its bounded nested-block Int/Bool/Text
  Core.

Do not edit only a digest to make a gate pass. A Stage 1 change must explain
which source is canonical, how the audited artifact was produced, and why its
fixtures still establish the claimed behavior.

### `bootstrap/stage2/`

Stage 2 contains the broadest collection of semantic frontend checkpoints:

- canonical `compiler.kofun` plus audited `compiler.c`;
- the transactional lexer/parser, scopes, typing slices, diagnostics, and
  bounded C11 lowering;
- focused ADT, generic, module, import, visibility, re-export, KIF, and
  incremental helpers;
- semantic-event producer and typed-tooling boundary;
- fixtures, exact stdout/stderr, and `SHA256SUMS`; and
- `check.sh`, which compiles and compares the expected artifacts.

Not every focused helper is routed through ordinary `./bin/kofun` commands.
The detailed [`bootstrap/stage2/README.md`](../bootstrap/stage2/README.md)
states whether a capability is user-facing, typed-only, reference lowering, or
tooling projection. Preserve those qualifiers in code, tests, docs, and release
notes.

### `bootstrap/native/`

This owns direct static ELF64 output for x86-64 and AArch64 bounded profiles.
It includes Kofun encoder sources, checked C compiler artifacts, fixture
emitters, and binary/runtime checks. Start with
[`bootstrap/native/README.md`](../bootstrap/native/README.md) and
[Native backends](NATIVE_BACKEND.md).

### `bootstrap/wasm/`

This owns direct wasm32 output and the Node runner for the checked arithmetic
Core. The browser example under `examples/wasm-browser/` consumes that output
but is a separate integration surface.

### `bootstrap/c_abi/`

This is the explicit host-C, libc, archive, and dynamic-linker boundary. It is
intentionally separate from direct static native output. Read its security and
ownership limitations before adding an external library path.

### `bootstrap/selfhost/`

This contains the frozen source profile, frontend and C11 evidence, driver,
native corpus, and fixed-point checks for compiler-produced compiler artifacts.
“Compiler source is written in Kofun” and “semantic self-hosting fixed point”
are different claims. The latter remains governed by the explicit generation
and artifact-equivalence gates in the self-host documentation.

### `bootstrap/fixtures/`

Small canonical inputs used across launcher and compiler smoke tests live here.
Prefer the nearest specialized corpus for a feature regression; keep these
fixtures minimal because many unrelated gates depend on them.

## `spec/`, `docs/`, and executable status

These directories answer different questions:

| Source | Question it answers | Authority |
|---|---|---|
| `spec/` | “What is the accepted normative contract?” | normative draft plus named executable examples |
| `docs/MVP_IMPLEMENTED.md` | “What can the checked repository execute now?” | concise status matrix tied to gates |
| other `docs/*.md` | “Why is this designed this way, and how do I use or develop it?” | explanatory; may include planned behavior |
| issues and `docs/ROADMAP.md` | “What outcome is planned next?” | planning only |

When you implement a feature described in `docs/`, do not simply remove every
future-tense qualifier. First add the executable evidence, then update the
implemented-status row and the relevant design text to name the exact boundary.

## `tests/`: choose the corpus that matches the contract

### Public CLI and integration

Top-level shell runners such as `tests/cli.sh`, `tests/build_system.sh`, and
`tests/package_manager.sh` exercise public commands and cross-component
behavior. Use them when changing `bin/kofun`, routing, artifact handling, or
exit statuses.

### Conformance

`tests/conformance/` groups accepted semantics by capability:

- backend adapters and normalized cases;
- functions, numeric operations, Lists, and Text;
- modules, imports, visibility, and re-exports;
- ADTs, patterns, generics, and incremental behavior; and
- syntax milestone corpora.

Conformance should observe language behavior, not internal implementation
details, unless the contract is specifically an artifact schema.

### Diagnostics

`tests/diagnostics/registry.tsv` is the canonical active diagnostic registry.
Family runners own status, stdout/stderr channel, spans, artifact policy, and
exact fixtures. Read `tests/diagnostics/README.md` before changing a public
diagnostic or using the bless workflow.

### Fuzzing

`tests/fuzz/` contains deterministic bounded generators and independent
semantic oracles. It is CI evidence, not an invitation to accept flaky random
output. A failure must retain enough seed, source, tool identity, and raw
observation data to replay exactly.

### Tooling and Unicode

`tests/lsp/`, `tests/typed-sidecar/`, and `tests/unicode/` own their respective
protocol, authority, replacement, position-encoding, security, and data
integrity boundaries.

## Standard library and frameworks

Many `stdlib/` modules specify the intended Kofun API while their current gate
executes a smaller honest projection through available backends. Read each
module README before claiming its full ADT or runtime surface is connected to
ordinary Stage 2 code generation.

The same discipline applies to frameworks:

- `framework/http/` owns the bounded HTTP/API surface and C runtime adapter;
- `framework/cli/` owns declarative CLI source, native compiler, runtime
  template, tutorial, and security boundary; and
- `framework/tui/` owns the shared terminal UI C library and behavior tests.

Examples show how to use these surfaces; their subsystem gates establish what
is currently supported.

## Editor and tooling paths

There are three related but distinct developer-tool surfaces:

1. `tooling/lsp/` is the dependency-free stdio language server.
2. `editor/vscode/` bundles language metadata, TextMate highlighting, and a
   copy of the LSP entrypoint for extension development.
3. `editor/tree-sitter-kofun/` is a structural grammar with generated parser
   sources and editor queries.

Changing syntax can require updates in all three, but they do not share one
parser or one semantic authority. Run both `make lsp` and `make tree-sitter`
when the user-visible edit experience changes.

## Official site and documentation pipeline

The official site has a deliberately simple authority chain:

```text
docs/*.md or selected subsystem README
            |
            v
app/docs/docs-manifest.ts
            |
            v
app/docs/[slug]/page.tsx + ReactMarkdown
            |
            v
Next.js build / static export
            |
            v
GitHub Pages
```

- `app/docs/docs-manifest.ts` is the curated list of first-class docs pages.
- `app/docs/docs-nav.tsx` groups those pages in the sidebar.
- `app/docs/page.tsx` is the docs landing and new-contributor route.
- `app/docs/[slug]/page.tsx` reads Markdown during the build and rewrites local
  links to another rendered document or the matching GitHub source.
- `site/docs-links.test.mjs` proves every curated source and local link exists.
- `site/build-pages.mjs` creates the GitHub Pages export under ignored `out/`.
- `.github/workflows/docs-hourly.yml` synchronizes status snapshots and
  publishes the checked `gh-pages` tree.

Generated directories are ignored:

- `node_modules/` — installed JavaScript dependencies;
- `.next/` — Next.js development and build state;
- `out/` — static GitHub Pages export;
- `.open-next/` — Cloudflare-compatible worker build;
- `public/tour/` — copy of authored `docs/tour/`; and
- `build/` / `.kofun/` — compiler and project output.

Never make a source fix only inside one of these directories.

## Find the owner for a change

| You want to change… | Start in… | Read next… | Run first… |
|---|---|---|---|
| public CLI routing or exit behavior | `bin/kofun` | `tests/cli.sh` | `make test` |
| Stage 1 Int/Bool/Text Core | `bootstrap/stage1/` | its README and `check.sh` | `make bootstrap` |
| Stage 2 syntax, typing, or C lowering | `bootstrap/stage2/` | its README and matching fixture | `make stage2` |
| a stable error code/message/span | emitter plus `tests/diagnostics/` | diagnostics README/registry | `make diagnostics` |
| x86-64 or AArch64 direct output | `bootstrap/native/` | native README and docs | `make native` |
| wasm32 arithmetic output | `bootstrap/wasm/` | wasm README | `make wasm` |
| C or Rust interoperability | `bootstrap/c_abi/`, `examples/rust-shim/` | security/third-party docs | `make c-abi` or `make rust-shim` |
| a standard-library contract | matching `stdlib/<name>/` | module README | its `tests/verify.sh` |
| HTTP, CLI, or TUI framework | matching `framework/<name>/` | subsystem README | matching Make target |
| LSP behavior | `tooling/lsp/`, `tests/lsp/` | LSP README | `make lsp` |
| structural editor parsing | `editor/tree-sitter-kofun/` | package README | `make tree-sitter` |
| VS Code packaging or metadata | `editor/vscode/` | extension README | `make lsp` plus extension check |
| language contract | `spec/` | spec index and conformance owner | matching spec/conformance gate |
| explanatory docs | `docs/` | `site/README.md` | `npm run test:docs` |
| docs UI or playground | `app/` | `site/README.md` | `npm run verify:site` |
| delivery/status snapshots | `site/sync-*.mjs` | workflow and tests | matching `npm run check:*` |

## What to read on your first day

Choose the shortest path for your work:

- **Compiler contributor:** [Compiler architecture](COMPILER_ARCHITECTURE.md),
  `bootstrap/README.md`, the relevant stage README, then its `check.sh`.
- **Language designer:** [Implemented status](MVP_IMPLEMENTED.md), the relevant
  `spec/` contract, then [Syntax](SYNTAX.md) or
  [Type system](TYPE_SYSTEM.md).
- **Tooling contributor:** [Developer discovery](DEVELOPER_DISCOVERY.md), the
  LSP or typed-sidecar README, then its protocol tests.
- **Library/framework contributor:** `stdlib/README.md` or the framework
  README, followed by the focused fixtures and projection boundary.
- **Docs/site contributor:** [Contributing](CONTRIBUTING.md), `site/README.md`,
  `app/docs/docs-manifest.ts`, then `npm run verify:site`.

The next practical step for every route is
[Contributing](CONTRIBUTING.md): it turns this map into a safe edit, test, and
review workflow.
