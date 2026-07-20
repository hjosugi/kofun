"""x86-64 machine code encoder.

This module emits instruction bytes directly. There is no assembler in the
pipeline: `Assembler` produces the exact encoding, resolves label references
itself, and hands back a `bytes` blob that the ELF writer maps into memory.

Only the instructions the code generator actually needs are implemented.
Anything else must be added deliberately, because a wrong encoding here is a
miscompile that no later stage can detect.
"""

from __future__ import annotations

from dataclasses import dataclass, field

# General purpose registers, in hardware encoding order.
RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI = range(8)
R8, R9, R10, R11, R12, R13, R14, R15 = range(8, 16)

REG_NAMES = [
    "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
]

# System V AMD64 integer argument registers, in order.
ARG_REGS = (RDI, RSI, RDX, RCX, R8, R9)

# Condition codes, as used by Jcc (0x0F 0x80+cc) and SETcc (0x0F 0x90+cc).
CC_O, CC_NO, CC_B, CC_AE, CC_E, CC_NE, CC_BE, CC_A = range(8)
CC_S, CC_NS, CC_P, CC_NP, CC_L, CC_GE, CC_LE, CC_G = range(8, 16)

CC_BY_NAME = {
    "e": CC_E, "ne": CC_NE,
    "l": CC_L, "le": CC_LE, "g": CC_G, "ge": CC_GE,
    "b": CC_B, "be": CC_BE, "a": CC_A, "ae": CC_AE,
    "s": CC_S, "ns": CC_NS,
}


def _rex(w: int = 0, r: int = 0, x: int = 0, b: int = 0) -> int:
    return 0x40 | (w << 3) | (r << 2) | (x << 1) | b


def _modrm(mod: int, reg: int, rm: int) -> int:
    return (mod << 6) | ((reg & 7) << 3) | (rm & 7)


@dataclass(slots=True)
class _Fixup:
    """A 32-bit displacement to patch once every label offset is known.

    Both `jmp`/`jcc`/`call` (rel32) and RIP-relative `lea` measure their
    displacement from the end of the instruction, so a single fixup kind
    covers every case here.
    """

    at: int          # offset of the 4-byte field
    end: int         # offset of the first byte after the instruction
    label: str


