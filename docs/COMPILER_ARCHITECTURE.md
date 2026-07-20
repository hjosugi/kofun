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

Two executable checkpoints extend this path without claiming full integration:

```text
bootstrap/stage2/compiler.kofun
  -> token-span tape + structural function IR + stable Kofun projection
  -> bounded multi-function Int Core C11 lowering

bootstrap/native/encoder.kofun
  -> ELF64 headers + x86-64 instruction bytes
  -> static Linux executable
```

The Stage 2 checkpoint lowers a bounded `Int` Core with parameters, results,
recursion, and forward references. It does not lower its own Text/List/file-I/O
implementation. The native checkpoint is registered for explicit Linux
targets. Its x86-64 Int profile executes parameters, results, forward and mutual
recursion, guarded returns, and checked arithmetic directly; the shared
x86-64/AArch64 scalar profile and the x86-64 List/Text profiles remain separate
bounded frontends.

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
