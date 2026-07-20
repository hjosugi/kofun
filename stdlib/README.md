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

Run the repository-local, Python-free contract checks with:

```sh
sh stdlib/tests/verify.sh
```

The checks validate the syscall table, the trusted-surface boundary, ownership
signatures, errno conversion, and an executable file round-trip. The native
gate builds Kofun-authored ELF bytes, runs `open`/`write`/`lseek`/`read`/
`close`, verifies the read-back bytes, and cleans up its fixture without a
Python or C implementation.
