"""Differential tests for the x86-64 instruction encoder, against objdump.

A wrong encoding is a miscompile that no later stage can detect, so every
instruction the backend emits is checked against a real disassembler. Each case
emits exactly one instruction and asserts on how objdump reads it back.
"""

from __future__ import annotations

import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))

from frost import x64                                        # noqa: E402
from frost.x64 import (                                      # noqa: E402
    RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R10, R11, R12, R13, R15,
    Assembler,
)

OBJDUMP = shutil.which("objdump")
WHITESPACE = re.compile(r"\s+")


def normalise(text: str) -> str:
    text = text.split("#")[0]      # objdump's trailing comments
    text = text.split("<")[0]      # symbol annotations
    return WHITESPACE.sub(" ", text).strip()


def disassemble(code: bytes) -> list[str]:
    with tempfile.TemporaryDirectory() as tmp:
        raw = Path(tmp) / "code.bin"
        raw.write_bytes(code)
        output = subprocess.run(
            [OBJDUMP, "-D", "-b", "binary", "-m", "i386:x86-64", "-M", "att", str(raw)],
            capture_output=True, text=True, check=True,
        ).stdout
    instructions = []
    for line in output.splitlines():
        parts = line.split("\t")     # "  0:\t48 89 c3\tmov %rax,%rbx"
        if len(parts) >= 3:
            instructions.append(normalise(parts[2]))
    return instructions


CASES: list[tuple[str, object, str]] = [
    ("mov_ri small",     lambda a: a.mov_ri(RAX, 1),         "mov $0x1,%eax"),
    ("mov_ri extended",  lambda a: a.mov_ri(R10, 5),         "mov $0x5,%r10d"),
    ("mov_ri negative",  lambda a: a.mov_ri(RAX, -1),        "mov $0xffffffffffffffff,%rax"),
    ("mov_ri 64-bit",    lambda a: a.mov_ri(RAX, 1 << 40),   "movabs $0x10000000000,%rax"),
    ("mov_rr",           lambda a: a.mov_rr(RBX, RAX),       "mov %rax,%rbx"),
    ("mov_rr extended",  lambda a: a.mov_rr(R15, RCX),       "mov %rcx,%r15"),
    ("mov_rm disp8",     lambda a: a.mov_rm(RAX, RBP, -8),   "mov -0x8(%rbp),%rax"),
    ("mov_rm disp32",    lambda a: a.mov_rm(RAX, RBP, -500), "mov -0x1f4(%rbp),%rax"),
    ("mov_mr",           lambda a: a.mov_mr(RBP, -16, RDX),  "mov %rdx,-0x10(%rbp)"),
    ("mov_rm rsp/SIB",   lambda a: a.mov_rm(RAX, RSP, 0),    "mov (%rsp),%rax"),
    ("mov_rm r12/SIB",   lambda a: a.mov_rm(RAX, R12, 8),    "mov 0x8(%r12),%rax"),
    ("mov_rm r13/disp8", lambda a: a.mov_rm(RAX, R13, 0),    "mov 0x0(%r13),%rax"),
    ("mov_mr rbp zero",  lambda a: a.mov_mr(RBP, 0, RAX),    "mov %rax,0x0(%rbp)"),
    ("mov_m8_r dl",      lambda a: a.mov_m8_r(RSI, 0, RDX),  "mov %dl,(%rsi)"),
    ("mov_m8_r r11b",    lambda a: a.mov_m8_r(RSI, 0, R11),  "mov %r11b,(%rsi)"),
    ("mov_m8_r sil",     lambda a: a.mov_m8_r(RAX, 0, RSI),  "mov %sil,(%rax)"),
    ("movzx_r8",         lambda a: a.movzx_r8(RAX, RAX),     "movzbq %al,%rax"),
    ("push",             lambda a: a.push(RBP),              "push %rbp"),
    ("push extended",    lambda a: a.push(R13),              "push %r13"),
    ("pop",              lambda a: a.pop(RBP),               "pop %rbp"),
    ("add_rr",           lambda a: a.add_rr(RAX, RCX),       "add %rcx,%rax"),
    ("sub_rr",           lambda a: a.sub_rr(RAX, RCX),       "sub %rcx,%rax"),
    ("and_rr",           lambda a: a.and_rr(RAX, RCX),       "and %rcx,%rax"),
    ("or_rr",            lambda a: a.or_rr(RAX, RCX),        "or %rcx,%rax"),
    ("xor_rr",           lambda a: a.xor_rr(RAX, RAX),       "xor %rax,%rax"),
    ("cmp_rr",           lambda a: a.cmp_rr(RAX, RCX),       "cmp %rcx,%rax"),
    ("test_rr",          lambda a: a.test_rr(RAX, RAX),      "test %rax,%rax"),
    ("imul_rr",          lambda a: a.imul_rr(RAX, RCX),      "imul %rcx,%rax"),
    ("add_ri imm8",      lambda a: a.add_ri(RSP, 16),        "add $0x10,%rsp"),
    ("sub_ri imm32",     lambda a: a.sub_ri(RSP, 4096),      "sub $0x1000,%rsp"),
    ("cmp_ri",           lambda a: a.cmp_ri(RAX, 2),         "cmp $0x2,%rax"),
    ("neg",              lambda a: a.neg(RAX),               "neg %rax"),
    ("cqo",              lambda a: a.cqo(),                  "cqto"),
    ("idiv",             lambda a: a.idiv(RCX),              "idiv %rcx"),
    ("sar_ri",           lambda a: a.sar_ri(RAX, 63),        "sar $0x3f,%rax"),
    ("setcc e",          lambda a: a.setcc(x64.CC_E, RAX),   "sete %al"),
    ("setcc l",          lambda a: a.setcc(x64.CC_L, RAX),   "setl %al"),
    ("ret",              lambda a: a.ret(),                  "ret"),
    ("syscall",          lambda a: a.syscall(),              "syscall"),
]


