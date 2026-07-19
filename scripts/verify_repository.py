#!/usr/bin/env python3
from __future__ import annotations

import ast as py_ast
import hashlib
import json
import re
import sys
from pathlib import Path
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[1]
TEXT_SUFFIXES = {
    ".frost",
    ".py",
    ".md",
    ".json",
    ".toml",
    ".ebnf",
    ".txt",
    ".sh",
}


def fail(errors: list[str], message: str) -> None:
    errors.append(message)


def read_utf8(path: Path, errors: list[str]) -> str:
    data = path.read_bytes()
    if b"\r" in data:
        fail(errors, f"non-LF line ending: {path.relative_to(ROOT)}")
    try:
        return data.decode("utf-8")
    except UnicodeDecodeError as error:
        fail(errors, f"invalid UTF-8: {path.relative_to(ROOT)}: {error}")
        return ""


def check_required_files(errors: list[str]) -> None:
    required = (
        "README.md",
        "Makefile",
        "pyproject.toml",
        "LICENSE-APACHE",
        "LICENSE-MIT",
        "NOTICE",
        "bin/frost",
        "src/frost/cli.py",
        "src/frost/c_runtime.py",
        "src/frost/laws.py",
        "bootstrap/stage1/compiler.frost",
        "bootstrap/manifest.json",
        "artifacts/optional-bool-monad.evidence.json",
        "artifacts/verification-summary.json",
        "spec/grammar.ebnf",
        "spec/law-evidence.schema.json",
        "spec/semantics.md",
        "editor/vscode/package.json",
        "editor/vscode/syntaxes/frost.tmLanguage.json",
        "backlog/summary.json",
    )
    for relative in required:
        path = ROOT / relative
        if not path.is_file():
            fail(errors, f"missing required file: {relative}")


def check_versions(errors: list[str]) -> None:
    pyproject = read_utf8(ROOT / "pyproject.toml", errors)
    init_source = read_utf8(ROOT / "src/frost/__init__.py", errors)
    cli_source = read_utf8(ROOT / "src/frost/cli.py", errors)
    editor = json.loads(read_utf8(ROOT / "editor/vscode/package.json", errors) or "{}")

    project_match = re.search(r'^version\s*=\s*"([^"]+)"', pyproject, re.MULTILINE)
    init_match = re.search(r'^__version__\s*=\s*"([^"]+)"', init_source, re.MULTILINE)
    cli_match = re.search(r'^VERSION\s*=\s*"([^"]+)"', cli_source, re.MULTILINE)
    if not project_match or not init_match or not cli_match:
        fail(errors, "cannot extract one or more version declarations")
        return
    project = project_match.group(1)
    expected_runtime = f"{project}-bootstrap"
    if init_match.group(1) != expected_runtime:
        fail(errors, f"package version mismatch: {init_match.group(1)} != {expected_runtime}")
    if cli_match.group(1) != expected_runtime:
        fail(errors, f"CLI version mismatch: {cli_match.group(1)} != {expected_runtime}")
    if editor.get("version") != project:
        fail(errors, f"VS Code metadata version mismatch: {editor.get('version')} != {project}")


def check_json_files(errors: list[str]) -> None:
    for path in sorted(ROOT.rglob("*.json")):
        if any(part == "__pycache__" for part in path.parts):
            continue
        try:
            json.loads(read_utf8(path, errors))
        except json.JSONDecodeError as error:
            fail(errors, f"invalid JSON: {path.relative_to(ROOT)}: {error}")


def check_python_syntax(errors: list[str]) -> None:
    for path in sorted(ROOT.rglob("*.py")):
        if any(part == "__pycache__" for part in path.parts):
            continue
        source = read_utf8(path, errors)
        try:
            py_ast.parse(source, filename=str(path))
        except SyntaxError as error:
            fail(errors, f"invalid Python: {path.relative_to(ROOT)}:{error.lineno}: {error.msg}")


def check_text_encoding(errors: list[str]) -> None:
    for path in sorted(ROOT.rglob("*")):
        if not path.is_file() or path.suffix not in TEXT_SUFFIXES:
            continue
        if any(part == "__pycache__" for part in path.parts):
            continue
        read_utf8(path, errors)


def check_markdown_links(errors: list[str]) -> None:
    pattern = re.compile(r"!?\[[^\]]*\]\(([^)]+)\)")
    for path in sorted(ROOT.rglob("*.md")):
        source = read_utf8(path, errors)
        in_fence = False
        offset = 0
        for line in source.splitlines(keepends=True):
            if line.lstrip().startswith("```"):
                in_fence = not in_fence
                offset += len(line)
                continue
            if in_fence:
                offset += len(line)
                continue
            for match in pattern.finditer(line):
                raw_target = match.group(1).strip()
                if raw_target.startswith("<") and raw_target.endswith(">"):
                    raw_target = raw_target[1:-1]
                # Strip an optional Markdown title. Internal paths in this project do not contain spaces.
                raw_target = raw_target.split()[0]
                if not raw_target or raw_target.startswith("#"):
                    continue
                if re.match(r"^[A-Za-z][A-Za-z0-9+.-]*:", raw_target):
                    continue
                target_text = unquote(raw_target.split("#", 1)[0].split("?", 1)[0])
                target = (path.parent / target_text).resolve()
                try:
                    target.relative_to(ROOT)
                except ValueError:
                    fail(errors, f"Markdown link escapes repository: {path.relative_to(ROOT)} -> {raw_target}")
                    continue
                if not target.exists():
                    line_number = source.count("\n", 0, offset + match.start()) + 1
                    fail(errors, f"broken Markdown link: {path.relative_to(ROOT)}:{line_number} -> {raw_target}")
            offset += len(line)


