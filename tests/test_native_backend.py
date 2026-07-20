"""Differential tests for the direct x86-64 backend.

The reference interpreter is the oracle. Every program here is executed twice
-- interpreted, and compiled to a native ELF executable -- and the two stdout
streams must match byte for byte. A divergence is a miscompile.
"""

from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))

from kofun.frontend import check_source                      # noqa: E402
from kofun.native_backend import NativeBackend, compile_to_executable  # noqa: E402
from kofun.c_backend import BackendFailure                   # noqa: E402


PROGRAMS: dict[str, str] = {
    "arithmetic": """
fn main() {
    print(1 + 2)
    print(10 - 30)
    print(6 * 7)
    print(-5)
    print(2 + 3 * 4 - 6)
}
""",
    # `//` and `%` are floored, following the interpreter rather than C.
    # These cases are exactly where a C-style truncating backend diverges.
    "floor_division": """
fn main() {
    print(7 // 2)
    print(-7 // 2)
    print(7 // -2)
    print(-7 // -2)
    print(7 % 2)
    print(-7 % 2)
    print(7 % -2)
    print(-7 % -2)
    print(0 // 5)
    print(0 % 5)
}
""",
    "comparisons": """
fn main() {
    print(1 < 2)
    print(2 < 1)
    print(3 <= 3)
    print(4 > 5)
    print(5 >= 5)
    print(6 == 6)
    print(6 != 6)
    print(-1 < 0)
}
""",
    "logic": """
fn main() {
    print(true && true)
    print(true && false)
    print(false || true)
    print(false || false)
    print(!true)
    print(!false)
    print(1 < 2 && 3 < 4)
}
""",
    "control_flow": """
fn main() {
    let n = 7
    if n > 5 {
        print(1)
    } else {
        print(0)
    }
    let mut i = 0
    while i < 5 {
        print(i)
        i = i + 1
    }
}
""",
    "for_range": """
fn main() {
    let mut total = 0
    for i in 0 .. 10 {
        total = total + i
    }
    print(total)
    for j in 3 .. 6 {
        print(j)
    }
}
""",
    "recursion": """
fn fib(n: Int) -> Int {
    if n < 2 {
        return n
    }
    return fib(n - 1) + fib(n - 2)
}

fn main() {
    print(fib(20))
}
""",
    "multi_param": """
fn add3(a: Int, b: Int, c: Int) -> Int {
    return a + b + c
}

fn scale(x: Int, k: Int) -> Int {
    return x * k
}

fn main() {
    print(add3(1, 2, 3))
    print(scale(add3(1, 2, 3), 2))
    print(add3(scale(2, 3), scale(4, 5), 6))
}
""",
    # Nested calls inside argument lists stress rsp alignment at call sites.
    "nested_calls": """
fn ident(x: Int) -> Int {
    return x
}

fn sum2(a: Int, b: Int) -> Int {
    return a + b
}

fn main() {
    print(sum2(ident(1), ident(2)))
    print(sum2(sum2(1, 2), sum2(3, 4)))
    print(ident(sum2(ident(5), ident(6))))
}
""",
    "text_literal": """
fn main() {
    print("hello")
    print("Kofun")
}
""",
    "text_operations": """
fn greet(name: Text) -> Text {
    return "hello, " + name
}

fn main() {
    let a = "hello"
    let b = "world"
    print(a + " " + b)
    print(a == "hello")
    print(a == b)
    print(a != b)
    print(greet("kofun"))
    print(greet(greet("nested")))
    print("" + a)
    print(a + "")
    print("" == "")
}
""",
    # `len` counts codepoints, matching the interpreter, so multi-byte UTF-8
    # must not be counted per byte. These strings are 6, 9, and 4 bytes but 5,
    # 3, and 1 codepoints respectively.
    "text_unicode_length": """
fn main() {
    print(len("hello"))
    print(len("héllo"))
    print(len("日本語"))
    print(len("🌍"))
    print(len(""))
    print(len("a" + "é"))
    print("日本語" == "日本語")
    print("é" == "e")
}
""",
    "loop_control": """
fn main() {
    let mut i = 0
    while i < 10 {
        i = i + 1
        if i == 3 {
            continue
        }
        if i == 6 {
            break
        }
        print(i)
    }
}
""",
    # More bindings than the callee-saved pool, so this function falls back to
    # frame slots and keeps its frame pointer. Exercises the path the
    # register-allocated functions no longer take.
    "spills_to_frame": """
fn main() {
    let a = 1
    let b = 2
    let c = 3
    let d = 4
    let e = 5
    let f = 6
    let g = 7
    let h = 8
    print(a + b + c + d + e + f + g + h)
    let mut total = 0
    for i in 0 .. 5 {
        total = total + i + a
    }
    print(total)
}
""",
    # Two parameters means an even number of saved registers, which flips the
    # stack alignment the ABI requires at a call. Recursion makes it visible.
    "even_saved_registers": """
fn gcd(a: Int, b: Int) -> Int {
    if b == 0 {
        return a
    }
    return gcd(b, a % b)
}

fn ack(m: Int, n: Int) -> Int {
    if m == 0 {
        return n + 1
    }
    if n == 0 {
        return ack(m - 1, 1)
    }
    return ack(m - 1, ack(m, n - 1))
}

fn main() {
    print(gcd(1071, 462))
    print(ack(2, 3))
}
""",
    "big_numbers": """
fn main() {
    print(1000000)
    print(-1000000)
    print(1000000 * 1000)
}
""",
}


