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

The active host-C profiles pass link inputs as argument-vector entries and
never interpolate them into a shell command. The direct-static native CLI
application compiler does not invoke a host compiler, assembler, linker, or
shell while emitting the final ELF.

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

The Rust crate shim example keeps managed Rust values inside Rust, catches
panics before returning, and uses checked buffer/length/status records.
Vendoring and checksums improve reproducibility but do not make third-party
native code memory-safe from Kofun's perspective.

The bounded native CLI profile validates declaration sizes, command and option
uniqueness, and action shapes before serialization. Its product uses only
Linux `write`, `ioctl`, and `exit` syscalls, but process-provided argument and
environment bytes are still untrusted terminal output. See
`framework/cli/SECURITY.md` for its exact boundary.

## Compile-time law execution

The active compiler does not execute `law monad`; it rejects the syntax with
`E2S02`. There is therefore no active law-evaluator attack surface or release
gate. The removed Stage 0 prototype did evaluate user functions during
compilation, and the controls below record its historical threat model and
requirements for any future replacement under
[#551](https://github.com/hjosugi/kofun/issues/551).

The historical evaluator denied ordinary I/O:

```text
print: denied
debug: denied
clock: denied
process args: denied
file read/write: denied
network: unavailable
```

It also enforced a declared case budget with a default limit of 100,000. It did
not provide a bytecode instruction budget, heap quota, or OS-level sandbox, so
its untrusted law declarations were never a fully isolated workload. A future
active evaluator must re-establish and test these limits before accepting
untrusted law source.

The retained historical evidence scoped trust by assurance:

- `bounded-exhaustive` proves only the declared finite model was traversed.
- `proven-finite` is valid only for a compiler-certified complete finite carrier and complete total-function space.
- `proven` is reserved for a future trusted proof kernel.

The retained JSON evidence includes a source SHA-256 and compiler version, but
it is not digitally signed and is not emitted by the active compiler. Any
future package integration must recompute it or verify signed build provenance
before trusting third-party evidence.

## Bootstrap security

The trusted computing base is the checked-in Kofun sources, C11 seeds and C ABI
compiler, the host C compiler/linker, and the operating system. Stage 1, Stage
2, and C ABI artifact checks are reproducibility gates, not a defense against a
malicious seed and host compiler acting together. Diverse double compilation
remains open in `bootstrap/manifest.json`.
