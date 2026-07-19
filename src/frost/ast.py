from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Optional


@dataclass(frozen=True, slots=True)
class Span:
    line: int
    column: int
    end_line: int
    end_column: int

    @staticmethod
    def merge(a: "Span", b: "Span") -> "Span":
        return Span(a.line, a.column, b.end_line, b.end_column)


@dataclass(slots=True)
class Node:
    span: Span


@dataclass(slots=True)
class TypeRef(Node):
    name: str
    args: list["TypeRef"] = field(default_factory=list)
    optional: bool = False

    def __str__(self) -> str:
        suffix = "?" if self.optional else ""
        if self.args:
            return f"{self.name}[{', '.join(map(str, self.args))}]{suffix}"
        return f"{self.name}{suffix}"


@dataclass(slots=True)
class Program(Node):
    declarations: list["Stmt"]


@dataclass(slots=True)
class Param(Node):
    name: str
    annotation: Optional[TypeRef]
    mode: str = "value"  # value | read | edit | take


@dataclass(slots=True)
class Stmt(Node):
    pass


@dataclass(slots=True)
class FunctionDecl(Stmt):
    name: str
    params: list[Param]
    return_type: Optional[TypeRef]
    body: "Block"
    is_meta: bool = False


@dataclass(slots=True)
class LawDecl(Stmt):
    """Compile-time law declaration.

    Stage 0 implements the `monad` law family with bounded exhaustive model
    checking.  Keeping the entries as ordinary expressions lets the normal
    parser, type checker, and evaluator participate instead of introducing a
    second compile-time language.
    """

    kind: str
    name: str
    entries: dict[str, "Expr"]


@dataclass(slots=True)
class Block(Node):
    statements: list[Stmt]


@dataclass(slots=True)
class LetStmt(Stmt):
    name: str
    annotation: Optional[TypeRef]
    value: "Expr"
    owned: bool = False
    mutable: bool = False


@dataclass(slots=True)
class AssignStmt(Stmt):
    name: str
    value: "Expr"


@dataclass(slots=True)
class ReturnStmt(Stmt):
    value: Optional["Expr"]


@dataclass(slots=True)
class WhileStmt(Stmt):
    condition: "Expr"
    body: Block


@dataclass(slots=True)
class ForStmt(Stmt):
    name: str
    iterable: "Expr"
    body: Block


@dataclass(slots=True)
class TakeStmt(Stmt):
    name: str


@dataclass(slots=True)
class BreakStmt(Stmt):
    pass


@dataclass(slots=True)
class ContinueStmt(Stmt):
    pass


@dataclass(slots=True)
class ExprStmt(Stmt):
    expr: "Expr"


@dataclass(slots=True)
class Expr(Node):
    inferred_type: Any = field(default=None, init=False, repr=False)


@dataclass(slots=True)
class Literal(Expr):
    value: Any
    kind: str


@dataclass(slots=True)
class Variable(Expr):
    name: str


@dataclass(slots=True)
class ListLiteral(Expr):
    items: list[Expr]


@dataclass(slots=True)
class TupleLiteral(Expr):
    items: list[Expr]


@dataclass(slots=True)
class UnaryExpr(Expr):
    op: str
    operand: Expr


@dataclass(slots=True)
class BinaryExpr(Expr):
    left: Expr
    op: str
    right: Expr


@dataclass(slots=True)
class CallExpr(Expr):
    callee: Expr
    args: list[Expr]


@dataclass(slots=True)
class MemberExpr(Expr):
    target: Expr
    name: str


@dataclass(slots=True)
class IndexExpr(Expr):
    target: Expr
    index: Expr


@dataclass(slots=True)
class LambdaExpr(Expr):
    params: list[Param]
    body: Block


@dataclass(slots=True)
class IfExpr(Expr):
    condition: Expr
    then_branch: Block
    else_branch: Optional[Block]


@dataclass(slots=True)
class PipeExpr(Expr):
    value: Expr
    target: Expr
