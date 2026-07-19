from __future__ import annotations

import contextlib
import io
import unittest

from frost.evaluator import Evaluator
from frost.frontend import check_source


def run(source: str) -> list[str]:
    result = check_source(source)
    if not result.ok:
        raise AssertionError([(item.code, item.message) for item in result.diagnostics])
    evaluator = Evaluator()
    output = io.StringIO()
    with contextlib.redirect_stdout(output):
        evaluator.evaluate_program(result.program)
    return output.getvalue().splitlines()


class EvaluatorTests(unittest.TestCase):
    def test_recursive_function(self) -> None:
        output = run(
            "fn fib(n: Int) -> Int {\n"
            "  if n < 2 { return n }\n"
            "  return fib(n - 1) + fib(n - 2)\n"
            "}\n"
            "fn main() { print(fib(10)) }\n"
        )
        self.assertEqual(output, ["55"])

    def test_pipeline_and_lambdas(self) -> None:
        output = run(
            "fn main() {\n"
            " let x = 1 .. 6 |> map(fn(n: Int) => n * 2) |> sum()\n"
            " print(x)\n"
            "}\n"
        )
        self.assertEqual(output, ["30"])

    def test_list_methods(self) -> None:
        output = run(
            "fn main() {\n"
            " let x = [1, 2, 3].map(fn(n: Int) => n * n).reverse()\n"
            " print(x)\n"
            "}\n"
        )
        self.assertEqual(output, ["[9, 4, 1]"])

    def test_scientific_builtins(self) -> None:
        output = run(
            "fn main() {\n"
            " let x = [1.0, 2.0, 3.0]\n"
            " print(dot(x, x))\n"
            " print(mean(x))\n"
            "}\n"
        )
        self.assertEqual(output, ["14.0", "2.0"])

    def test_null_and_else_if(self) -> None:
        output = run(
            "fn label(x: Int) -> Text {\n"
            " return if x > 0 { \"positive\" } else if x < 0 { \"negative\" } else { \"zero\" }\n"
            "}\n"
            "fn main() { let x: Int? = null\n print(label(x ?? -1)) }\n"
        )
        self.assertEqual(output, ["negative"])

    def test_integer_floor_division(self) -> None:
        output = run("fn main() { print(7 // 2) }\n")
        self.assertEqual(output, ["3"])

    def test_loop_and_indexing(self) -> None:
        output = run(
            "fn main() {\n"
            " let values = [2, 4, 6]\n"
            " let mut total = 0\n"
            " for i in 0 .. len(values) { total = total + values[i] }\n"
            " print(total)\n"
            "}\n"
        )
        self.assertEqual(output, ["12"])

    def test_unicode_identifier(self) -> None:
        output = run("fn main() { let 合計 = 40 + 2\n print(合計) }\n")
        self.assertEqual(output, ["42"])


if __name__ == "__main__":
    unittest.main()