@unittest.skipIf(OBJDUMP is None, "objdump is not installed")
class EncoderMatchesObjdumpTest(unittest.TestCase):
    def test_each_instruction_disassembles_as_expected(self) -> None:
        for name, build, expected in CASES:
            with self.subTest(instruction=name):
                asm = Assembler()
                build(asm)
                got = disassemble(asm.link())
                self.assertEqual(
                    len(got), 1,
                    f"{name} encoded to {len(got)} instructions: {got}",
                )
                self.assertEqual(got[0], normalise(expected), f"bytes={asm.code.hex()}")

    def test_forward_jump_resolves_to_the_label(self) -> None:
        asm = Assembler()
        asm.jmp("skip")     # 5 bytes
        asm.ret()           # 1 byte, so `skip` sits at offset 6
        asm.label("skip")
        asm.syscall()
        self.assertEqual(disassemble(asm.link()), ["jmp 0x6", "ret", "syscall"])

    def test_backward_jump_resolves_to_the_label(self) -> None:
        asm = Assembler()
        asm.label("top")
        asm.ret()
        asm.jmp("top")
        self.assertEqual(disassemble(asm.link()), ["ret", "jmp 0x0"])


class LabelResolutionTest(unittest.TestCase):
    def test_rip_relative_lea_points_at_the_label(self) -> None:
        asm = Assembler()
        asm.lea_label(RDI, "data")   # 7 bytes
        asm.ret()                    # 1 byte, so `data` sits at offset 8
        asm.label("data")
        asm.emit_bytes(b"hi\0")
        code = asm.link()
        # The displacement is measured from the end of the lea, at offset 7.
        self.assertEqual(int.from_bytes(code[3:7], "little", signed=True), 8 - 7)

    def test_undefined_label_is_an_error(self) -> None:
        asm = Assembler()
        asm.jmp("nowhere")
        with self.assertRaises(KeyError):
            asm.link()

    def test_duplicate_label_is_an_error(self) -> None:
        asm = Assembler()
        asm.label("here")
        with self.assertRaises(ValueError):
            asm.label("here")


if __name__ == "__main__":
    unittest.main()
