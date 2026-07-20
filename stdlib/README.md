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

Run the repository-local, Python-free contract checks with:

```sh
sh stdlib/tests/verify.sh
```

The checks validate the syscall table, the trusted-surface boundary, ownership
signatures, errno conversion, and an executable file round-trip. The native
gate builds Kofun-authored ELF bytes, runs `open`/`write`/`lseek`/`read`/
compare/`close`, verifies the six-byte result, and cleans up its fixture. A
forced `-EISDIR` open proves the negative raw return exits 1 rather than becoming
a descriptor.

`text/utf8.kofun` adds the pure-Kofun strict UTF-8 reference for Issue #484.
The same verification command executes a Kofun-authored native scanner over
ASCII, Japanese, Arabic, Hindi, emoji, and malformed byte fixtures.

`bytes/reference.kofun` adds the pure-Kofun immutable Bytes reference for Issue
#485. Its Kofun-authored native gate executes validation, bounds, slice,
concat/equality, and checked little-endian read/write behavior.

`list/reference.kofun` adds the pure-Kofun immutable `List[Int]` reference for
Issue #8. It defines the `+0` Int64 length and `+8` contiguous Int64 item
layout, `align16(8 + 8 * length)` allocation, typed bounds/range/length errors,
and validation, length, get, slice, concat, equality, map, filter, and fold.
Its Kofun-authored packed ELF executes the main operations and exact `R023`
bounds and `R024` length-overflow paths. The verifier's shell code only
transports byte words and compares observations; no assembler or linker
participates in the gate.

Successful output currently contains six independent checkpoints:

```text
stdlib contract: PASS
stdlib Stage 1 errno Core: PASS
stdlib native file round-trip: PASS
stdlib Text UTF-8 native scan: PASS
stdlib Bytes native reference: PASS
stdlib immutable List[Int] native reference: PASS
```

These are executable reference/ABI gates, not complete built-in wiring.
`Utf8Text`, `BytesValue`, and `ImmutableIntList` still use reference
representations, and Stage 2 does not compile their ADTs, records,
higher-order callbacks, and list-heavy implementations. Native Text is not
completed by the UTF-8 scanner. Generic `List[T]` built-in wiring, general
compiler/native lowering, ownership, mutable builders, and reclamation/GC
remain open.