def run_interpreted(path: Path) -> str:
    result = subprocess.run(
        [sys.executable, "-m", "kofun.cli", "run", str(path)],
        capture_output=True, text=True, cwd=ROOT,
        env={"PYTHONPATH": str(ROOT / "src"), "PATH": "/usr/bin:/bin"},
    )
    if result.returncode != 0:
        raise AssertionError(f"interpreter failed: {result.stderr}")
    return result.stdout


def run_native(source: str, workdir: Path) -> str:
    checked = check_source(source)
    if checked.diagnostics:
        raise AssertionError(f"frontend diagnostics: {checked.diagnostics}")
    binary = workdir / "program"
    compile_to_executable(checked.program, str(binary), "test")
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    if result.returncode != 0:
        raise AssertionError(
            f"native binary exited {result.returncode}: {result.stderr}"
        )
    return result.stdout


class NativeDifferentialTest(unittest.TestCase):
    """Compiled output must match interpreted output exactly."""

    def test_programs_match_the_interpreter(self) -> None:
        for name, source in PROGRAMS.items():
            with self.subTest(program=name):
                with tempfile.TemporaryDirectory() as tmp:
                    workdir = Path(tmp)
                    source_path = workdir / f"{name}.kofun"
                    source_path.write_text(source)

                    expected = run_interpreted(source_path)
                    actual = run_native(source, workdir)
                    self.assertEqual(
                        expected, actual,
                        f"{name}: interpreter and native backend disagree",
                    )


