# Roadmap

## Release rule

Kofun advances milestones by correctness gate, not by feature count.

- never silently fall back on unsupported behavior
- attach a negative test and a threat model to every safety claim
- attach a reproducible benchmark to every performance claim
- distinguish the strength of law evidence as `bounded`, `proven-finite`, or `proven`
- do not call the project "self-hosting complete" before the Stage 2 fixed point

## Current critical-path order

The roadmap does not schedule frontier features ahead of the two P0 language
blockers:

1. keep the bounded C11/direct-x86-64 user-function gate green while widening
   its type and control-flow coverage
   ([#549](https://github.com/hjosugi/kofun/issues/549); first executable slice
   complete)
2. add a heterogeneous product type suitable for tokens and AST nodes
   ([#546](https://github.com/hjosugi/kofun/issues/546))
3. generalize the existing Monad-specific checker into declared algebraic laws
   ([#551](https://github.com/hjosugi/kofun/issues/551))

Advanced effects, dependent or refinement types, concurrency, and an optional
second backend remain after this sequence. The evidence and keep/defer/reject
decisions are indexed in the
[README project-status section](../README.md#measured-project-status).

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
- compiler-integrated finite Monad law checker prototype
- bootstrap stage manifest

Exit criteria:

- no unresolved P0 ambiguity in the core syntax
- ownership examples can be explained without Rust experience
- null/optional behavior is fixed
- the coding interview sample set is complete
- law assurance labels do not mislead
- bootstrap status can be verified machine-readably

Current Stage 0 achievements:

- Kofun-written Python-free arithmetic Core compiler seed
- affine ownership prototype
- `law monad` bounded exhaustive checker
- complete Bool/Optional[Bool] function-space enumeration
- versioned JSON law evidence and assurance gates
- Kofun-written Stage 1 arithmetic compiler seed built as native code by Stage 0
- interpreted/native Stage 1 fixture-output equivalence
- 13,500 generated implementation issues

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

Current status: `B1` plus a Stage 0-to-native-Stage 1 differential gate. `B4` and `B5` are open.

## Law verification milestones

```text
L0  bounded exhaustive Monad checking                  implemented
L1  complete finite Bool/Optional[Bool] proof          implemented
L1.5 versioned JSON evidence and assurance build gate  implemented
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
