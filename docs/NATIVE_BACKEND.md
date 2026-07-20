# Native backend

`bootstrap/native/encoder.kofun` now implements the first direct-native
checkpoint: little-endian encoding, ELF64 and program headers, separate RX/RW
segments, immediate moves, and Linux x86-64 `syscall`. Its executable gate
builds a deterministic 188-byte image and verifies that it exits with status
42. The current CLI still uses the C11 bootstrap backend and reports that
limitation.

Run:

```sh
sh bootstrap/native/check.sh
```

The remaining native backend work includes:

- deterministic label and fixup resolution;
- general AST/IR lowering and register allocation;
- connecting the Kofun `List[Int]` encoder to Stage 2;
- raw syscall intrinsic lowering and allocation support;
- checked `Int64` operations and canonical runtime diagnostics;
- registration in the shared differential corpus.

Unsupported cases must be explicit skips, never implicit passes.