@dataclass(slots=True)
class Assembler:
    code: bytearray = field(default_factory=bytearray)
    labels: dict[str, int] = field(default_factory=dict)
    fixups: list[_Fixup] = field(default_factory=list)

    # ---- buffer primitives -------------------------------------------------

    def emit(self, *values: int) -> None:
        self.code.extend(values)

    def emit_bytes(self, data: bytes) -> None:
        self.code.extend(data)

    def _imm32(self, value: int) -> None:
        self.code.extend((value & 0xFFFFFFFF).to_bytes(4, "little"))

    def _imm64(self, value: int) -> None:
        self.code.extend((value & 0xFFFFFFFFFFFFFFFF).to_bytes(8, "little"))

    @property
    def offset(self) -> int:
        return len(self.code)

    def label(self, name: str) -> None:
        if name in self.labels:
            raise ValueError(f"duplicate label: {name}")
        self.labels[name] = len(self.code)

    def _fixup(self, label: str) -> None:
        at = len(self.code)
        self._imm32(0)
        self.fixups.append(_Fixup(at=at, end=len(self.code), label=label))

    def link(self) -> bytes:
        """Resolve every label reference and return the finished blob."""
        for fixup in self.fixups:
            if fixup.label not in self.labels:
                raise KeyError(f"undefined label: {fixup.label}")
            delta = self.labels[fixup.label] - fixup.end
            self.code[fixup.at:fixup.at + 4] = (delta & 0xFFFFFFFF).to_bytes(4, "little")
        return bytes(self.code)

    # ---- memory operands ---------------------------------------------------

    def _mem(self, reg: int, base: int, disp: int) -> None:
        """Encode ModRM (+SIB, +disp) for `[base + disp]`.

        rsp/r12 always need a SIB byte because rm=100 means "SIB follows".
        rbp/r13 cannot use mod=00 because rm=101 means RIP-relative, so a
        zero displacement is encoded as disp8.
        """
        needs_sib = (base & 7) == RSP
        if disp == 0 and (base & 7) != RBP:
            mod = 0
        elif -128 <= disp <= 127:
            mod = 1
        else:
            mod = 2

        self.emit(_modrm(mod, reg, RSP if needs_sib else base))
        if needs_sib:
            # scale=0, index=100 (none), base=base
            self.emit(_modrm(0, RSP, base))
        if mod == 1:
            self.emit(disp & 0xFF)
        elif mod == 2:
            self._imm32(disp)

    # ---- data movement -----------------------------------------------------

    def mov_ri(self, dst: int, imm: int) -> None:
        """dst = imm (64-bit), using the shortest correct encoding."""
        if 0 <= imm <= 0xFFFFFFFF:
            # 32-bit destination writes zero-extend to 64 bits.
            if dst >= 8:
                self.emit(_rex(b=1))
            self.emit(0xB8 + (dst & 7))
            self._imm32(imm)
        elif -0x80000000 <= imm < 0:
            # REX.W C7 /0 sign-extends its imm32 to 64 bits.
            self.emit(_rex(w=1, b=dst >> 3), 0xC7, _modrm(3, 0, dst))
            self._imm32(imm)
        else:
            self.emit(_rex(w=1, b=dst >> 3), 0xB8 + (dst & 7))
            self._imm64(imm)

    def mov_rr(self, dst: int, src: int) -> None:
        if dst == src:
            return
        self.emit(_rex(w=1, r=src >> 3, b=dst >> 3), 0x89, _modrm(3, src, dst))

    def mov_rm(self, dst: int, base: int, disp: int) -> None:
        """dst = [base + disp]"""
        self.emit(_rex(w=1, r=dst >> 3, b=base >> 3), 0x8B)
        self._mem(dst, base, disp)

    def mov_mr(self, base: int, disp: int, src: int) -> None:
        """[base + disp] = src"""
        self.emit(_rex(w=1, r=src >> 3, b=base >> 3), 0x89)
        self._mem(src, base, disp)

    def movzx_r8(self, dst: int, src: int) -> None:
        """dst = zero_extend(src as 8-bit)"""
        self.emit(_rex(w=1, r=dst >> 3, b=src >> 3), 0x0F, 0xB6, _modrm(3, dst, src))

    def movzx_r8_m(self, dst: int, base: int, disp: int) -> None:
        """dst = zero_extend(byte at [base + disp])"""
        self.emit(_rex(w=1, r=dst >> 3, b=base >> 3), 0x0F, 0xB6)
        self._mem(dst, base, disp)

    def mov_m8_r(self, base: int, disp: int, src: int) -> None:
        """[base + disp] = low byte of src.

        A REX prefix is required to name spl/bpl/sil/dil; without one those
        encodings mean ah/ch/dh/bh instead, which would store the wrong byte.
        """
        if src >= 4 or base >= 8:
            self.emit(_rex(r=src >> 3, b=base >> 3))
        self.emit(0x88)
        self._mem(src, base, disp)

    def lea_label(self, dst: int, label: str) -> None:
        """dst = address of `label` (RIP-relative, so the blob stays PIC)."""
        self.emit(_rex(w=1, r=dst >> 3), 0x8D, _modrm(0, dst, RBP))  # rm=101 -> RIP
        self._fixup(label)

    def push(self, reg: int) -> None:
        if reg >= 8:
            self.emit(_rex(b=1))
        self.emit(0x50 + (reg & 7))

    def pop(self, reg: int) -> None:
        if reg >= 8:
            self.emit(_rex(b=1))
        self.emit(0x58 + (reg & 7))

    # ---- arithmetic --------------------------------------------------------

    def _alu_rr(self, opcode: int, dst: int, src: int) -> None:
        self.emit(_rex(w=1, r=src >> 3, b=dst >> 3), opcode, _modrm(3, src, dst))

    def add_rr(self, dst: int, src: int) -> None:
        self._alu_rr(0x01, dst, src)

    def sub_rr(self, dst: int, src: int) -> None:
        self._alu_rr(0x29, dst, src)

    def and_rr(self, dst: int, src: int) -> None:
        self._alu_rr(0x21, dst, src)

    def or_rr(self, dst: int, src: int) -> None:
        self._alu_rr(0x09, dst, src)

    def xor_rr(self, dst: int, src: int) -> None:
        self._alu_rr(0x31, dst, src)

    def cmp_rr(self, left: int, right: int) -> None:
        """Set flags from `left - right`."""
        self._alu_rr(0x39, left, right)

    def test_rr(self, left: int, right: int) -> None:
        self._alu_rr(0x85, left, right)

    def imul_rr(self, dst: int, src: int) -> None:
        self.emit(_rex(w=1, r=dst >> 3, b=src >> 3), 0x0F, 0xAF, _modrm(3, dst, src))

    def _alu_ri(self, ext: int, dst: int, imm: int) -> None:
        if -128 <= imm <= 127:
            self.emit(_rex(w=1, b=dst >> 3), 0x83, _modrm(3, ext, dst), imm & 0xFF)
        else:
            self.emit(_rex(w=1, b=dst >> 3), 0x81, _modrm(3, ext, dst))
            self._imm32(imm)

    def add_ri(self, dst: int, imm: int) -> None:
        self._alu_ri(0, dst, imm)

    def sub_ri(self, dst: int, imm: int) -> None:
        self._alu_ri(5, dst, imm)

    def cmp_ri(self, dst: int, imm: int) -> None:
        self._alu_ri(7, dst, imm)

    def and_ri(self, dst: int, imm: int) -> None:
        self._alu_ri(4, dst, imm)

    def or_ri(self, dst: int, imm: int) -> None:
        self._alu_ri(1, dst, imm)

    def neg(self, reg: int) -> None:
        self.emit(_rex(w=1, b=reg >> 3), 0xF7, _modrm(3, 3, reg))

    def cqo(self) -> None:
        """Sign-extend rax into rdx:rax, as idiv requires."""
        self.emit(_rex(w=1), 0x99)

    def idiv(self, reg: int) -> None:
        """rax = rdx:rax / reg, rdx = remainder."""
        self.emit(_rex(w=1, b=reg >> 3), 0xF7, _modrm(3, 7, reg))

    def sar_ri(self, dst: int, imm: int) -> None:
        self.emit(_rex(w=1, b=dst >> 3), 0xC1, _modrm(3, 7, dst), imm & 0xFF)

    def setcc(self, cc: int, reg: int) -> None:
        """Set the low byte of `reg` to 1 or 0 based on flags."""
        # REX (even if empty) is required to address sil/dil/spl/bpl as 8-bit.
        if reg >= 4:
            self.emit(_rex(b=reg >> 3))
        self.emit(0x0F, 0x90 + cc, _modrm(3, 0, reg))

    # ---- control flow ------------------------------------------------------

    def jmp(self, label: str) -> None:
        self.emit(0xE9)
        self._fixup(label)

    def jcc(self, cc: int, label: str) -> None:
        self.emit(0x0F, 0x80 + cc)
        self._fixup(label)

    def call(self, label: str) -> None:
        self.emit(0xE8)
        self._fixup(label)

    def ret(self) -> None:
        self.emit(0xC3)

    def syscall(self) -> None:
        self.emit(0x0F, 0x05)

    def ud2(self) -> None:
        """Guaranteed-invalid opcode; used to make 'unreachable' explicit."""
        self.emit(0x0F, 0x0B)
