# Compiler architecture

## Implemented bootstrap

```text
bootstrap/stage1/compiler.kofun
        |
        | audited generated seed
        v
bootstrap/stage1/compiler.c
        |
        | cc -std=c11
        v
kofun-stage1 INPUT.kofun OUTPUT.c
```

The current frontend validates `fn main() { print(EXPR) }`, where `EXPR` is the
documented integer arithmetic Core, and emits deterministic standalone C11.

Three executable checkpoint families extend this path without claiming full
integration:

```text
bootstrap/stage2/compiler.kofun
  -> token-span tape
  -> structural function IR + bounded recovery report
  -> stable Kofun projection
  -> typed Int/Bool/List-index Core parser/lowerer -> checked C11
  -> executable fixtures

bootstrap/native/encoder.kofun
  -> ELF64 headers + x86-64 instruction bytes + abs32/rel32 fixups
  -> Core-shaped static Linux executables
  -> mmap bump heap + fixed/variable immutable List[Int] and heap Text fixtures

stdlib/*.kofun
  -> syscall, strict UTF-8 Text, immutable Bytes, and immutable List contracts
  -> Kofun-authored packed executable fixtures
```

Stage 2 lowers a real but intentionally narrow body grammar: Int/Bool bindings,
unary/arithmetic/comparison and short-circuit expressions, lexical scopes,
mutable assignment, nested `if`/`else` and `while`, `print`, and `return` in one
zero-argument `main`. It also lowers non-empty immutable `List[Int]` literals
and checked runtime indexing. Its parser recovery is capped at eight diagnostics
and a 4096-token synchronization scan. It does not lower its own
Text/List/file-I/O implementation.

The native checkpoint owns deterministic labels and fixups and executes six
ELF images, including a Core call/data rel32 fixture and an allocator/List
fixture. Its heap Text fixture executes runtime concatenation, equality,
structural UTF-8 iteration/length, and code-point indexing. Its variable-length
List v1 fixture executes checked indexing and native map/filter/fold loops with
exact `R023` bounds behavior. It is not registered as a general CLI backend and
is not fed by Stage 2 IR.

The stdlib executable gates prove a file syscall round-trip, strict UTF-8
validation/counting, immutable Bytes operations, and immutable List reference
operations. Their reference values are not yet wired to compiler built-ins or
the native backend.

## Target pipeline

```text
UTF-8 source
  -> lexer
  -> parser
  -> name resolution
  -> type and ownership checking
  -> typed IR
  -> optimization
  -> native / C11 / wasm backend
```

Future compiler components must be implemented in `.kofun`. Generated
bootstrap artifacts require canonical Kofun source, a reproduction command, and
a recorded digest.

## Open integration edges

```text
Stage 2 IR  - - open - -> native encoder
full Stage 2 source - - open - -> Stage 1/Stage 2 equivalent artifact
Text/Bytes/List references - - open - -> built-in storage and backend lowering
```

The Stage 2 typed Core/List-index path and bounded native Text/List operations
are executable, but a checkpoint must not be promoted to the target pipeline
until its general input path and differential behavior are executable. Full
Unicode scalar validation and arbitrary Stage 2-to-native Text/List lowering
remain open.
