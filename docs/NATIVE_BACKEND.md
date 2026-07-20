# Native backend

`bootstrap/native/encoder.kofun` implements the direct-native checkpoint:
little-endian encoding, ELF64 and program/section headers, separate RX/RW
segments, immediate moves, Linux syscalls, and generic DWARF v4 metadata.
The Python-free CLI exposes the supported arithmetic Core for x86-64 and
AArch64 Linux. The x86-64 profile additionally lowers `List[Int]` literals,
length, and indexing plus UTF-8 Text concatenation, equality, codepoint length,
runtime `chars`, and codepoint indexing:

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
- connecting general Text/List bindings and calls to Stage 2 (#33);
- allocator reuse/reclamation and general raw syscall intrinsic lowering;
- checked `Int64` operations and canonical runtime diagnostics;
- AArch64 List/Text lowering;
- AArch64 debug information and variable-location DIEs;
- expanding the registered native adapter beyond the active Text corpus.

Unsupported cases must be explicit skips, never implicit passes.
