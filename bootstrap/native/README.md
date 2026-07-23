# Direct native bootstrap

This directory advances issues #14 and #33 with a Kofun-owned, Python-free
native path. `encoder.kofun` is the canonical instruction and image
implementation. It constructs little-endian integers, x86-64 and AArch64
instruction bytes, target-parameterized ELF64 headers, and two `PT_LOAD`
program headers directly. It does not emit assembly or invoke a linker.

## AArch64 Native Core v1

The CLI compiles the same deliberately small, target-independent Core through
both registered Linux targets:

```kofun
fn main() {
    print((6 + 1) * 6)
}
```

```sh
./bin/kofun build program.kofun \
    --target aarch64-linux -o build/program-aarch64
./bin/kofun build program.kofun \
    --target x86_64-linux -o build/program-x86_64
```

The shared scalar Native Core accepts exactly one zero-argument `main`, one
`print`, integer literals in `0..65535`, parentheses, `+`, and `*`. Checked
constant analysis must prove every known intermediate value fits that range
and a statically known final integer is two digits. Unsupported input fails
before an output file is written.

## Native user-defined Int functions

Both native backends accept a bounded multi-function Int Core:

```kofun
fn fib(n: Int) -> Int {
    if n < 2 {
        return n
    }
    return fib(n - 1) + fib(n - 2)
}

fn main() {
    print(fib(20))
}
```

Top-level declarations are collected before bodies are parsed, so forward and
mutual recursion do not depend on source order. Calls support up to six integer
argument registers (`rdi..r9` on x86-64, `x0..x5` on AArch64). Parameters are
stored in frame slots, returns come back in the first result register (`rax` /
`x0`), and every call is resolved as a checked fixup (`rel32` on x86-64, a
26-bit `bl` immediate on AArch64). Both backends lower the same
target-independent parsed program, so the profile supports Int literals,
parameters, direct calls, unary `-`, `+`, `-`, `*`, integer comparisons,
`if { return ... }` guards, expression statements, multiple `print(Int)`
statements in `main`, and explicit or implicit main returns identically.

Runtime Int output covers zero, negative values, and the complete signed
64-bit decimal width. Arithmetic branches to a deterministic overflow
diagnostic — the same `kofun: integer overflow` message and exit status on both
targets; AArch64 detects multiply overflow with a `smulh`/sign-bit comparison
and add/sub/negate overflow through the `V` flag. Unknown functions, duplicate
declarations or parameters, wrong arity, more than six arguments, non-Int
signatures, missing helper returns, and `-g` are rejected before an artifact is
written. `-g` debug information remains x86-64-only.

`tests/conformance/functions` runs the same five programs under the C11 and
direct x86-64 adapters, covering ordinary/forward calls, recursion, mutual
recursion, signed/zero output, and the six-argument boundary. The native gate
additionally rebuilds the `fibonacci` example and the checked-overflow fixture
for AArch64 and, when `qemu-aarch64` is installed, executes them and asserts the
output, diagnostic, and exit status match the x86-64 observations byte for byte.

Both Linux targets also accept local `Int`/`List[Int]` bindings and a
deliberately narrow collection Core:

```kofun
fn main() {
    let values = [1, 2, 3]
    let mapped = map(values, fn(value: Int) => value * 21)
    print(mapped[1])
}
```

List values use the historical native ABI
`[length: i64][element: i64] * length`. Literals allocate writable storage
through a raw Linux `mmap` runtime, indexing executes against that storage,
negative indexes count from the end, and `len` reads the header. An invalid
index writes `kofun: list index out of range` to stderr and exits 1. Allocation
failure writes `kofun: out of memory` and exits 70. `map`, `filter`, and
`fold` execute real generated loops over that storage. `map` allocates an
output List, `filter` records its actual result length, and `fold` carries a
runtime accumulator. Their typed inline `fn` lambdas may use integer
arithmetic, comparisons, parameters, and captured Core locals. The gate
compares bindings and every operation, including their composition, with an
independent C11 implementation.

