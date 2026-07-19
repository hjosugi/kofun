from __future__ import annotations

import unittest

from kofun.formatter import format_source


class FormatterTests(unittest.TestCase):
    def test_indents_blocks_and_preserves_comments(self) -> None:
        source = "fn main() {\n# comment\nlet x = 1\nif x > 0 {\nprint(x)\n}\n}\n"
        expected = "fn main() {\n    # comment\n    let x = 1\n    if x > 0 {\n        print(x)\n    }\n}\n"
        self.assertEqual(format_source(source), expected)

    def test_is_idempotent(self) -> None:
        source = "fn main() {\n    print(42)\n}\n"
        self.assertEqual(format_source(format_source(source)), source)


if __name__ == "__main__":
    unittest.main()
