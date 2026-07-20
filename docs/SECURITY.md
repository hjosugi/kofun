# Security model

## Safe-by-default contract

Safe Kofun code must not produce:

- use-after-free
- double free
- invalid mutable aliasing
- data race
- uninitialized read
- unchecked null dereference
- unchecked out-of-bounds access
- silent integer narrowing
- arbitrary compile-time process execution

Stage 0 is a UX prototype and does not yet constitute a complete proof of this contract.

## Compiler threat model

Untrusted source may attempt:

- parser stack exhaustion
- exponential type inference
- macro resource exhaustion
- path traversal
- generated filename collision
- backend command injection
- malicious debug metadata
- cache poisoning
- package dependency confusion

Mitigations:

- iterative or bounded parser paths
- inference budgets and cycle detection
- sandboxed macro runtime
- canonical path checks
- no shell invocation for compiler subprocesses
- content-addressed caches
- checksummed lockfiles
- structured backend invocation
- fuzzing and corpus testing

The Stage 0 C compiler invocation uses an argument vector through `subprocess.run`, not a shell command string.

## Runtime threat model

- allocation denial of service
- GC pause amplification
- adversarial hashing
- regex denial of service
- unbounded recursion
- task explosion
- deadlock
- unsafe FFI
- finalizer abuse

Runtime profiles expose limits for heap, stack, tasks, macro instructions, and execution time where feasible.

## Package security

planned defaults:

- lockfile checksums
- registry TLS and signed metadata
- package signatures as an additional signal
- dependency source shown in lockfile
- namespace conflict defense
- no install-time arbitrary script by default
- capability declaration for build plugins
- offline and vendor modes
- reproducible build metadata
- SBOM generation

## Macro security

Default macro capabilities:

```text
filesystem: declared inputs only
network: denied
process: denied
clock: denied
random: deterministic seed only
memory: bounded
instructions: bounded
```

## FFI

FFI is a trust boundary.

- ABI-safe types only across default C boundary
- explicit ownership annotations
- GC handles instead of raw managed pointers
- callback lifetime tracked
- foreign exceptions cannot cross unchecked
- thread attachment required before accessing runtime
- sanitizer build profiles

## Reporting

A production project must publish:

- security contact
- encrypted reporting path
- response targets
- supported versions
- CVE process
- disclosure policy

The bounded C ABI profile is executable, but the broader FFI policy above
remains target design. Foreign libraries and their transitive dependencies are
trusted native code; no operational security team is implied.

## Compile-time law execution

`law monad` checking evaluates user functions during compilation, so it is a compiler attack surface.

Stage 0 runs law checking with ordinary I/O disabled:

```text
print: denied
debug: denied
clock: denied
process args: denied
file read/write: denied
network: unavailable
```

It also enforces a declared case budget with a default limit of 100,000. Stage 0 does not yet provide a bytecode instruction budget, heap quota, or OS-level sandbox, so untrusted law declarations must not be treated as fully isolated.

Evidence trust is scoped by assurance:

- `bounded-exhaustive` proves only the declared finite model was traversed.
- `proven-finite` is valid only for a compiler-certified complete finite carrier and complete total-function space.
- `proven` is reserved for a future trusted proof kernel.

The JSON evidence includes a source SHA-256 and compiler version, but it is not digitally signed. Package registries must recompute or verify signed build provenance before trusting third-party evidence.

## Bootstrap security

The active bootstrap contains no Python runtime or source. Its trusted
computing base is the checked-in Kofun sources, C11 seeds and C ABI compiler,
the host C compiler/linker, and the operating system. Stage 1, Stage 2, and C
ABI artifact checks are reproducibility gates, not a defense against a
malicious seed and host compiler acting together. Diverse double compilation
remains open in `bootstrap/manifest.json`.
