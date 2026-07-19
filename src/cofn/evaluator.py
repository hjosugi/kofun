from __future__ import annotations

import math
import statistics
import time
from itertools import product
from pathlib import Path
from dataclasses import dataclass
from typing import Any, Callable

from . import ast
from .diagnostics import Diagnostic, CofnError


@dataclass(slots=True)
class RuntimeBinding:
    value: Any
    mutable: bool = False
    owned: bool = False
    moved: bool = False


class Environment:
    def __init__(self, parent: "Environment | None" = None) -> None:
        self.parent = parent
        self.values: dict[str, RuntimeBinding] = {}
        self.closed = False

    def define(self, name: str, value: Any, *, mutable: bool = False, owned: bool = False) -> None:
        self.values[name] = RuntimeBinding(value, mutable, owned, False)

    def resolve_binding(self, name: str) -> RuntimeBinding | None:
        if name in self.values:
            return self.values[name]
        if self.parent:
            return self.parent.resolve_binding(name)
        return None

    def get(self, name: str, span: ast.Span) -> Any:
        binding = self.resolve_binding(name)
        if binding is None:
            raise runtime_error(f"undefined name `{name}`", span, "R001")
        if binding.moved:
            raise runtime_error(f"owned value `{name}` was already taken", span, "R002")
        return binding.value

    def assign(self, name: str, value: Any, span: ast.Span) -> None:
        binding = self.resolve_binding(name)
        if binding is None:
            raise runtime_error(f"undefined variable `{name}`", span, "R003")
        if binding.moved:
            raise runtime_error(f"owned value `{name}` was already taken", span, "R002")
        if not binding.mutable:
            raise runtime_error(f"cannot assign to immutable binding `{name}`", span, "R004")
        binding.value = value

    def take(self, name: str, span: ast.Span) -> Any:
        binding = self.resolve_binding(name)
        if binding is None:
            raise runtime_error(f"undefined variable `{name}`", span, "R003")
        if not binding.owned:
            raise runtime_error(f"`{name}` is not owned", span, "R005")
        if binding.moved:
            raise runtime_error(f"owned value `{name}` was already taken", span, "R002")
        binding.moved = True
        return binding.value

    def close(self) -> None:
        if self.closed:
            return
        self.closed = True
        for binding in self.values.values():
            if binding.owned and not binding.moved:
                dispose(binding.value)
                binding.moved = True


@dataclass(slots=True)
class ResourceValue:
    name: str
    open: bool = True

    def close(self) -> None:
        self.open = False

    def __repr__(self) -> str:
        state = "open" if self.open else "closed"
        return f"Resource({self.name!r}, {state})"


@dataclass(slots=True)
class BuiltinFunction:
    name: str
    fn: Callable[..., Any]
    min_arity: int
    max_arity: int | None = None

    def call(self, args: list[Any], span: ast.Span) -> Any:
        max_arity = self.min_arity if self.max_arity is None else self.max_arity
        if len(args) < self.min_arity or len(args) > max_arity:
            expected = str(self.min_arity) if self.min_arity == max_arity else f"{self.min_arity}..{max_arity}"
            raise runtime_error(f"`{self.name}` expects {expected} arguments, found {len(args)}", span, "R006")
        try:
            return self.fn(*args)
        except CofnError:
            raise
        except Exception as exc:
            raise runtime_error(f"`{self.name}` failed: {exc}", span, "R007") from exc

    def __repr__(self) -> str:
        return f"<builtin {self.name}>"


class CompleteFunctionSpace(list[Any]):
    """All total functions between two explicitly enumerated finite carriers."""

    def __init__(self, values: list[Any], domain: list[Any], codomain: list[Any]) -> None:
        super().__init__(values)
        self.domain = list(domain)
        self.codomain = list(codomain)


@dataclass(slots=True)
class UserFunction:
    declaration: ast.FunctionDecl | ast.LambdaExpr
    closure: Environment
    name: str

    @property
    def params(self) -> list[ast.Param]:
        return self.declaration.params

    @property
    def body(self) -> ast.Block:
        return self.declaration.body

    def __repr__(self) -> str:
        return f"<fn {self.name}>"


