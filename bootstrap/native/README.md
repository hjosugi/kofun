# Direct native bootstrap

This directory advances issue #33 with a Kofun-owned, Python-free first native
artifact. `encoder.kofun` is the canonical implementation. It constructs
little-endian integers, x86-64 instruction bytes, ELF64 headers, and two
`PT_LOAD` program headers directly. It does not emit assembly or C.

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

`check.sh` compiles and runs all three bridges with Kofun, transports their decimal
RLE streams to raw bytes using POSIX shell, checks image hashes and ELF
metadata, inspects the resolved rel32 fields, and executes all results:

```sh
sh bootstrap/native/check.sh
```

The shell does not choose headers or instructions; those values are authored
in Kofun. No C implementation of the ELF writer is maintained here. The C file
created under `build/native-check/` is only the current Stage1 bootstrap
artifact used to execute the Core bridge.

## Honest boundary

Implemented here:

- deterministic ELF64 and program-header byte encoding in Kofun;
- x86-64 `mov r32, imm32` and `syscall` encoders;
- deterministic absolute and PC-relative label/fixup resolution;
- raw `write(1, address, length)` and `exit(status)` sequences;
- native lowering of the fixture expressions `40 + 2` and `(6 + 1) * 6`;
- two-digit integer-to-ASCII conversion for the fixture result;
- distinct RX and RW mappings;
- three end-to-end Linux x86-64 executable artifact gates.

Still open:

- connecting `encoder.kofun` to the Stage2 compiler once lists and calls
  self-compile;
- lowering arbitrary Core expression trees and bindings;
- general signed integer formatting and checked arithmetic diagnostics;
- native stdout/stderr formatting and canonical `R010` diagnostics;
- conditional branches, allocation, and more targets;
- registering the native backend in the full differential runner.
