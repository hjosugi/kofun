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

Each path -> focused check.sh or test runner -> Taskfile target -> task verify
```

The launcher source is readable shell. When command routing is surprising,
start at [`bin/kofun`](../bin/kofun), find the public subcommand, and follow the
`ensure_*_compiler` or build function it invokes.

## Top-level directory map

| Path | What it owns | Start here | Typical gate |
|---|---|---|---|
| `bin/` | repository-local public launcher | `bin/kofun` | `task test` |
| `bootstrap/` | compiler seeds, frontends, direct backends, self-host evidence | `bootstrap/README.md` | `task bootstrap`, `task stage2`, `task native` |
| `spec/` | normative language and tooling contracts plus executable examples | `spec/README.md` | the matching `task *-spec` target |
| `tests/` | public behavior, conformance, diagnostics, fuzzing, tooling, integration | nearest runner or README | `task test`, `task diagnostics`, `task fuzz` |
| `stdlib/` | Kofun-authored standard-library contracts and focused projections | `stdlib/README.md` | `task stdlib` |
| `framework/` | bounded HTTP, CLI, and terminal UI surfaces | subsystem README | `task http`, `task cli-framework`, `task tui-framework` |
| `tooling/` | language server, typed-sidecar codec/projector, and disclosure-safe documentation index | subsystem README and [`docs/DOCUMENTATION_INDEX.md`](DOCUMENTATION_INDEX.md) | `task lsp`, `task typed-sidecar-codec`, `task documentation-index` |
| `editor/` | VS Code metadata/LSP bundle and Tree-sitter grammar | subsystem README | `task tree-sitter`, `task lsp` |
| `unicode/` | Unicode tables, generator, provenance, and C boundary | `unicode/README.md` | `task unicode` |
| `vendor/` | reviewed third-party source copied into the tree | `vendor/*/README.kofun.md` | integrity owner named by subsystem |
| `package/` | locked external native-artifact package manager | `package/README.md` | `task packages` |
| `examples/` | user-facing and interoperability examples | example plus its nearest check | relevant build or `check.sh` |
| `docs/` | authored guides, designs, status, and browser-tour source | this guide | `task tour` for `docs/tour/`; `spec/*/check.sh` for the gated documents |
| `benchmarks/` | reproducible benchmark programs, harnesses, and recorded results | `benchmarks/README.md` | subsystem benchmark script |
| `artifacts/` | checked evidence summaries and cost/law artifacts | inspect producer named in the artifact | producer-specific gate |
| `scripts/` | repository policy verification written in Kofun; **currently inert**, see below | script source | none — no task invokes it |
| `.github/workflows/` | CI | workflow YAML | GitHub Actions |

`scripts/verify_repository.kofun` deserves a warning rather than a row. It is
written in Kofun against `read_text`, which the current Core does not implement
(`error[E2S10]: unsupported Core builtin call 'read_text'`, reproducible with a
one-line `main`). No task invokes it, so nothing reported the breakage, and two
of its assertions had gone stale unnoticed: it required a deleted `Makefile`
and a README string the README no longer carries. Both are re-pinned against
this tree, but nothing keeps them that way. Treat it as an aspiration, not a
gate, until the Core supports reading files — at which point it should be wired
into `verify` so it cannot rot again.

What is deliberately not here: the official site, its docs renderer, the browser
playground, the delivery-planning snapshots, and the long-range issue catalogue
all live in [`hjosugi/kofun-site`](https://github.com/hjosugi/kofun-site), which
reads this repository as a submodule. The dependency runs one way. No gate in
this repository reads anything from there, and `task verify` needs no npm,
Next.js, or Cloudflare toolchain. `docs/tour/` is the exception that proves the
rule: it looks like site material but `docs/tour/compiler.mjs` is a browser port
of `bootstrap/wasm/compiler.c` that `task tour` pins to the native wasm32 output
byte for byte, so it is compiler source and stays here.

Root files are also part of the architecture:

| File | Purpose |
|---|---|
| `Taskfile.yml` | contributor-facing index of executable gates |
| `README.md` | concise public project entrypoint |
| `DESIGN.md` | early high-level language design context |
| `LICENSE-*` / `NOTICE` | dual-license and attribution terms |

## `bootstrap/`: the compiler is several checkpoints

### `bootstrap/stage1/`

Stage 1 is the Python-free bootstrap seed.

- `compiler.kofun` is the canonical Kofun source.
- `compiler.c` is the checked-in audited C11 seed.
- `SHA256SUMS` pins the source/artifact relationship.
- `check.sh` builds the seed and verifies its bounded nested-block
  Int/Bool/Text/List[Text] Core.

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
- fixtures, exact stdout/stderr, and `SHA256SUMS`;
- `check.sh`, which compiles and compares the expected artifacts; and
- `build.sh`, sourced rather than run, which is the single definition of how a
  Stage 2 compiler binary is produced for a gate that needs one.

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

The typed-sidecar documentation projection is documented separately in
[`docs/DOCUMENTATION_INDEX.md`](DOCUMENTATION_INDEX.md). Start there before
adding a renderer or search consumer: public and package-internal views have
different disclosure authority, and partial/stale output is never a complete
current index.

### What stays in `docs/`, and why

`docs/` looks like the obvious third thing to move out after the site and the
backlog. It mostly is not, and the rule is worth stating so the question is
settled with evidence rather than re-asked every time the repository feels
large.

**A document stays when this repository resolves it.** Four resolvers count, and
each one fails a gate when the document it names goes missing or drifts:

| Resolver | What it names | Gate |
|---|---|---|
| `release/claims.json` | `docs/MVP_IMPLEMENTED.md` as the sole `public_sources` entry, twelve documents as `specification`, and `docs/MEMORY_MODEL.md` and `docs/SECURITY.md` as `threat_model` | `task release-claims` |
| `rfcs/index.json` | `docs/DESIGN_DECISIONS.md` as a decision `source`, and four documents as `normative_spec` | `task rfc-registry` |
| `artifacts/release-evidence/index.json` | a SHA-256 of each of fourteen documents under `evidence_digests` | `task release-claims`, regenerated by `task release-evidence` |
| a gate script | `spec/*/check.sh`, `tests/**/run.sh`, `bootstrap/*/check.sh` and `docs/tour/check.sh` read documents directly | the owning `task` target |

Measured on this tree, of the thirty-one tracked `.md` files under `docs/`:

- **twenty-five are resolved by one of the four.** Moving any of them would not
  lose prose, it would break a gate.
- **five are resolved only by `app/docs/docs-manifest.ts`** in
  [`hjosugi/kofun-site`](https://github.com/hjosugi/kofun-site), which renders
  them out of the submodule: `CONTRIBUTING.md`, `LANGUAGE_VISION.md`,
  `RELEASE_EVIDENCE.md`, `REPOSITORY_GUIDE.md` and `RFC_PROCESS.md`. That
  resolver lives in the other repository, and the dependency still runs one
  way: the site reads these, and nothing here reads the site.
- **one is resolved by nothing:** `docs/tour/README.md`, kept because it
  documents `docs/tour/`, every other file of which stays.

Reproduce the split:

```sh
git ls-files docs | grep '\.md$'
git grep -oh -E 'docs/[A-Za-z0-9_./-]+\.md' -- \
    '*.sh' Taskfile.yml '*.mjs' '*.ts' '*.c' '*.kofun' '*.json'
```

**That grep under-reports, and the gap is load-bearing.** It matches literal
paths, and `docs/tour/check.sh` builds its path from a loop variable:

```sh
for language in python typescript go rust
do
    test -s "$ROOT/docs/tour/guides/$language.md"
    grep -Fq 'Where Kofun is worse today' \
        "$ROOT/docs/tour/guides/$language.md"
done
```

So the four `docs/tour/guides/*.md` files read as free and are not: `task tour`
asserts each is non-empty and still contains that heading. Before moving a
document out on the strength of a grep, check that no gate constructs its path.

Four documents did leave, into `content/` in `hjosugi/kofun-site`. No resolver
named them, and none of them was rendered even by the site:

| Document | Why it left |
|---|---|
| `ISSUE_TRIAGE.md` | issue-workflow policy, cited from issues by URL; no gate reads it |
| `ONE_DAY_TUTORIAL.md` | narrative walkthrough |
| `CODING_INTERVIEW.md` | narrative comparison |
| `SCIENTIFIC_COMPUTING.md` | long-range design with no implementation to gate |

That is about 30 KB, 0.3% of tracked bytes — which is the finding, not a
disappointment. The size was never in `docs/`; it was in the site and the
backlog, and both are gone.

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

### How a gate reports a failure

`tests/assertions/assert.sh` is sourced, never run, and holds the assertion
helpers a gate should use — `assert_eq`, `assert_num`, `assert_file_empty`,
`assert_absent`, and the rest. Each takes a label first and prints one line
naming the label, the expectation, and the observation.

They exist because every gate runs under `set -eu`, where a bare
`test "$a" = "$b"` that fails exits the script and prints **nothing**. #794
records that costing real time — the native gate's digest check failing with an
empty stderr — and #814 sized the problem at 459 assertions in that shape.

`tests/assertions/check.sh` (`task assertions`) counts them and holds every
script to the budget recorded in `tests/assertions/budget.tsv`. It fails in
both directions: over budget is a regression, and under budget means a fix was
made without lowering the budget to record it.

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
parser or one semantic authority. Run both `task lsp` and `task tree-sitter`
when the user-visible edit experience changes.

## Official site and documentation pipeline

The official site has a deliberately simple authority chain, and it crosses a
repository boundary exactly once:

```text
docs/*.md or selected subsystem README      (this repository)
            |
            v  checked out at the CI-verified main commit
app/docs/docs-manifest.ts                   (hjosugi/kofun-site)
            |
            v
app/docs/[slug]/page.tsx + ReactMarkdown
            |
            v
Pages workflow + Next.js static export      (this repository)
            |
            v
GitHub Pages, still served at hjosugi.github.io/kofun/
```

The renderer and its tests live in
[`hjosugi/kofun-site`](https://github.com/hjosugi/kofun-site) and are documented
in that repository's `site/README.md`. This repository's
`.github/workflows/pages.yml` pins a reviewed renderer commit, checks out the
exact Kofun commit whose main CI passed, refreshes the public tracker snapshots,
and deploys the verified static artifact directly with GitHub Pages Actions. No
generated publication branch is part of that authority chain. The deployed
artifact records both revisions in `.kofun-source-commit` and
`.kofun-site-commit` for production read-back.

What this repository owes the renderer is this: the documents named in the
site's manifest must keep existing at their current paths, and their relative
links must keep resolving. Moving or renaming a document under `docs/` is
therefore a cross-repository change.

Generated directories are ignored:

- `node_modules/` — dependencies of `editor/tree-sitter-kofun`, the only npm
  project left in this repository; and
- `build/` / `.kofun/` — compiler and project output.

Never make a source fix only inside one of these directories.

## Find the owner for a change

| You want to change… | Start in… | Read next… | Run first… |
|---|---|---|---|
| public CLI routing or exit behavior | `bin/kofun` | `tests/cli.sh` | `task test` |
| Stage 1 Int/Bool/Text/List[Text] Core | `bootstrap/stage1/` | its README and `check.sh` | `task bootstrap` |
| Stage 2 syntax, typing, or C lowering | `bootstrap/stage2/` | its README and matching fixture | `task stage2` |
| a stable error code/message/span | emitter plus `tests/diagnostics/` | diagnostics README/registry | `task diagnostics` |
| x86-64 or AArch64 direct output | `bootstrap/native/` | native README and docs | `task native` |
| wasm32 arithmetic output | `bootstrap/wasm/` | wasm README | `task wasm` |
| C or Rust interoperability | `bootstrap/c_abi/`, `examples/rust-shim/` | security/third-party docs | `task c-abi` or `task rust-shim` |
| a standard-library contract | matching `stdlib/<name>/` | module README | its `tests/verify.sh` |
| HTTP, CLI, or TUI framework | matching `framework/<name>/` | subsystem README | matching task target |
| LSP behavior | `tooling/lsp/`, `tests/lsp/` | LSP README | `task lsp` |
| structural editor parsing | `editor/tree-sitter-kofun/` | package README | `task tree-sitter` |
| VS Code packaging or metadata | `editor/vscode/` | extension README | `task lsp` plus extension check |
| language contract | `spec/` | spec index and conformance owner | matching spec/conformance gate |
| explanatory docs | `docs/` | this guide | the `spec/*/check.sh` that reads the document, if any |
| the browser tour | `docs/tour/` | `docs/tour/README.md` | `task tour` |
| docs UI, playground, or delivery snapshots | `hjosugi/kofun-site` | that repository's `site/README.md` | `npm run verify:site` there |

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
- **Docs contributor:** [Contributing](CONTRIBUTING.md), then the document you
  are changing. If it is one the site renders, check
  `app/docs/docs-manifest.ts` in
  [`hjosugi/kofun-site`](https://github.com/hjosugi/kofun-site) before moving or
  renaming it.

The next practical step for every route is
[Contributing](CONTRIBUTING.md): it turns this map into a safe edit, test, and
review workflow.
