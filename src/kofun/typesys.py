from __future__ import annotations

from dataclasses import dataclass, field
from typing import Iterable

from . import ast
from .diagnostics import DiagnosticBag


@dataclass(frozen=True, slots=True)
class Type:
    name: str
    args: tuple["Type", ...] = ()

    def __str__(self) -> str:
        if self.name == "Optional" and self.args:
            return f"{self.args[0]}?"
        if self.args:
            return f"{self.name}[{', '.join(map(str, self.args))}]"
        return self.name

    @property
    def optional_inner(self) -> "Type | None":
        return self.args[0] if self.name == "Optional" and self.args else None


INT = Type("Int")
FLOAT = Type("Float")
BOOL = Type("Bool")
TEXT = Type("Text")
NULL = Type("Null")
VOID = Type("Void")
ANY = Type("Any")
UNKNOWN = Type("Unknown")
RESOURCE = Type("Resource")


def LIST(item: Type = ANY) -> Type:
    return Type("List", (item,))


def TUPLE(items: Iterable[Type]) -> Type:
    return Type("Tuple", tuple(items))


def FN(params: Iterable[Type], result: Type) -> Type:
    return Type("Fn", tuple(params) + (result,))


def OPTIONAL(inner: Type) -> Type:
    return Type("Optional", (inner,))


@dataclass(slots=True)
class FunctionType:
    params: list[Type]
    modes: list[str]
    result: Type

    def as_type(self) -> Type:
        return FN(self.params, self.result)


@dataclass(slots=True)
class Binding:
    type: Type
    mutable: bool = False
    owned: bool = False
    moved: bool = False
    declaration_span: ast.Span | None = None
    #: The type this binding was declared with. `type` may be temporarily
    #: narrower inside a branch that has proved the value is not null; the
    #: declared type is what assignment is checked against, so a narrowing can
    #: never make a legal assignment illegal.
    declared_type: Type | None = None

    @property
    def declared(self) -> Type:
        return self.declared_type if self.declared_type is not None else self.type

    def clone(self) -> "Binding":
        return Binding(
            self.type, self.mutable, self.owned, self.moved,
            self.declaration_span, self.declared_type,
        )


class Scope:
    def __init__(self, parent: "Scope | None" = None) -> None:
        self.parent = parent
        self.values: dict[str, Binding] = {}

    def define(self, name: str, binding: Binding) -> bool:
        if name in self.values:
            return False
        self.values[name] = binding
        return True

    def resolve(self, name: str) -> Binding | None:
        if name in self.values:
            return self.values[name]
        if self.parent:
            return self.parent.resolve(name)
        return None

    def clone_chain(self) -> "Scope":
        parent = self.parent.clone_chain() if self.parent else None
        cloned = Scope(parent)
        cloned.values = {name: binding.clone() for name, binding in self.values.items()}
        return cloned


