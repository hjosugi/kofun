# Self-hosting and bootstrap

## Active path

```text
compiler.kofun (canonical source)
  + compiler.c (audited trusted seed)
  -> host C11 compiler
  -> Kofun Stage 1
  -> arithmetic Core C11
```

Run:

```sh
sh bootstrap/stage1/check.sh
```

The gate verifies source and seed hashes, builds Stage 1, compiles the Core
fixture, and verifies output `42`. It uses a POSIX shell and C11 compiler; no
Python runtime or source is part of the bootstrap.

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

## Honest status

Stage 1 does not yet parse and lower every construct in its complete own source.
Therefore trusted-seed bootstrap is working, while semantic self-recompile and
the executable Stage 2 fixed point remain open.

The active trusted computing base is the canonical Kofun source, audited C11
seed, host compiler, shell, operating system, and hashing/comparison tools.
