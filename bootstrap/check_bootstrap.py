#!/usr/bin/env python3
from __future__ import annotations

import argparse
import contextlib
import io
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
sys.path.insert(0, str(SRC))

from kofun.c_backend import BackendFailure, CBackend, compile_c  # noqa: E402
from kofun.evaluator import Evaluator  # noqa: E402
from kofun.frontend import check_source  # noqa: E402


def verify_stage1() -> tuple[bool, str]:
    compiler_path = ROOT / "bootstrap" / "stage1" / "compiler.kofun"
    fixture_path = ROOT / "bootstrap" / "fixtures" / "answer.kofun"
    source = compiler_path.read_text(encoding="utf-8")
    result = check_source(source)
    if not result.ok:
        messages = "; ".join(f"{item.code}: {item.message}" for item in result.diagnostics)
        return False, f"Stage 1 compiler does not type-check: {messages}"

    cc = next((shutil.which(name) for name in ("cc", "clang", "gcc") if shutil.which(name)), None)
    if cc is None:
        return False, "no C compiler found"

    with tempfile.TemporaryDirectory(prefix="kofun-bootstrap-") as directory:
        work = Path(directory)
        interpreted_c = work / "answer-interpreted.c"
        native_c = work / "answer-native.c"
        stage1_c = work / "kofun-stage1.c"
        stage1_binary = work / "kofun-stage1"
        answer_binary = work / "answer"

        # Path A: Stage 0 interpreter executes the Kofun-written compiler.
        evaluator = Evaluator(program_args=[str(fixture_path), str(interpreted_c)])
        captured = io.StringIO()
        with contextlib.redirect_stdout(captured):
            evaluator.evaluate_program(result.program)
        if not interpreted_c.exists():
            return False, f"interpreted Stage 1 did not emit C; output was {captured.getvalue()!r}"

        # Path B: Stage 0 compiles the same Stage 1 source to a native compiler.
        try:
            stage1_source = CBackend().emit_program(result.program, str(compiler_path))
            compile_c(stage1_source, stage1_binary, emit_c=stage1_c, compiler=cc)
        except BackendFailure as failure:
            return False, f"Stage 0 could not build native Stage 1: {failure}"

        native_result = subprocess.run(
            [str(stage1_binary), str(fixture_path), str(native_c)],
            text=True,
            capture_output=True,
            check=False,
        )
        if native_result.returncode != 0:
            return False, (
                f"native Stage 1 exited {native_result.returncode}: "
                f"{native_result.stderr.strip() or native_result.stdout.strip()}"
            )
        if not native_c.exists():
            return False, f"native Stage 1 did not emit C; output was {native_result.stdout!r}"

        interpreted_bytes = interpreted_c.read_bytes()
        native_bytes = native_c.read_bytes()
        if interpreted_bytes != native_bytes:
            return False, "interpreted and native Stage 1 emitted different C11 artifacts"

        compile_result = subprocess.run(
            [cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", str(native_c), "-o", str(answer_binary)],
            text=True,
            capture_output=True,
            check=False,
        )
        if compile_result.returncode != 0:
            return False, f"Stage 1 generated C did not compile: {compile_result.stderr.strip()}"
        run_result = subprocess.run([str(answer_binary)], text=True, capture_output=True, check=False)
        if run_result.returncode != 0:
            return False, f"generated binary exited {run_result.returncode}: {run_result.stderr.strip()}"
        if run_result.stdout.strip() != "42":
            return False, f"generated binary returned {run_result.stdout.strip()!r}, expected '42'"

    return (
        True,
        "Stage 0 built native Stage 1; interpreted and native Stage 1 emitted identical C11; "
        "the native fixture returned 42",
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--require-stage2", action="store_true")
    args = parser.parse_args()

    ok, detail = verify_stage1()
    if not ok:
        print(f"FAIL: {detail}", file=sys.stderr)
        return 1
    print(f"PASS: {detail}")

    manifest = json.loads((ROOT / "bootstrap" / "manifest.json").read_text(encoding="utf-8"))
    stage2 = manifest["stages"]["stage2"]["status"]
    print(f"Stage 2 self-recompile gate: {stage2}")
    if args.require_stage2 and stage2 != "working":
        print("FAIL: true self-hosting fixed point is not complete", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