def check_backlog_metadata(errors: list[str]) -> None:
    summary_path = ROOT / "backlog/summary.json"
    summary = json.loads(read_utf8(summary_path, errors) or "{}")
    expected = {
        "total": 13_500,
        "first_id": "FROST-00001",
        "last_id": "FROST-13500",
        "areas": 27,
    }
    for key, value in expected.items():
        if summary.get(key) != value:
            fail(errors, f"backlog summary {key}: {summary.get(key)!r} != {value!r}")
    issue_files = sorted((ROOT / "backlog").glob("issues-*.md"))
    if len(issue_files) != 27:
        fail(errors, f"expected 27 backlog area files, found {len(issue_files)}")


def check_bootstrap_manifest(errors: list[str]) -> None:
    manifest = json.loads(read_utf8(ROOT / "bootstrap/manifest.json", errors) or "{}")
    stages = manifest.get("stages", {})
    expected = {
        "stage0": "working",
        "stage1": "working-seed",
        "stage2": "open",
    }
    for stage, status in expected.items():
        actual = stages.get(stage, {}).get("status")
        if actual != status:
            fail(errors, f"bootstrap status {stage}: {actual!r} != {status!r}")
    truthful = str(manifest.get("truthful_status", "")).lower()
    if "not yet" not in truthful or "self-host" not in truthful:
        fail(errors, "bootstrap manifest must explicitly state that full self-hosting is not complete")
    gates = manifest.get("gates", {})
    expected_gates = {
        "stage0_builds_native_stage1": "working",
        "interpreted_native_stage1_equivalence": "working",
        "stage1_self_recompile": "open",
    }
    for gate, status in expected_gates.items():
        actual = gates.get(gate)
        if actual != status:
            fail(errors, f"bootstrap gate {gate}: {actual!r} != {status!r}")


def check_documented_counts(errors: list[str]) -> None:
    for relative in ("README.md", "docs/CODING_INTERVIEW.md", "docs/MVP_IMPLEMENTED.md", "docs/ROADMAP.md"):
        source = read_utf8(ROOT / relative, errors)
        if "12,500" in source:
            fail(errors, f"stale 12,500 backlog count in {relative}")
    readme = read_utf8(ROOT / "README.md", errors)
    for required in (
        "13,500",
        "proven-finite",
        "frost.law-evidence/v1",
        "Stage 2 self-recompile",
        "full self-hosting",
    ):
        if required not in readme:
            fail(errors, f"README is missing required status phrase: {required}")


def check_law_evidence(errors: list[str]) -> None:
    evidence_path = ROOT / "artifacts/optional-bool-monad.evidence.json"
    evidence = json.loads(read_utf8(evidence_path, errors) or "{}")
    source_path = ROOT / "examples/proven_optional_bool_monad.frost"
    source_bytes = source_path.read_bytes()
    expected_hash = hashlib.sha256(source_bytes).hexdigest()
    if evidence.get("schema") != "frost.law-evidence/v1":
        fail(errors, "law evidence schema mismatch")
    if evidence.get("status") != "passed":
        fail(errors, "law evidence must be passing")
    if evidence.get("source", {}).get("sha256") != expected_hash:
        fail(errors, "law evidence source hash is stale")
    reports = evidence.get("reports", [])
    if len(reports) != 1:
        fail(errors, f"expected one law evidence report, found {len(reports)}")
        return
    report = reports[0]
    expected = {
        "name": "OptionalBoolMonad",
        "status": "passed",
        "assurance": "proven-finite",
        "cases_checked": 264,
        "cases_planned": 264,
    }
    for key, value in expected.items():
        if report.get(key) != value:
            fail(errors, f"law evidence {key}: {report.get(key)!r} != {value!r}")


def check_verification_summary(errors: list[str]) -> None:
    summary = json.loads(read_utf8(ROOT / "artifacts/verification-summary.json", errors) or "{}")
    expected_paths = {
        ("schema",): "frost.verification/v1",
        ("toolchain", "frost"): "0.2.0-bootstrap",
        ("tests", "python_unit"): 31,
        ("tests", "frost_language"): 5,
        ("laws", "optional_bool_monad", "cases"): 264,
        ("bootstrap", "stage0_builds_native_stage1"): True,
        ("bootstrap", "stage2_self_recompile"): False,
        ("bootstrap", "full_self_hosting"): False,
        ("backlog", "issues"): 13_500,
        ("backlog", "areas"): 27,
    }
    for path, expected in expected_paths.items():
        value = summary
        for key in path:
            value = value.get(key) if isinstance(value, dict) else None
        if value != expected:
            fail(errors, f"verification summary {'.'.join(path)}: {value!r} != {expected!r}")


def main() -> int:
    errors: list[str] = []
    check_required_files(errors)
    if errors:
        for message in errors:
            print(f"error: {message}", file=sys.stderr)
        return 1

    check_versions(errors)
    check_json_files(errors)
    check_python_syntax(errors)
    check_text_encoding(errors)
    check_markdown_links(errors)
    check_backlog_metadata(errors)
    check_bootstrap_manifest(errors)
    check_documented_counts(errors)
    check_law_evidence(errors)
    check_verification_summary(errors)

    if errors:
        for message in errors:
            print(f"error: {message}", file=sys.stderr)
        print(f"repository verification failed with {len(errors)} error(s)", file=sys.stderr)
        return 1
    print("verified repository metadata, UTF-8/LF text, JSON, Python syntax, and Markdown links")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
