"""Static ELF64 executable writer.

This replaces the system linker. Kofun points `e_entry` at the generated
`_start` and writes the file itself. There is no dynamic loader, no libc, and no
relocation processing: the binary is self-contained and the kernel executes it
directly.

The image is one contiguous blob split into two segments:

    [ code | rodata | pad ][ data ]
      R | X                  R | W

Virtual addresses stay `BASE_VADDR + file_offset` across the whole image, so an
offset inside the blob equals a virtual-address delta. That is what lets the
assembler resolve RIP-relative references by subtracting blob offsets, without
knowing anything about segment layout. The only constraint is that the split
land on a page boundary, which the caller arranges by padding.
"""

from __future__ import annotations

import os
import struct
from pathlib import Path

# The traditional load address for x86-64 static executables. Anything above
# the mmap_min_addr floor works; this one keeps `readelf` output familiar.
BASE_VADDR = 0x400000
PAGE_SIZE = 0x1000

EHDR_SIZE = 64
PHDR_SIZE = 56
PHDR_COUNT = 2

#: Bytes occupied by the ELF header and program headers, before the blob.
HEADER_SIZE = EHDR_SIZE + PHDR_SIZE * PHDR_COUNT

ET_EXEC = 2
EM_X86_64 = 0x3E
PT_LOAD = 1
PF_X, PF_W, PF_R = 1, 2, 4


def padding_to_page(blob_offset: int) -> int:
    """Bytes of padding needed so `blob_offset` lands on a file page boundary.

    Callers use this to place the writable section: virtual addresses track file
    offsets exactly, so a page-aligned file offset is also a page-aligned vaddr.
    """
    return -(HEADER_SIZE + blob_offset) % PAGE_SIZE


def build(code: bytes, entry_offset: int, data_offset: int | None = None) -> bytes:
    """Wrap `code` in an ELF64 executable.

    `data_offset` is the offset within `code` where writable data begins; it must
    be page-aligned once `HEADER_SIZE` is added (see `padding_to_page`). Passing
    `None` produces a single read-execute segment, for programs with no mutable
    globals.
    """
    entry = BASE_VADDR + HEADER_SIZE + entry_offset
    total = HEADER_SIZE + len(code)

    if data_offset is None:
        data_offset = len(code)
    if (HEADER_SIZE + data_offset) % PAGE_SIZE:
        raise ValueError(
            f"data_offset {data_offset} is not page-aligned "
            f"(file offset {HEADER_SIZE + data_offset})"
        )

    text_end = HEADER_SIZE + data_offset
    data_size = len(code) - data_offset

    ehdr = struct.pack(
        "<4sBBBBB7sHHIQQQIHHHHHH",
        b"\x7fELF",
        2,              # EI_CLASS   = ELFCLASS64
        1,              # EI_DATA    = ELFDATA2LSB
        1,              # EI_VERSION = EV_CURRENT
        0,              # EI_OSABI   = SYSV
        0,              # EI_ABIVERSION
        b"\0" * 7,      # EI_PAD
        ET_EXEC,
        EM_X86_64,
        1,              # e_version
        entry,
        EHDR_SIZE,      # e_phoff
        0,              # e_shoff: no section headers; execution does not need them
        0,              # e_flags
        EHDR_SIZE,
        PHDR_SIZE,
        PHDR_COUNT,
        0, 0, 0,        # e_shentsize, e_shnum, e_shstrndx
    )
    assert len(ehdr) == EHDR_SIZE, len(ehdr)

    # Mapping the headers into the executable segment is harmless and keeps
    # offset/vaddr congruence trivial.
    text = struct.pack(
        "<IIQQQQQQ",
        PT_LOAD,
        PF_R | PF_X,
        0,                      # p_offset
        BASE_VADDR,             # p_vaddr
        BASE_VADDR,             # p_paddr
        text_end,               # p_filesz
        text_end,               # p_memsz
        PAGE_SIZE,
    )

    # A second, writable segment. Even when empty it is emitted, so the header
    # count is constant and `HEADER_SIZE` stays a compile-time constant.
    data = struct.pack(
        "<IIQQQQQQ",
        PT_LOAD,
        PF_R | PF_W,
        text_end,
        BASE_VADDR + text_end,
        BASE_VADDR + text_end,
        data_size,
        data_size,
        PAGE_SIZE,
    )
    assert len(text) == len(data) == PHDR_SIZE

    return ehdr + text + data + code


def write_executable(
    path: str | os.PathLike[str],
    code: bytes,
    entry_offset: int,
    data_offset: int | None = None,
) -> Path:
    target = Path(path)
    if target.parent != Path(""):
        target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(build(code, entry_offset, data_offset))
    target.chmod(0o755)
    return target
