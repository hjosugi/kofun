from __future__ import annotations

import argparse
import contextlib
import hashlib
import io
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

from .c_backend import BackendFailure, CBackend, compile_c
from .native_backend import compile_to_executable
from .diagnostics import Diagnostic, CofnError, read_source, render_diagnostic
from .evaluator import Evaluator, display
from .formatter import format_source
from .frontend import check_source, parse_source

VERSION = "0.2.0-bootstrap"

ASSURANCE_RANK = {
    "unsupported": 0,
    "uncertified": 0,
    "bounded-exhaustive": 1,
    "proven-finite": 2,
    "proven": 3,
}
ASSURANCE_CHOICES = ("bounded-exhaustive", "proven-finite", "proven")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="cofn",
        description="Cofn bootstrap language toolchain",
    )
    parser.add_argument("--version", action="version", version=f"Cofn {VERSION}")
    sub = parser.add_subparsers(dest="command", required=True)

    run = sub.add_parser("run", help="type-check and run a source file")
    run.add_argument("file")
    run.add_argument("--native", action="store_true", help="compile the supported subset through C11")
    run.add_argument("--emit-c", type=Path, help="keep generated C at this path")
    run.add_argument("program_args", nargs=argparse.REMAINDER)

    check = sub.add_parser("check", help="parse and type-check a source file")
    check.add_argument("file")
    check.add_argument(
        "--require-law-assurance",
        choices=ASSURANCE_CHOICES,
        help="reject declared laws below this evidence level",
    )

    laws = sub.add_parser("laws", help="check declared algebraic laws and print model evidence")
    laws.add_argument("file")
    laws.add_argument("--json", action="store_true", help="emit machine-readable evidence")
    laws.add_argument("-o", "--output", type=Path, help="write JSON evidence to this file")
    laws.add_argument(
        "--require-assurance",
        choices=ASSURANCE_CHOICES,
        help="reject declared laws below this evidence level",
    )

    build = sub.add_parser("build", help="compile a source file to a native executable")
    build.add_argument("file")
    build.add_argument("-o", "--output", type=Path)
    build.add_argument(
        "--backend",
        choices=("native", "c"),
        default="native",
        help=(
            "native: emit x86-64 machine code and a static ELF directly, with no "
            "external toolchain. c: lower to C11 and invoke a C compiler, which "
            "supports more of the language but needs cc installed."
        ),
    )
    build.add_argument("--emit-c", type=Path, help="write generated C (implies --backend c)")
    build.add_argument("--cc", help="C compiler executable (--backend c only)")
    build.add_argument(
        "--require-law-assurance",
        choices=ASSURANCE_CHOICES,
        help="reject declared laws below this evidence level",
    )

    fmt = sub.add_parser("fmt", help="format Cofn source")
    fmt.add_argument("files", nargs="+")
    fmt.add_argument("-w", "--write", action="store_true")
    fmt.add_argument("--check", action="store_true")

    repl = sub.add_parser("repl", help="start an interactive reference interpreter")
    repl.add_argument("--no-banner", action="store_true")

    test = sub.add_parser("test", help="run .cofn files containing # expect: output directives")
    test.add_argument("path", nargs="?", default="tests/cofn")

    new = sub.add_parser("new", help="create a small Cofn project")
    new.add_argument("name")

    ast_cmd = sub.add_parser("ast", help="print the parsed AST for debugging")
    ast_cmd.add_argument("file")

    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.command == "run":
            return command_run(args)
        if args.command == "check":
            return command_check(args)
        if args.command == "laws":
            return command_laws(args)
        if args.command == "build":
            return command_build(args)
        if args.command == "fmt":
            return command_fmt(args)
        if args.command == "repl":
            return command_repl(args)
        if args.command == "test":
            return command_test(args)
        if args.command == "new":
            return command_new(args)
        if args.command == "ast":
            return command_ast(args)
    except KeyboardInterrupt:
        print("interrupted", file=sys.stderr)
        return 130
    return 2


