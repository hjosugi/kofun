from __future__ import annotations

from dataclasses import dataclass, field

from .ast import Program
from .diagnostics import Diagnostic
from .lexer import Lexer
from .laws import LawChecker, LawReport
from .parser import Parser
from .typesys import TypeChecker


@dataclass(slots=True)
class FrontendResult:
    program: Program
    diagnostics: list[Diagnostic]
    law_reports: list[LawReport] = field(default_factory=list)

    @property
    def ok(self) -> bool:
        return not any(item.severity == "error" for item in self.diagnostics)


def parse_source(source: str) -> FrontendResult:
    tokens, lexer_diagnostics = Lexer(source).lex()
    parser = Parser(tokens)
    program, parser_diagnostics = parser.parse()
    diagnostics = [*lexer_diagnostics.items, *parser_diagnostics.items]
    return FrontendResult(program, diagnostics)


def check_source(source: str, *, check_laws: bool = True) -> FrontendResult:
    parsed = parse_source(source)
    if any(item.severity == "error" for item in parsed.diagnostics):
        return parsed
    checker = TypeChecker()
    checker.check(parsed.program)
    parsed.diagnostics.extend(checker.diagnostics.items)
    if check_laws and not any(item.severity == "error" for item in parsed.diagnostics):
        law_result = LawChecker().check(parsed.program)
        parsed.diagnostics.extend(law_result.diagnostics)
        parsed.law_reports.extend(law_result.reports)
    return parsed
