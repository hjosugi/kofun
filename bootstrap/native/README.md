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

The x86-64 target also accepts local `Int`/`List[Int]` bindings and a
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
List. AArch64 rejects `List[Int]` and local bindings with explicit diagnostics
instead of emitting scalar code with different semantics.

The x86-64 target also implements immutable UTF-8 `Text` values with the
historical native ABI `[byte length: i64][UTF-8 bytes]`. Literals live in the
read-only image, while concatenation and one-codepoint indexing allocate the
same layout through the mmap runtime. `+`, `==`, `!=`, direct Text printing,
codepoint `len`, and positive or negative codepoint indexing execute in the
generated ELF. Consequently, `len("aé古🌍")` is 4 rather than its UTF-8 byte
length.

`chars` scans its runtime Text value into the existing list ABI with Text
pointers as its elements. Each one-codepoint Text and the List[Text] storage
are allocated by the generated ELF. The closed Core frontend also retains
codepoint metadata for static typing and result validation. A Text index
outside the codepoint range
writes `kofun: text index out of range` to stderr and exits 1. The gate compares
Japanese, accented Latin, and emoji cases with an independent C11 UTF-8
reference and executes the Text OOM and index-failure paths.

The obsolete `tests/kofun/*.kf` acceptance path no longer exists. The active
Python-free `tests/conformance/list` and `tests/conformance/text` corpora are
registered with the native x86-64 adapter and execute all 22 cases. General
Text bindings, calls, and the Stage 1 compiler port are tracked separately by
issue #33. AArch64 List and Text support is a follow-up target boundary and
rejects with an explicit diagnostic today.

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
dynamic section.

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

Native Core currently admits exactly one function, `main`, so one function DIE
is complete for every accepted program. When calls and more functions enter
Native Core, lowering must add one DIE and symbol for each emitted function.
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

The active Stage1 compiler cannot yet compile lists, general function calls, or
file output. Therefore `fixtures/exit_42.rle.kofun` is an intentionally narrow
Stage1-Core bridge. `fixtures/print_sum_42.rle.kofun` is the corresponding
bridge for `elf64_print_sum_image(40, 2)`, and
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

When `qemu-aarch64` is installed, the gate executes both AArch64 differential
fixtures and compares exact status, stdout, and stderr with the Stage 1 C11
reference. Without qemu it reports an explicit execution skip while still
validating deterministic hashes, ELF metadata, encoded instruction bytes, and
x86-64/reference parity.

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
- x86-64 `List[Int]` literal, `len`, and positive/negative indexing lowering;
- x86-64 local `Int`/`List[Int]` bindings with frame-backed variable loads;
- generated `map`, `filter`, and `fold` loops with typed inline Int lambdas;
- raw `mmap` list allocation with defined OOM and bounds diagnostics;
- x86-64 UTF-8 Text literals, concatenation, equality, codepoint `len`, and
  positive/negative codepoint indexing;
- runtime `chars` lowering to allocated List[Text] and Text/Bool printing;
- registered Python-free Text conformance coverage with 9/9 cases executed by
  the native x86-64 adapter;
- registered Python-free List conformance coverage with 13/13 cases executed by
  the native x86-64 adapter;
- two-digit integer-to-ASCII conversion for the fixture result;
- distinct RX and RW mappings;
- three end-to-end Linux x86-64 executable artifact gates;
- opt-in section headers, symbols, and DWARF v4 line/function information for
  arbitrary source accepted by the general x86-64 Native Core CLI.

Still open:

- replacing the audited C11 Native Core driver after lists and calls
  self-compile;
- lowering general calls, conditionals, and non-constant expression trees;
- general signed integer formatting and checked arithmetic diagnostics;
- native stdout/stderr formatting and canonical `R010` diagnostics;
- conditional branches, allocator reuse/reclamation, Mach-O, and additional targets;
- first-class/nested collection lambdas, general collection types, and
  AArch64 lists;
- general Text bindings/calls and the Stage 1 compiler port tracked by #33;
- extending the registered native adapter beyond List/Text and adding AArch64
  List/Text;
- AArch64 debug information and variable/location DIEs.