class NativeExecutableTest(unittest.TestCase):
    def test_output_is_a_static_elf(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "hello"
            checked = check_source("fn main() {\n    print(42)\n}\n")
            compile_to_executable(checked.program, str(binary), "test")
            header = binary.read_bytes()[:20]
            self.assertEqual(header[:4], b"\x7fELF")
            self.assertEqual(header[4], 2, "ELFCLASS64")
            self.assertEqual(header[5], 1, "little endian")
            self.assertEqual(header[16], 2, "ET_EXEC")
            self.assertEqual(header[18], 0x3E, "EM_X86_64")

    def test_main_return_value_becomes_the_exit_code(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "code"
            checked = check_source("fn main() -> Int {\n    return 7\n}\n")
            compile_to_executable(checked.program, str(binary), "test")
            self.assertEqual(subprocess.run([str(binary)]).returncode, 7)

    def test_image_has_separate_executable_and_writable_segments(self) -> None:
        # The heap pointer lives in writable memory, but code must not be
        # writable and data must not be executable.
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "seg"
            checked = check_source("fn main() {\n    print(1)\n}\n")
            compile_to_executable(checked.program, str(binary), "test")
            image = binary.read_bytes()

            phoff = int.from_bytes(image[32:40], "little")
            phentsize = int.from_bytes(image[54:56], "little")
            phnum = int.from_bytes(image[56:58], "little")
            self.assertEqual(phnum, 2, "expected one R|X and one R|W segment")

            flags = []
            for index in range(phnum):
                header = image[phoff + index * phentsize:][:phentsize]
                flags.append(int.from_bytes(header[4:8], "little"))
            self.assertEqual(flags[0], 0x5, "text segment must be read+execute")
            self.assertEqual(flags[1], 0x6, "data segment must be read+write")

    def test_unsupported_features_fail_loudly(self) -> None:
        # Lists are not lowered yet; the backend must refuse rather than
        # silently emit something with different meaning.
        checked = check_source("fn main() {\n    let xs = [1, 2, 3]\n    print(xs[0])\n}\n")
        with self.assertRaises(BackendFailure):
            NativeBackend().emit_program(checked.program, "test")


class HeapAllocatorTest(unittest.TestCase):
    """The mmap-backed bump allocator, exercised directly.

    There is no language syntax that allocates yet, so the runtime routine is
    driven by a hand-written entry point. Once `Text` and `List` are lowered,
    these guarantees are what they will rest on.
    """

    def _run_driver(self, emit_driver) -> str:
        from kofun import elf
        from kofun.native_backend import NativeBackend

        backend = NativeBackend()
        checked = check_source("fn main() {\n    print(0)\n}\n")
        backend._emit_start = lambda: emit_driver(backend)
        code, entry, data_offset = backend.emit_program(checked.program, "test")

        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "driver"
            elf.write_executable(str(binary), code, entry, data_offset)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            return result.stdout

    def test_allocations_are_distinct_aligned_and_writable(self) -> None:
        from kofun.x64 import RAX, RCX, RDI, R12, R13

        def driver(backend) -> None:
            asm = backend.asm
            asm.label("_start")
            asm.mov_ri(RDI, 32)
            asm.call("rt.alloc")
            asm.mov_rr(R12, RAX)
            asm.mov_ri(RCX, 111)
            asm.mov_mr(R12, 0, RCX)          # write into the first block

            asm.mov_ri(RDI, 64)
            asm.call("rt.alloc")
            asm.mov_rr(R13, RAX)
            asm.mov_ri(RCX, 222)
            asm.mov_mr(R13, 0, RCX)          # write into the second

            asm.mov_rm(RDI, R12, 0)          # first survived the second write
            asm.call("rt.print_int")
            asm.mov_rm(RDI, R13, 0)
            asm.call("rt.print_int")
            asm.mov_rr(RDI, R13)             # blocks do not overlap
            asm.sub_rr(RDI, R12)
            asm.call("rt.print_int")
            asm.mov_rr(RDI, R12)             # pointers are 16-byte aligned
            asm.and_ri(RDI, 15)
            asm.call("rt.print_int")

            asm.mov_ri(RDI, 0)
            asm.mov_ri(RAX, 60)
            asm.syscall()
            asm.ud2()

        self.assertEqual(self._run_driver(driver), "111\n222\n32\n0\n")

    def test_allocating_past_a_chunk_boundary_takes_a_new_chunk(self) -> None:
        # 300 allocations of 8 KiB is 2.4 MiB, more than the 1 MiB chunk, so the
        # allocator must fall back to mmap mid-loop and keep returning usable
        # memory across the boundary.
        from kofun.x64 import RAX, RCX, RDI, R12, R13, R14

        def driver(backend) -> None:
            asm = backend.asm
            asm.label("_start")
            asm.xor_rr(R13, R13)             # loop counter
            asm.xor_rr(R14, R14)             # running checksum

            asm.label("loop")
            asm.mov_ri(RDI, 8192)
            asm.call("rt.alloc")
            asm.mov_rr(R12, RAX)
            asm.mov_rr(RCX, R13)
            asm.mov_mr(R12, 0, RCX)          # stamp the block with its index
            asm.mov_mr(R12, 8184, RCX)       # and near its far end
            asm.mov_rm(RCX, R12, 8184)       # read the far end back
            asm.add_rr(R14, RCX)
            asm.add_ri(R13, 1)
            asm.cmp_ri(R13, 300)
            asm.jcc(12, "loop")              # CC_L

            asm.mov_rr(RDI, R14)             # expect sum(0..299) == 44850
            asm.call("rt.print_int")
            asm.mov_ri(RDI, 0)
            asm.mov_ri(RAX, 60)
            asm.syscall()
            asm.ud2()

        self.assertEqual(self._run_driver(driver), f"{sum(range(300))}\n")


if __name__ == "__main__":
    unittest.main()