This remains a closed collection Core rather than general collection
lowering: lambdas are inline and non-escaping, nested higher-order operations
inside a lambda body are rejected, and allocation uses one mmap chunk per
List. x86-64 and AArch64 consume the same parsed expression tree and preserve
the same frame-slot, value-layout, bounds, allocation, and output contracts.

The x86-64 target also implements immutable UTF-8 `Text` values with the
historical native ABI `[byte length: i64][UTF-8 bytes]`. The closed native Core
contains constant Text expressions, so its frontend applies the pinned Unicode
17 database and emits the resulting literal or scalar directly. `+`, `==`,
`!=`, direct Text printing, grapheme-cluster `len`, `chars`, and positive or
negative grapheme indexing execute in the generated ELF. Concatenation is
re-segmented across the join.

`chars` exposes extended grapheme clusters. `bytes` exposes UTF-8 byte values,
and `codepoints` exposes Unicode scalars, both through the existing list ABI.
A Text index outside the grapheme range
writes `kofun: text index out of range` to stderr and exits 1. The gate compares
Arabic, Hebrew, Hindi, Thai, Japanese, Hangul/Jamo, accented Latin, and complex
emoji cases and executes the Text OOM and index-failure paths.

The obsolete `tests/kofun/*.kf` acceptance path no longer exists. The active
Python-free `tests/conformance/list` and `tests/conformance/text` corpora are
registered with the native x86-64 adapter and execute all 34 cases. The List
corpus is also registered with the AArch64 adapter and executes all 13 cases
under qemu. General Text bindings, calls, and the Stage 1 compiler port are
tracked separately by issue #33. AArch64 Text remains an explicit target
boundary and rejects before artifact creation.

The frontend creates one AST; both instruction selectors consume it. The
equivalent canonical Kofun representation is a postfix stream of
`[opcode, operand]` pairs consumed by `x64_native_core_text` and
`a64_native_core_text`. This keeps parsing, precedence, constant validation,
and Core semantics out of the target encoders.

The AArch64 encoder writes 64-bit `MOVZ`, register `ADD`/`MUL`, `UDIV`, `MSUB`,
`STRB`, `MOVK`, and `SVC` instructions as little-endian words. Runtime code
computes the expression, converts the result to ASCII in the RW segment, calls
Linux AArch64 `write` (64), and calls `exit` (93). The ELF header uses
`EM_AARCH64` (`e_machine = 183`), entry `0x4000b0`, and no interpreter or
dynamic section. The function profile adds a stack-machine lowering that also
emits `STP`/`LDP` frames, `BL`/`RET` calls, `STUR`/`LDUR` parameter slots,
`CBZ` guards, flag-setting `ADDS`/`SUBS`/`SUBS(neg)` with `B.VS`, and a
`SMULH`/`ASR`/`B.NE` multiply-overflow check; every fixed instruction word was
cross-checked against `llvm-mc --triple=aarch64`.
The List profile additionally emits frame-backed locals, stack temporaries,
indexed 64-bit loads/stores, generated loop branches, and a leaf Linux AArch64
`mmap` helper. Its failure fixups target exact OOM and list-index diagnostics.

`core_compiler.c` is the audited C11 bootstrap driver used until the Kofun
compiler can self-compile the complete `List[Int]` encoder. It shares one
frontend across both targets and uses no Python, assembler, linker, or target
cross-compiler when compiling a program.

The deterministic fixture is a 188-byte static `ET_EXEC` image:

```text
0x0000..0x003f  ELF64 header
0x0040..0x0077  RX PT_LOAD (R|X)
0x0078..0x00af  RW PT_LOAD (R|W, zero-filled)
0x00b0..0x00bb  mov eax,60; mov edi,42; syscall
```

The entry point is `0x4000b0`. The RX segment maps the whole 188-byte file at
`0x400000`. The separate RW segment reserves one zero-filled page at
`0x401000`; it has no file bytes. Both segments use 4096-byte alignment.

The second fixture is a 4099-byte image that exercises a compilation-shaped
path rather than only process exit:

```text
Kofun inputs       left=40, right=2
native arithmetic eax = 40 + 2
native conversion div 10; add ASCII '0' to quotient and remainder
absolute fixups    store the two digits at the RW `output` label
raw syscall        write(1, output, 3)
raw syscall        exit(0)
observable result  exact stdout "42\n", empty stderr, status 0
```