class Evaluator:
    def __init__(self, *, program_args: list[str] | None = None, allow_io: bool = True) -> None:
        self.globals = Environment()
        self.env = self.globals
        self.output: list[str] = []
        self.program_args = list(program_args or [])
        self.allow_io = allow_io
        self._install_builtins()

    def evaluate_program(self, program: ast.Program, *, call_main: bool = True) -> Any:
        # First register all functions for recursion and forward calls.
        for declaration in program.declarations:
            if isinstance(declaration, ast.FunctionDecl):
                self.globals.define(declaration.name, UserFunction(declaration, self.globals, declaration.name))
        last = None
        for declaration in program.declarations:
            if not isinstance(declaration, ast.FunctionDecl):
                if isinstance(declaration, ast.LawDecl):
                    continue
                last = self._execute(declaration)
        if call_main and self.globals.resolve_binding("main") is not None:
            main = self.globals.get("main", program.span)
            last = self._call_value(main, [], [], program.span)
        return last

    def evaluate_expression(self, expr: ast.Expr) -> Any:
        return self._eval(expr)

    def call_value(self, callee: Any, args: list[Any], span: ast.Span) -> Any:
        """Call a runtime function from compiler services such as law checking."""

        return self._call_value(callee, args, [None] * len(args), span)

    def _execute(self, node: ast.Stmt) -> Any:
        if isinstance(node, ast.FunctionDecl):
            self.env.define(node.name, UserFunction(node, self.env, node.name))
            return None
        if isinstance(node, ast.LawDecl):
            return None
        if isinstance(node, ast.LetStmt):
            value = self._eval(node.value)
            self.env.define(node.name, value, mutable=node.mutable, owned=node.owned)
            return None
        if isinstance(node, ast.AssignStmt):
            value = self._eval(node.value)
            self.env.assign(node.name, value, node.span)
            return None
        if isinstance(node, ast.ReturnStmt):
            value = None if node.value is None else self._eval(node.value)
            raise ReturnFlow(value)
        if isinstance(node, ast.WhileStmt):
            while truthy(self._eval(node.condition)):
                try:
                    self._execute_block(node.body)
                except ContinueFlow:
                    continue
                except BreakFlow:
                    break
            return None
        if isinstance(node, ast.ForStmt):
            iterable = self._eval(node.iterable)
            try:
                iterator = iter(iterable)
            except TypeError as exc:
                raise runtime_error("for-loop value is not iterable", node.iterable.span, "R008") from exc
            for value in iterator:
                loop_env = Environment(self.env)
                loop_env.define(node.name, value)
                previous = self.env
                self.env = loop_env
                try:
                    try:
                        for statement in node.body.statements:
                            self._execute(statement)
                    except ContinueFlow:
                        continue
                    except BreakFlow:
                        break
                finally:
                    loop_env.close()
                    self.env = previous
            return None
        if isinstance(node, ast.TakeStmt):
            value = self.env.take(node.name, node.span)
            dispose(value)
            return None
        if isinstance(node, ast.BreakStmt):
            raise BreakFlow()
        if isinstance(node, ast.ContinueStmt):
            raise ContinueFlow()
        if isinstance(node, ast.ExprStmt):
            return self._eval(node.expr)
        raise runtime_error(f"unsupported statement {type(node).__name__}", node.span, "R099")

    def _eval(self, node: ast.Expr) -> Any:
        if isinstance(node, ast.Literal):
            return node.value
        if isinstance(node, ast.Variable):
            return self.env.get(node.name, node.span)
        if isinstance(node, ast.ListLiteral):
            return [self._eval(item) for item in node.items]
        if isinstance(node, ast.TupleLiteral):
            return tuple(self._eval(item) for item in node.items)
        if isinstance(node, ast.UnaryExpr):
            value = self._eval(node.operand)
            if node.op == "-":
                return -value
            if node.op == "+":
                return +value
            if node.op == "!":
                return not truthy(value)
        if isinstance(node, ast.BinaryExpr):
            return self._eval_binary(node)
        if isinstance(node, ast.CallExpr):
            callee = self._eval(node.callee)
            return self._call_ast(callee, node.args, node.span)
        if isinstance(node, ast.MemberExpr):
            target = self._eval(node.target)
            return BoundMethod(target, node.name)
        if isinstance(node, ast.IndexExpr):
            target = self._eval(node.target)
            index = self._eval(node.index)
            try:
                return target[index]
            except (IndexError, KeyError, TypeError) as exc:
                raise runtime_error(f"index operation failed: {exc}", node.span, "R009") from exc
        if isinstance(node, ast.LambdaExpr):
            return UserFunction(node, self.env, "<lambda>")
        if isinstance(node, ast.IfExpr):
            if truthy(self._eval(node.condition)):
                return self._execute_block(node.then_branch)
            if node.else_branch is not None:
                return self._execute_block(node.else_branch)
            return None
        if isinstance(node, ast.PipeExpr):
            value = self._eval(node.value)
            if isinstance(node.target, ast.CallExpr):
                callee = self._eval(node.target.callee)
                return self._call_ast(callee, node.target.args, node.span, injected=[value])
            callee = self._eval(node.target)
            return self._call_value(callee, [value], [None], node.span)
        raise runtime_error(f"unsupported expression {type(node).__name__}", node.span, "R098")

    def _eval_binary(self, node: ast.BinaryExpr) -> Any:
        if node.op == "&&":
            left = self._eval(node.left)
            return self._eval(node.right) if truthy(left) else False
        if node.op == "||":
            left = self._eval(node.left)
            return left if truthy(left) else self._eval(node.right)
        if node.op == "??":
            left = self._eval(node.left)
            return self._eval(node.right) if left is None else left

        left = self._eval(node.left)
        right = self._eval(node.right)
        operations: dict[str, Callable[[Any, Any], Any]] = {
            "+": lambda a, b: a + b,
            "-": lambda a, b: a - b,
            "*": lambda a, b: a * b,
            "/": lambda a, b: a / b,
            "//": lambda a, b: a // b,
            "%": lambda a, b: a % b,
            "**": lambda a, b: a**b,
            "==": lambda a, b: a == b,
            "!=": lambda a, b: a != b,
            "<": lambda a, b: a < b,
            "<=": lambda a, b: a <= b,
            ">": lambda a, b: a > b,
            ">=": lambda a, b: a >= b,
            "..": lambda a, b: list(range(a, b)),
        }
        try:
            return operations[node.op](left, right)
        except Exception as exc:
            raise runtime_error(f"operator `{node.op}` failed: {exc}", node.span, "R010") from exc

    def _call_ast(
        self,
        callee: Any,
        arg_nodes: list[ast.Expr],
        span: ast.Span,
        *,
        injected: list[Any] | None = None,
    ) -> Any:
        injected = injected or []
        if isinstance(callee, UserFunction):
            expected = len(callee.params)
            total = len(injected) + len(arg_nodes)
            if total != expected:
                raise runtime_error(f"`{callee.name}` expects {expected} arguments, found {total}", span, "R011")
            values = list(injected)
            origins: list[ast.Expr | None] = [None] * len(injected)
            for index, arg in enumerate(arg_nodes, start=len(injected)):
                param = callee.params[index]
                if param.mode == "take" and isinstance(arg, ast.Variable):
                    values.append(self.env.take(arg.name, arg.span))
                else:
                    values.append(self._eval(arg))
                origins.append(arg)
            return self._call_value(callee, values, origins, span)
        values = list(injected) + [self._eval(arg) for arg in arg_nodes]
        origins = [None] * len(injected) + list(arg_nodes)
        return self._call_value(callee, values, origins, span)

    def _call_value(self, callee: Any, args: list[Any], origins: list[ast.Expr | None], span: ast.Span) -> Any:
        if isinstance(callee, BuiltinFunction):
            return callee.call(args, span)
        if isinstance(callee, BoundMethod):
            return self._call_method(callee.target, callee.name, args, span)
        if isinstance(callee, UserFunction):
            if len(args) != len(callee.params):
                raise runtime_error(f"`{callee.name}` expects {len(callee.params)} arguments, found {len(args)}", span, "R011")
            call_env = Environment(callee.closure)
            for param, value in zip(callee.params, args):
                call_env.define(param.name, value, mutable=param.mode == "edit", owned=param.mode == "take")
            previous = self.env
            self.env = call_env
            try:
                try:
                    result = self._execute_block(callee.body, create_scope=False)
                except ReturnFlow as flow:
                    result = flow.value
                return result
            finally:
                call_env.close()
                self.env = previous
        raise runtime_error(f"value `{display(callee)}` is not callable", span, "R012")

    def _execute_block(self, block: ast.Block, *, create_scope: bool = True) -> Any:
        previous = self.env
        block_env = Environment(previous) if create_scope else self.env
        if create_scope:
            self.env = block_env
        last = None
        try:
            for statement in block.statements:
                last = self._execute(statement)
            return last
        finally:
            if create_scope:
                block_env.close()
                self.env = previous

    def _call_method(self, target: Any, name: str, args: list[Any], span: ast.Span) -> Any:
        if name == "len" and not args:
            return len(target)
        if isinstance(target, list):
            if name == "map" and len(args) == 1:
                return [self._call_value(args[0], [item], [None], span) for item in target]
            if name == "filter" and len(args) == 1:
                return [item for item in target if truthy(self._call_value(args[0], [item], [None], span))]
            if name == "fold" and len(args) == 2:
                acc, fn = args
                for item in target:
                    acc = self._call_value(fn, [acc, item], [None, None], span)
                return acc
            if name == "sort" and not args:
                return sorted(target)
            if name == "reverse" and not args:
                return list(reversed(target))
            if name == "contains" and len(args) == 1:
                return args[0] in target
            if name == "push" and len(args) == 1:
                return [*target, args[0]]
            if name == "first" and not args:
                return target[0] if target else None
            if name == "last" and not args:
                return target[-1] if target else None
        raise runtime_error(f"unsupported method `{name}` for {type_name(target)}", span, "R013")

    def _install_builtins(self) -> None:
        def register(name: str, fn: Callable[..., Any], min_arity: int, max_arity: int | None = None) -> None:
            self.globals.define(name, BuiltinFunction(name, fn, min_arity, max_arity))

        def forbidden_effect(name: str) -> Callable[..., Any]:
            def fail(*_args: Any) -> Any:
                raise RuntimeError(f"effect `{name}` is disabled during compile-time law checking")

            return fail

        register("print", self._builtin_print if self.allow_io else forbidden_effect("print"), 1)
        register("debug", self._builtin_debug if self.allow_io else forbidden_effect("debug"), 1)
        register("len", len, 1)
        register("range", lambda start, end: list(range(start, end)), 2)
        register("sum", sum, 1)
        register("min", min, 1, 64)
        register("max", max, 1, 64)
        register("abs", abs, 1)
        register("map", lambda values, fn: [self._call_value(fn, [item], [None], ast.Span(1, 1, 1, 1)) for item in values], 2)
        register("filter", lambda values, fn: [item for item in values if truthy(self._call_value(fn, [item], [None], ast.Span(1, 1, 1, 1)))], 2)
        register("fold", lambda values, initial, fn: self._fold(values, initial, fn), 3)
        register("sort", lambda values: sorted(values), 1)
        register("reverse", lambda values: list(reversed(values)), 1)
        register("enumerate", lambda values: list(enumerate(values)), 1)
        register("zip", lambda left, right: list(zip(left, right)), 2)
        register("contains", lambda container, value: value in container, 2)
        register("push", lambda values, value: [*values, value], 2)
        register("concat", lambda left, right: [*left, *right], 2)
        register("first", lambda values: values[0] if values else None, 1)
        register("last", lambda values: values[-1] if values else None, 1)
        register("assert", self._builtin_assert, 1)
        register("assert_eq", self._builtin_assert_eq, 2)
        register("sqrt", math.sqrt, 1)
        register("sin", math.sin, 1)
        register("cos", math.cos, 1)
        register("exp", math.exp, 1)
        register("log", math.log, 1)
        register("mean", statistics.fmean, 1)
        register("dot", lambda left, right: sum(a * b for a, b in zip(left, right)), 2)
        register("zeros", lambda size: [0.0] * size, 1)
        register("ones", lambda size: [1.0] * size, 1)
        register("linspace", linspace, 3)
        register("vadd", lambda left, right: vector_binary(left, right, lambda a, b: a + b), 2)
        register("vsub", lambda left, right: vector_binary(left, right, lambda a, b: a - b), 2)
        register("vmul", lambda left, right: vector_binary(left, right, lambda a, b: a * b), 2)
        register("vdiv", lambda left, right: vector_binary(left, right, lambda a, b: a / b), 2)
        register("resource", lambda name: ResourceValue(name), 1)
        register("is_open", lambda value: isinstance(value, ResourceValue) and value.open, 1)
        register("type_of", type_name, 1)
        register("clock_ms", (lambda: time.perf_counter() * 1000.0) if self.allow_io else forbidden_effect("clock_ms"), 0)
        register("unwrap", self._builtin_unwrap, 1)
        register("finite_functions", self._finite_functions, 2)
        register("args", (lambda: list(self.program_args)) if self.allow_io else forbidden_effect("args"), 0)
        register("read_text", self._read_text if self.allow_io else forbidden_effect("read_text"), 1)
        register("write_text", self._write_text if self.allow_io else forbidden_effect("write_text"), 2)
        register("chars", lambda value: list(value), 1)
        register("text_join", lambda values, separator: separator.join(values), 2)
        register("text_slice", lambda value, start, end: value[start:end], 3)
        register("trim", lambda value: value.strip(), 1)
        register("replace", lambda value, old, new: value.replace(old, new), 3)
        register("starts_with", lambda value, prefix: value.startswith(prefix), 2)
        register("ends_with", lambda value, suffix: value.endswith(suffix), 2)
        register("find", lambda value, needle: value.find(needle), 2)
        register("to_text", display, 1)
        register("parse_int", lambda value: int(value.strip()), 1)
        register("is_digit", lambda value: len(value) == 1 and value.isdigit(), 1)
        register("is_space", lambda value: len(value) == 1 and value.isspace(), 1)
        # Coding interview collections. They are persistent-by-convention: updates return new values.
        register("set_of", lambda values: frozenset(values), 1)
        register("map_of", lambda pairs: dict(pairs), 1)
        register("has", lambda container, key: key in container, 2)
        register("get", lambda mapping, key, default=None: mapping.get(key, default), 2, 3)
        register("put", lambda mapping, key, value: {**mapping, key: value}, 3)
        register("keys", lambda mapping: list(mapping.keys()), 1)
        register("values", lambda mapping: list(mapping.values()), 1)
        register("queue", lambda values: list(values), 1)
        register("enqueue", lambda queue, value: [*queue, value], 2)
        register("dequeue", lambda queue: (queue[0], queue[1:]) if queue else None, 1)
        register("stack_push", lambda stack, value: [*stack, value], 2)
        register("stack_pop", lambda stack: (stack[-1], stack[:-1]) if stack else None, 1)

    def _builtin_print(self, value: Any) -> None:
        text = display(value)
        self.output.append(text)
        print(text)

    def _builtin_debug(self, value: Any) -> Any:
        text = repr(value)
        self.output.append(text)
        print(text)
        return value

    def _fold(self, values: list[Any], initial: Any, fn: Any) -> Any:
        acc = initial
        span = ast.Span(1, 1, 1, 1)
        for item in values:
            acc = self._call_value(fn, [acc, item], [None, None], span)
        return acc

    @staticmethod
    def _builtin_unwrap(value: Any) -> Any:
        if value is None:
            raise ValueError("cannot unwrap null")
        return value

    def _finite_functions(self, domain: list[Any], codomain: list[Any]) -> CompleteFunctionSpace:
        if not domain:
            raise ValueError("finite function domain cannot be empty")
        if not codomain:
            raise ValueError("finite function codomain cannot be empty")
        count = len(codomain) ** len(domain)
        if count > 4096:
            raise ValueError(f"finite function space has {count} functions; limit is 4096")
        functions: list[Any] = []
        for index, outputs in enumerate(product(codomain, repeat=len(domain))):
            table = list(zip(domain, outputs))

            def table_call(value: Any, *, _table: list[tuple[Any, Any]] = table) -> Any:
                for key, result in _table:
                    if value == key:
                        return result
                raise ValueError(f"value {display(value)} is outside the finite function domain")

            rendered = ",".join(f"{display(key)}->{display(result)}" for key, result in table)
            functions.append(BuiltinFunction(f"finite#{index}[{rendered}]", table_call, 1))
        return CompleteFunctionSpace(functions, domain, codomain)

    @staticmethod
    def _read_text(path: str) -> str:
        return Path(path).read_text(encoding="utf-8")

    @staticmethod
    def _write_text(path: str, value: str) -> None:
        target = Path(path)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(value, encoding="utf-8")

    @staticmethod
    def _builtin_assert(condition: Any) -> None:
        if not truthy(condition):
            raise AssertionError("assertion failed")

    @staticmethod
    def _builtin_assert_eq(left: Any, right: Any) -> None:
        if left != right:
            raise AssertionError(f"expected {display(left)} == {display(right)}")


