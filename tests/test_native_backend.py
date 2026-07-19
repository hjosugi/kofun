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

from cofn.frontend import check_source                      # noqa: E402
from cofn.native_backend import NativeBackend, compile_to_executable  # noqa: E402
from cofn.c_backend import BackendFailure                   # noqa: E402


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
    print("Cofn")
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
        [sys.executable, "-m", "cofn.cli", "run", str(path)],
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
                    source_path = workdir / f"{name}.cofn"
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

    def test_unsupported_features_fail_loudly(self) -> None:
        # Lists are not lowered yet; the backend must refuse rather than
        # silently emit something with different meaning.
        checked = check_source("fn main() {\n    let xs = [1, 2, 3]\n    print(xs[0])\n}\n")
        with self.assertRaises(BackendFailure):
            NativeBackend().emit_program(checked.program, "test")


if __name__ == "__main__":
    unittest.main()
