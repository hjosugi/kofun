# Native backend

`bootstrap/native/encoder.kofun` implements the direct-native checkpoint:
little-endian encoding, ELF64 and program/section headers, separate RX/RW
segments, immediate moves, Linux syscalls, and generic DWARF v4 metadata.
The Python-free CLI exposes the supported arithmetic Core for x86-64 and
AArch64 Linux. The x86-64 profile additionally lowers local `Int` and
`List[Int]` bindings; List literals, length, indexing, and generated
`map`/`filter`/`fold` loops with typed inline lambdas; plus UTF-8 Text
concatenation, equality, codepoint length, runtime `chars`, and codepoint
indexing. A separate bounded Int profile lowers up to six function arguments,
returns, forward and mutual recursion, comparison-guarded early returns,
checked arithmetic, and signed Int64 output directly:

```sh
./bin/kofun build source.kofun \
  --target x86_64-linux -o build/program
./bin/kofun build source.kofun \
  --target x86_64-linux -g -o build/program-debug
./bin/kofun build source.kofun \
  --target aarch64-linux -o build/program-aarch64
```

`-g` is currently x86-64-only. It adds source-specific `.debug_line`,
`.debug_info`, symbols, and section headers without changing release output or
loaded code/data. The executable gate validates the structures with `readelf`
and, when installed, proves source stepping and a named `main` backtrace with
GDB.

Run:

```sh
sh bootstrap/native/check.sh
```

The remaining native backend work includes:

- general AST/IR lowering and register allocation;
- connecting general Text/List calls and types to Stage 2 (#33);
- local bindings and general control flow inside user-defined functions;
- allocator reuse/reclamation and general raw syscall intrinsic lowering;
- canonical runtime diagnostic codes shared with the C11 backend;
- AArch64 user-defined functions;
- AArch64 List/Text lowering;
- AArch64 debug information and variable-location DIEs;
- unifying the currently separate function, List, and Text profiles.

Unsupported cases must be explicit skips, never implicit passes.
