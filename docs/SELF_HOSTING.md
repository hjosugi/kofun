# Self-hosting and bootstrap

## Active path

```text
bootstrap/stage1/compiler.kofun (canonical source S)
  + bootstrap/stage1/compiler.c (audited trusted seed)
  + declared host C11 compiler
  -> Kofun Stage 1 seed
  -> arithmetic Core C11 and executable
```

Run:

```sh
sh bootstrap/stage1/check.sh
```

The gate verifies source and seed hashes, builds Stage 1, compiles the Core
fixture, and verifies output `42`. It uses a POSIX shell and C11 compiler; no
Python runtime or source is part of the bootstrap.

The exact feature surface of `S` is frozen in
`bootstrap/selfhost/profile.tsv`. Run `make selfhost-profile` to verify its
SHA-256, derive the source inventory, and reject missing or stale coverage
rows. Reusing the existing Stage 1 source avoids creating two competing
canonical compilers.

## Stage 2 frontend checkpoint

`bootstrap/stage2/compiler.kofun` implements lexical scanning, token spans,
top-level function structure, deterministic textual IR, a parse-gated identity
emitter, and bounded multi-function `Int` Core C11 lowering. The lowerer
supports parameters, results, recursion, and forward references. Its audited
C11 seed round-trips the Stage 1 compiler, the Stage 2 frontend, and a fixture.
Repeating the projection produces identical source, token tape, and IR.

Run:

```sh
sh bootstrap/stage2/check.sh
```

This is a deterministic frontend boundary, not the Stage 2 fixed point. It
lowers the dedicated Int Core fixture, but cannot yet lower the Text, List,
file-I/O, and control-flow surface used by its own source or reproduce its
executable seed.

## First executable fixed point

Generated C11 is an allowed bootstrap artifact for B4/B5. The exact chain is:

```text
trusted seed(S) -> C1 -> normalized host cc -> A1
A1(S)           -> C2 -> normalized host cc -> A2
A2(S)           -> C3 -> normalized host cc -> A3
```

The gate must compare `C1`, `C2`, and `C3` byte for byte, then compare `A1`,
`A2`, and `A3` byte for byte. It must also record the source, seed, target,
host-compiler identity, flags, environment normalization, commands, and
digests.

Direct-native reproduction is a separate strengthening track. It is desirable
for removing the host C compiler from the trusted path, but does not block the
first C11 fixed point. The native track keeps x86-64 and AArch64 parity visible
from the start.

## Honest status

Stage 1 does not yet parse and lower every construct in `S`. Therefore the
trusted-seed bootstrap and frozen-profile gate are working, while semantic
self-recompile and the executable Stage 2 fixed point remain open.

The active trusted computing base is the canonical Kofun source, audited C11
seed, host compiler, shell, operating system, and hashing/comparison tools.
