from __future__ import annotations

import copy
import hashlib
import json
from dataclasses import dataclass, field
from typing import Any

from . import ast
from .diagnostics import Diagnostic, KofunError
from .evaluator import BuiltinFunction, CompleteFunctionSpace, Evaluator, display


DEFAULT_CASE_LIMIT = 100_000
MAX_REPORTED_FAILURES = 3


@dataclass(slots=True)
class LawFailure:
    law: str
    inputs: dict[str, str]
    left: str | None = None
    right: str | None = None
    detail: str | None = None

    def as_dict(self) -> dict[str, Any]:
        return {
            "law": self.law,
            "inputs": dict(sorted(self.inputs.items())),
            "left": self.left,
            "right": self.right,
            "detail": self.detail,
        }


@dataclass(slots=True)
class LawReport:
    name: str
    family: str
    status: str
    assurance: str
    cases_checked: int
    cases_planned: int
    model_digest: str
    failures: list[LawFailure] = field(default_factory=list)

    @property
    def passed(self) -> bool:
        return self.status == "passed"

    def as_dict(self) -> dict[str, Any]:
        return {
            "name": self.name,
            "family": self.family,
            "status": self.status,
            "assurance": self.assurance,
            "cases_checked": self.cases_checked,
            "cases_planned": self.cases_planned,
            "model_digest": self.model_digest,
            "failures": [failure.as_dict() for failure in self.failures],
        }


@dataclass(slots=True)
class LawCheckResult:
    diagnostics: list[Diagnostic]
    reports: list[LawReport]

    @property
    def ok(self) -> bool:
        return not any(item.severity == "error" for item in self.diagnostics)


