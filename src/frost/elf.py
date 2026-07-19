"""Static ELF64 executable writer.

This replaces the system linker. Frost emits one PT_LOAD segment holding code
and read-only data, points `e_entry` at the generated `_start`, and writes the
file. There is no dynamic loader, no libc, and no relocation processing: the
binary is self-contained and the kernel can execute it directly.
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

ET_EXEC = 2
EM_X86_64 = 0x3E
PT_LOAD = 1
PF_X, PF_W, PF_R = 1, 2, 4


def layout(code_size: int) -> tuple[int, int]:
    """Return (file offset, virtual address) where the blob will be placed.

    Headers occupy the start of the file, so the blob follows them. Keeping the
    offset and the vaddr congruent modulo the page size is what lets a single
    mmap satisfy the segment.
    """
    offset = EHDR_SIZE + PHDR_SIZE
    return offset, BASE_VADDR + offset


def build(code: bytes, entry_offset: int) -> bytes:
    """Wrap `code` in an ELF64 executable whose entry point is `entry_offset`."""
    file_offset, vaddr = layout(len(code))
    entry = vaddr + entry_offset
    total = file_offset + len(code)

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
        1,              # e_phnum
        0, 0, 0,        # e_shentsize, e_shnum, e_shstrndx
    )
    assert len(ehdr) == EHDR_SIZE, len(ehdr)

    # One RX segment covering the headers and the blob. Mapping the headers too
    # is harmless and keeps offset/vaddr congruence trivial.
    phdr = struct.pack(
        "<IIQQQQQQ",
        PT_LOAD,
        PF_R | PF_X,
        0,              # p_offset
        BASE_VADDR,     # p_vaddr
        BASE_VADDR,     # p_paddr
        total,          # p_filesz
        total,          # p_memsz
        PAGE_SIZE,      # p_align
    )
    assert len(phdr) == PHDR_SIZE, len(phdr)

    return ehdr + phdr + code


def write_executable(path: str | os.PathLike[str], code: bytes, entry_offset: int) -> Path:
    target = Path(path)
    if target.parent != Path(""):
        target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(build(code, entry_offset))
    target.chmod(0o755)
    return target
