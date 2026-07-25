# Native backend

`bootstrap/native/encoder.kofun` implements the direct-native checkpoint:
little-endian encoding, ELF64 and program/section headers, separate RX/RW
segments, immediate moves, Linux syscalls, and generic DWARF v4 metadata.
The Python-free CLI exposes the supported arithmetic Core for x86-64 and
AArch64 Linux. Both targets lower local `Int` and `List[Int]` bindings; List
literals, length, indexing, and generated `map`/`filter`/`fold` loops with typed
inline lambdas. Both also lower UTF-8 Text concatenation, equality,
grapheme-cluster length, `chars`, grapheme indexing, and explicit `bytes` /
`codepoints` views. A separate bounded Int profile
lowers up to six function arguments,
returns, forward and mutual recursion, comparison-guarded early returns,
checked arithmetic, and signed Int64 output directly. A `return` whose value is
a direct call is lowered as a branch instead of a call on both targets: a call
to the enclosing function reassigns the parameters and jumps past the prologue,
and a call to any other function restores the registers the body claimed, drops
the frame, and jumps, so it returns straight to this function's caller. Direct
and mutual recursion written that way therefore run in constant stack. It also
lowers `//` and `%` with the floor semantics `docs/SEMANTICS.md` defines, and
lowers `/` with the truncating behavior of the executable seed while the
conflicting normative Float claim remains tracked by #687. Both targets guard
a zero divisor and the one non-representable quotient before dividing, and bind
as many locals as fit the shared 32-slot parameter/local frame, taking a
local's type from its initializer when no annotation is written. That function profile is
shared by both backends: the same target-independent parsed program is lowered
to x86-64 and to AArch64, and both emit the same checked, per-operator
`error[R010]` diagnostic bytes and exit status:

```sh
./bin/kofun build source.kofun \
  --target x86_64-linux -o build/program
./bin/kofun build source.kofun \
  --target x86_64-linux -g -o build/program-debug
./bin/kofun build source.kofun \
  --target aarch64-linux -o build/program-aarch64
```

Both selectors additionally implement a bounded compiler-shaped Text function
bridge: two Text parameters/results through the existing pointer ABI,
direct/forward calls, concatenation, immutable frame locals, and
`print(Text)`. Direct and CLI-produced static ELF artifacts are byte-identical
and compared with an independent C11 reference; AArch64 images are always
built/audited and execute under `qemu-aarch64` when available.

`-g` covers the single-`main` Core on both Linux targets. It adds
source-specific `.debug_line`, `.debug_info`, symbols, and section headers
without changing release output or loaded code/data. Both targets emit one
shared metadata contract: the same sections, the same `main` symbol and DIE,
and the same retained source lines, each at its own instruction addresses. The
executable gate validates the structures with `readelf` for both targets and,
when the tooling is installed, proves source stepping and a named `main`
backtrace with GDB — natively for x86-64 and through the `qemu-aarch64` gdbstub
for AArch64. Missing emulator or debugger tooling skips only the stepping
check. `-g` on the function profile (including a single-main fallback), and on
the AArch64 List/Text aggregate Core, remains an explicit rejection that
writes no artifact.

Run:

```sh
sh bootstrap/native/check.sh
```

The remaining native backend work includes:

- general AST/IR lowering, and register allocation for AArch64 functions;
- accumulator-style loops for recursion that is not already in a returned
  position;
- broader Text/List calls and types beyond the bounded two-target bridge;
- local bindings and general control flow inside user-defined functions;
- allocator reuse/reclamation and general raw syscall intrinsic lowering;
- diagnostic coverage beyond the checked-Int64 `R010` runtime paths;
- variable-location DIEs, multi-function debug information, and AArch64
  List/Text debug rows;
- unifying the currently separate function, List, and Text profiles.

Unsupported cases must be explicit skips, never implicit passes.
