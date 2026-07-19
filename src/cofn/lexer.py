from __future__ import annotations

from .ast import Span
from .diagnostics import DiagnosticBag
from .tokens import KEYWORDS, Token, TokenKind


class Lexer:
    def __init__(self, source: str) -> None:
        self.source = source
        self.length = len(source)
        self.index = 0
        self.line = 1
        self.column = 1
        self.tokens: list[Token] = []
        self.diagnostics = DiagnosticBag()
        self.paren_depth = 0
        self.bracket_depth = 0

    def lex(self) -> tuple[list[Token], DiagnosticBag]:
        while not self._at_end():
            self._scan_token()
        span = Span(self.line, self.column, self.line, self.column)
        self.tokens.append(Token(TokenKind.EOF, "", None, span))
        return self.tokens, self.diagnostics

    def _scan_token(self) -> None:
        start_index = self.index
        start_line = self.line
        start_col = self.column
        ch = self._advance()

        if ch in " \t\r":
            return
        if ch == "\n":
            if self.paren_depth == 0 and self.bracket_depth == 0:
                self._emit_semicolon(start_line, start_col)
            return
        if ch == "#":
            while not self._at_end() and self._peek() != "\n":
                self._advance()
            return
        if ch.isalpha() or ch == "_" or ord(ch) >= 128:
            self._identifier(start_index, start_line, start_col)
            return
        if ch.isdigit():
            self._number(start_index, start_line, start_col)
            return
        if ch == '"':
            self._string(start_index, start_line, start_col)
            return

        single = {
            ";": TokenKind.SEMI,
            ",": TokenKind.COMMA,
            ":": TokenKind.COLON,
            "?": TokenKind.QUESTION,
            "+": TokenKind.PLUS,
            "%": TokenKind.PERCENT,
            "(": TokenKind.LPAREN,
            ")": TokenKind.RPAREN,
            "{": TokenKind.LBRACE,
            "}": TokenKind.RBRACE,
            "[": TokenKind.LBRACKET,
            "]": TokenKind.RBRACKET,
        }
        if ch == "?" and self._match("?"):
            self._emit(TokenKind.COALESCE, start_index, start_line, start_col)
            return
        if ch in single:
            kind = single[ch]
            if kind == TokenKind.LPAREN:
                self.paren_depth += 1
            elif kind == TokenKind.RPAREN:
                self.paren_depth = max(0, self.paren_depth - 1)
            elif kind == TokenKind.LBRACKET:
                self.bracket_depth += 1
            elif kind == TokenKind.RBRACKET:
                self.bracket_depth = max(0, self.bracket_depth - 1)
            self._emit(kind, start_index, start_line, start_col)
            return

        if ch == ".":
            self._emit(TokenKind.RANGE if self._match(".") else TokenKind.DOT, start_index, start_line, start_col)
        elif ch == "/":
            self._emit(TokenKind.FLOOR_DIV if self._match("/") else TokenKind.SLASH, start_index, start_line, start_col)
        elif ch == "-":
            self._emit(TokenKind.ARROW if self._match(">") else TokenKind.MINUS, start_index, start_line, start_col)
        elif ch == "=":
            if self._match("="):
                self._emit(TokenKind.EQEQ, start_index, start_line, start_col)
            elif self._match(">"):
                self._emit(TokenKind.FAT_ARROW, start_index, start_line, start_col)
            else:
                self._emit(TokenKind.EQ, start_index, start_line, start_col)
        elif ch == "!":
            self._emit(TokenKind.NE if self._match("=") else TokenKind.BANG, start_index, start_line, start_col)
        elif ch == "<":
            self._emit(TokenKind.LE if self._match("=") else TokenKind.LT, start_index, start_line, start_col)
        elif ch == ">":
            self._emit(TokenKind.GE if self._match("=") else TokenKind.GT, start_index, start_line, start_col)
        elif ch == "&" and self._match("&"):
            self._emit(TokenKind.AND, start_index, start_line, start_col)
        elif ch == "|":
            if self._match(">"):
                self._emit(TokenKind.PIPE, start_index, start_line, start_col)
            elif self._match("|"):
                self._emit(TokenKind.OR, start_index, start_line, start_col)
            else:
                self._unexpected(ch, start_line, start_col)
        elif ch == "*":
            self._emit(TokenKind.POWER if self._match("*") else TokenKind.STAR, start_index, start_line, start_col)
        else:
            self._unexpected(ch, start_line, start_col)

    def _identifier(self, start: int, line: int, col: int) -> None:
        while not self._at_end() and (self._peek().isalnum() or self._peek() == "_" or ord(self._peek()) >= 128):
            self._advance()
        text = self.source[start : self.index]
        kind = KEYWORDS.get(text, TokenKind.IDENT)
        self._emit(kind, start, line, col, text if kind == TokenKind.IDENT else None)

    def _number(self, start: int, line: int, col: int) -> None:
        while self._peek().isdigit() or self._peek() == "_":
            self._advance()
        is_float = False
        if self._peek() == "." and self._peek_next().isdigit():
            is_float = True
            self._advance()
            while self._peek().isdigit() or self._peek() == "_":
                self._advance()
        if self._peek() in "eE" and (self._peek_next().isdigit() or self._peek_next() in "+-"):
            is_float = True
            self._advance()
            if self._peek() in "+-":
                self._advance()
            while self._peek().isdigit() or self._peek() == "_":
                self._advance()
        text = self.source[start : self.index]
        normalized = text.replace("_", "")
        try:
            value = float(normalized) if is_float else int(normalized)
            self._emit(TokenKind.FLOAT if is_float else TokenKind.INT, start, line, col, value)
        except ValueError:
            span = Span(line, col, self.line, self.column)
            self.diagnostics.error(f"invalid numeric literal `{text}`", span, "E101")

    def _string(self, start: int, line: int, col: int) -> None:
        chars: list[str] = []
        while not self._at_end() and self._peek() != '"':
            ch = self._advance()
            if ch == "\n":
                span = Span(line, col, self.line, self.column)
                self.diagnostics.error("unterminated string literal", span, "E102")
                return
            if ch == "\\":
                if self._at_end():
                    break
                escape = self._advance()
                mapping = {"n": "\n", "r": "\r", "t": "\t", '"': '"', "\\": "\\", "0": "\0"}
                if escape not in mapping:
                    span = Span(self.line, self.column - 2, self.line, self.column)
                    self.diagnostics.error(f"unknown escape `\\{escape}`", span, "E103")
                    chars.append(escape)
                else:
                    chars.append(mapping[escape])
            else:
                chars.append(ch)
        if self._at_end():
            span = Span(line, col, self.line, self.column)
            self.diagnostics.error("unterminated string literal", span, "E102")
            return
        self._advance()  # closing quote
        self._emit(TokenKind.STRING, start, line, col, "".join(chars))

    def _emit_semicolon(self, line: int, col: int) -> None:
        if self.tokens and self.tokens[-1].kind == TokenKind.SEMI:
            return
        # A pipeline may be visually aligned at the beginning of the next line.
        lookahead = self.index
        while lookahead < self.length and self.source[lookahead] in " \t\r\n":
            lookahead += 1
        if self.source.startswith("|>", lookahead):
            return
        # Do not terminate an expression after a continuation token.
        if self.tokens and self.tokens[-1].kind in {
            TokenKind.PIPE,
            TokenKind.PLUS,
            TokenKind.MINUS,
            TokenKind.STAR,
            TokenKind.SLASH,
            TokenKind.FLOOR_DIV,
            TokenKind.PERCENT,
            TokenKind.POWER,
            TokenKind.EQ,
            TokenKind.EQEQ,
            TokenKind.NE,
            TokenKind.LT,
            TokenKind.LE,
            TokenKind.GT,
            TokenKind.GE,
            TokenKind.AND,
            TokenKind.OR,
            TokenKind.COMMA,
            TokenKind.ARROW,
            TokenKind.FAT_ARROW,
        }:
            return
        span = Span(line, col, line, col + 1)
        self.tokens.append(Token(TokenKind.SEMI, ";", None, span))

    def _emit(self, kind: TokenKind, start: int, line: int, col: int, value: object | None = None) -> None:
        text = self.source[start : self.index]
        span = Span(line, col, self.line, self.column)
        self.tokens.append(Token(kind, text, value, span))

    def _unexpected(self, ch: str, line: int, col: int) -> None:
        span = Span(line, col, self.line, self.column)
        hint = None
        if ch == "'":
            hint = 'Cofn strings use double quotes: "text"'
        self.diagnostics.error(f"unexpected character `{ch}`", span, "E100", hint)

    def _advance(self) -> str:
        ch = self.source[self.index]
        self.index += 1
        if ch == "\n":
            self.line += 1
            self.column = 1
        else:
            self.column += 1
        return ch

    def _match(self, expected: str) -> bool:
        if self._at_end() or self.source[self.index] != expected:
            return False
        self._advance()
        return True

    def _peek(self) -> str:
        return "\0" if self._at_end() else self.source[self.index]

    def _peek_next(self) -> str:
        return "\0" if self.index + 1 >= self.length else self.source[self.index + 1]

    def _at_end(self) -> bool:
        return self.index >= self.length
