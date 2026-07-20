"""Flow-sensitive narrowing of optional types.

Proving a value is not null should let it be used as the non-optional type, but
only where the proof actually holds. The rejection tests matter more than the
acceptance tests: an unsound narrowing silently reintroduces exactly the null
dereference the type system exists to prevent.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))

from kofun.frontend import check_source     # noqa: E402


def diagnostics(source: str) -> list[str]:
    return [d.message for d in check_source(source).diagnostics]


def accepts(source: str) -> bool:
    return not diagnostics(source)


class NarrowingAcceptsProvableCases(unittest.TestCase):
    def test_inequality_check_narrows_the_then_branch(self) -> None:
        self.assertTrue(accepts("""
fn main() {
    let x: Int? = 5
    if x != null {
        print(x + 1)
    }
}
"""))

    def test_equality_check_narrows_the_else_branch(self) -> None:
        self.assertTrue(accepts("""
fn main() {
    let x: Int? = 5
    if x == null {
        print(0)
    } else {
        print(x + 1)
    }
}
"""))

    def test_early_return_guard_narrows_the_remainder(self) -> None:
        self.assertTrue(accepts("""
fn f(x: Int?) -> Int {
    if x == null {
        return 0
    }
    return x + 1
}
"""))

    def test_null_on_the_left_of_the_comparison_also_narrows(self) -> None:
        self.assertTrue(accepts("""
fn main() {
    let x: Int? = 5
    if null != x {
        print(x + 1)
    }
}
"""))

    def test_reassignment_is_checked_against_the_declared_type(self) -> None:
        # The narrowing must not make a legal assignment illegal: `x` was
        # declared `Int?`, so it still accepts null afterwards.
        self.assertTrue(accepts("""
fn main() {
    let mut x: Int? = 5
    if x != null {
        print(x + 1)
    }
    x = null
    print(x ?? 0)
}
"""))


class NarrowingRejectsUnprovableCases(unittest.TestCase):
    """Each of these would be a null dereference if narrowing were unsound."""

    def test_unchecked_optional_is_still_rejected(self) -> None:
        self.assertTrue(diagnostics("""
fn main() {
    let x: Int? = null
    print(x + 1)
}
"""))

    def test_narrowing_does_not_leak_into_the_other_branch(self) -> None:
        self.assertTrue(diagnostics("""
fn main() {
    let x: Int? = 5
    if x != null {
        print(x + 1)
    } else {
        print(x + 1)
    }
"""[:-1] + "}\n"))

    def test_narrowing_does_not_outlive_a_non_returning_if(self) -> None:
        self.assertTrue(diagnostics("""
fn main() {
    let x: Int? = 5
    if x != null {
        print(1)
    }
    print(x + 1)
}
"""))

    def test_a_guard_that_does_not_return_does_not_narrow(self) -> None:
        self.assertTrue(diagnostics("""
fn f(x: Int?) -> Int {
    if x == null {
        print(0)
    }
    return x + 1
}
"""))

    def test_reassigning_null_discards_the_narrowing(self) -> None:
        self.assertTrue(diagnostics("""
fn main() {
    let mut x: Int? = 5
    if x != null {
        x = null
        print(x + 1)
    }
}
"""))

    def test_comparison_against_a_non_null_value_does_not_narrow(self) -> None:
        # `x != 0` says nothing about whether x is null.
        self.assertTrue(diagnostics("""
fn main() {
    let x: Int? = 5
    if x != 0 {
        print(x + 1)
    }
}
"""))

    def test_null_still_cannot_enter_a_non_optional(self) -> None:
        self.assertTrue(diagnostics("""
fn main() {
    let x: Int = null
    print(x)
}
"""))

    def test_optional_still_cannot_be_passed_as_non_optional(self) -> None:
        self.assertTrue(diagnostics("""
fn f(n: Int) -> Int {
    return n * 2
}

fn main() {
    let x: Int? = 3
    print(f(x))
}
"""))


if __name__ == "__main__":
    unittest.main()
