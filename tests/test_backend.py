from __future__ import annotations

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from kofun.c_backend import BackendFailure, CBackend, compile_c
from kofun.frontend import check_source


class CBackendTests(unittest.TestCase):
    def test_emits_c_for_numeric_program(self) -> None:
        source = (
            "fn add(a: Int, b: Int) -> Int { return a + b }\n"
            "fn main() { print(add(20, 22)) }\n"
        )
        result = check_source(source)
        self.assertTrue(result.ok)
        emitted = CBackend().emit_program(result.program, "test.kf")
        self.assertIn("kofun_fn_add", emitted)
        self.assertIn("PRId64", emitted)

    @unittest.skipUnless(shutil.which("cc") or shutil.which("clang") or shutil.which("gcc"), "C compiler unavailable")
    def test_native_fibonacci(self) -> None:
        source = (
            "fn fib(n: Int) -> Int {\n"
            " if n < 2 { return n }\n"
            " return fib(n - 1) + fib(n - 2)\n"
            "}\n"
            "fn main() { print(fib(10)) }\n"
        )
        result = check_source(source)
        self.assertTrue(result.ok, [d.message for d in result.diagnostics])
        emitted = CBackend().emit_program(result.program, "fib.kf")
        with tempfile.TemporaryDirectory() as tmp:
            binary = compile_c(emitted, Path(tmp) / "fib")
            completed = subprocess.run([str(binary)], text=True, capture_output=True, check=False)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            self.assertEqual(completed.stdout.strip(), "55")

    def test_unsupported_list_fails_explicitly(self) -> None:
        source = "fn main() { let x = [1, 2, 3]\n print(len(x)) }\n"
        result = check_source(source)
        self.assertTrue(result.ok)
        with self.assertRaises(BackendFailure):
            CBackend().emit_program(result.program, "list.kf")


if __name__ == "__main__":
    unittest.main()