class TypeChecker:
    def __init__(self) -> None:
        self.diagnostics = DiagnosticBag()
        self.globals = Scope()
        self.scope = self.globals
        self.functions: dict[str, FunctionType] = {}
        self.current_return: Type = VOID
        self.current_function: str | None = None
        self.loop_depth = 0
        self._install_builtins()

    def check(self, program: ast.Program) -> DiagnosticBag:
        # Header pass enables recursion and forward calls.
        for declaration in program.declarations:
            if isinstance(declaration, ast.FunctionDecl):
                self._declare_function_header(declaration)

        for declaration in program.declarations:
            self._check_stmt(declaration)
        return self.diagnostics

    def _install_builtins(self) -> None:
        builtins: dict[str, FunctionType] = {
            "print": FunctionType([ANY], ["read"], VOID),
            "debug": FunctionType([ANY], ["read"], ANY),
            "len": FunctionType([ANY], ["read"], INT),
            "range": FunctionType([INT, INT], ["value", "value"], LIST(INT)),
            "sum": FunctionType([LIST(ANY)], ["read"], ANY),
            "min": FunctionType([ANY], ["read"], ANY),
            "max": FunctionType([ANY], ["read"], ANY),
            "abs": FunctionType([ANY], ["value"], ANY),
            "map": FunctionType([LIST(ANY), FN([ANY], ANY)], ["read", "read"], LIST(ANY)),
            "filter": FunctionType([LIST(ANY), FN([ANY], BOOL)], ["read", "read"], LIST(ANY)),
            "fold": FunctionType([LIST(ANY), ANY, FN([ANY, ANY], ANY)], ["read", "value", "read"], ANY),
            "sort": FunctionType([LIST(ANY)], ["read"], LIST(ANY)),
            "reverse": FunctionType([LIST(ANY)], ["read"], LIST(ANY)),
            "enumerate": FunctionType([LIST(ANY)], ["read"], LIST(TUPLE([INT, ANY]))),
            "zip": FunctionType([LIST(ANY), LIST(ANY)], ["read", "read"], LIST(TUPLE([ANY, ANY]))),
            "contains": FunctionType([ANY, ANY], ["read", "read"], BOOL),
            "push": FunctionType([LIST(ANY), ANY], ["read", "value"], LIST(ANY)),
            "concat": FunctionType([LIST(ANY), LIST(ANY)], ["read", "read"], LIST(ANY)),
            "first": FunctionType([LIST(ANY)], ["read"], OPTIONAL(ANY)),
            "last": FunctionType([LIST(ANY)], ["read"], OPTIONAL(ANY)),
            "assert": FunctionType([BOOL], ["value"], VOID),
            "assert_eq": FunctionType([ANY, ANY], ["read", "read"], VOID),
            "sqrt": FunctionType([ANY], ["value"], FLOAT),
            "sin": FunctionType([ANY], ["value"], FLOAT),
            "cos": FunctionType([ANY], ["value"], FLOAT),
            "exp": FunctionType([ANY], ["value"], FLOAT),
            "log": FunctionType([ANY], ["value"], FLOAT),
            "mean": FunctionType([LIST(ANY)], ["read"], FLOAT),
            "dot": FunctionType([LIST(ANY), LIST(ANY)], ["read", "read"], ANY),
            "zeros": FunctionType([INT], ["value"], LIST(FLOAT)),
            "ones": FunctionType([INT], ["value"], LIST(FLOAT)),
            "linspace": FunctionType([FLOAT, FLOAT, INT], ["value", "value", "value"], LIST(FLOAT)),
            "vadd": FunctionType([LIST(ANY), LIST(ANY)], ["read", "read"], LIST(ANY)),
            "vsub": FunctionType([LIST(ANY), LIST(ANY)], ["read", "read"], LIST(ANY)),
            "vmul": FunctionType([LIST(ANY), LIST(ANY)], ["read", "read"], LIST(ANY)),
            "vdiv": FunctionType([LIST(ANY), LIST(ANY)], ["read", "read"], LIST(FLOAT)),
            "resource": FunctionType([TEXT], ["value"], RESOURCE),
            "is_open": FunctionType([RESOURCE], ["read"], BOOL),
            "type_of": FunctionType([ANY], ["read"], TEXT),
            "clock_ms": FunctionType([], [], FLOAT),
            "unwrap": FunctionType([OPTIONAL(ANY)], ["read"], ANY),
            "finite_functions": FunctionType([LIST(ANY), LIST(ANY)], ["read", "read"], LIST(FN([ANY], ANY))),
            "args": FunctionType([], [], LIST(TEXT)),
            "read_text": FunctionType([TEXT], ["read"], TEXT),
            "write_text": FunctionType([TEXT, TEXT], ["read", "read"], VOID),
            "chars": FunctionType([TEXT], ["read"], LIST(TEXT)),
            "text_join": FunctionType([LIST(TEXT), TEXT], ["read", "read"], TEXT),
            "text_slice": FunctionType([TEXT, INT, INT], ["read", "value", "value"], TEXT),
            "trim": FunctionType([TEXT], ["read"], TEXT),
            "replace": FunctionType([TEXT, TEXT, TEXT], ["read", "read", "read"], TEXT),
            "starts_with": FunctionType([TEXT, TEXT], ["read", "read"], BOOL),
            "ends_with": FunctionType([TEXT, TEXT], ["read", "read"], BOOL),
            "find": FunctionType([TEXT, TEXT], ["read", "read"], INT),
            "to_text": FunctionType([ANY], ["read"], TEXT),
            "parse_int": FunctionType([TEXT], ["read"], INT),
            "is_digit": FunctionType([TEXT], ["read"], BOOL),
            "is_space": FunctionType([TEXT], ["read"], BOOL),
        }
        for name, fn_type in builtins.items():
            self.functions[name] = fn_type
            self.globals.define(name, Binding(fn_type.as_type()))

    def _declare_function_header(self, node: ast.FunctionDecl) -> None:
        if node.name in self.functions:
            self._error(node.span, f"duplicate function `{node.name}`", "E301")
            return
        params = [self._from_ref(param.annotation) if param.annotation else ANY for param in node.params]
        result = self._from_ref(node.return_type) if node.return_type else UNKNOWN
        signature = FunctionType(params, [param.mode for param in node.params], result)
        self.functions[node.name] = signature
        if not self.globals.define(node.name, Binding(signature.as_type(), declaration_span=node.span)):
            self._error(node.span, f"name `{node.name}` is already defined", "E302")

    def _check_stmt(self, node: ast.Stmt) -> Type:
        if isinstance(node, ast.FunctionDecl):
            return self._check_function(node)
        if isinstance(node, ast.LawDecl):
            return self._check_law_decl(node)
        if isinstance(node, ast.LetStmt):
            value_type = self._infer(node.value)
            annotation = self._from_ref(node.annotation) if node.annotation else value_type
            if node.annotation and not self._assignable(value_type, annotation):
                self._type_error(node.value.span, value_type, annotation)
            if node.owned and annotation in {INT, FLOAT, BOOL, TEXT, NULL}:
                self.diagnostics.warning(
                    f"`{node.name}` is a copyable {annotation}; `own` is unnecessary",
                    node.span,
                    "W301",
                    "remove `own`, or use it for a resource/heap value",
                )
            binding = Binding(annotation, node.mutable, node.owned, False, node.span)
            if not self.scope.define(node.name, binding):
                self._error(node.span, f"duplicate binding `{node.name}` in this scope", "E303")
            return VOID
        if isinstance(node, ast.AssignStmt):
            binding = self.scope.resolve(node.name)
            if binding is None:
                self._error(node.span, f"undefined variable `{node.name}`", "E304")
                return VOID
            if binding.moved:
                self._moved_error(node.span, node.name)
                return VOID
            if not binding.mutable:
                self._error(node.span, f"cannot assign to immutable binding `{node.name}`", "E305", "declare it with `let mut`")
            value_type = self._infer(node.value)
            # Check against the declared type, not any narrowing in force: a
            # binding declared `Int?` must still accept null even inside a
            # branch that proved it was not null a moment ago.
            target = binding.declared
            if not self._assignable(value_type, target):
                self._type_error(node.value.span, value_type, target)
            # The assignment invalidates what the narrowing proved.
            binding.type = target
            binding.declared_type = None
            return VOID
        if isinstance(node, ast.ReturnStmt):
            actual = VOID if node.value is None else self._infer(node.value)
            if self.current_function is None:
                self._error(node.span, "`return` is only valid inside a function", "E306")
            elif self.current_return == UNKNOWN:
                self.current_return = actual
            elif not self._assignable(actual, self.current_return):
                self._type_error(node.span, actual, self.current_return, context="return type")
            return actual
        if isinstance(node, ast.WhileStmt):
            condition = self._infer(node.condition)
            self._expect_bool(condition, node.condition.span)
            before = self._capture_visible_bindings()
            self.loop_depth += 1
            self._check_block(node.body)
            self.loop_depth -= 1
            self._merge_loop_moves(before)
            return VOID
        if isinstance(node, ast.ForStmt):
            iterable = self._infer(node.iterable)
            item = iterable.args[0] if iterable.name == "List" and iterable.args else ANY
            parent = self.scope
            self.scope = Scope(parent)
            self.scope.define(node.name, Binding(item, mutable=False))
            self.loop_depth += 1
            for statement in node.body.statements:
                self._check_stmt(statement)
            self.loop_depth -= 1
            self.scope = parent
            return VOID
        if isinstance(node, ast.TakeStmt):
            binding = self.scope.resolve(node.name)
            if binding is None:
                self._error(node.span, f"undefined variable `{node.name}`", "E307")
            elif not binding.owned:
                self._error(node.span, f"`{node.name}` is not owned", "E308", "declare it with `let own` before consuming it")
            elif binding.moved:
                self._moved_error(node.span, node.name)
            else:
                binding.moved = True
            return VOID
        if isinstance(node, ast.BreakStmt):
            if self.loop_depth == 0:
                self._error(node.span, "`break` is only valid inside a loop", "E309")
            return VOID
        if isinstance(node, ast.ContinueStmt):
            if self.loop_depth == 0:
                self._error(node.span, "`continue` is only valid inside a loop", "E310")
            return VOID
        if isinstance(node, ast.ExprStmt):
            return self._infer(node.expr, statement_context=True)
        self._error(node.span, f"internal checker gap for {type(node).__name__}", "E399")
        return ANY

    def _check_law_decl(self, node: ast.LawDecl) -> Type:
        if node.kind != "monad":
            self._error(node.span, f"unsupported law family `{node.kind}`", "E331")
            return VOID

        required = {"pure", "bind", "values", "monads", "functions"}
        allowed = required | {"equal", "limit", "complete"}
        for key in sorted(required - node.entries.keys()):
            self._error(node.span, f"monad law `{node.name}` is missing `{key}`", "E332")
        for key in sorted(node.entries.keys() - allowed):
            self._error(node.entries[key].span, f"unknown monad law entry `{key}`", "E333")

        inferred: dict[str, Type] = {}
        for key, expression in node.entries.items():
            inferred[key] = self._infer(expression)

        pure = inferred.get("pure")
        if pure is not None and pure not in {ANY, UNKNOWN}:
            if pure.name != "Fn" or len(pure.args) != 2:
                self._error(node.entries["pure"].span, "`pure` must be a one-argument function", "E334")

        bind = inferred.get("bind")
        if bind is not None and bind not in {ANY, UNKNOWN}:
            if bind.name != "Fn" or len(bind.args) != 3:
                self._error(node.entries["bind"].span, "`bind` must be a two-argument function", "E335")

        for key in ("values", "monads", "functions"):
            value = inferred.get(key)
            if value is not None and value not in {ANY, UNKNOWN} and value.name != "List":
                self._error(node.entries[key].span, f"`{key}` must be a List", "E336")

        equal = inferred.get("equal")
        if equal is not None and equal not in {ANY, UNKNOWN}:
            if equal.name != "Fn" or len(equal.args) != 3 or equal.args[-1] not in {BOOL, ANY}:
                self._error(node.entries["equal"].span, "`equal` must be a two-argument function returning Bool", "E337")

        limit = inferred.get("limit")
        if limit is not None and limit not in {INT, ANY, UNKNOWN}:
            self._type_error(node.entries["limit"].span, limit, INT, context="law case limit")
        complete = inferred.get("complete")
        if complete is not None and complete not in {BOOL, ANY, UNKNOWN}:
            self._type_error(node.entries["complete"].span, complete, BOOL, context="finite proof marker")
        return VOID

    def _check_function(self, node: ast.FunctionDecl) -> Type:
        signature = self.functions.get(node.name)
        if signature is None:
            return VOID
        previous_scope = self.scope
        previous_return = self.current_return
        previous_function = self.current_function
        self.scope = Scope(self.globals)
        self.current_return = signature.result
        self.current_function = node.name
        for param, param_type in zip(node.params, signature.params):
            self.scope.define(
                param.name,
                Binding(param_type, mutable=param.mode == "edit", owned=param.mode == "take", declaration_span=param.span),
            )
        block_type = self._check_block(node.body, create_scope=False)
        if signature.result == UNKNOWN:
            inferred = self.current_return if self.current_return != UNKNOWN else block_type
            if inferred == UNKNOWN:
                inferred = VOID
            signature.result = inferred
            binding = self.globals.resolve(node.name)
            if binding:
                binding.type = signature.as_type()
        elif signature.result != VOID and not self._block_definitely_returns(node.body):
            # Expression-bodied functions are represented with ReturnStmt and pass this test.
            self._error(
                node.span,
                f"function `{node.name}` may finish without returning {signature.result}",
                "E311",
                "add a return on every control-flow path",
            )
        self.scope = previous_scope
        self.current_return = previous_return
        self.current_function = previous_function
        return signature.as_type()

    def _check_block(self, block: ast.Block, create_scope: bool = True) -> Type:
        parent = self.scope
        if create_scope:
            self.scope = Scope(parent)
        result = VOID
        for statement in block.statements:
            result = self._check_stmt(statement)
        if create_scope:
            self.scope = parent
        return result

    def _infer(self, node: ast.Expr, statement_context: bool = False) -> Type:
        result = ANY
        if isinstance(node, ast.Literal):
            result = {"Int": INT, "Float": FLOAT, "Text": TEXT, "Bool": BOOL, "Null": NULL}[node.kind]
        elif isinstance(node, ast.Variable):
            binding = self.scope.resolve(node.name)
            if binding is None:
                self._error(node.span, f"undefined name `{node.name}`", "E312")
                result = ANY
            elif binding.moved:
                self._moved_error(node.span, node.name)
                result = binding.type
            else:
                result = binding.type
        elif isinstance(node, ast.ListLiteral):
            if not node.items:
                result = LIST(ANY)
            else:
                item_type = self._infer(node.items[0])
                for item in node.items[1:]:
                    item_type = self._join(item_type, self._infer(item), item.span)
                result = LIST(item_type)
        elif isinstance(node, ast.TupleLiteral):
            result = TUPLE(self._infer(item) for item in node.items)
        elif isinstance(node, ast.UnaryExpr):
            operand = self._infer(node.operand)
            if node.op == "!":
                self._expect_bool(operand, node.operand.span)
                result = BOOL
            elif node.op in {"+", "-"}:
                if not self._numeric(operand):
                    self._error(node.span, f"unary `{node.op}` requires a number, found {operand}", "E313")
                result = operand
        elif isinstance(node, ast.BinaryExpr):
            result = self._infer_binary(node)
        elif isinstance(node, ast.CallExpr):
            result = self._infer_call(node)
        elif isinstance(node, ast.MemberExpr):
            result = self._infer_member(node)
        elif isinstance(node, ast.IndexExpr):
            target = self._infer(node.target)
            index = self._infer(node.index)
            if target.name in {"List", "Tuple"}:
                if index != INT and index != ANY:
                    self._type_error(node.index.span, index, INT, context="index type")
                if target.name == "List":
                    result = target.args[0] if target.args else ANY
                else:
                    result = self._join_many(list(target.args), node.span)
            elif target == TEXT:
                result = TEXT
            else:
                self._error(node.target.span, f"type {target} is not indexable", "E314")
                result = ANY
        elif isinstance(node, ast.LambdaExpr):
            parent = self.scope
            self.scope = Scope(parent)
            param_types: list[Type] = []
            for param in node.params:
                param_type = self._from_ref(param.annotation) if param.annotation else ANY
                param_types.append(param_type)
                self.scope.define(param.name, Binding(param_type, mutable=param.mode == "edit", owned=param.mode == "take"))
            previous_return = self.current_return
            previous_function = self.current_function
            self.current_return = UNKNOWN
            self.current_function = "<lambda>"
            body_type = self._check_block(node.body, create_scope=False)
            return_type = self.current_return if self.current_return != UNKNOWN else body_type
            self.current_return = previous_return
            self.current_function = previous_function
            self.scope = parent
            result = FN(param_types, return_type)
        elif isinstance(node, ast.IfExpr):
            condition = self._infer(node.condition)
            self._expect_bool(condition, node.condition.span)
            narrow_true, narrow_false = self._narrowing_from(node.condition)
            before = self._capture_visible_bindings()
            value_context = node.else_branch is not None and not statement_context

            self._apply_narrowing(narrow_true)
            then_type = self._check_branch(node.then_branch, value_context=value_context)
            then_state = self._capture_visible_bindings()
            self._restore_visible_bindings(before)

            if node.else_branch:
                self._apply_narrowing(narrow_false)
                else_type = self._check_branch(node.else_branch, value_context=value_context)
                else_state = self._capture_visible_bindings()
            else:
                else_type = VOID
                else_state = self._capture_visible_bindings()
            self._restore_visible_bindings(before)
            self._merge_branch_moves(then_state, else_state)

            # Guard form: `if x == null { return }` leaves the rest of the
            # function on the branch where x is present, so the narrowing holds
            # from here on rather than only inside the block.
            if narrow_false and self._block_definitely_returns(node.then_branch):
                self._apply_narrowing(narrow_false)
            result = self._join(then_type, else_type, node.span) if node.else_branch and not statement_context else VOID
        elif isinstance(node, ast.PipeExpr):
            result = self._infer_pipe(node)
        else:
            self._error(node.span, f"internal checker gap for {type(node).__name__}", "E398")
            result = ANY
        node.inferred_type = result
        return result

    def _infer_binary(self, node: ast.BinaryExpr) -> Type:
        left = self._infer(node.left)
        right = self._infer(node.right)
        if node.op in {"+", "-", "*", "/", "//", "%", "**"}:
            if node.op == "+" and left == TEXT and right == TEXT:
                return TEXT
            if node.op == "+" and left.name == "List" and right.name == "List":
                return LIST(self._join(left.args[0], right.args[0], node.span))
            if not self._numeric(left) or not self._numeric(right):
                self._error(node.span, f"operator `{node.op}` requires numbers, found {left} and {right}", "E315")
                return ANY
            if node.op == "//":
                return INT
            if node.op == "/" or FLOAT in {left, right}:
                return FLOAT
            return INT
        if node.op in {"<", "<=", ">", ">="}:
            if not ((self._numeric(left) and self._numeric(right)) or (left == right == TEXT) or ANY in {left, right}):
                self._error(node.span, f"cannot compare {left} with {right}", "E316")
            return BOOL
        if node.op in {"==", "!="}:
            return BOOL
        if node.op in {"&&", "||"}:
            self._expect_bool(left, node.left.span)
            self._expect_bool(right, node.right.span)
            return BOOL
        if node.op == "??":
            inner = left.optional_inner
            if left == NULL:
                return right
            if inner is None and left != ANY:
                self._error(node.left.span, f"left side of `??` must be optional, found {left}", "E317")
                return self._join(left, right, node.span)
            return self._join(inner or ANY, right, node.span)
        if node.op == "..":
            if left != INT or right != INT:
                self._error(node.span, "range bounds must be Int", "E318")
            return LIST(INT)
        return ANY

    def _infer_call(self, node: ast.CallExpr, injected_first: ast.Expr | None = None) -> Type:
        args = ([injected_first] if injected_first is not None else []) + node.args
        # Builtins get precise polymorphic behavior.
        if isinstance(node.callee, ast.Variable):
            name = node.callee.name
            if name in self.functions:
                return self._check_named_call(name, args, node.span)
        if isinstance(node.callee, ast.MemberExpr):
            return self._infer_method_call(node.callee, args, node.span)
        callee_type = self._infer(node.callee)
        if callee_type.name != "Fn":
            self._error(node.callee.span, f"value of type {callee_type} is not callable", "E319")
            for arg in args:
                self._infer(arg)
            return ANY
        param_types = list(callee_type.args[:-1])
        result = callee_type.args[-1] if callee_type.args else ANY
        if len(args) != len(param_types):
            self._error(node.span, f"expected {len(param_types)} arguments, found {len(args)}", "E320")
        for index, arg in enumerate(args):
            actual = self._infer(arg)
            if index < len(param_types) and not self._assignable(actual, param_types[index]):
                self._type_error(arg.span, actual, param_types[index], context=f"argument {index + 1}")
        return result

    def _check_named_call(self, name: str, args: list[ast.Expr], span: ast.Span) -> Type:
        signature = self.functions[name]
        if len(args) != len(signature.params):
            self._error(span, f"`{name}` expects {len(signature.params)} arguments, found {len(args)}", "E321")
        actual_types: list[Type] = []
        for index, arg in enumerate(args):
            actual = self._infer(arg)
            actual_types.append(actual)
            if index < len(signature.params):
                expected = signature.params[index]
                if expected != ANY and not self._assignable(actual, expected):
                    self._type_error(arg.span, actual, expected, context=f"argument {index + 1} of `{name}`")
                mode = signature.modes[index]
                if mode == "take":
                    self._consume_argument(arg)
        # Polymorphic standard functions.
        if name == "print":
            return VOID
        if name == "debug":
            return actual_types[0] if actual_types else ANY
        if name == "sum":
            if actual_types and actual_types[0].name == "List" and actual_types[0].args:
                return actual_types[0].args[0]
            return ANY
        if name in {"min", "max"}:
            if len(actual_types) == 1 and actual_types[0].name == "List":
                return actual_types[0].args[0]
            return self._join_many(actual_types, span)
        if name == "map" and len(actual_types) >= 2:
            fn_type = actual_types[1]
            item = fn_type.args[-1] if fn_type.name == "Fn" and fn_type.args else ANY
            return LIST(item)
        if name == "filter" and actual_types:
            return actual_types[0]
        if name == "fold" and len(actual_types) >= 2:
            return actual_types[1]
        if name in {"sort", "reverse", "push", "concat"} and actual_types:
            if name == "push" and actual_types[0].name == "List" and len(actual_types) > 1:
                return LIST(self._join(actual_types[0].args[0], actual_types[1], span))
            if name == "concat" and len(actual_types) > 1 and actual_types[0].name == actual_types[1].name == "List":
                return LIST(self._join(actual_types[0].args[0], actual_types[1].args[0], span))
            return actual_types[0]
        if name in {"first", "last"} and actual_types and actual_types[0].name == "List":
            return OPTIONAL(actual_types[0].args[0])
        if name in {"vadd", "vsub", "vmul", "vdiv"} and len(actual_types) >= 2:
            left_item = actual_types[0].args[0] if actual_types[0].name == "List" else ANY
            right_item = actual_types[1].args[0] if actual_types[1].name == "List" else ANY
            item = FLOAT if name == "vdiv" else self._join(left_item, right_item, span)
            return LIST(item)
        if name == "dot" and len(actual_types) >= 2:
            left_item = actual_types[0].args[0] if actual_types[0].name == "List" else ANY
            right_item = actual_types[1].args[0] if actual_types[1].name == "List" else ANY
            return self._join(left_item, right_item, span)
        if name == "unwrap" and actual_types:
            return actual_types[0].optional_inner or ANY
        if name == "finite_functions":
            domain_item = actual_types[0].args[0] if actual_types and actual_types[0].name == "List" else ANY
            codomain_item = actual_types[1].args[0] if len(actual_types) > 1 and actual_types[1].name == "List" else ANY
            return LIST(FN([domain_item], codomain_item))
        return signature.result

    def _infer_member(self, node: ast.MemberExpr) -> Type:
        target = self._infer(node.target)
        methods = {
            "len": FN([], INT),
            "map": FN([FN([ANY], ANY)], LIST(ANY)),
            "filter": FN([FN([ANY], BOOL)], target),
            "fold": FN([ANY, FN([ANY, ANY], ANY)], ANY),
            "sort": FN([], target),
            "reverse": FN([], target),
            "contains": FN([ANY], BOOL),
            "push": FN([ANY], target),
            "first": FN([], OPTIONAL(target.args[0] if target.name == "List" and target.args else ANY)),
            "last": FN([], OPTIONAL(target.args[0] if target.name == "List" and target.args else ANY)),
        }
        if target.name in {"List", "Text", "Tuple"} and node.name in methods:
            return methods[node.name]
        self._error(node.span, f"type {target} has no member `{node.name}`", "E322")
        return ANY

    def _infer_method_call(self, member: ast.MemberExpr, args: list[ast.Expr], span: ast.Span) -> Type:
        target_type = self._infer(member.target)
        actual_types = [self._infer(arg) for arg in args]
        name = member.name
        if name == "len":
            if args:
                self._error(span, "`.len()` takes no arguments", "E323")
            return INT
        if target_type.name == "List":
            item = target_type.args[0] if target_type.args else ANY
            if name == "map":
                if len(actual_types) != 1:
                    self._error(span, "`.map()` expects one function", "E324")
                    return LIST(ANY)
                fn_type = actual_types[0]
                return LIST(fn_type.args[-1] if fn_type.name == "Fn" and fn_type.args else ANY)
            if name == "filter":
                return target_type
            if name == "fold":
                return actual_types[0] if actual_types else ANY
            if name in {"sort", "reverse"}:
                return target_type
            if name == "contains":
                return BOOL
            if name == "push":
                return LIST(self._join(item, actual_types[0] if actual_types else ANY, span))
            if name in {"first", "last"}:
                return OPTIONAL(item)
        self._error(span, f"unsupported method `{name}` for {target_type}", "E325")
        return ANY

    def _infer_pipe(self, node: ast.PipeExpr) -> Type:
        if isinstance(node.target, ast.CallExpr):
            return self._infer_call(node.target, injected_first=node.value)
        if isinstance(node.target, ast.Variable):
            synthetic = ast.CallExpr(node.span, node.target, [])
            return self._infer_call(synthetic, injected_first=node.value)
        if isinstance(node.target, ast.MemberExpr):
            # `x |> f.method` means `f.method(x)`.
            synthetic = ast.CallExpr(node.span, node.target, [])
            return self._infer_call(synthetic, injected_first=node.value)
        self._infer(node.value)
        self._error(node.target.span, "right side of `|>` must be a function or call", "E326")
        return ANY

    def _check_branch(self, block: ast.Block, *, value_context: bool = False) -> Type:
        parent = self.scope
        self.scope = Scope(parent)
        result = VOID
        for index, statement in enumerate(block.statements):
            is_last = index == len(block.statements) - 1
            if value_context and is_last and isinstance(statement, ast.ExprStmt):
                result = self._infer(statement.expr, statement_context=False)
            else:
                result = self._check_stmt(statement)
        self.scope = parent
        return result

    def _consume_argument(self, arg: ast.Expr) -> None:
        if not isinstance(arg, ast.Variable):
            return
        binding = self.scope.resolve(arg.name)
        if binding is None:
            return
        if not binding.owned:
            self._error(arg.span, f"argument `{arg.name}` must be owned for a `take` parameter", "E327", "bind it with `let own`")
        elif binding.moved:
            self._moved_error(arg.span, arg.name)
        else:
            binding.moved = True

    def _from_ref(self, ref: ast.TypeRef | None) -> Type:
        if ref is None:
            return ANY
        aliases = {
            "Int": INT,
            "Float": FLOAT,
            "Bool": BOOL,
            "Text": TEXT,
            "String": TEXT,
            "Null": NULL,
            "Void": VOID,
            "Any": ANY,
            "Resource": RESOURCE,
        }
        base = aliases.get(ref.name, Type(ref.name, tuple(self._from_ref(arg) for arg in ref.args)))
        if ref.name == "List":
            base = LIST(self._from_ref(ref.args[0]) if ref.args else ANY)
        elif ref.name == "Tuple":
            base = TUPLE(self._from_ref(arg) for arg in ref.args)
        return OPTIONAL(base) if ref.optional else base

    @staticmethod
    def _numeric(value: Type) -> bool:
        return value in {INT, FLOAT, ANY}

    def _expect_bool(self, actual: Type, span: ast.Span) -> None:
        if actual not in {BOOL, ANY}:
            self._type_error(span, actual, BOOL, context="condition")

    def _assignable(self, source: Type, target: Type) -> bool:
        if ANY in {source, target} or UNKNOWN in {source, target}:
            return True
        if source == target:
            return True
        if source == INT and target == FLOAT:
            return True
        if source == NULL and target.name == "Optional":
            return True
        if target.name == "Optional" and target.args:
            return self._assignable(source, target.args[0])
        if source.name == target.name and len(source.args) == len(target.args):
            return all(self._assignable(a, b) for a, b in zip(source.args, target.args))
        return False

    def _join(self, left: Type, right: Type, span: ast.Span) -> Type:
        if left == right:
            return left
        if ANY in {left, right}:
            return ANY
        if UNKNOWN in {left, right}:
            return right if left == UNKNOWN else left
        if {left, right} == {INT, FLOAT}:
            return FLOAT
        if left == NULL:
            return right if right.name == "Optional" else OPTIONAL(right)
        if right == NULL:
            return left if left.name == "Optional" else OPTIONAL(left)
        if left.name == right.name == "Optional":
            return OPTIONAL(self._join(left.args[0], right.args[0], span))
        if left.name == "Optional":
            return OPTIONAL(self._join(left.args[0], right, span))
        if right.name == "Optional":
            return OPTIONAL(self._join(left, right.args[0], span))
        if left.name == right.name == "List":
            return LIST(self._join(left.args[0], right.args[0], span))
        self._error(span, f"incompatible branch/item types: {left} and {right}", "E328")
        return ANY

    def _join_many(self, values: list[Type], span: ast.Span) -> Type:
        if not values:
            return ANY
        result = values[0]
        for value in values[1:]:
            result = self._join(result, value, span)
        return result

    def _narrowing_from(
        self, condition: ast.Expr
    ) -> tuple[list[tuple[Binding, Type]], list[tuple[Binding, Type]]]:
        """Work out what a null test proves, per branch.

        Returns (narrowed when true, narrowed when false). `x != null` proves
        `x` is present on the true side; `x == null` proves it on the false
        side. Only a direct comparison of a name against `null` is recognised:
        anything more clever risks claiming a guarantee the program does not
        actually make, and an unsound narrowing is far worse than a missing one.
        """
        if not isinstance(condition, ast.BinaryExpr) or condition.op not in ("==", "!="):
            return [], []

        for name_side, null_side in (
            (condition.left, condition.right),
            (condition.right, condition.left),
        ):
            if not isinstance(name_side, ast.Variable):
                continue
            if not (isinstance(null_side, ast.Literal) and null_side.kind == "Null"):
                continue

            binding = self.scope.resolve(name_side.name)
            if binding is None:
                continue
            inner = binding.type.optional_inner
            if inner is None:
                continue        # not optional: nothing to narrow

            present = [(binding, inner)]
            return (present, []) if condition.op == "!=" else ([], present)

        return [], []

    @staticmethod
    def _apply_narrowing(narrowed: list[tuple[Binding, Type]]) -> None:
        for binding, narrower in narrowed:
            if binding.declared_type is None:
                binding.declared_type = binding.type
            binding.type = narrower

    def _capture_visible_bindings(self) -> dict[int, tuple[Binding, bool, Type]]:
        result: dict[int, tuple[Binding, bool, Type]] = {}
        scope: Scope | None = self.scope
        while scope:
            for binding in scope.values.values():
                result[id(binding)] = (binding, binding.moved, binding.type)
            scope = scope.parent
        return result

    @staticmethod
    def _restore_visible_bindings(
        snapshot: dict[int, tuple[Binding, bool, Type]]
    ) -> None:
        # Types are restored alongside move state, so a narrowing applied inside
        # one branch cannot leak into the other or outlive the `if`.
        for binding, moved, declared in snapshot.values():
            binding.moved = moved
            binding.type = declared

    @staticmethod
    def _state_by_name(scope: Scope) -> dict[str, Binding]:
        result: dict[str, Binding] = {}
        current: Scope | None = scope
        while current:
            for name, binding in current.values.items():
                result.setdefault(name, binding)
            current = current.parent
        return result

    def _merge_branch_moves(
        self,
        then_state: dict[int, tuple[Binding, bool, Type]],
        else_state: dict[int, tuple[Binding, bool, Type]],
    ) -> None:
        for key, (binding, then_moved, _type) in then_state.items():
            entry = else_state.get(key)
            else_moved = entry[1] if entry is not None else False
            if binding.owned:
                binding.moved = then_moved or else_moved

    def _merge_loop_moves(self, before: dict[int, tuple[Binding, bool, Type]]) -> None:
        # A loop may run, so any outer value consumed in its body is considered consumed after it.
        for key, (binding, was_moved, _type) in before.items():
            if binding.owned:
                binding.moved = binding.moved or was_moved

    def _block_definitely_returns(self, block: ast.Block) -> bool:
        for statement in block.statements:
            if isinstance(statement, ast.ReturnStmt):
                return True
            if isinstance(statement, ast.ExprStmt) and isinstance(statement.expr, ast.IfExpr):
                expr = statement.expr
                if expr.else_branch and self._block_definitely_returns(expr.then_branch) and self._block_definitely_returns(expr.else_branch):
                    return True
        return False

    def _type_error(self, span: ast.Span, actual: Type, expected: Type, context: str = "type") -> None:
        self._error(span, f"{context} mismatch: expected {expected}, found {actual}", "E329")

    def _moved_error(self, span: ast.Span, name: str) -> None:
        self._error(span, f"owned value `{name}` was already taken", "E330", "use it before `take`, or create an explicit shared value")

    def _error(self, span: ast.Span, message: str, code: str, hint: str | None = None) -> None:
        self.diagnostics.error(message, span, code, hint)
