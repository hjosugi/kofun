# Standard library charter

Status: accepted architecture and naming policy.

Issue: [#636](https://github.com/hjosugi/kofun/issues/636).

## Promise

Kofun should be pleasant for ordinary work without requiring users to select
an unvetted third-party dependency for every common task. "Batteries included"
means:

- a coherent first-party path exists for common programs;
- support, portability, effects, ownership, and costs are explicit;
- the API is documented and discoverable;
- compatibility and security updates have a published policy;
- the issue tracker never substitutes a plan for executable evidence.

It does not mean every convenience is implicit, every protocol is frozen into
the compiler release, or every library is implemented with no reviewed
dependency.

Ruby demonstrates the value of a broad distribution and independently
updateable default libraries. Go demonstrates the value of cohesive standard
packages and a strong compatibility promise. Kofun adopts both lessons without
copying either package layout or object model.

## Four tiers

### Tier 0: prelude and built-ins

The prelude contains only language essentials that are useful in almost every
program and do not require ambient authority:

- primitive/core types and their lawful basic operations;
- `Optional`, `Result`, ranges, and the minimum function/iterator vocabulary;
- compiler-known syntax support.

I/O, network, process, clock, randomness, hidden concurrency, and
allocation-heavy conveniences are not implicit. Adding a prelude name is a
language compatibility decision.

### Tier 1: portable standard library

Tier 1 ships and is tested with every supported Kofun toolchain. Modules are
explicitly imported and are source-compatible within an edition.

Expected members include collections, text/bytes, portable path-independent
encodings, JSON/CSV, testing primitives, and platform-neutral interfaces.
Implementations prefer Kofun source and may use small reviewed intrinsics.

### Tier 2: platform standard adapters

Tier 2 implements explicit platform capabilities: filesystem, process, clock,
entropy, sockets, signals, and terminal behavior.

The public interface is portable where the semantics really are portable.
Target-specific behavior, layouts, error values, and missing capabilities are
explicit. An unsupported target fails during build/resolution; it never loads
another target's syscall table or silently weakens behavior.

The current `stdlib/linux_x86_64/` seed belongs here.

### Tier 3: official independently versioned modules

Tier 3 is first-party and may be included in the default distribution, but it
can receive compatibility and security updates independently of the compiler.

Protocol-heavy, security-heavy, and fast-moving data belongs here: HTTP/TLS,
time-zone databases, YAML, database drivers, and application frameworks. A
module may graduate into Tier 1 only through an explicit compatibility and
maintenance review.

## Compatibility and updates

### Language-coupled compatibility

Tier 0 follows the language edition and deprecation process. Removing or
semantically changing a public prelude operation requires an edition boundary
unless the old behavior is a safety/correctness defect.

Tier 1 follows the same source-compatibility promise within an edition:

- additions use stable qualified identities and cannot change existing lookup
  results ambiguously;
- deprecations include a replacement and tooling-visible migration;
- serialization and wire formats are versioned independently from source APIs;
- target support cannot disappear without a documented support transition.

### Independently updateable compatibility

Tier 3 uses semantic versions and a lockfile-resolved module version. The Kofun
distribution may recommend a default version, but an application can take a
security update without taking a new compiler.

A Tier 3 security advisory states affected versions, fixed versions, supported
toolchain range, and whether a lockfile update is sufficient. Critical fixes
must not wait for the next language edition.

Tier 2 follows both policies: its portable public interface follows Tier 1,
while target adapter revisions may ship with toolchain patch releases when
kernel/OS correctness requires it.

## Naming guide

- Public modules use lower-case, ASCII, dot-separated paths under `std`, such
  as `std.fs`, `std.json`, and `std.net.http`.
- Tier 3 official modules use an explicit first-party package identity and may
  expose similar paths without occupying the `std` compatibility namespace
  until graduated.
- Public types use `UpperCamelCase`.
- Functions and values use `lower_snake_case`.
- Names describe the domain or operation. Avoid `utils`, `helpers`, `common`,
  and target names in portable public modules.
- Constructors and conversion functions state fallibility through `Result`,
  not suffix conventions or sentinel returns.
- Receiver-style display is an ergonomic view of a stable qualified symbol.
  `value.map(f)` and `std.list.map(value, f)`, if both supported, identify the
  same operation.
- Target adapters live below internal/platform namespaces and cannot be
  imported accidentally as portable APIs.

The discoverability contract in `docs/DEVELOPER_DISCOVERY.md` displays the
stable qualified name, origin, ownership mode, and effects for every operation.

## Engineering rules

### Trusted code and dependencies

- Prefer Kofun source.
- Keep trusted intrinsics and native adapters small, enumerated, and audited.
- A third-party native dependency is allowed only when declared, pinned,
  license-recorded, reproducible, replaceable behind a Kofun contract, and
  excluded from profiles/targets that do not opt in.
- Generated tables include source/version hashes and a regeneration/audit path.
- A first-party wrapper does not turn an unpinned external dependency into
  standard-library evidence.

### Effects, ownership, and errors

- Filesystem, process, clock, entropy, network, and terminal access retain
  explicit effects/capabilities.
- Resource handles are affine. APIs express observation, mutation, and transfer
  with `read`, `edit`, and `take`.
- Cleanup behavior is defined for success, typed error, panic, cancellation,
  and early return.
- Public errors are typed `Result` values. Raw errno values, undocumented
  sentinels, partial output, and silent fallback do not cross module
  boundaries.

### Security and adversarial input

- Parsers and protocols publish input, nesting, allocation, time, and output
  limits.
- Negative tests cover malformed, truncated, ambiguous, oversized, and
  resource-exhausting inputs.
- Fuzzing is required for parsers/protocols before stabilization.
- Cryptography and TLS require dedicated security ownership; generic
  "algorithm implemented" tests are insufficient.

### Cost and portability

- Unused modules add zero bytes to a bounded static artifact.
- Included modules publish relevant code-size, startup, allocation, and
  throughput costs with reproducible methodology.
- A portable module cannot read the clock, environment, filesystem, network, or
  entropy source implicitly.
- Deterministic modes and test fakes are provided at capability boundaries.

### Documentation and discovery

Every public module provides:

- a short task-oriented recipe;
- a precise API/effect/ownership reference;
- a runnable positive example;
- negative/error examples;
- searchable exported operation metadata;
- a named validation command.

## Capability evidence

`stdlib/capabilities.tsv` is the mechanically checked current matrix.

Allowed states are:

- `implemented`: the evidence column names a runnable repository gate;
- `specified`: a normative contract exists, but the general implementation
  gate is not complete;
- `planned`: a scoped issue owns the missing outcome;
- `deferred`: valid work deliberately outside the active milestone;
- `non-goal`: intentionally excluded, with a reason.

An open generated planning issue is not enough for `implemented` or
`specified`. Bounded profiles such as the current HTTP server and CLI framework
must say `bounded` in the note and cannot be generalized silently.

Run:

```sh
sh stdlib/check-capabilities.sh
sh stdlib/tests/verify.sh
```

## Initial decisions

- JSON and CSV belong in Tier 1; TOML is the first Tier 1 configuration format.
- YAML belongs in Tier 3 because its schema/types, aliases, parser limits, and
  security/update surface need an independently versioned contract.
- Clock/monotonic time is a Tier 2 capability behind a Tier 1 interface.
  Calendar arithmetic and time-zone data are separate Tier 1/Tier 3 concerns,
  specified in [`stdlib/date-time.md`](stdlib/date-time.md).
- CLI argument parsing and HTTP servers are libraries/frameworks, not language
  core. Existing bounded profiles are evidence only for their documented
  shapes.
- The portable HTTP client remains Tier 3; its URL, streaming, redirect, proxy,
  certificate, TLS, cancellation, and resource-limit policy is specified in
  [`stdlib/http-client.md`](stdlib/http-client.md).
- Benchmark harness APIs belong in Tier 1 developer/testing support under
  [`stdlib/benchmark.md`](stdlib/benchmark.md). Runtime and allocation profilers
  remain developer/runtime subsystems with a first-party user-facing bridge.
- Scoped concurrency is the intended high-level path; raw threads are not
  automatically a Tier 1 promise.
- Secure randomness is distinct from reproducible pseudo-randomness.

## Review and graduation

A capability moves from `planned` to `specified` only when a normative contract
and negative behavior exist. It moves to `implemented` only when the matrix can
name a runnable gate on the current branch.

Tier 3 graduation to Tier 1 requires:

- at least one supported compatibility cycle;
- named maintainers and security ownership;
- deterministic cross-target conformance fixtures;
- bounded resource behavior and fuzz history;
- measured artifact cost;
- a migration and update policy.
