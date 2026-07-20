"""Tests for the built-in build system.

The properties that matter are correctness properties. A build system that is
fast but serves a stale artifact is worse than a slow one, so most of these
tests are about invalidation.
"""

from __future__ import annotations

import io
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))

from kofun.build_system import (                             # noqa: E402
    BuildError,
    Builder,
    Manifest,
    Target,
    clean,
    find_manifest,
)
from kofun.tui import PlainRenderer, summary_line, supports_tui   # noqa: E402


def write(root: Path, relative: str, content: str) -> Path:
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(textwrap.dedent(content), encoding="utf-8")
    return path


class RecordingCompiler:
    """Stands in for the real backend, and counts how often each target ran."""

    def __init__(self) -> None:
        self.calls: list[str] = []

    def __call__(self, target: Target, output: Path) -> None:
        self.calls.append(target.name)
        body = b"".join(src.read_bytes() for src in target.srcs)
        output.write_bytes(b"ARTIFACT:" + target.name.encode() + b":" + body)


class ManifestTest(unittest.TestCase):
    def test_parses_targets_and_defaults(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write(root, "src/main.kofun", "fn main() {}\n")
            path = write(root, "kofun.toml", """
                [workspace]
                name = "demo"
                default_targets = ["app"]

                [target.app]
                kind = "binary"
                srcs = ["src/main.kofun"]
            """)
            manifest = Manifest.load(path)
            self.assertEqual(manifest.name, "demo")
            self.assertEqual(manifest.default_targets, ["app"])
            self.assertEqual(manifest.targets["app"].kind, "binary")

    def test_defaults_to_every_runnable_target_when_unspecified(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write(root, "a.kofun", "fn main() {}\n")
            write(root, "b.kofun", "fn main() {}\n")
            path = write(root, "kofun.toml", """
                [target.app]
                srcs = ["a.kofun"]

                [target.tool]
                srcs = ["b.kofun"]
            """)
            self.assertEqual(Manifest.load(path).default_targets, ["app", "tool"])

    def test_library_kind_is_rejected_until_modules_exist(self) -> None:
        # Reserved in the schema but unimplemented: there is no separate
        # compilation, so a library could not be linked into anything.
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write(root, "a.kofun", "fn helper() {}\n")
            path = write(root, "kofun.toml", """
                [target.helper]
                kind = "library"
                srcs = ["a.kofun"]
            """)
            with self.assertRaisesRegex(BuildError, "not supported yet"):
                Manifest.load(path)

    def test_missing_source_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path = write(root, "kofun.toml", """
                [target.app]
                srcs = ["nope.kofun"]
            """)
            with self.assertRaisesRegex(BuildError, "does not exist"):
                Manifest.load(path)

    def test_escaping_the_workspace_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path = write(root, "kofun.toml", """
                [target.app]
                srcs = ["../outside.kofun"]
            """)
            with self.assertRaisesRegex(BuildError, "stay inside the workspace"):
                Manifest.load(path)

    def test_unknown_key_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write(root, "a.kofun", "fn main() {}\n")
            path = write(root, "kofun.toml", """
                [target.app]
                srcs = ["a.kofun"]
                optimise = true
            """)
            with self.assertRaisesRegex(BuildError, "unknown keys"):
                Manifest.load(path)

    def test_find_manifest_walks_upwards(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write(root, "a.kofun", "fn main() {}\n")
            write(root, "kofun.toml", """
                [target.app]
                srcs = ["a.kofun"]
            """)
            nested = root / "src" / "deep" / "deeper"
            nested.mkdir(parents=True)
            found = find_manifest(nested)
            self.assertIsNotNone(found)
            self.assertEqual(found.parent.resolve(), root.resolve())


class IncrementalBuildTest(unittest.TestCase):
    def _workspace(self, tmp: str) -> tuple[Manifest, Path]:
        root = Path(tmp)
        write(root, "src/main.kofun", "fn main() {}\n")
        path = write(root, "kofun.toml", """
            [target.app]
            srcs = ["src/main.kofun"]
        """)
        return Manifest.load(path), root

    def test_second_build_is_served_from_cache(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            manifest, _ = self._workspace(tmp)
            compiler = RecordingCompiler()

            first = Builder(manifest, compile_target=compiler).build()
            self.assertTrue(first.ok)
            self.assertEqual(first.executed, 1)
            self.assertEqual(first.cached, 0)

            second = Builder(manifest, compile_target=compiler).build()
            self.assertTrue(second.ok)
            self.assertEqual(second.executed, 0)
            self.assertEqual(second.cached, 1)
            self.assertEqual(compiler.calls, ["app"], "must not recompile")

    def test_editing_a_source_invalidates_the_cache(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            manifest, root = self._workspace(tmp)
            compiler = RecordingCompiler()
            Builder(manifest, compile_target=compiler).build()

            write(root, "src/main.kofun", "fn main() { print(1) }\n")
            manifest = Manifest.load(root / "kofun.toml")
            report = Builder(manifest, compile_target=compiler).build()

            self.assertEqual(report.executed, 1, "edited source must rebuild")
            self.assertEqual(compiler.calls, ["app", "app"])

    def test_reverting_a_source_restores_the_original_artifact(self) -> None:
        # Early cutoff: going back to a previously seen state is a cache hit.
        with tempfile.TemporaryDirectory() as tmp:
            manifest, root = self._workspace(tmp)
            compiler = RecordingCompiler()
            Builder(manifest, compile_target=compiler).build()

            write(root, "src/main.kofun", "fn main() { print(1) }\n")
            Builder(manifest=Manifest.load(root / "kofun.toml"),
                    compile_target=compiler).build()

            write(root, "src/main.kofun", "fn main() {}\n")
            report = Builder(manifest=Manifest.load(root / "kofun.toml"),
                             compile_target=compiler).build()
            self.assertEqual(report.cached, 1, "reverting should hit the cache")

    def test_no_cache_forces_a_rebuild(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            manifest, _ = self._workspace(tmp)
            compiler = RecordingCompiler()
            Builder(manifest, compile_target=compiler).build()
            report = Builder(manifest, compile_target=compiler, use_cache=False).build()
            self.assertEqual(report.executed, 1)

    def test_a_different_compiler_fingerprint_invalidates(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            manifest, _ = self._workspace(tmp)
            compiler = RecordingCompiler()
            builder = Builder(manifest, compile_target=compiler)
            builder.build()

            # Simulate the code generator changing underneath the cache.
            stale = Builder(manifest, compile_target=compiler)
            stale._fingerprint = "0" * 64
            report = stale.build()
            self.assertEqual(
                report.executed, 1,
                "a changed compiler must not serve artifacts built by the old one",
            )


class DependencyTest(unittest.TestCase):
    def _graph(self, tmp: str) -> Manifest:
        root = Path(tmp)
        for name in ("base", "middle", "app"):
            write(root, f"{name}.kofun", f"fn {name}() {{}}\n")
        path = write(root, "kofun.toml", """
            [workspace]
            default_targets = ["app"]

            [target.base]
            srcs = ["base.kofun"]

            [target.middle]
            srcs = ["middle.kofun"]
            deps = ["base"]

            [target.app]
            srcs = ["app.kofun"]
            deps = ["middle"]
        """)
        return Manifest.load(path)

    def test_dependencies_build_before_dependents(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            compiler = RecordingCompiler()
            report = Builder(self._graph(tmp), compile_target=compiler).build()
            self.assertTrue(report.ok)
            self.assertEqual(compiler.calls, ["base", "middle", "app"])

    def test_changing_a_dependency_rebuilds_dependents(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            manifest = self._graph(tmp)
            compiler = RecordingCompiler()
            Builder(manifest, compile_target=compiler).build()

            write(root, "base.kofun", "fn base() { print(9) }\n")
            report = Builder(Manifest.load(root / "kofun.toml"),
                             compile_target=compiler).build()
            self.assertEqual(
                report.executed, 3,
                "a changed dependency must invalidate everything downstream",
            )

    def test_cycles_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write(root, "a.kofun", "fn a() {}\n")
            write(root, "b.kofun", "fn b() {}\n")
            path = write(root, "kofun.toml", """
                [target.a]
                srcs = ["a.kofun"]
                deps = ["b"]

                [target.b]
                srcs = ["b.kofun"]
                deps = ["a"]
            """)
            builder = Builder(Manifest.load(path), compile_target=RecordingCompiler())
            with self.assertRaisesRegex(BuildError, "dependency cycle"):
                builder.build(["a"])

    def test_unknown_dependency_is_reported(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write(root, "a.kofun", "fn a() {}\n")
            path = write(root, "kofun.toml", """
                [target.a]
                srcs = ["a.kofun"]
                deps = ["ghost"]
            """)
            builder = Builder(Manifest.load(path), compile_target=RecordingCompiler())
            with self.assertRaisesRegex(BuildError, "unknown target `ghost`"):
                builder.build(["a"])

    def test_a_failed_dependency_skips_its_dependents(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            manifest = self._graph(tmp)

            def failing(target: Target, output: Path) -> None:
                if target.name == "base":
                    raise BuildError("base is broken")
                output.write_bytes(b"ok")

            report = Builder(manifest, compile_target=failing).build()
            self.assertFalse(report.ok)
            by_name = {r.target: r for r in report.results}
            self.assertIn("base is broken", by_name["base"].error)
            self.assertIn("skipped", by_name["app"].error)
            self.assertFalse(by_name["app"].output.exists())


class CleanTest(unittest.TestCase):
    def test_clean_removes_outputs_but_keeps_the_cache(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            write(root, "a.kofun", "fn main() {}\n")
            path = write(root, "kofun.toml", """
                [target.app]
                srcs = ["a.kofun"]
            """)
            manifest = Manifest.load(path)
            Builder(manifest, compile_target=RecordingCompiler()).build()

            clean(root)
            self.assertFalse((root / ".kofun" / "bin").exists())
            self.assertTrue((root / ".kofun" / "cache").exists())

            report = Builder(manifest, compile_target=RecordingCompiler()).build()
            self.assertEqual(report.cached, 1, "cache should survive a plain clean")

            clean(root, cache=True)
            self.assertFalse((root / ".kofun" / "cache").exists())


class RendererTest(unittest.TestCase):
    def test_plain_renderer_reports_each_target(self) -> None:
        stream = io.StringIO()
        renderer = PlainRenderer(stream=stream)
        renderer.start(1)
        target = Target(name="app", kind="binary", srcs=[])
        from kofun.build_system import ActionResult, BuildReport

        result = ActionResult(target="app", output=Path("x"), cached=False, duration_ms=3.0)
        renderer.on_event("finish", target, result)
        renderer.finish(BuildReport(results=[result], total_ms=5.0))

        output = stream.getvalue()
        self.assertIn("[1/1] app", output)
        self.assertIn("1 executed", output)
        self.assertNotIn("\x1b", output, "plain output must contain no escapes")

    def test_tui_is_disabled_for_non_tty_and_ci(self) -> None:
        import os

        stream = io.StringIO()   # not a TTY
        self.assertFalse(supports_tui(stream))

        class FakeTTY(io.StringIO):
            def isatty(self) -> bool:
                return True

        previous = dict(os.environ)
        try:
            os.environ.pop("CI", None)
            os.environ.pop("NO_COLOR", None)
            os.environ["TERM"] = "xterm-256color"
            self.assertTrue(supports_tui(FakeTTY()))
            self.assertFalse(supports_tui(FakeTTY(), force_off=True))

            os.environ["CI"] = "true"
            self.assertFalse(supports_tui(FakeTTY()), "CI must get plain output")
            del os.environ["CI"]

            os.environ["TERM"] = "dumb"
            self.assertFalse(supports_tui(FakeTTY()))
        finally:
            os.environ.clear()
            os.environ.update(previous)

    def test_summary_line_reports_failures(self) -> None:
        from kofun.build_system import ActionResult, BuildReport

        failed = ActionResult(
            target="app", output=Path("x"), cached=False, duration_ms=1.0, error="boom"
        )
        line = summary_line(BuildReport(results=[failed], total_ms=2.0), color=False)
        self.assertIn("1 failed", line)


if __name__ == "__main__":
    unittest.main()