@dataclass(slots=True)
class BoundMethod:
    target: Any
    name: str


class ReturnFlow(Exception):
    def __init__(self, value: Any) -> None:
        self.value = value


class BreakFlow(Exception):
    pass


class ContinueFlow(Exception):
    pass


def runtime_error(message: str, span: ast.Span, code: str) -> CofnError:
    return CofnError(Diagnostic(message, span, code))


def truthy(value: Any) -> bool:
    if value is None:
        return False
    if isinstance(value, bool):
        return value
    return bool(value)


def dispose(value: Any) -> None:
    close = getattr(value, "close", None)
    if callable(close):
        close()


def display(value: Any) -> str:
    if value is None:
        return "null"
    if value is True:
        return "true"
    if value is False:
        return "false"
    if isinstance(value, str):
        return value
    if isinstance(value, list):
        return "[" + ", ".join(display(item) for item in value) + "]"
    if isinstance(value, tuple):
        return "(" + ", ".join(display(item) for item in value) + ")"
    if isinstance(value, dict):
        return "{" + ", ".join(f"{display(k)}: {display(v)}" for k, v in value.items()) + "}"
    if isinstance(value, (set, frozenset)):
        return "set[" + ", ".join(sorted(display(item) for item in value)) + "]"
    return str(value)


def type_name(value: Any) -> str:
    if value is None:
        return "Null"
    if isinstance(value, bool):
        return "Bool"
    if isinstance(value, int):
        return "Int"
    if isinstance(value, float):
        return "Float"
    if isinstance(value, str):
        return "Text"
    if isinstance(value, list):
        return "List"
    if isinstance(value, tuple):
        return "Tuple"
    if isinstance(value, dict):
        return "Map"
    if isinstance(value, (set, frozenset)):
        return "Set"
    if isinstance(value, ResourceValue):
        return "Resource"
    if isinstance(value, (BuiltinFunction, UserFunction, BoundMethod)):
        return "Fn"
    return type(value).__name__


def linspace(start: float, end: float, count: int) -> list[float]:
    if count < 0:
        raise ValueError("count must be non-negative")
    if count == 0:
        return []
    if count == 1:
        return [float(start)]
    step = (end - start) / (count - 1)
    return [start + index * step for index in range(count)]


def vector_binary(left: list[Any], right: list[Any], op: Callable[[Any, Any], Any]) -> list[Any]:
    if len(left) != len(right):
        raise ValueError(f"vector length mismatch: {len(left)} != {len(right)}")
    return [op(a, b) for a, b in zip(left, right)]
