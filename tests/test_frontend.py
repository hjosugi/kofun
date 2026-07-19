from __future__ import annotations

import unittest

from cofn.frontend import check_source, parse_source
from cofn.lexer import Lexer
from cofn.tokens import TokenKind


class LexerParserTests(unittest.TestCase):
    def test_lexer_recognizes_floor_division_and_coalesce(self) -> None:
        tokens, diagnostics = Lexer("let x = 7 // 2\nlet y = null ?? x\n").lex()
        self.assertFalse(diagnostics.has_errors)
        kinds = [token.kind for token in tokens]
        self.assertIn(TokenKind.FLOOR_DIV, kinds)
        self.assertIn(TokenKind.COALESCE, kinds)

    def test_leading_pipeline_continues_expression(self) -> None:
        result = parse_source(
            "fn main() {\n"
            "  let x = [1, 2]\n"
            "    |> map(fn(n: Int) => n + 1)\n"
            "  print(x)\n"
            "}\n"
        )
        self.assertTrue(result.ok, [d.message for d in result.diagnostics])

    def test_else_if_parses(self) -> None:
        result = check_source(
            "fn f(x: Int) -> Text {\n"
            "  return if x > 0 { \"p\" } else if x < 0 { \"n\" } else { \"z\" }\n"
            "}\n"
        )
        self.assertTrue(result.ok, [d.message for d in result.diagnostics])

    def test_null_requires_optional_annotation(self) -> None:
        result = check_source("fn main() { let x: Int = null }\n")
        self.assertFalse(result.ok)
        self.assertTrue(any(item.code == "E329" for item in result.diagnostics))

    def test_optional_accepts_null(self) -> None:
        result = check_source("fn main() { let x: Int? = null\n print(x ?? 1) }\n")
        self.assertTrue(result.ok, [d.message for d in result.diagnostics])

    def test_use_after_take_is_static_error(self) -> None:
        result = check_source(
            "fn main() {\n"
            "  let own x = resource(\"x\")\n"
            "  take x\n"
            "  print(is_open(x))\n"
            "}\n"
        )
        self.assertFalse(result.ok)
        self.assertTrue(any(item.code == "E330" for item in result.diagnostics))

    def test_immutable_assignment_is_error(self) -> None:
        result = check_source("fn main() { let x = 1\n x = 2 }\n")
        self.assertFalse(result.ok)
        self.assertTrue(any(item.code == "E305" for item in result.diagnostics))

    def test_mutable_assignment_is_allowed(self) -> None:
        result = check_source("fn main() { let mut x = 1\n x = 2\n print(x) }\n")
        self.assertTrue(result.ok, [d.message for d in result.diagnostics])


if __name__ == "__main__":
    unittest.main()