def command_run(args: argparse.Namespace) -> int:
    source, filename = read_source(args.file)
    result = check_source(source)
    if not report_diagnostics(source, filename, result.diagnostics):
        return 1
    if args.native:
        try:
            c_source = CBackend().emit_program(result.program, filename)
            with tempfile.TemporaryDirectory(prefix="cofn-") as directory:
                output = Path(directory) / "program"
                compile_c(c_source, output, emit_c=args.emit_c)
                completed = subprocess.run([str(output), *args.program_args], check=False)
                return completed.returncode
        except BackendFailure as failure:
            print_backend_failure(source, filename, failure)
            return 1
    program_args = list(args.program_args)
    if program_args and program_args[0] == "--":
        program_args = program_args[1:]
    evaluator = Evaluator(program_args=program_args)
    try:
        evaluator.evaluate_program(result.program)
        return 0
    except CofnError as error:
        print(render_diagnostic(source, error.diagnostic, filename), file=sys.stderr)
        return 1


def command_check(args: argparse.Namespace) -> int:
    source, filename = read_source(args.file)
    result = check_source(source)
    ok = report_diagnostics(source, filename, result.diagnostics)
    assurance_ok = report_assurance_requirement(
        result.law_reports,
        args.require_law_assurance,
        filename=filename,
    )
    ok = ok and assurance_ok
    if ok:
        print(f"ok: {filename}")
        print_law_reports(result.law_reports)
    return 0 if ok else 1


