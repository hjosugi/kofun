#!/usr/bin/env python3
# run_spec.py -- executable-spec runner (golden tests, implementation-agnostic).
#
# Each spec is a pair:
#   spec/<area>/NNN_name.lang       source program
#   spec/<area>/NNN_name.expected   expected combined stdout (incl. diagnostics)
#
# By default specs run against the reference interpreter (langc/langc.py).
# To run the SAME specs against another implementation (differential testing):
#   python3 run_spec.py --cmd "path/to/your-compiler run {file}"
#
# Options:
#   --filter SUBSTR   run only specs whose path contains SUBSTR
#   --update          bless current output into .expected files

import argparse
import difflib
import pathlib
import shlex
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent
SPEC = ROOT / "spec"
LANGC = ROOT / "langc" / "langc.py"


def run_one(path, cmd_template):
    if cmd_template:
        cmd = [a.replace("{file}", str(path)) for a in shlex.split(cmd_template)]
    else:
        cmd = [sys.executable, str(LANGC), "run", str(path)]
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
    out = proc.stdout
    if proc.stderr.strip():
        out += proc.stderr
    return out.rstrip("\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--filter", default="")
    ap.add_argument("--update", action="store_true")
    ap.add_argument("--cmd", default="",
                    help="command template with {file}, e.g. 'mylang run {file}'")
    args = ap.parse_args()

    specs = sorted(SPEC.rglob("*.lang"))
    if args.filter:
        specs = [s for s in specs if args.filter in str(s)]
    if not specs:
        print("no specs found")
        return 1

    passed, failed = 0, 0
    for src in specs:
        rel = src.relative_to(ROOT)
        exp_path = src.with_suffix(".expected")
        try:
            got = run_one(src, args.cmd)
        except subprocess.TimeoutExpired:
            got = "<timeout>"
        if args.update:
            exp_path.write_text(got + "\n", encoding="utf-8")
            print(f"UPDATED {rel}")
            continue
        want = exp_path.read_text(encoding="utf-8").rstrip("\n") if exp_path.exists() else "<missing .expected>"
        if got == want:
            passed += 1
            print(f"PASS  {rel}")
        else:
            failed += 1
            print(f"FAIL  {rel}")
            diff = difflib.unified_diff(
                want.splitlines(), got.splitlines(),
                fromfile="expected", tofile="actual", lineterm="")
            for line in diff:
                print("      " + line)
    if not args.update:
        print(f"\n{passed} passed, {failed} failed, {passed + failed} total")
        return 1 if failed else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