class LawChecker:
    """Compiler-integrated checker for declared algebraic laws.

    Stage 0 deliberately labels this as *bounded exhaustive model checking*.
    Every combination in the user-provided finite model is checked, but that is
    not a universal mathematical proof.  A later proof-kernel stage can upgrade
    the assurance to `proven` without changing the source-level law contract.
    """

    def check(self, program: ast.Program) -> LawCheckResult:
        declarations = [node for node in program.declarations if isinstance(node, ast.LawDecl)]
        if not declarations:
            return LawCheckResult([], [])

        evaluator = Evaluator(allow_io=False)
        diagnostics: list[Diagnostic] = []
        reports: list[LawReport] = []
        try:
            evaluator.evaluate_program(program, call_main=False)
        except KofunError as error:
            diagnostics.append(
                Diagnostic(
                    f"compile-time law environment failed: {error.diagnostic.message}",
                    error.diagnostic.span,
                    "L001",
                    "law checking runs in a deterministic sandbox; remove top-level effects",
                )
            )
            return LawCheckResult(diagnostics, reports)

        for declaration in declarations:
            if declaration.kind == "monad":
                report, report_diagnostics = self._check_monad(evaluator, declaration)
            else:
                report = LawReport(
                    declaration.name,
                    declaration.kind,
                    "failed",
                    "unsupported",
                    0,
                    0,
                    "",
                )
                report_diagnostics = [
                    Diagnostic(f"unsupported law family `{declaration.kind}`", declaration.span, "L002")
                ]
            reports.append(report)
            diagnostics.extend(report_diagnostics)
        return LawCheckResult(diagnostics, reports)

    def _check_monad(
        self,
        evaluator: Evaluator,
        declaration: ast.LawDecl,
    ) -> tuple[LawReport, list[Diagnostic]]:
        required = ("pure", "bind", "values", "monads", "functions")
        if any(key not in declaration.entries for key in required):
            # The type checker already emits precise missing-entry diagnostics.
            return (
                LawReport(declaration.name, "monad", "failed", "bounded-exhaustive", 0, 0, ""),
                [],
            )

        try:
            values = self._eval_entry(evaluator, declaration, "values")
            monads = self._eval_entry(evaluator, declaration, "monads")
            functions = self._eval_entry(evaluator, declaration, "functions")
            pure = self._eval_entry(evaluator, declaration, "pure")
            bind = self._eval_entry(evaluator, declaration, "bind")
            equal = (
                self._eval_entry(evaluator, declaration, "equal")
                if "equal" in declaration.entries
                else None
            )
            limit_value = (
                self._eval_entry(evaluator, declaration, "limit")
                if "limit" in declaration.entries
                else DEFAULT_CASE_LIMIT
            )
            complete = (
                self._eval_entry(evaluator, declaration, "complete")
                if "complete" in declaration.entries
                else False
            )
        except KofunError as error:
            diagnostic = Diagnostic(
                f"cannot construct finite model for monad law `{declaration.name}`: {error.diagnostic.message}",
                error.diagnostic.span,
                "L003",
            )
            return (
                LawReport(declaration.name, "monad", "failed", "bounded-exhaustive", 0, 0, ""),
                [diagnostic],
            )

        runtime_error = self._validate_model(declaration, values, monads, functions, limit_value, complete)
        if runtime_error is not None:
            return (
                LawReport(declaration.name, "monad", "failed", "bounded-exhaustive", 0, 0, ""),
                [runtime_error],
            )

        assert isinstance(values, list)
        assert isinstance(monads, list)
        assert isinstance(functions, list)
        assert isinstance(limit_value, int)
        assert isinstance(complete, bool)
        planned = len(values) * len(functions) + len(monads) + len(monads) * len(functions) ** 2
        model_digest = self._model_digest(values, monads, functions)
        assurance = "bounded-exhaustive"
        if complete:
            certified, reason = self._certify_complete_finite_model(values, monads, functions)
            if not certified:
                diagnostic = Diagnostic(
                    f"monad law `{declaration.name}` requested `complete = true`, but the finite model is not certifiable: {reason}",
                    declaration.entries["complete"].span,
                    "L008",
                    "use a compiler-known complete carrier and `finite_functions(values, monads)`, or remove `complete`",
                )
                return (
                    LawReport(
                        declaration.name,
                        "monad",
                        "failed",
                        "uncertified",
                        0,
                        planned,
                        model_digest,
                    ),
                    [diagnostic],
                )
            assurance = "proven-finite"
        if planned > limit_value:
            diagnostic = Diagnostic(
                f"monad law `{declaration.name}` plans {planned:,} cases, above limit {limit_value:,}",
                declaration.entries.get("limit", declaration).span,
                "L004",
                "raise `limit`, or reduce the finite model while keeping boundary cases",
            )
            return (
                LawReport(
                    declaration.name,
                    "monad",
                    "failed",
                    "bounded-exhaustive",
                    0,
                    planned,
                    model_digest,
                ),
                [diagnostic],
            )

        ordered_values = sorted(values, key=self._case_order)
        ordered_monads = sorted(monads, key=self._case_order)
        ordered_functions = sorted(functions, key=self._function_order)
        span = declaration.span
        failures: list[LawFailure] = []
        checked = 0

        def same(left: Any, right: Any) -> bool:
            if equal is None:
                return left == right
            result = evaluator.call_value(equal, [left, right], span)
            if not isinstance(result, bool):
                raise TypeError(f"custom equality returned {display(result)}, not Bool")
            return result

        # left identity: pure(a) >>= f == f(a)
        for value in ordered_values:
            for fn in ordered_functions:
                try:
                    left = evaluator.call_value(
                        bind,
                        [evaluator.call_value(pure, [copy.deepcopy(value)], span), fn],
                        span,
                    )
                    right = evaluator.call_value(fn, [copy.deepcopy(value)], span)
                    checked += 1
                    if not same(left, right):
                        failures.append(
                            LawFailure(
                                "left identity",
                                {"a": display(value), "f": self._function_name(fn)},
                                display(left),
                                display(right),
                            )
                        )
                except (KofunError, Exception) as error:  # normalized below
                    checked += 1
                    failures.append(
                        LawFailure(
                            "left identity",
                            {"a": display(value), "f": self._function_name(fn)},
                            detail=self._error_text(error),
                        )
                    )
                if len(failures) >= MAX_REPORTED_FAILURES:
                    return self._failed_report(declaration, checked, planned, model_digest, failures, assurance)

        # right identity: m >>= pure == m
        for monad in ordered_monads:
            try:
                left = evaluator.call_value(bind, [copy.deepcopy(monad), pure], span)
                right = copy.deepcopy(monad)
                checked += 1
                if not same(left, right):
                    failures.append(
                        LawFailure(
                            "right identity",
                            {"m": display(monad)},
                            display(left),
                            display(right),
                        )
                    )
            except (KofunError, Exception) as error:
                checked += 1
                failures.append(
                    LawFailure(
                        "right identity",
                        {"m": display(monad)},
                        detail=self._error_text(error),
                    )
                )
            if len(failures) >= MAX_REPORTED_FAILURES:
                return self._failed_report(declaration, checked, planned, model_digest, failures, assurance)

        # associativity: (m >>= f) >>= g == m >>= (x -> f(x) >>= g)
        for monad in ordered_monads:
            for fn in ordered_functions:
                for next_fn in ordered_functions:
                    try:
                        left_once = evaluator.call_value(bind, [copy.deepcopy(monad), fn], span)
                        left = evaluator.call_value(bind, [left_once, next_fn], span)

                        def composed(value: Any, *, _fn: Any = fn, _next: Any = next_fn) -> Any:
                            first = evaluator.call_value(_fn, [value], span)
                            return evaluator.call_value(bind, [first, _next], span)

                        composed_fn = BuiltinFunction(
                            f"<law-compose {self._function_name(fn)} {self._function_name(next_fn)}>",
                            composed,
                            1,
                        )
                        right = evaluator.call_value(bind, [copy.deepcopy(monad), composed_fn], span)
                        checked += 1
                        if not same(left, right):
                            failures.append(
                                LawFailure(
                                    "associativity",
                                    {
                                        "m": display(monad),
                                        "f": self._function_name(fn),
                                        "g": self._function_name(next_fn),
                                    },
                                    display(left),
                                    display(right),
                                )
                            )
                    except (KofunError, Exception) as error:
                        checked += 1
                        failures.append(
                            LawFailure(
                                "associativity",
                                {
                                    "m": display(monad),
                                    "f": self._function_name(fn),
                                    "g": self._function_name(next_fn),
                                },
                                detail=self._error_text(error),
                            )
                        )
                    if len(failures) >= MAX_REPORTED_FAILURES:
                        return self._failed_report(declaration, checked, planned, model_digest, failures, assurance)

        report = LawReport(
            declaration.name,
            "monad",
            "passed",
            assurance,
            checked,
            planned,
            model_digest,
            [],
        )
        return report, []

    @staticmethod
    def _eval_entry(evaluator: Evaluator, declaration: ast.LawDecl, key: str) -> Any:
        return evaluator.evaluate_expression(declaration.entries[key])

    @staticmethod
    def _validate_model(
        declaration: ast.LawDecl,
        values: Any,
        monads: Any,
        functions: Any,
        limit: Any,
        complete: Any,
    ) -> Diagnostic | None:
        for key, value in (("values", values), ("monads", monads), ("functions", functions)):
            if not isinstance(value, list):
                return Diagnostic(
                    f"monad law `{declaration.name}` entry `{key}` must evaluate to a List",
                    declaration.entries[key].span,
                    "L005",
                )
            if not value:
                return Diagnostic(
                    f"monad law `{declaration.name}` entry `{key}` cannot be empty",
                    declaration.entries[key].span,
                    "L006",
                    "provide finite boundary cases so the compiler has a meaningful model",
                )
        if isinstance(limit, bool) or not isinstance(limit, int) or limit <= 0:
            return Diagnostic(
                f"monad law `{declaration.name}` entry `limit` must be a positive Int",
                declaration.entries.get("limit", declaration).span,
                "L007",
            )
        if not isinstance(complete, bool):
            return Diagnostic(
                f"monad law `{declaration.name}` entry `complete` must be Bool",
                declaration.entries.get("complete", declaration).span,
                "L009",
            )
        return None

    def _failed_report(
        self,
        declaration: ast.LawDecl,
        checked: int,
        planned: int,
        digest: str,
        failures: list[LawFailure],
        assurance: str = "bounded-exhaustive",
    ) -> tuple[LawReport, list[Diagnostic]]:
        report = LawReport(
            declaration.name,
            "monad",
            "failed",
            assurance,
            checked,
            planned,
            digest,
            failures,
        )
        diagnostics: list[Diagnostic] = []
        codes = {
            "left identity": "L101",
            "right identity": "L102",
            "associativity": "L103",
        }
        for failure in failures:
            inputs = ", ".join(f"{key}={value}" for key, value in failure.inputs.items())
            if failure.detail:
                message = (
                    f"monad law `{declaration.name}` failed {failure.law} for {inputs}: "
                    f"{failure.detail}"
                )
            else:
                message = (
                    f"monad law `{declaration.name}` failed {failure.law} for {inputs}: "
                    f"left={failure.left}, right={failure.right}"
                )
            diagnostics.append(
                Diagnostic(
                    message,
                    declaration.span,
                    codes.get(failure.law, "L199"),
                    "the compiler checked every combination in the declared finite model",
                )
            )
        return report, diagnostics

    @staticmethod
    def _certify_complete_finite_model(
        values: list[Any],
        monads: list[Any],
        functions: list[Any],
    ) -> tuple[bool, str]:
        """Validate a proof-complete carrier known to the Stage 0 kernel.

        The initial kernel intentionally recognizes only tiny built-in finite
        carriers.  This keeps the `proven-finite` label meaningful: no user
        assertion can silently turn a sample set into a proof.
        """

        bool_carrier = {("Bool", False), ("Bool", True)}
        optional_bool_carrier = {("Null", None), *bool_carrier}
        value_keys = {LawChecker._carrier_key(value) for value in values}
        monad_keys = {LawChecker._carrier_key(value) for value in monads}
        if len(value_keys) != len(values):
            return False, "`values` contains duplicates"
        if len(monad_keys) != len(monads):
            return False, "`monads` contains duplicates"
        if value_keys != bool_carrier:
            return False, "Stage 0 can currently certify only the complete Bool value carrier"
        if frozenset(monad_keys) not in {frozenset(bool_carrier), frozenset(optional_bool_carrier)}:
            return False, "Stage 0 can currently certify only Bool or Optional[Bool] monadic carriers"
        if not isinstance(functions, CompleteFunctionSpace):
            return False, "`functions` was not generated by `finite_functions`"
        function_domain = {LawChecker._carrier_key(value) for value in functions.domain}
        function_codomain = {LawChecker._carrier_key(value) for value in functions.codomain}
        if function_domain != value_keys:
            return False, "finite function domain does not match `values`"
        if function_codomain != monad_keys:
            return False, "finite function codomain does not match `monads`"
        expected = len(monads) ** len(values)
        if len(functions) != expected:
            return False, f"finite function space is incomplete: expected {expected}, found {len(functions)}"
        return True, "compiler-known complete finite carrier"

    @staticmethod
    def _carrier_key(value: Any) -> tuple[str, Any]:
        if value is None:
            return ("Null", None)
        if isinstance(value, bool):
            return ("Bool", value)
        if isinstance(value, int):
            return ("Int", value)
        if isinstance(value, float):
            return ("Float", repr(value))
        if isinstance(value, str):
            return ("Text", value)
        return (type(value).__name__, display(value))

    @staticmethod
    def _case_order(value: Any) -> tuple[int, int, str]:
        def size(item: Any) -> int:
            if item is None or isinstance(item, (bool, int, float, str)):
                return 1
            if isinstance(item, (list, tuple, set, frozenset)):
                return 1 + sum(size(child) for child in item)
            if isinstance(item, dict):
                return 1 + sum(size(k) + size(v) for k, v in item.items())
            return 2

        rendered = display(value)
        return size(value), len(rendered), rendered

    @staticmethod
    def _function_order(value: Any) -> tuple[str]:
        return (LawChecker._function_name(value),)

    @staticmethod
    def _function_name(value: Any) -> str:
        name = getattr(value, "name", None)
        return str(name) if name else display(value)

    @staticmethod
    def _model_digest(values: list[Any], monads: list[Any], functions: list[Any]) -> str:
        payload = {
            "values": [display(value) for value in values],
            "monads": [display(value) for value in monads],
            "functions": [LawChecker._function_name(value) for value in functions],
        }
        encoded = json.dumps(payload, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        return hashlib.sha256(encoded.encode("utf-8")).hexdigest()[:16]

    @staticmethod
    def _error_text(error: BaseException) -> str:
        if isinstance(error, KofunError):
            return f"{error.diagnostic.code}: {error.diagnostic.message}"
        return f"{type(error).__name__}: {error}"
