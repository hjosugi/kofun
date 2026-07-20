from __future__ import annotations

from dataclasses import dataclass
from enum import Enum, auto

from .ast import Span


class TokenKind(Enum):
    EOF = auto()
    IDENT = auto()
    INT = auto()
    FLOAT = auto()
    STRING = auto()
    SEMI = auto()
    COMMA = auto()
    COLON = auto()
    DOT = auto()
    QUESTION = auto()
    LPAREN = auto()
    RPAREN = auto()
    LBRACE = auto()
    RBRACE = auto()
    LBRACKET = auto()
    RBRACKET = auto()
    PLUS = auto()
    MINUS = auto()
    STAR = auto()
    SLASH = auto()
    FLOOR_DIV = auto()
    PERCENT = auto()
    POWER = auto()
    EQ = auto()
    EQEQ = auto()
    NE = auto()
    LT = auto()
    LE = auto()
    GT = auto()
    GE = auto()
    AND = auto()
    OR = auto()
    BANG = auto()
    ARROW = auto()
    FAT_ARROW = auto()
    PIPE = auto()
    COALESCE = auto()
    RANGE = auto()
    FN = auto()
    LET = auto()
    MUT = auto()
    OWN = auto()
    IF = auto()
    ELSE = auto()
    WHILE = auto()
    FOR = auto()
    IN = auto()
    RETURN = auto()
    TRUE = auto()
    FALSE = auto()
    NULL = auto()
    TAKE = auto()
    READ = auto()
    EDIT = auto()
    BREAK = auto()
    CONTINUE = auto()
    META = auto()
    LAW = auto()
    MONAD = auto()
    RECORD = auto()


KEYWORDS = {
    "fn": TokenKind.FN,
    "let": TokenKind.LET,
    "mut": TokenKind.MUT,
    "own": TokenKind.OWN,
    "if": TokenKind.IF,
    "else": TokenKind.ELSE,
    "while": TokenKind.WHILE,
    "for": TokenKind.FOR,
    "in": TokenKind.IN,
    "return": TokenKind.RETURN,
    "true": TokenKind.TRUE,
    "false": TokenKind.FALSE,
    "null": TokenKind.NULL,
    "take": TokenKind.TAKE,
    "read": TokenKind.READ,
    "edit": TokenKind.EDIT,
    "break": TokenKind.BREAK,
    "continue": TokenKind.CONTINUE,
    "meta": TokenKind.META,
    "law": TokenKind.LAW,
    "monad": TokenKind.MONAD,
    "record": TokenKind.RECORD,
}


@dataclass(frozen=True, slots=True)
class Token:
    kind: TokenKind
    lexeme: str
    value: object | None
    span: Span
