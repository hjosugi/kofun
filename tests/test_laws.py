from __future__ import annotations

import contextlib
import io
import json
import tempfile
import unittest
from pathlib import Path

from kofun import cli
from kofun.frontend import check_source


GOOD_MONAD = """
fn list_pure(value: Int) -> List[Int] { return [value] }
fn list_bind(values: List[Int], next: Fn[Int, List[Int]]) -> List[Int] {
    return fold(values, [], fn(acc: List[Int], value: Int) => concat(acc, next(value)))
}
fn keep(value: Int) -> List[Int] { return [value] }
fn increment(value: Int) -> List[Int] { return [value + 1] }
fn duplicate(value: Int) -> List[Int] { return [value, value] }

law monad ListMonad {
    pure = list_pure
    bind = list_bind
    values = [-1, 0, 1]
    monads = [[], [0], [1, -1]]
    functions = [keep, increment, duplicate]
}

fn main() { print(42) }
"""


class LawCheckerTests(unittest.TestCase):
    def test_list_monad_passes_bounded_exhaustive_model(self) -> None:
        result = check_source(GOOD_MONAD)
        self.assertTrue(result.ok, [(item.code, item.message) for item in result.diagnostics])
        self.assertEqual(len(result.law_reports), 1)
        report = result.law_reports[0]
        self.assertTrue(report.passed)
        self.assertEqual(report.assurance, "bounded-exhaustive")
        self.assertEqual(report.cases_checked, 39)
        self.assertEqual(report.cases_planned, 39)
        self.assertEqual(len(report.model_digest), 16)

    def test_bad_pure_produces_compile_error_and_counterexample(self) -> None:
        source = GOOD_MONAD.replace(
            "fn list_pure(value: Int) -> List[Int] { return [value] }",
            "fn list_pure(value: Int) -> List[Int] { return [value, value] }",
        )
        result = check_source(source)
        self.assertFalse(result.ok)
        self.assertTrue(any(item.code == "L101" for item in result.diagnostics))
        self.assertTrue(any("a=0" in item.message for item in result.diagnostics))
        self.assertEqual(result.law_reports[0].status, "failed")

    def test_case_limit_is_enforced(self) -> None:
        source = GOOD_MONAD.replace(
            "functions = [keep, increment, duplicate]",
            "functions = [keep, increment, duplicate]\n    limit = 5",
        )
        result = check_source(source)
        self.assertFalse(result.ok)
        self.assertTrue(any(item.code == "L004" for item in result.diagnostics))

    def test_missing_entry_is_type_error_without_running_model(self) -> None:
        source = GOOD_MONAD.replace("    monads = [[], [0], [1, -1]]\n", "")
        result = check_source(source)
        self.assertFalse(result.ok)
        self.assertTrue(any(item.code == "E332" for item in result.diagnostics))
        self.assertEqual(result.law_reports, [])

    def test_law_check_can_be_disabled_for_tooling_parse(self) -> None:
        source = GOOD_MONAD.replace(
            "fn list_pure(value: Int) -> List[Int] { return [value] }",
            "fn list_pure(value: Int) -> List[Int] { return [value, value] }",
        )
        result = check_source(source, check_laws=False)
        self.assertTrue(result.ok)
        self.assertEqual(result.law_reports, [])

    def test_optional_bool_can_be_proven_over_complete_finite_carrier(self) -> None:
        source = """
fn optional_pure(value: Bool) -> Bool? { return value }
fn optional_bind(value: Bool?, next: Fn[Bool, Bool?]) -> Bool? {
    return if value == null { null } else { next(unwrap(value)) }
}
law monad OptionalBoolMonad {
    pure = optional_pure
    bind = optional_bind
    values = [false, true]
    monads = [null, false, true]
    functions = finite_functions([false, true], [null, false, true])
    complete = true
}
fn main() { print(42) }
"""
        result = check_source(source)
        self.assertTrue(result.ok, [(item.code, item.message) for item in result.diagnostics])
        report = result.law_reports[0]
        self.assertEqual(report.assurance, "proven-finite")
        self.assertEqual(report.cases_checked, 264)

    def test_complete_marker_cannot_upgrade_an_incomplete_sample_set(self) -> None:
        source = GOOD_MONAD.replace(
            "functions = [keep, increment, duplicate]",
            "functions = [keep, increment, duplicate]\n    complete = true",
        )
        result = check_source(source)
        self.assertFalse(result.ok)
        self.assertTrue(any(item.code == "L008" for item in result.diagnostics))

    def test_cli_emits_machine_readable_law_evidence(self) -> None:
        root = Path(__file__).resolve().parents[1]
        source_path = root / "examples" / "proven_optional_bool_monad.kofun"
        capture = io.StringIO()
        with contextlib.redirect_stdout(capture):
            status = cli.main(["laws", str(source_path), "--json", "--require-assurance", "proven-finite"])
        self.assertEqual(status, 0)
        evidence = json.loads(capture.getvalue())
        self.assertEqual(evidence["schema"], "kofun.law-evidence/v1")
        self.assertEqual(evidence["status"], "passed")
        self.assertEqual(len(evidence["source"]["sha256"]), 64)
        self.assertEqual(evidence["reports"][0]["assurance"], "proven-finite")
        self.assertEqual(evidence["reports"][0]["cases_checked"], 264)

    def test_cli_assurance_gate_rejects_bounded_evidence(self) -> None:
        root = Path(__file__).resolve().parents[1]
        source_path = root / "examples" / "lawful_list_monad.kofun"
        with tempfile.TemporaryDirectory(prefix="kofun-law-test-") as directory:
            output = Path(directory) / "evidence.json"
            capture = io.StringIO()
            with contextlib.redirect_stdout(capture):
                status = cli.main(
                    [
                        "laws",
                        str(source_path),
                        "--output",
                        str(output),
                        "--require-assurance",
                        "proven-finite",
                    ]
                )
            self.assertEqual(status, 1)
            evidence = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(evidence["status"], "failed")
            self.assertEqual(evidence["assurance_violations"][0]["actual"], "bounded-exhaustive")
            self.assertEqual(evidence["assurance_violations"][0]["required"], "proven-finite")


if __name__ == "__main__":
    unittest.main()
