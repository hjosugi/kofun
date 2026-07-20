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
emitter, and bounded recovery. Its audited C11 seed round-trips the Stage 1
compiler, the Stage 2 frontend, and a fixture. Repeating the projection produces
identical source, token tape, and IR.

For a deliberately small user-program slice, the same checkpoint performs real
statement and precedence-aware expression parsing and lowers one zero-argument
`main` to deterministic checked C11. The gate compiles and executes immutable
and mutable integer bindings, unary and arithmetic expressions, floor `//` and
`%`, Int/Bool comparisons, `!`, short-circuit `&&`/`||`, nested `if`/`else` and
`while`, lexical scopes, assignment, non-empty immutable `List[Int]` literals,
checked runtime indexing, `print`, and `return`; it also checks exact
division-by-zero and `R023` bounds behavior, type/mutability/scope diagnostics,
and explicit rejection of structurally valid non-Core source.

Recovery is observable and bounded: it synchronizes at column-zero `fn`
boundaries, scans at most 4096 tokens per synchronization, records at most eight
diagnostics, always advances, and preserves later valid function IR. Normal
error compilation writes no output artifacts.

Run:

```sh
sh bootstrap/stage2/check.sh
```

This is a deterministic frontend and typed Core lowering boundary, not the
Stage 2 fixed point. It does not lower the Text, `List[Text]`, file-I/O, and
other constructs needed by its complete compiler source or reproduce its
executable seed.

## Native and stdlib checkpoints

`bootstrap/native/check.sh` and `stdlib/tests/verify.sh` extend executable
coverage without changing the self-hosting criterion. Kofun-authored fixture
streams produce ELF64 images that exercise rel32 Core call/data fixups,
`mmap`-backed allocation and `List[Int]`, raw file syscalls, strict UTF-8
scanning, immutable Bytes operations, and heap Text concatenation/equality/
UTF-8 length/index/iteration. A variable-length immutable List image additionally
executes checked indexing and native map/filter/fold loops; the stdlib gate
executes the matching pure-Kofun List reference contract.

These prove bounded backend/runtime behaviors. They are not a general native
backend, do not compile the whole compiler, and do not wire the Text/Bytes
or List references into built-in runtime representations or general Stage 2
lowering.

## Honest status

Stage 1 does not yet parse and lower every construct in its complete own source.
Therefore trusted-seed bootstrap is working, while semantic self-recompile and
the executable Stage 2 fixed point remain open.

The executable Int/Bool/List-index fixture proves only the documented typed
Core subset; it is not a general parser or type checker. The native heap Text
and List fixtures are also bounded: full Unicode scalar validation,
arbitrary-size Text, slices, normalization, generic List built-ins, callbacks,
and general Stage 2-to-native lowering remain open.

The active trusted computing base is the canonical Kofun source, audited C11
seeds, checked-in Kofun-authored fixture streams, host compiler, shell,
operating system, and hashing/comparison tools. Reducing that boundary requires
Stage 1 to compile the full Stage 2 source and reproduce an equivalent artifact.
