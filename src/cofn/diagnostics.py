from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

from .ast import Span


@dataclass(slots=True)
class Diagnostic:
    message: str
    span: Span
    code: str = "E000"
    hint: str | None = None
    severity: str = "error"


class CofnError(Exception):
    def __init__(self, diagnostic: Diagnostic):
        super().__init__(diagnostic.message)
        self.diagnostic = diagnostic


class DiagnosticBag:
    def __init__(self) -> None:
        self.items: list[Diagnostic] = []

    def error(self, message: str, span: Span, code: str = "E000", hint: str | None = None) -> None:
        self.items.append(Diagnostic(message, span, code, hint, "error"))

    def warning(self, message: str, span: Span, code: str = "W000", hint: str | None = None) -> None:
        self.items.append(Diagnostic(message, span, code, hint, "warning"))

    @property
    def has_errors(self) -> bool:
        return any(item.severity == "error" for item in self.items)

    def extend(self, values: Iterable[Diagnostic]) -> None:
        self.items.extend(values)


def render_diagnostic(source: str, diagnostic: Diagnostic, filename: str = "<input>") -> str:
    lines = source.splitlines() or [""]
    line_no = max(1, min(diagnostic.span.line, len(lines)))
    line = lines[line_no - 1]
    col = max(1, diagnostic.span.column)
    width = max(1, diagnostic.span.end_column - diagnostic.span.column)
    marker = " " * (col - 1) + "^" * min(width, max(1, len(line) - col + 2))
    label = "error" if diagnostic.severity == "error" else "warning"
    output = [
        f"{label}[{diagnostic.code}]: {diagnostic.message}",
        f"  --> {filename}:{line_no}:{col}",
        "   |",
        f"{line_no:3} | {line}",
        f"   | {marker}",
    ]
    if diagnostic.hint:
        output.append(f"help: {diagnostic.hint}")
    return "\n".join(output)


def read_source(path: str) -> tuple[str, str]:
    if path == "-":
        import sys

        return sys.stdin.read(), "<stdin>"
    file_path = Path(path)
    return file_path.read_text(encoding="utf-8"), str(file_path)