Its RX segment ends at file offset `0x00f6`. Its RW segment maps the three
initialized bytes at file offset `0x1000` to virtual address `0x401000` and
reserves one page.

The third fixture is a compact 231-byte Core-shaped image:

```text
Core expression     (6 + 1) * 6
forward call fixup  _start -> main
message fixup       RIP-relative lea -> "42\n"
observable result   exact stdout "42\n", empty stderr, status 42
```

Unlike the page-backed print fixture, this image keeps its message in the RX
segment. It demonstrates deterministic multi-label rel32 resolution for both a
forward `call` and a RIP-relative data reference.

## Opt-in x86-64 debug information

The general Native Core CLI accepts `-g` for x86-64 Linux:

```sh
./bin/kofun build source.kofun \
  --target x86_64-linux -g -o build/program
```

The frontend retains source lines on parsed expression nodes. x86-64 lowering
records the exact instruction offset for each distinct line, then the shared
metadata builder appends non-allocating ELF sections:

```text
.text           executable Core code
.data           output buffer
.debug_abbrev   DWARF v4 compile-unit and subprogram declarations
.debug_info     source-specific Kofun compilation unit and `main` function DIE
.debug_line     emitted instruction addresses mapped to parsed source lines
.debug_str      producer, source path, and function name
.symtab/.strtab `main` function symbol
.shstrtab       ELF section names
```

Without `-g`, the compiler follows the original release path: a 4,099-byte
static ELF with no section table. The gate compares the release artifact to its
pre-debug SHA-256 and compares every loaded byte after the ELF header with the
debug image. Debug metadata therefore cannot change the executable code, data,
entry point, or `PT_LOAD` layout.

`core_compiler.c` mirrors the generic `dwarf_debug_*_for` builder in canonical
`encoder.kofun`; it does not maintain a second fixture-specific DWARF layout.
The compact historical fixture remains available as an independent metadata
regression:

```sh
sh bootstrap/native/emit-fixture.sh \
  -o build/core-answer-release
sh bootstrap/native/emit-fixture.sh \
  -g -o build/core-answer-debug
```

The first command reproduces the unchanged 231-byte release image with no
section headers. The `-g` command emits a 1,360-byte image with DWARF while
leaving the executable code, read-only message, entry point, and segment layout
unchanged. All layout and DWARF bytes are authored by `encoder.kofun`; the
current Stage1-compatible packed bridge only transports those bytes until
Stage1 can compile the encoder's list operations.

`check.sh` requires `readelf` and validates every section, the source path,
`main` `DW_TAG_subprogram`, symbol, and exact line rows of a CLI-built program.
When `gdb` is installed, a batch session breaks at `main`, shows Kofun line 3,
steps to line 4, and verifies a named Kofun `main` backtrace.

Debug Native Core currently admits exactly one function, `main`, so one
function DIE is complete for every debug-accepted program. The multi-function
Int profile rejects `-g` until lowering adds one DIE and symbol for each emitted
function.
`-g` on AArch64 is rejected explicitly; AArch64 and Mach-O debug formats are
separate future work.

## Labels and fixups

`encoder.kofun` owns fixup resolution. `patch_u32_le` deterministically rebuilds
an image with one little-endian field replaced. `resolve_abs32_fixup` resolves
an image-base-relative label plus addend; `resolve_rel32_fixup` resolves a
signed PC-relative displacement. `resolve_rel32_fixups` validates label and
fixup tables before applying a deterministic sequence of relocations.

The page-backed print fixture uses three absolute fixups: the tens store, the
ones store, and the buffer argument to `write`. The compact Core fixture
executes both forward rel32 fixups and checks their resolved bytes.

The x86-64 encoder also provides a rel32 jump placeholder for the next
control-flow lowering step. No assembler or linker participates in any
fixture.

## Executable gate

