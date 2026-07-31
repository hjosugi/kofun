# Standard library charter

## Status

This document is the accepted charter for GitHub issue
[#636](https://github.com/hjosugi/kofun/issues/636). It decides what "the
standard library is enough for ordinary work" means for Kofun: the tiers, the
coverage goal, the compatibility and update policy, and the engineering rules
every library module answers to.

It is a policy contract, not a status report. The machine-checked coverage
matrix lives in [`stdlib/capabilities.tsv`](../stdlib/capabilities.tsv) and is
validated by `sh stdlib/check-capabilities.sh`; a row saying `planned` or
`deferred` there is the honest state, and nothing in this charter promotes it.
The generated planning issues #479–#503 are routed by this charter; an open
planning issue is never implementation evidence.

## The four tiers

Every library capability belongs to exactly one tier. The tier decides how it
ships, how it is versioned, and what compatibility it promises.

### T0 — Prelude / built-ins

Tiny, implicitly available language essentials only: `Result`, `Optional`
(`T?`), `Validated`, core numeric/text/boolean operations, comparison, and the
collection literals the language itself defines. No I/O, no network, no clock,
no randomness, no process access, no hidden allocation-heavy convenience.

- Ships: inseparable from the compiler release.
- Compatibility: edition-scoped source compatibility; a break is a language
  change and goes through the RFC ledger.

### T1 — Portable standard library

Shipped and tested with every Kofun toolchain; explicitly imported; pure Kofun
where practical; no platform authority. Collections, text/bytes, encodings,
JSON/CSV/TOML, regex, logging, testing primitives, validation, the benchmark
API, and portable interfaces (such as the stream protocol) belong here.

- Ships: with the toolchain, versioned with it.
- Compatibility: source-compatible within an edition.

### T2 — Platform standard adapters

First-party implementations of filesystem, process, environment, clock,
entropy, socket, and terminal capabilities. Target support is explicit:
an unsupported target fails at build time with a typed refusal rather than
silently degrading. The current `stdlib/linux_x86_64` syscall contract is the
first T2 adapter.

- Ships: with the toolchain, per target.
- Compatibility: the portable interface is T1-stable; the adapter set may
  grow per target.

### T3 — Official independently versioned modules

Security- and protocol-heavy components: HTTP client and TLS, time-zone
database, compression/archive formats, database drivers, and the application
frameworks under `framework/`. They may ship in the default distribution but
must be updateable without waiting for a compiler release, because their
threat model moves faster than the language.

- Ships: in the default distribution, independently versioned (semver).
- Compatibility: per-module semver; each module names its own support window
  and security-update channel.

## Alternatives considered

**One monolithic stdlib, everything compiler-versioned (Go's shape).**
Merits: one import namespace, one compatibility promise, no tier bookkeeping.
Demerits: TLS root stores, time-zone data, and protocol parsers cannot wait
for compiler releases to take security updates; Go itself has had to carve
out `golang.org/x/` for exactly this pressure. Rejected.

**Minimal core plus third-party ecosystem (Rust's shape).** Merits: smallest
trusted surface, fastest ecosystem iteration. Demerits: ordinary work
(JSON, HTTP, dates, logging) immediately requires unvetted dependency
choices, which contradicts the batteries-included outcome #636 requires and
multiplies supply-chain review for every user. Rejected.

**Everything implicit in the prelude (scripting-language shape).** Merits:
zero-import ergonomics. Demerits: ambient authority (I/O, clock, randomness
reachable without a visible capability) breaks Kofun's explicit-capability
rule, and unused convenience bloats every static artifact. Rejected.

**The four tiers above.** Merits: batteries are reliable and documented
(Ruby's lesson), the compatibility promise is cohesive where it can be
(Go's lesson), and security-critical parts update independently (the
default-gem lesson). Demerits: tier assignment is a real decision per module
and must be policed by the matrix checker; the boundary between T1 and T3
will occasionally be argued. Accepted: the demerit is bookkeeping, and the
bookkeeping is mechanical.

## Coverage goal and the matrix

A user must be able to do all of the following without choosing an unvetted
third-party dependency: files/paths, environment/process, time (clock,
calendar, zones), JSON/CSV/TOML, logging, unit testing, CLI parsing, HTTP
server and client, streams/concurrency composition, validation, and
benchmarking — plus the Go-style gap set: buffered I/O, URL handling,
hashes/checksums, compression/archives, crypto/TLS, MIME, temporary files,
and secure randomness.

[`stdlib/capabilities.tsv`](../stdlib/capabilities.tsv) is the single
machine-checked matrix. Each row assigns one capability a tier, exactly one
state — `implemented`, `specified`, `planned`, `deferred`, or `non-goal` —
and evidence: repository paths for `implemented`/`specified`, an issue or
this charter for the rest. `sh stdlib/check-capabilities.sh` fails the build
of truth when a state is unknown, evidence is missing, or a named path does
not exist. Breadth is this decision matrix, not an automatic promise to
implement every package.

### Decisions the matrix records (first child set)

- **HTTP client**: T3 official module. Contract accepted in
  [`docs/stdlib/http-client.md`](stdlib/http-client.md) (#638); first
  implementation slice is #644.
- **Calendar / date / time zones**: split across tiers — portable civil
  types T1, clock capability T2, time-zone data T3. Contract accepted in
  [`docs/stdlib/date-time.md`](stdlib/date-time.md) (#639); slices #645,
  #647, #648.
- **Benchmark harness**: T1 library API plus a `kofun bench` runner.
  Contract accepted in [`docs/stdlib/benchmark.md`](stdlib/benchmark.md)
  (#640); first slice is #646.
- **YAML**: **non-goal** as a first-party module. Merits of shipping it:
  ubiquitous config format. Demerits: the specification is large and
  security-hostile (anchors, aliases, implicit typing have a long CVE
  history), JSON+TOML already cover Kofun's config needs, and Ruby's own
  trajectory (Psych moving toward safe-by-default loading) shows the cost
  of owning it. A future community or T3 module may revisit this with a
  safe-subset profile; the charter does not.
- **Stream protocol**: T1 portable interface, accepted in
  [`docs/stdlib/stream-protocol.md`](stdlib/stream-protocol.md) (#627).
- **Concurrency**: deferred behind the #555 runtime contract; the charter
  assigns scoped tasks/channels to T1-interface/T2-runtime once that
  contract exists.

## Engineering rules

1. Prefer Kofun source over trusted/native code; keep the trusted platform
   surface small and audited (`stdlib/CONTRACT.md` governs the seed).
2. Third-party native dependencies are permitted only when declared, pinned,
   licensed, reproducible, replaceable behind a Kofun contract, and absent
   from targets/profiles that do not opt in.
3. No ambient authority: filesystem, process, clock, randomness, and network
   operations keep explicit effect/capability boundaries at every tier.
4. Resource APIs use `read` / `edit` / `take` with deterministic cleanup;
   raw handles and errno values do not leak through public signatures.
5. Pay-for-what-you-use: an unused module adds zero bytes to a static
   artifact; the included cost of each module is recorded beside its
   release evidence.
6. Parsers and protocol modules carry adversarial input limits, fuzz
   fixtures, and typed `Result` errors; no undocumented sentinel values.
7. Every public module has a short recipe, a precise reference, a runnable
   example, and is listable/searchable by `kofun` tooling.
8. T3 security-critical modules name an update channel and support window
   separate from compiler releases.

## Compatibility and deprecation

- T0/T1: source compatibility within an edition; removal requires a
  deprecation period of at least one minor toolchain release with a
  diagnostic that names the replacement.
- T2: the portable interface follows T1; per-target adapters may be added
  freely and removed only with the same deprecation discipline.
- T3: per-module semver; a security fix may break API in a major bump, and
  the module's support window says how long the previous major receives
  patches.

## Routing of existing work

- #479–#503 (generated planning): become children of the matrix rows they
  name; they inherit this charter's tiers and rules rather than restating
  library-wide policy.
- #231–#234 (platform abstractions): T2 adapter contracts.
- #398 / #476 (profiling counters): providers behind the #640 benchmark
  contract, not duplicated stdlib API.
- Closed #24 (HTTP server) and #25 (CLI): remain T3/T1 evidence of their
  documented bounded profiles; neither is evidence for a portable HTTP
  client or a general options API.

## Non-goals

- implementing every module named in the matrix;
- Ruby API compatibility or Go package-name cloning;
- making convenience functions implicit in the prelude;
- treating `framework/` as language core;
- banning auditable third-party code categorically;
- reading an open planning issue as implementation evidence.
