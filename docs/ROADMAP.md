# Roadmap

## Release rule

Kofun advances milestones by correctness gate, not by feature count.

- never silently fall back on unsupported behavior
- attach a negative test and a threat model to every safety claim
- attach a reproducible benchmark to every performance claim
- distinguish the strength of law evidence as `bounded`, `proven-finite`, or `proven`
- do not call the project "self-hosting complete" before the Stage 2 fixed point

A milestone is advanced by evidence, not by a decision having been accepted.
[`RFC_PROCESS.md`](RFC_PROCESS.md) records public semantic decisions and keeps
`accepted` separate from `implemented`; an accepted RFC carries no schedule and
moves no milestone until [`release/claims.json`](../release/claims.json)
evidences the capability. The RFC sets named here are decision work, and appear
on this roadmap only once they have implementation evidence to advance.

## Current critical-path order

The smallest compiler source `S`, its typed profile, deterministic C11
lowering, and the first runnable compiler-produced compiler are active. The
remaining fixed-point path is intentionally short:

1. use the generated compiler to produce `C2/A2` and `C3/A3`
   ([#271](https://github.com/hjosugi/kofun/issues/271)); and
2. require equivalent C sources and executables across the three generations
   ([#272](https://github.com/hjosugi/kofun/issues/272)).

That gate may use one declared, normalized host C11 compiler. Direct x86-64
and AArch64 compiler reproduction is a separate strengthening track; both
native backends already execute bounded Int, function, `List[Int]`, and UTF-8
`Text` profiles. The [implemented-status matrix](MVP_IMPLEMENTED.md) is the
authority for their exact active boundary.

Heterogeneous records ([#546](https://github.com/hjosugi/kofun/issues/546)),
the concrete-first law system
([#551](https://github.com/hjosugi/kofun/issues/551)), syntax decisions in
[#624](https://github.com/hjosugi/kofun/issues/624) through
[#626](https://github.com/hjosugi/kofun/issues/626), and the small-core
reactive protocol in [#627](https://github.com/hjosugi/kofun/issues/627)
remain important P1 work. None expands the frozen string-scanning profile
before B4/B5. Advanced effects, dependent or refinement types, concurrency
runtime implementation, and an optional second backend remain later. The
evidence and keep/defer/reject decisions are indexed in the
[implemented-status matrix](MVP_IMPLEMENTED.md).

## M0 — Specification and UX validation

Deliverables:

- working title decision process
- syntax RFC set
- memory model RFC
- type/effect/law model RFC
- standard library naming guide
- error code policy
- executable reference semantics corpus
- one-day tutorial user tests
- concrete-first finite-law checker design and executable status audit
- bootstrap stage manifest

Exit criteria:

- no unresolved P0 ambiguity in the core syntax
- ownership examples can be explained without Rust experience
- null/optional behavior is fixed
- the coding interview sample set is complete
- law assurance labels do not mislead
- bootstrap status can be verified machine-readably

Current foundation:

- Kofun-written nested-block Int/Bool/Text Core compiler seed
- frozen self-host profile and runnable first compiler generation
- direct x86-64 and AArch64 bounded native checkpoints
- compiler-wide stable diagnostic and semantic-oracle gates
- affine ownership prototype; the general checker remains design work
- historical bounded-Monad examples, finite-model artifacts, and JSON schema;
  active compiler integration remains open in
  [#551](https://github.com/hjosugi/kofun/issues/551)

## M1 — Bootstrap compiler

Deliverables:

- lossless parser
- module resolver
- stronger type inference
- MIR-based ownership checker
- bytecode VM
- C11 backend expansion
- full CLI skeleton
- package manifest and lockfile draft
- formatter and language server prototype
- Stage 1 frontend written in Kofun
- Stage 1 type, ownership, and law checker written in Kofun
- Stage 1 C11 backend written in Kofun
- Stage 2 self-recompile pipeline
- normalized Stage 1/Stage 2 artifact comparison

Law deliverables:

- Functor, Applicative, Semigroup, and Monoid law families
- deterministic model checker budgets
- type-directed counterexample shrinking
- finite ADT enumeration
- evidence serialization and cache keys

Exit criteria:

- self-contained medium programs run
- compiler never silently accepts unsupported backend behavior
- lexer/parser/checker fuzzing is continuous
- 1,000+ conformance tests
- Stage 1 compiles its own source
- Stage 2 rebuilds an equivalent compiler artifact
- Stage 0/Stage 2 diagnostics and semantics agree on the bootstrap corpus

## M2 — Alpha native runtime

Deliverables:

- generational GC
- deterministic owned resources
- native backend
- ADT, match, generics, traits
- Result/error propagation
- effects phase 1
- standard collections
- async runtime prototype
- C/Python interoperability
- N-dimensional arrays phase 1
- lawful trait declarations
- small generic proof kernel
- proof certificate format

Exit criteria:

- safe subset memory safety audit
- VM/native differential tests
- benchmark suite against C, Rust, Python, Julia, and Go where meaningful
- Linux/macOS primary support
- generic optimizer rewrites require checked proof evidence
- malformed proof certificates cannot crash or escape the kernel

## M3 — Beta ecosystem

Deliverables:

- package registry
- signed packages and lockfiles
- language server
- debugger/profiler integration
- typed hygienic macros
- scientific stack phase 2
- Windows support
- Wasm/WASI support
- documentation generator
- migration and edition tooling
- external SMT/proof-search adapters that emit kernel-checkable certificates
- cross-package law evidence ABI
- reproducible bootstrap and diverse double compilation

Exit criteria:

- no open P0/P1 compiler correctness bug
- stable package and module model
- production pilot projects
- reproducible builds
- active security response process
- bootstrap provenance can be independently audited
- law-based optimizations have differential and certificate tests

## M4 — 1.0

Deliverables:

- language and runtime stability policy
- stable ABI boundaries where promised
- long-term support plan
- complete specification
- conformance suite
- multi-platform release
- adoption guide
- compatibility and edition process
- audited proof kernel
- audited bootstrap chain

Exit criteria:

- external security audit
- sustained fuzzing without unresolved critical findings
- performance regression gates
- independent production use
- governance and funding model
- fixed-point self-hosting release artifacts reproduced by independent builders

## Performance milestones

### P0 correctness baseline

- interpreter is source of truth
- no unsafe optimization
- unsupported constructs fail explicitly
- bounded law evidence is never treated as a generic proof

### P1 numeric baseline

- unboxed primitive loops
- C/Rust-compatible integer and float semantics
- measured bounds checks
- native math library calls

### P2 allocation baseline

- escape analysis
- stack allocation
- owned reuse
- generational nursery
- collection specialization

### P3 scientific baseline

- contiguous arrays
- broadcasting
- SIMD
- BLAS/LAPACK
- kernel fusion
- parallel execution

### P4 production tuning

- PGO
- LTO
- cross-module specialization
- GC pause targets
- CPU and allocation profiler
- proof-backed algebraic rewrites

## Self-hosting milestones

```text
B0  Stage 0 type-checks Stage 1 source
B1  Stage 1 compiles a useful Kofun Core subset
B2  Stage 1 contains the full frontend
B3  Stage 1 contains safety and law checking
B4  Stage 1 compiles itself
B5  Stage 2 artifact is equivalent
B6  independent reproducible bootstrap
B7  diverse double compilation
```

Current status: the canonical B4 source profile is frozen and audited, and the
repository produces and executes the first generated compiler. B5 remains
open until the `C1/C2/C3` and `A1/A2/A3` equivalence gates pass. This is a
runnable first generation, not a semantic self-hosting fixed point.

## Law verification milestones

```text
L0  bounded exhaustive Monad model                     historical evidence; active gate open
L1  complete finite Bool/Optional[Bool] model           historical evidence; active gate open
L1.5 versioned JSON evidence and assurance build gate  schema/artifacts only; active gate open
L2  user-defined finite ADT enumeration                planned
L3  Functor/Applicative/Monoid families                planned
L4  typed proposition IR                               planned
L5  small proof-term kernel                            planned
L6  external certificate-producing solvers            planned
L7  proof evidence ABI and law-aware optimizer         planned
```

## Backlog mapping

The generated backlog uses:

```text
M0-spec
M1-bootstrap
M2-alpha
M3-beta
M4-1.0
```

27 areas each contain 25 subjects. Every subject has a 20-step lifecycle from requirements through release acceptance. Total: 13,500 issues.