def command_laws(args: argparse.Namespace) -> int:
    source, filename = read_source(args.file)
    result = check_source(source)
    violations = assurance_violations(result.law_reports, args.require_assurance)
    ok = result.ok and not violations

    if args.json or args.output is not None:
        document = law_evidence_document(
            source,
            filename,
            result.diagnostics,
            result.law_reports,
            args.require_assurance,
            violations,
        )
        rendered = json.dumps(document, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
        if args.output is not None:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(rendered, encoding="utf-8")
        if args.json:
            print(rendered, end="")
        elif args.output is not None:
            print(args.output.resolve())
        return 0 if ok else 1

    report_diagnostics(source, filename, result.diagnostics)
    report_assurance_requirement(result.law_reports, args.require_assurance, filename=filename)
    if not result.law_reports and ok:
        print(f"no law declarations: {filename}")
        return 0
    print_law_reports(result.law_reports, verbose=True)
    return 0 if ok else 1


def command_build(args: argparse.Namespace) -> int:
    source, filename = read_source(args.file)
    result = check_source(source)
    if not report_diagnostics(source, filename, result.diagnostics):
        return 1
    if not report_assurance_requirement(
        result.law_reports,
        args.require_law_assurance,
        filename=filename,
    ):
        return 1
    input_path = Path(args.file)
    output = args.output or input_path.with_suffix("")
    backend = "c" if args.emit_c else args.backend
    try:
        if backend == "native":
            compile_to_executable(result.program, str(output), filename)
        else:
            c_source = CBackend().emit_program(result.program, filename)
            compile_c(c_source, output, emit_c=args.emit_c, compiler=args.cc)
    except BackendFailure as failure:
        print_backend_failure(source, filename, failure)
        if backend == "native":
            # The C backend covers more of the language; say so rather than
            # falling back silently and changing which semantics apply.
            print(
                "note: the C11 backend supports more features; retry with --backend c",
                file=sys.stderr,
            )
        return 1
    print(output.resolve())
    return 0


def command_fmt(args: argparse.Namespace) -> int:
    changed = False
    failed = False
    for filename in args.files:
        path = Path(filename)
        source = path.read_text(encoding="utf-8")
        formatted = format_source(source)
        if formatted != source:
            changed = True
            if args.check:
                print(f"needs formatting: {path}", file=sys.stderr)
                failed = True
            elif args.write:
                path.write_text(formatted, encoding="utf-8")
            else:
                if len(args.files) > 1:
                    print(f"// --- {path} ---")
                print(formatted, end="")
        elif not args.write and not args.check:
            if len(args.files) > 1:
                print(f"// --- {path} ---")
            print(source, end="")
    if args.write and changed:
        print("formatted")
    return 1 if failed else 0


def command_repl(args: argparse.Namespace) -> int:
    if not args.no_banner:
        print(f"Cofn {VERSION} reference REPL. :quit exits; :help shows commands.")
    evaluator = Evaluator()
    buffer: list[str] = []
    balance = 0
    while True:
        prompt = "... " if buffer else ">>> "
        try:
            line = input(prompt)
        except EOFError:
            print()
            break
        if not buffer and line.strip().startswith(":"):
            command = line.strip()
            if command in {":quit", ":q", ":exit"}:
                break
            if command == ":help":
                print(":quit  exit\n:help  show this message\n:reset clear definitions")
                continue
            if command == ":reset":
                evaluator = Evaluator()
                print("state reset")
                continue
            print(f"unknown REPL command: {command}")
            continue
        buffer.append(line)
        balance += brace_balance(line)
        if balance > 0:
            continue
        source = "\n".join(buffer) + "\n"
        buffer = []
        balance = 0
        parsed = parse_source(source)
        if not report_diagnostics(source, "<repl>", parsed.diagnostics):
            continue
        try:
            value = evaluator.evaluate_program(parsed.program, call_main=False)
            if value is not None:
                print(display(value))
        except CofnError as error:
            print(render_diagnostic(source, error.diagnostic, "<repl>"), file=sys.stderr)
    return 0


def command_test(args: argparse.Namespace) -> int:
    root = Path(args.path)
    files = [root] if root.is_file() else sorted(root.rglob("*.cofn"))
    if not files:
        print(f"no .cofn tests found under {root}", file=sys.stderr)
        return 1
    passed = 0
    failed = 0
    for path in files:
        source = path.read_text(encoding="utf-8")
        expected = [line.split("# expect:", 1)[1].strip() for line in source.splitlines() if "# expect:" in line]
        result = check_source(source)
        if any(item.severity == "error" for item in result.diagnostics):
            print(f"FAIL {path}: type/parse error")
            report_diagnostics(source, str(path), result.diagnostics)
            failed += 1
            continue
        evaluator = Evaluator()
        capture = io.StringIO()
        try:
            with contextlib.redirect_stdout(capture):
                evaluator.evaluate_program(result.program)
            actual = capture.getvalue().splitlines()
        except CofnError as error:
            print(f"FAIL {path}: {error.diagnostic.message}")
            failed += 1
            continue
        if expected and actual != expected:
            print(f"FAIL {path}: expected {expected!r}, found {actual!r}")
            failed += 1
        else:
            print(f"PASS {path}")
            passed += 1
    print(f"{passed} passed; {failed} failed")
    return 0 if failed == 0 else 1


def command_new(args: argparse.Namespace) -> int:
    root = Path(args.name)
    if root.exists() and any(root.iterdir()):
        print(f"destination is not empty: {root}", file=sys.stderr)
        return 1
    (root / "src").mkdir(parents=True, exist_ok=True)
    (root / "tests" / "cofn").mkdir(parents=True, exist_ok=True)
    (root / "cofn.toml").write_text(
        '[package]\nname = "' + root.name + '"\nversion = "0.1.0"\n\n[build]\nbackend = "interpreter"\n',
        encoding="utf-8",
    )
    (root / "src" / "main.cofn").write_text(
        'fn main() {\n    let message = "hello from Cofn"\n    print(message)\n}\n',
        encoding="utf-8",
    )
    (root / "tests" / "cofn" / "basic.cofn").write_text(
        '# expect: 42\nfn main() {\n    print(40 + 2)\n}\n',
        encoding="utf-8",
    )
    print(root.resolve())
    return 0


def command_ast(args: argparse.Namespace) -> int:
    import pprint

    source, filename = read_source(args.file)
    result = parse_source(source)
    if not report_diagnostics(source, filename, result.diagnostics):
        return 1
    pprint.pp(result.program, width=100, sort_dicts=False)
    return 0


def report_diagnostics(source: str, filename: str, diagnostics: list[Diagnostic]) -> bool:
    for diagnostic in diagnostics:
        stream = sys.stderr if diagnostic.severity == "error" else sys.stdout
        print(render_diagnostic(source, diagnostic, filename), file=stream)
    return not any(item.severity == "error" for item in diagnostics)


def print_law_reports(reports: list[object], *, verbose: bool = False) -> None:
    for report in reports:
        status = getattr(report, "status", "unknown")
        name = getattr(report, "name", "<unnamed>")
        family = getattr(report, "family", "law")
        checked = getattr(report, "cases_checked", 0)
        planned = getattr(report, "cases_planned", 0)
        assurance = getattr(report, "assurance", "unknown")
        digest = getattr(report, "model_digest", "")
        print(
            f"law {name}: {status} {family}; assurance={assurance}; "
            f"cases={checked}/{planned}; model={digest or '-'}"
        )
        if verbose:
            for failure in getattr(report, "failures", []):
                inputs = ", ".join(f"{key}={value}" for key, value in failure.inputs.items())
                detail = failure.detail or f"left={failure.left}; right={failure.right}"
                print(f"  {failure.law}: {inputs}; {detail}")


def assurance_violations(reports: list[object], required: str | None) -> list[dict[str, str]]:
    if required is None:
        return []
    required_rank = ASSURANCE_RANK[required]
    violations: list[dict[str, str]] = []
    for report in reports:
        actual = str(getattr(report, "assurance", "unsupported"))
        if ASSURANCE_RANK.get(actual, 0) < required_rank:
            violations.append(
                {
                    "law": str(getattr(report, "name", "<unnamed>")),
                    "required": required,
                    "actual": actual,
                }
            )
    return violations


def report_assurance_requirement(
    reports: list[object],
    required: str | None,
    *,
    filename: str,
) -> bool:
    violations = assurance_violations(reports, required)
    for violation in violations:
        print(
            f"error[L200]: law `{violation['law']}` has assurance={violation['actual']}; "
            f"required={violation['required']}\n  --> {filename}",
            file=sys.stderr,
        )
    return not violations


def law_evidence_document(
    source: str,
    filename: str,
    diagnostics: list[Diagnostic],
    reports: list[object],
    required: str | None,
    violations: list[dict[str, str]],
) -> dict[str, object]:
    return {
        "schema": "cofn.law-evidence/v1",
        "compiler": {"name": "Cofn", "version": VERSION},
        "source": {
            "path": filename,
            "sha256": hashlib.sha256(source.encode("utf-8")).hexdigest(),
        },
        "status": "passed"
        if not any(item.severity == "error" for item in diagnostics) and not violations
        else "failed",
        "required_assurance": required,
        "assurance_violations": violations,
        "diagnostics": [diagnostic_as_dict(item) for item in diagnostics],
        "reports": [report.as_dict() for report in reports],
    }


def diagnostic_as_dict(diagnostic: Diagnostic) -> dict[str, object]:
    return {
        "code": diagnostic.code,
        "severity": diagnostic.severity,
        "message": diagnostic.message,
        "hint": diagnostic.hint,
        "span": {
            "start": {
                "line": diagnostic.span.line,
                "column": diagnostic.span.column,
            },
            "end": {
                "line": diagnostic.span.end_line,
                "column": diagnostic.span.end_column,
            },
        },
    }


def print_backend_failure(source: str, filename: str, failure: BackendFailure) -> None:
    if failure.span is not None:
        diagnostic = Diagnostic(failure.message, failure.span, "C001")
        print(render_diagnostic(source, diagnostic, filename), file=sys.stderr)
    else:
        print(f"error[C001]: {failure.message}", file=sys.stderr)


def brace_balance(line: str) -> int:
    in_string = False
    escaped = False
    result = 0
    for char in line:
        if not in_string and char == "#":
            break
        if in_string:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                in_string = False
            continue
        if char == '"':
            in_string = True
        elif char == "{":
            result += 1
        elif char == "}":
            result -= 1
    return result


if __name__ == "__main__":
    raise SystemExit(main())
