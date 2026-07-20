"""Latency gates for the integrated build system.

The single-file path is the one that has to stay instant: no manifest parsing,
no cache lookup, no process spawn between source and machine code. It is easy
to lose that by accident -- one eager import, one stat() of the workspace, one
"just check if a manifest exists" -- so the budget is enforced rather than
assumed.

Timings are best-of-N. A shared machine makes the mean and the worst case
meaningless, but the best case is a stable floor: it is what the work costs
when nothing else is competing, and regressions move it.
"""

from __future__ import annotations

import shutil
import sys
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))

from kofun import elf                                    # noqa: E402
from kofun.build_system import Builder, Manifest         # noqa: E402
from kofun.frontend import check_source                  # noqa: E402
from kofun.native_backend import NativeBackend           # noqa: E402

#: The budget from the integrated-build issue. Generous against a measured
#: ~0.4 ms, because this must fail on a real regression and not on a busy CI
#: runner.
SINGLE_FILE_BUDGET_MS = 5.0

PROGRAM = """
fn fib(n: Int) -> Int {
    if n < 2 {
        return n
    }
    return fib(n - 1) + fib(n - 2)
}

fn main() {
    print(fib(20))
}
"""


def best_of(attempts: int, work) -> float:
    """Best wall-clock time in milliseconds across `attempts` runs."""
    best = float("inf")
    for _ in range(attempts):
        started = time.perf_counter()
        work()
        best = min(best, (time.perf_counter() - started) * 1000.0)
    return best


class SingleFileLatency(unittest.TestCase):
    def test_source_to_elf_image_stays_within_budget(self) -> None:
        def compile_once() -> None:
            checked = check_source(PROGRAM)
            code, entry, data = NativeBackend().emit_program(checked.program, "bench")
            elf.build(code, entry, data)

        elapsed = best_of(30, compile_once)
        self.assertLess(
            elapsed, SINGLE_FILE_BUDGET_MS,
            f"single-file build took {elapsed:.3f} ms, budget {SINGLE_FILE_BUDGET_MS} ms",
        )

    def test_code_generation_alone_is_a_fraction_of_the_budget(self) -> None:
        # Isolates the backend from parsing and type checking, so a regression
        # can be attributed rather than merely observed.
        checked = check_source(PROGRAM)
        elapsed = best_of(50, lambda: NativeBackend().emit_program(checked.program, "b"))
        self.assertLess(
            elapsed, SINGLE_FILE_BUDGET_MS / 5,
            f"code generation took {elapsed:.3f} ms",
        )

    def test_no_subprocess_is_spawned_for_a_single_file_build(self) -> None:
        """The structural claim behind the speed: nothing is executed.

        Bazel, Ninja and Make model a build as a graph of subprocesses, so each
        file costs a fork, an exec and a dynamic link before compilation
        begins. This backend runs in-process, and that must not silently
        change -- a shell-out would be invisible in a timing test on a fast
        machine but would dominate on a slow one.
        """
        import subprocess

        original = subprocess.run
        calls: list[object] = []

        def record(*args, **kwargs):
            calls.append(args[0] if args else kwargs.get("args"))
            return original(*args, **kwargs)

        subprocess.run = record
        try:
            with tempfile.TemporaryDirectory() as tmp:
                checked = check_source(PROGRAM)
                code, entry, data = NativeBackend().emit_program(checked.program, "b")
                elf.write_executable(str(Path(tmp) / "out"), code, entry, data)
        finally:
            subprocess.run = original

        self.assertEqual(calls, [], f"single-file build spawned: {calls}")


class WorkspaceLatency(unittest.TestCase):
    def _workspace(self, root: Path) -> Manifest:
        (root / "src").mkdir(parents=True, exist_ok=True)
        (root / "src" / "main.kofun").write_text(PROGRAM, encoding="utf-8")
        manifest = root / "kofun.toml"
        manifest.write_text(
            '[workspace]\nname = "bench"\ndefault_targets = ["app"]\n\n'
            '[target.app]\nkind = "binary"\nsrcs = ["src/main.kofun"]\n',
            encoding="utf-8",
        )
        return Manifest.load(manifest)

    def _compile(self, target, output: Path) -> None:
        source = target.srcs[0].read_text(encoding="utf-8")
        checked = check_source(source)
        code, entry, data = NativeBackend().emit_program(checked.program, "bench")
        elf.write_executable(str(output), code, entry, data)

    def test_a_fully_cached_rebuild_is_faster_than_building(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            manifest = self._workspace(root)

            cold = Builder(manifest, compile_target=self._compile).build()
            self.assertTrue(cold.ok)
            self.assertEqual(cold.executed, 1)

            warm = Builder(manifest, compile_target=self._compile).build()
            self.assertTrue(warm.ok)
            self.assertEqual(warm.cached, 1, "unchanged rebuild must hit the cache")
            self.assertLess(
                warm.total_ms, cold.total_ms,
                "a cached rebuild that is not faster means the cache is not working",
            )

    def test_the_cache_restores_a_byte_identical_artifact(self) -> None:
        # Early cutoff is only sound if the restored artifact is exactly what a
        # rebuild would have produced.
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            manifest = self._workspace(root)
            builder = Builder(manifest, compile_target=self._compile)

            builder.build()
            built = (root / ".kofun" / "bin" / "app").read_bytes()

            shutil.rmtree(root / ".kofun" / "bin")
            report = Builder(manifest, compile_target=self._compile).build()
            self.assertEqual(report.cached, 1)
            restored = (root / ".kofun" / "bin" / "app").read_bytes()

            self.assertEqual(built, restored, "cache returned a different binary")


if __name__ == "__main__":
    unittest.main()