The compatibility Stage 1 seed cannot compile lists, general function calls, or
file output. The Stage 2 C11 Core now handles bounded Int function calls, but
does not compile this List-heavy encoder. Therefore
`fixtures/exit_42.rle.kofun` is an intentionally narrow Stage1-Core bridge.
`fixtures/print_sum_42.rle.kofun` is the corresponding bridge for
`elf64_print_sum_image(40, 2)`, and
`fixtures/core_answer.rle.kofun` bridges `elf64_core_answer_image()`. Each
emits a run-length-encoded byte stream whose expansion is exactly the image
returned by its canonical encoder function.

`check.sh` compiles and runs the release bridges and debug packed bridge with
Kofun, transports their numeric streams to raw bytes using POSIX shell, compiles
Native Core v1 through both CLI targets, checks image hashes and ELF/DWARF
metadata, inspects resolved fixups and instructions, and executes every
host-compatible result:

```sh
sh bootstrap/native/check.sh
```

When `qemu-aarch64` (or `qemu-aarch64-static`) is installed, the gate executes
the AArch64 scalar, function, and List differentials and compares exact status,
stdout, and stderr with the C11/x86-64 observations. Without qemu it reports an
explicit execution skip while still building List success/failure images
twice and validating deterministic bytes, ELF metadata, encoded instructions,
and x86-64/reference parity.

The shell does not choose headers or instructions. Those values are authored
in Kofun and mirrored by the audited bootstrap seed. No Python, assembly, or
linker participates.

## Honest boundary

Implemented here:

- deterministic ELF64 and program-header byte encoding in Kofun;
- x86-64 `mov r32, imm32` and `syscall` encoders;
- deterministic absolute and PC-relative label/fixup resolution;
- raw `write(1, address, length)` and `exit(status)` sequences;
- a shared Native Core parser/AST for x86-64 and AArch64 Linux;
- direct AArch64 instruction encoding and static `EM_AARCH64` ELF output;
- the public `build --target aarch64-linux` CLI path;
- native lowering of the fixture expressions `40 + 2` and `(6 + 1) * 6`;
- direct x86-64 and AArch64 Int function calls with up to six arguments,
  results, forward references, recursion, mutual recursion, and
  comparison-guarded returns from one shared parsed program;
- checked `+`, `-`, `*`, and unary negation in the function profile, with an
  identical overflow trap on both targets;
- general signed Int64 decimal output for function-profile `print`;
- registered function conformance coverage with 5/5 cases executed by both
  the C11 and native x86-64 adapters, plus the `fibonacci` and checked-overflow
  fixtures executed on AArch64 under `qemu-aarch64` and matched against the
  x86-64 output;
- x86-64 and AArch64 `List[Int]` literal, `len`, and positive/negative indexing
  lowering;
- x86-64 and AArch64 local `Int`/`List[Int]` bindings with frame-backed
  variable loads;
- generated `map`, `filter`, and `fold` loops with typed inline Int lambdas on
  both Linux targets;
- raw target-specific Linux `mmap` list allocation with identical OOM and
  bounds diagnostics;
- x86-64 UTF-8 Text literals, concatenation, equality, grapheme `len`, and
  positive/negative grapheme indexing;
- `chars`, explicit byte/codepoint views, and Text/Bool printing;
- registered Python-free Text conformance coverage with 21/21 cases executed by
  the native x86-64 adapter;
- registered Python-free List conformance coverage with 13/13 cases executed by
  the native x86-64 and, under qemu, AArch64 adapters;
- two-digit integer-to-ASCII conversion for the shared scalar fixture;
- distinct RX and RW mappings;
- three end-to-end Linux x86-64 executable artifact gates;
- opt-in section headers, symbols, and DWARF v4 line/function information for
  arbitrary source accepted by the general x86-64 Native Core CLI.

Still open:

- replacing the audited C11 Native Core driver after lists and calls
  self-compile;
- local bindings and general statement/control-flow lowering inside
  user-defined functions;
- native stdout/stderr formatting and canonical `R010` diagnostics;
- conditional branches, allocator reuse/reclamation, Mach-O, and additional targets;
- first-class/nested collection lambdas and general collection types;
- general Text bindings/calls and the Stage 1 compiler port tracked by #33;
- AArch64 UTF-8 Text and `chars` lowering tracked by #630;
- AArch64 debug information and variable/location DIEs.
