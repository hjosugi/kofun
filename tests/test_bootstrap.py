from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "kofun_bootstrap_check",
    ROOT / "bootstrap" / "check_bootstrap.py",
)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class BootstrapTests(unittest.TestCase):
    def test_kofun_written_stage1_seed_compiles_native_fixture(self) -> None:
        ok, detail = MODULE.verify_stage1()
        self.assertTrue(ok, detail)


if __name__ == "__main__":
    unittest.main()
