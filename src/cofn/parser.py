from __future__ import annotations

from . import ast
from .diagnostics import DiagnosticBag
from .tokens import Token, TokenKind


class Parser:
    def __init__(self, tokens: list[Token]) -> None:
        self.tokens = tokens
        self.current = 0
        self.diagnostics = DiagnosticBag()

    def parse(self) -> tuple[ast.Program, DiagnosticBag]:
        declarations: list[ast.Stmt] = []
        self._skip_semis()
        start = self._peek().span
        while not self._check(TokenKind.EOF):
            try:
                declarations.append(self._declaration())
            except ParseAbort:
                self._synchronize()
            self._skip_semis()
        end = self._peek().span
        return ast.Program(ast.Span.merge(start, end), declarations), self.diagnostics

    def _declaration(self) -> ast.Stmt:
        if self._match(TokenKind.LAW):
            return self._law_decl()
        is_meta = False
        if self._match(TokenKind.META):
            is_meta = True
            self._consume(TokenKind.FN, "expected `fn` after `meta`", "E201")
            return self._function_decl(is_meta=True, fn_already_consumed=True)
        if self._match(TokenKind.FN):
            if self._check(TokenKind.IDENT):
                return self._function_decl(is_meta=False, fn_already_consumed=True)
            self._error(self._peek(), "a top-level lambda must be assigned to a name", "E202")
            raise ParseAbort()
        return self._statement()

    def _law_decl(self) -> ast.LawDecl:
        start = self._previous().span
        if self._match(TokenKind.MONAD):
            kind = "monad"
        else:
            self._error(self._peek(), "expected a supported law family after `law`", "E231")
            raise ParseAbort()

        name = self._consume(TokenKind.IDENT, "expected law declaration name", "E232")
        self._skip_semis()
        self._consume(TokenKind.LBRACE, "expected `{` after law declaration name", "E233")
        entries: dict[str, ast.Expr] = {}
        self._skip_semis()
        while not self._check(TokenKind.RBRACE) and not self._check(TokenKind.EOF):
            key = self._consume(TokenKind.IDENT, "expected law entry name", "E234")
            key_name = str(key.value)
            self._consume(TokenKind.EQ, "expected `=` after law entry name", "E235")
            value = self._expression()
            if key_name in entries:
                self._error(key, f"duplicate law entry `{key_name}`", "E236")
            else:
                entries[key_name] = value
            self._skip_semis()
            self._match(TokenKind.COMMA)
            self._skip_semis()
        close = self._consume(TokenKind.RBRACE, "expected `}` after law declaration", "E237")
        return ast.LawDecl(ast.Span.merge(start, close.span), kind, str(name.value), entries)

    def _function_decl(self, is_meta: bool, fn_already_consumed: bool = False) -> ast.FunctionDecl:
        start = self._previous().span if fn_already_consumed else self._peek().span
        name = self._consume(TokenKind.IDENT, "expected function name", "E203")
        self._consume(TokenKind.LPAREN, "expected `(` after function name", "E204")
        params = self._params()
        self._consume(TokenKind.RPAREN, "expected `)` after parameters", "E205")
        return_type = None
        if self._match(TokenKind.ARROW):
            return_type = self._type_ref()
        if self._match(TokenKind.EQ):
            value = self._expression()
            ret = ast.ReturnStmt(value.span, value)
            body = ast.Block(value.span, [ret])
        else:
            body = self._block("expected `{` or `=` before function body")
        return ast.FunctionDecl(ast.Span.merge(start, body.span), str(name.value), params, return_type, body, is_meta)

    def _params(self) -> list[ast.Param]:
        params: list[ast.Param] = []
        self._skip_semis()
        if self._check(TokenKind.RPAREN):
            return params
        while True:
            self._skip_semis()
            mode = "value"
            if self._match(TokenKind.READ):
                mode = "read"
            elif self._match(TokenKind.EDIT):
                mode = "edit"
            elif self._match(TokenKind.TAKE):
                mode = "take"
            name = self._consume(TokenKind.IDENT, "expected parameter name", "E206")
            annotation = None
            if self._match(TokenKind.COLON):
                annotation = self._type_ref()
            params.append(ast.Param(name.span, str(name.value), annotation, mode))
            self._skip_semis()
            if not self._match(TokenKind.COMMA):
                break
        return params

    def _type_ref(self) -> ast.TypeRef:
        token = self._consume(TokenKind.IDENT, "expected type name", "E207")
        args: list[ast.TypeRef] = []
        end = token.span
        if self._match(TokenKind.LBRACKET):
            self._skip_semis()
            if not self._check(TokenKind.RBRACKET):
                while True:
                    args.append(self._type_ref())
                    self._skip_semis()
                    if not self._match(TokenKind.COMMA):
                        break
            close = self._consume(TokenKind.RBRACKET, "expected `]` after type arguments", "E208")
            end = close.span
        optional = self._match(TokenKind.QUESTION)
        if optional:
            end = self._previous().span
        return ast.TypeRef(ast.Span.merge(token.span, end), str(token.value), args, optional)

    def _statement(self) -> ast.Stmt:
        if self._match(TokenKind.LET):
            return self._let_statement()
        if self._match(TokenKind.RETURN):
            return self._return_statement()
        if self._match(TokenKind.WHILE):
            return self._while_statement()
        if self._match(TokenKind.FOR):
            return self._for_statement()
        if self._match(TokenKind.TAKE):
            return self._take_statement()
        if self._match(TokenKind.BREAK):
            return ast.BreakStmt(self._previous().span)
        if self._match(TokenKind.CONTINUE):
            return ast.ContinueStmt(self._previous().span)

        expr = self._expression()
        if isinstance(expr, ast.Variable) and self._match(TokenKind.EQ):
            value = self._expression()
            return ast.AssignStmt(ast.Span.merge(expr.span, value.span), expr.name, value)
        return ast.ExprStmt(expr.span, expr)

    def _let_statement(self) -> ast.LetStmt:
        start = self._previous().span
        owned = self._match(TokenKind.OWN)
        mutable = self._match(TokenKind.MUT)
        if not owned and self._match(TokenKind.OWN):
            owned = True
        name = self._consume(TokenKind.IDENT, "expected variable name", "E209")
        annotation = None
        if self._match(TokenKind.COLON):
            annotation = self._type_ref()
        self._consume(TokenKind.EQ, "expected `=` in binding", "E210")
        value = self._expression()
        return ast.LetStmt(ast.Span.merge(start, value.span), str(name.value), annotation, value, owned, mutable)

    def _return_statement(self) -> ast.ReturnStmt:
        start = self._previous().span
        if self._check(TokenKind.SEMI) or self._check(TokenKind.RBRACE) or self._check(TokenKind.EOF):
            return ast.ReturnStmt(start, None)
        value = self._expression()
        return ast.ReturnStmt(ast.Span.merge(start, value.span), value)

    def _while_statement(self) -> ast.WhileStmt:
        start = self._previous().span
        condition = self._expression()
        body = self._block("expected `{` after while condition")
        return ast.WhileStmt(ast.Span.merge(start, body.span), condition, body)

    def _for_statement(self) -> ast.ForStmt:
        start = self._previous().span
        name = self._consume(TokenKind.IDENT, "expected loop variable", "E211")
        self._consume(TokenKind.IN, "expected `in` after loop variable", "E212")
        iterable = self._expression()
        body = self._block("expected `{` after for iterable")
        return ast.ForStmt(ast.Span.merge(start, body.span), str(name.value), iterable, body)

    def _take_statement(self) -> ast.TakeStmt:
        start = self._previous().span
        name = self._consume(TokenKind.IDENT, "expected owned variable after `take`", "E213")
        return ast.TakeStmt(ast.Span.merge(start, name.span), str(name.value))

    def _block(self, message: str) -> ast.Block:
        self._skip_semis()
        open_token = self._consume(TokenKind.LBRACE, message, "E214")
        statements: list[ast.Stmt] = []
        self._skip_semis()
        while not self._check(TokenKind.RBRACE) and not self._check(TokenKind.EOF):
            try:
                if self._check(TokenKind.FN) or self._check(TokenKind.META):
                    statements.append(self._declaration())
                else:
                    statements.append(self._statement())
            except ParseAbort:
                self._synchronize(in_block=True)
            self._skip_semis()
        close = self._consume(TokenKind.RBRACE, "expected `}` after block", "E215")
        return ast.Block(ast.Span.merge(open_token.span, close.span), statements)

    def _expression(self, min_bp: int = 0) -> ast.Expr:
        left = self._prefix()

        while True:
            token = self._peek()

            # Postfix operators.
            if token.kind == TokenKind.LPAREN and 100 >= min_bp:
                left = self._finish_call(left)
                continue
            if token.kind == TokenKind.DOT and 100 >= min_bp:
                self._advance()
                name = self._consume(TokenKind.IDENT, "expected member name after `.`", "E216")
                left = ast.MemberExpr(ast.Span.merge(left.span, name.span), left, str(name.value))
                continue
            if token.kind == TokenKind.LBRACKET and 100 >= min_bp:
                self._advance()
                index = self._expression()
                close = self._consume(TokenKind.RBRACKET, "expected `]` after index", "E217")
                left = ast.IndexExpr(ast.Span.merge(left.span, close.span), left, index)
                continue

            infix = self._infix_binding_power(token.kind)
            if infix is None:
                break
            left_bp, right_bp, op = infix
            if left_bp < min_bp:
                break
            self._advance()
            self._skip_semis()
            right = self._expression(right_bp)
            span = ast.Span.merge(left.span, right.span)
            if token.kind == TokenKind.PIPE:
                left = ast.PipeExpr(span, left, right)
            else:
                left = ast.BinaryExpr(span, left, op, right)
        return left

    def _prefix(self) -> ast.Expr:
        token = self._advance()
        kind = token.kind
        if kind == TokenKind.INT:
            return ast.Literal(token.span, token.value, "Int")
        if kind == TokenKind.FLOAT:
            return ast.Literal(token.span, token.value, "Float")
        if kind == TokenKind.STRING:
            return ast.Literal(token.span, token.value, "Text")
        if kind == TokenKind.TRUE:
            return ast.Literal(token.span, True, "Bool")
        if kind == TokenKind.FALSE:
            return ast.Literal(token.span, False, "Bool")
        if kind == TokenKind.NULL:
            return ast.Literal(token.span, None, "Null")
        if kind == TokenKind.IDENT:
            return ast.Variable(token.span, str(token.value))
        if kind in (TokenKind.MINUS, TokenKind.PLUS, TokenKind.BANG):
            operand = self._expression(90)
            return ast.UnaryExpr(ast.Span.merge(token.span, operand.span), token.lexeme, operand)
        if kind == TokenKind.LPAREN:
            self._skip_semis()
            if self._match(TokenKind.RPAREN):
                return ast.TupleLiteral(ast.Span.merge(token.span, self._previous().span), [])
            first = self._expression()
            self._skip_semis()
            if self._match(TokenKind.COMMA):
                items = [first]
                while not self._check(TokenKind.RPAREN):
                    self._skip_semis()
                    items.append(self._expression())
                    self._skip_semis()
                    if not self._match(TokenKind.COMMA):
                        break
                close = self._consume(TokenKind.RPAREN, "expected `)` after tuple", "E218")
                return ast.TupleLiteral(ast.Span.merge(token.span, close.span), items)
            close = self._consume(TokenKind.RPAREN, "expected `)` after expression", "E219")
            first.span = ast.Span.merge(token.span, close.span)
            return first
        if kind == TokenKind.LBRACKET:
            items: list[ast.Expr] = []
            self._skip_semis()
            if not self._check(TokenKind.RBRACKET):
                while True:
                    items.append(self._expression())
                    self._skip_semis()
                    if not self._match(TokenKind.COMMA):
                        break
                    self._skip_semis()
            close = self._consume(TokenKind.RBRACKET, "expected `]` after list", "E220")
            return ast.ListLiteral(ast.Span.merge(token.span, close.span), items)
        if kind == TokenKind.IF:
            return self._if_expression(token)
        if kind == TokenKind.FN:
            return self._lambda_expression(token)

        self._error(token, f"expected expression, found `{token.lexeme or token.kind.name}`", "E221")
        raise ParseAbort()

    def _if_expression(self, if_token: Token) -> ast.IfExpr:
        condition = self._expression()
        then_branch = self._block("expected `{` after if condition")
        self._skip_semis()
        else_branch = None
        end = then_branch.span
        if self._match(TokenKind.ELSE):
            self._skip_semis()
            if self._match(TokenKind.IF):
                nested = self._if_expression(self._previous())
                else_branch = ast.Block(nested.span, [ast.ExprStmt(nested.span, nested)])
            else:
                else_branch = self._block("expected `{` or `if` after `else`")
            end = else_branch.span
        return ast.IfExpr(ast.Span.merge(if_token.span, end), condition, then_branch, else_branch)

    def _lambda_expression(self, fn_token: Token) -> ast.LambdaExpr:
        self._consume(TokenKind.LPAREN, "lambda syntax is `fn(x) => expression`", "E222")
        params = self._params()
        self._consume(TokenKind.RPAREN, "expected `)` after lambda parameters", "E223")
        if self._match(TokenKind.FAT_ARROW):
            expression = self._expression()
            body = ast.Block(expression.span, [ast.ReturnStmt(expression.span, expression)])
        else:
            body = self._block("expected `=>` or `{` after lambda parameters")
        return ast.LambdaExpr(ast.Span.merge(fn_token.span, body.span), params, body)

    def _finish_call(self, callee: ast.Expr) -> ast.CallExpr:
        self._advance()  # (
        args: list[ast.Expr] = []
        self._skip_semis()
        if not self._check(TokenKind.RPAREN):
            while True:
                args.append(self._expression())
                self._skip_semis()
                if not self._match(TokenKind.COMMA):
                    break
                self._skip_semis()
        close = self._consume(TokenKind.RPAREN, "expected `)` after arguments", "E224")
        return ast.CallExpr(ast.Span.merge(callee.span, close.span), callee, args)

    @staticmethod
    def _infix_binding_power(kind: TokenKind) -> tuple[int, int, str] | None:
        table = {
            TokenKind.PIPE: (1, 2, "|>"),
            TokenKind.COALESCE: (3, 4, "??"),
            TokenKind.OR: (5, 6, "||"),
            TokenKind.AND: (7, 8, "&&"),
            TokenKind.EQEQ: (9, 10, "=="),
            TokenKind.NE: (9, 10, "!="),
            TokenKind.LT: (11, 12, "<"),
            TokenKind.LE: (11, 12, "<="),
            TokenKind.GT: (11, 12, ">"),
            TokenKind.GE: (11, 12, ">="),
            TokenKind.RANGE: (13, 14, ".."),
            TokenKind.PLUS: (15, 16, "+"),
            TokenKind.MINUS: (15, 16, "-"),
            TokenKind.STAR: (17, 18, "*"),
            TokenKind.SLASH: (17, 18, "/"),
            TokenKind.FLOOR_DIV: (17, 18, "//"),
            TokenKind.PERCENT: (17, 18, "%"),
            TokenKind.POWER: (20, 19, "**"),  # right associative
        }
        return table.get(kind)

    def _skip_semis(self) -> None:
        while self._match(TokenKind.SEMI):
            pass

    def _consume(self, kind: TokenKind, message: str, code: str) -> Token:
        if self._check(kind):
            return self._advance()
        self._error(self._peek(), message, code)
        raise ParseAbort()

    def _error(self, token: Token, message: str, code: str) -> None:
        self.diagnostics.error(message, token.span, code)

    def _synchronize(self, in_block: bool = False) -> None:
        while not self._check(TokenKind.EOF):
            if self._previous().kind == TokenKind.SEMI:
                return
            if in_block and self._check(TokenKind.RBRACE):
                return
            if self._peek().kind in {
                TokenKind.FN,
                TokenKind.LAW,
                TokenKind.LET,
                TokenKind.RETURN,
                TokenKind.IF,
                TokenKind.WHILE,
                TokenKind.FOR,
                TokenKind.TAKE,
                TokenKind.BREAK,
                TokenKind.CONTINUE,
            }:
                return
            self._advance()

    def _match(self, *kinds: TokenKind) -> bool:
        for kind in kinds:
            if self._check(kind):
                self._advance()
                return True
        return False

    def _check(self, kind: TokenKind) -> bool:
        return self._peek().kind == kind

    def _advance(self) -> Token:
        if not self._check(TokenKind.EOF):
            self.current += 1
        return self._previous()

    def _peek(self) -> Token:
        return self.tokens[self.current]

    def _previous(self) -> Token:
        return self.tokens[max(0, self.current - 1)]


class ParseAbort(Exception):
    pass
