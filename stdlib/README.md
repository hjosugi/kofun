# Kofun standard library seed

This directory is the canonical Kofun implementation of the first freestanding
standard-library boundary. It contains no libc adapter and no implementation in
another programming language.

The current Stage 1 compiler only accepts integer Core programs, so these
modules are not connected to the active C11 bootstrap path yet. The `.kofun`
files are the specification source for Stage 2 and the direct Linux x86-64
backend. Only the declarations marked `trusted intrinsic` are backend
primitives; errno conversion, resource construction, retry policy, and the
public API are implemented in Kofun.

The boundary is deliberately split in two:

- `linux_x86_64/abi.kofun` owns raw syscall numbers, register-width values, and
  the trusted memory/address/value intrinsics required to cross into the kernel.
- the remaining modules expose `SysResult[T]` and affine resources. Raw negative
  kernel returns never escape those modules.

The platform-independent [`testing/`](testing/) checkpoint defines pure,
value-returning assertions for `Int`, `Bool`, and `Text`, plus deterministic
summary helpers. Its canonical ADT surface is not yet connected to executable
Stage 2 code generation; its gate runs an honest Int-Core projection through
the C11 and direct x86-64 backends.

The platform-independent [`logging/`](logging/) checkpoint defines structured
records, explicit level thresholds, typed field-validation errors, and a
deterministic text renderer. It has no ambient clock, global logger, or sink;
its canonical ADT surface is likewise gated by a C11/x86-64 Int-Core
projection until the compiler can lower it directly.

The platform-independent [`toml/`](toml/) checkpoint defines a strict TOML 1.0
flat-document profile with typed scalar values and failures. Its canonical
record API is also beyond current Stage 2 code generation; its gate executes a
deterministic Int-Core projection and documents the unsupported TOML surface.

The platform-independent [`regex/`](regex/) checkpoint defines a small Regex v1
surface for literals, wildcard, repetition, and anchors with typed compile
errors and explicit backtracking costs. Its canonical ADT surface is gated by a
deterministic C11/x86-64 Int-Core projection until the compiler can lower it.

The platform-independent [`clock/`](clock/) checkpoint defines clock-tagged
instants, non-negative durations, checked elapsed/deadline arithmetic, and an
explicit manual clock. Its Linux adapter is the only system-time read; the
focused gate executes a deterministic Int-Core projection of the pure rules.

The platform-independent [`list/`](list/) checkpoint defines an eager,
value-returning `List[Int]` surface with typed empty/index failures and ordered
pipeline contracts. Its gate executes real Lists through direct x86-64 plus a
compact checked-access projection through both C11 and x86-64.

The platform-independent [`vector/`](vector/) checkpoint defines an immutable,
ordered `IntVector` reference surface with explicit List conversion and typed
checked indexing. Its gate executes real List-backed behavior through direct
x86-64 plus a compact checked-index projection through C11 and x86-64.

The platform-independent [`tuple/`](tuple/) checkpoint defines immutable,
ordered `Tuple[Int, Int]` helpers for construction, fixed-position access,
transforms, folds, and comparison. Its gate executes an honest two-scalar
projection through both C11 and direct x86-64 until tuple lowering exists.

The platform-independent [`set/`](set/) checkpoint defines an eager,
value-returning `SetInt` surface with unique membership, total algebraic
operations, and deterministic ascending traversal. Its canonical record is
gated by a finite C11/x86-64 semantic projection until Set lowering exists.

The platform-independent [`map/`](map/) checkpoint defines an immutable
Int-to-Int map with typed missing-key lookup, value-returning upsert/removal,
and deterministic ascending-key traversal. Its canonical record is gated by a
finite C11/x86-64 semantic projection until Map lowering exists.

The platform-independent [`json/`](json/) checkpoint defines a strict JSON v1
value codec with lossless number lexemes, ordered unique object members, typed
failures, and bounded nesting. Its recursive ADT surface is gated by a
deterministic Int-Core projection and explicitly does not yet decode `\uXXXX`.

Run the repository-local, Python-free contract checks with:

```sh
sh stdlib/tests/verify.sh
```

The checks validate the syscall table, the trusted-surface boundary, ownership
signatures, errno conversion, and an executable file round-trip. The native
gate builds Kofun-authored ELF bytes, runs `open`/`write`/`lseek`/`read`/
`close`, verifies the read-back bytes, and cleans up its fixture without a
Python or C implementation.
