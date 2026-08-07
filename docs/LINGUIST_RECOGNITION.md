# GitHub Linguist recognition — issue drafts

This document contains drafted GitHub Issues for the work required to have
Kofun recognized as a programming language by
[github-linguist/linguist](https://github.com/github-linguist/linguist).
Each section is a self-contained issue draft formatted to match this
repository's issue conventions. Open each as a separate GitHub Issue when
the phase is ready to start.

---

## Issue L-1 — Linguist recognition: tracker (planning umbrella)

**Labels:** `planning`, `kind:planning`, `area:ecosystem`, `P2`

### Metadata

- State: planned
- Kind: planning
- Area: ecosystem
- Priority: P2
- Size: L — split into child issues below; never directly implementation-ready

### Summary

Track the end-to-end work needed for Kofun (`.kofun`) to be accepted into
[github-linguist/linguist](https://github.com/github-linguist/linguist),
the library GitHub uses to detect languages, display language bars, power
syntax highlighting, and index files in code search.

Recognition requires a merged Linguist PR. The Linguist maintainers require
the language to already be in real-world use: 2 000+ indexed `.kofun` files
across independent repositories and users (forks excluded) within the past
year. A PR submitted before that threshold is likely to be closed immediately.

### Ordered phases

1. **L-2** Publish TextMate grammar as a standalone repository.
2. **L-3** Add 20–50 practical Linguist code samples.
3. **L-4** Publish VS Code extension to the Marketplace.
4. **L-5** Starter repositories and `create-kofun-app` scaffold.
5. **L-6** GitHub Actions workflow for Kofun projects.
6. **L-7** Docker image for Kofun toolchain.
7. **L-8** Monitor `.kofun` usage threshold (Phase 3 gate).
8. **L-9** Submit Linguist PR (Phase 4).

### Final acceptance criteria

- [ ] `github-linguist/linguist` PR merged and included in a Linguist release.
- [ ] `.kofun` files highlighted correctly on GitHub.
- [ ] Kofun appears in GitHub language statistics for qualifying repositories.
- [ ] `language:kofun` code search returns results.

---

## Issue L-2 — Publish TextMate grammar as a standalone repository

**Labels:** `kind:maintenance`, `area:tooling`, `area:ecosystem`, `P2`, `size:S`

### Metadata

- State: planned
- Kind: maintenance
- Area: tooling, ecosystem
- Priority: P2
- Size: S
- Parent: L-1

### Summary

Linguist requires a TextMate grammar hosted in its own GitHub repository, which
`script/add-grammar` pins as a git submodule. That is now satisfied without a
purpose-built repository: the grammar moved to
[`kofun-lang/kofun-vscode`](https://github.com/kofun-lang/kofun-vscode) with the rest
of the VS Code extension, at `syntaxes/kofun.tmLanguage.json`. A VS Code
extension repository is the shape most Linguist grammar submodules already
take, so no separate `kofun-language` repository is needed.

What remains is the licensing and manifest check against that repository rather
than a move. The grammar must carry a Linguist-approved open-source licence;
`kofun-vscode` is Apache-2.0 OR MIT, carried over from here.

### Acceptance criteria

- [x] A public repository holds the grammar on its own: `kofun-lang/kofun-vscode`.
- [x] `syntaxes/kofun.tmLanguage.json` is present and valid JSON.
- [x] `package.json` identifies the grammar scope `source.kofun`.
- [x] Approved open-source licence files are present (`LICENSE-APACHE`,
  `LICENSE-MIT`).
- [ ] `script/add-grammar https://github.com/kofun-lang/kofun-vscode` runs
  without error in a local Linguist checkout.

---

## Issue L-3 — Add practical Linguist code samples

**Labels:** `kind:documentation`, `area:ecosystem`, `P2`, `size:M`

### Metadata

- State: planned
- Kind: documentation
- Area: ecosystem
- Priority: P2
- Size: M
- Parent: L-1
- Depends on: L-2

### Summary

Linguist requires real-world, practical code samples in
`samples/Kofun/` to train its language classifier. The
[Linguist sample policy](https://github.com/github-linguist/linguist/blob/master/CONTRIBUTING.md)
explicitly rejects:

- `print("Hello, world!")` alone
- auto-generated boilerplate
- trivially short files

Acceptable samples must demonstrate language structure and idiomatic usage.

Collect 20–50 `.kofun` files covering:

| Sample | Demonstrates |
|---|---|
| `http_server.kofun` | framework, ADTs, pattern match |
| `json_parser.kofun` | recursion, ADTs, `Result` |
| `cli_tool.kofun` | CLI framework, error handling |
| `actor.kofun` | concurrency / message-passing design |
| `ownership.kofun` | read/edit/take ownership |
| `law_check.kofun` | algebraic law declaration |
| `pipeline.kofun` | iterator/pipeline combinators |
| `wasm_export.kofun` | WebAssembly interop |
| `tui_dashboard.kofun` | TUI framework |
| `science_stats.kofun` | numeric / stdlib |

Several candidates already exist under `examples/`; they should be reviewed
for adequacy and copied/adapted into a `samples/Kofun/` staging area.

Each sample's source and licence must be recorded in the Linguist PR
description.

### Acceptance criteria

- [ ] At least 20 `.kofun` sample files exist in `samples/Kofun/` (within
  this repository or a dedicated `hjosugi/kofun-samples` repo).
- [ ] No sample is a bare Hello World; each demonstrates a distinct language
  feature.
- [ ] Every sample compiles (or passes `./bin/kofun check`) without errors.
- [ ] Sample licences are recorded and all are Linguist-approved.
- [ ] `bundle exec bin/github-linguist --breakdown` on a directory of these
  samples classifies them as `Kofun`.

---

## Issue L-4 — Publish VS Code extension to the Marketplace

**Labels:** `kind:maintenance`, `area:tooling`, `area:ecosystem`, `P2`, `size:S`

### Metadata

- State: planned
- Kind: maintenance
- Area: tooling, ecosystem
- Priority: P2
- Size: S
- Parent: L-1
- Depends on: L-2

### Summary

Publishing the VS Code extension, now
[`kofun-lang/kofun-vscode`](https://github.com/kofun-lang/kofun-vscode), to the
[VS Code Marketplace](https://marketplace.visualstudio.com/) provides:

1. Discoverability — developers searching for Kofun find it immediately.
2. A stable, versioned grammar artefact that the community uses.
3. Evidence of real tooling adoption when submitting the Linguist PR.

Steps:
1. Create or link a publisher account on the Marketplace (`hjosugi`).
2. ~~Add required `publisher` and `repository` fields.~~ Done.
3. ~~Write a meaningful `README.md` for the extension page.~~ Done.
4. ~~Run `vsce package` and `vsce publish`.~~ Packaging is scripted; publishing
   waits on step 1 and on the `VSCE_PAT` secret.
5. ~~Add a publish step to CI so releases stay in sync.~~ Done — the
   **Extension Package** workflow packages `linux-x64`, `darwin-x64` and
   `darwin-arm64` and publishes them together on dispatch.

### Acceptance criteria

- [ ] Extension is listed at `https://marketplace.visualstudio.com/items?itemName=hjosugi.kofun`.
- [ ] Installing it enables `.kofun` syntax highlighting in VS Code.
- [x] The extension manifest contains `publisher`, `repository`, and a
  populated `description`.
- [x] CI produces versioned per-platform `.vsix` artefacts. Per platform is not
  a refinement: the bundled server loads a natively compiled bridge, and an
  untargeted VSIX installs anywhere and then answers every request with `null`,
  showing neither an error nor the syntactic fallback.

---

## Issue L-5 — Starter repositories and `create-kofun-app` scaffold

**Labels:** `kind:implementation`, `area:ecosystem`, `P2`, `size:M`

### Metadata

- State: planned
- Kind: implementation
- Area: ecosystem
- Priority: P2
- Size: M
- Parent: L-1
- Depends on: L-4

### Summary

Independent developers are the only path to the 2 000-file threshold
Linguist requires. Reducing setup friction is the most direct lever. Provide:

1. **`create-kofun-app`** — a CLI scaffolding tool (similar to
   `create-react-app` or `cargo new`) that generates a project skeleton with
   a `main.kofun`, `build.kofun`, and CI workflow in one command.
2. **Template repositories** tagged `kofun-template` on GitHub:
   - `kofun-http-api` — minimal HTTP API server
   - `kofun-cli` — CLI application
   - `kofun-lib` — reusable library package
3. Mark each template with the `kofun` GitHub topic so they are discoverable.

Note: template repositories owned by `hjosugi` count only weakly toward the
Linguist threshold. Their purpose is to lower the barrier so independent
users create their own repositories.

### Acceptance criteria

- [ ] `create-kofun-app` (or `kofun new`) produces a buildable project
  directory with `./bin/kofun build` succeeding.
- [ ] Three template repositories are public and tagged `kofun-template`.
- [ ] Each template includes a working `.github/workflows/kofun.yml`.
- [ ] Installation and scaffold steps are documented in the main README.

---

## Issue L-6 — GitHub Actions reusable workflow for Kofun projects

**Labels:** `kind:implementation`, `area:ecosystem`, `P2`, `size:S`

### Metadata

- State: planned
- Kind: implementation
- Area: ecosystem
- Priority: P2
- Size: S
- Parent: L-1
- Depends on: L-5

### Summary

A reusable GitHub Actions workflow (`kofun-lang/kofun/.github/workflows/kofun.yml`
or a dedicated `hjosugi/setup-kofun` action) lets any project add Kofun CI
with a two-line workflow. When developers use it in their own repositories,
they commit `.kofun` files that count towards the Linguist threshold.

Provide:

1. `hjosugi/setup-kofun` composite action — installs the Kofun toolchain
   from a release artefact.
2. Example workflow snippet in the documentation showing `uses:
   hjosugi/setup-kofun@v1`.
3. Publish the action to the GitHub Marketplace.

### Acceptance criteria

- [ ] `hjosugi/setup-kofun@v1` composite action is public.
- [ ] A project using it can run `./bin/kofun build` in CI without manual
  setup steps.
- [ ] Action is listed on the GitHub Marketplace.
- [ ] Documentation includes a copy-pasteable example workflow.

---

## Issue L-7 — Docker image for the Kofun toolchain

**Labels:** `kind:maintenance`, `area:ecosystem`, `P2`, `size:S`

### Metadata

- State: planned
- Kind: maintenance
- Area: ecosystem
- Priority: P2
- Size: S
- Parent: L-1

### Summary

A public Docker image (`ghcr.io/hjosugi/kofun` or Docker Hub) provides a
zero-install path for:

- CI pipelines that prefer Docker-based environments
- Playground / online REPL deployments
- Contributors on non-Linux hosts

Publish `kofun-lang/kofun:latest` and `kofun-lang/kofun:<version>` tags built from
the release artefact on each tag push. The image should contain
`/usr/local/bin/kofun` and the standard library.

### Acceptance criteria

- [ ] `docker run --rm ghcr.io/hjosugi/kofun kofun --version` prints the
  version string.
- [ ] Image is published to GHCR (or Docker Hub) on each release tag via CI.
- [ ] A `Dockerfile` lives in the repository and is reviewed for size and
  reproducibility.
- [ ] Documentation references the Docker image in the getting-started guide.

---

## Issue L-8 — Monitor `.kofun` usage threshold (Phase 3 gate)

**Labels:** `kind:research`, `area:ecosystem`, `P2`, `size:S`

### Metadata

- State: planned
- Kind: research
- Area: ecosystem
- Priority: P2
- Size: S
- Parent: L-1

### Summary

Linguist requires 2 000+ indexed `.kofun` files across independent
repositories and users (forks excluded, indexed within the last year) before
a PR will be accepted. This issue is the recurring gate check that precedes
**L-9**.

Track periodically using GitHub code search:

```
extension:kofun
```

Record:

| Date | Indexed files | Unique repos | Unique users | Fork share |
|---|---|---|---|---|
| YYYY-MM-DD | — | — | — | — |

The goal is:

- 2 000+ results
- Many independent repositories (not concentrated in `hjosugi/*`)
- Many independent users (not concentrated in one account)
- Low fork share

This issue is not closed until the threshold is confirmed and L-9 is ready
to open.

### Acceptance criteria

- [ ] A recorded snapshot shows ≥ 2 000 indexed `.kofun` files.
- [ ] Snapshot shows meaningful distribution across independent users and
  repositories (not the author's own accounts).
- [ ] Forks are excluded from the count.
- [ ] Snapshot is taken within 30 days of opening L-9.

---

## Issue L-9 — Submit Kofun to github-linguist/linguist

**Labels:** `kind:implementation`, `area:ecosystem`, `P2`, `size:M`

### Metadata

- State: planned
- Kind: implementation
- Area: ecosystem
- Priority: P2
- Size: M
- Parent: L-1
- Depends on: L-2, L-3, L-8

### Summary

With the usage threshold confirmed in L-8, submit a Pull Request to
[github-linguist/linguist](https://github.com/github-linguist/linguist)
adding Kofun as a recognized language.

The PR must include all five components required by the Linguist contributing
guide:

#### 1. `languages.yml` entry

```yaml
Kofun:
  type: programming
  color: "#5C3D9C"       # adjust to match the project mark
  aliases:
    - kofun
  extensions:
    - ".kofun"
  tm_scope: source.kofun
  ace_mode: text
  language_id:           # generated by script/update-ids; leave blank initially
```

#### 2. TextMate grammar (via `script/add-grammar`)

```sh
script/add-grammar https://github.com/hjosugi/kofun-language
```

Completed by **L-2**.

#### 3. Code samples in `samples/Kofun/`

Completed by **L-3**.

#### 4. ID generation and test suite

```sh
script/bootstrap
script/update-ids
bundle exec rake test
bundle exec script/cross-validation --test
bundle exec bin/github-linguist --breakdown
```

All must pass.

#### 5. PR description

Must fully fill in the Linguist PR template, including:

- GitHub code search results confirming usage threshold
- Breakdown by unique repositories and users (forks excluded)
- Grammar licence
- Sample code sources and licences
- Passing CI results

Leaving any section blank causes the PR to be ignored per the Linguist
contributing guide.

### Acceptance criteria

- [ ] Linguist PR opened with all five components.
- [ ] PR template fully completed (no blank sections).
- [ ] CI passes on the Linguist fork.
- [ ] `bundle exec bin/github-linguist --breakdown` on the samples directory
  reports `Kofun`.
- [ ] PR merged into `github-linguist/linguist`.
