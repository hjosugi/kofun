#!/usr/bin/env python3
# langc.py -- reference implementation (oracle) for the unnamed language.
#
# Pipeline: lex -> parse -> ownership check (read/edit/take) -> tree-walk eval.
# This file defines the *dynamic semantics* and the *ownership static rules*.
# It is intentionally small and readable so the rules can be ported to the
# real compiler. Full type inference (HM) is a later milestone; the oracle
# checks ownership, mutability, name resolution, and arity statically, and
# everything else dynamically.
#
# CLI:
#   python3 langc.py run   file.lang   # check + execute
#   python3 langc.py check file.lang   # static checks only
#
# All diagnostics go to stdout in a stable format:
#   error[E001] line 12: use of moved value 'xs' (moved at line 10)

import sys
import copy as _copy

# =========================================================================
# Tokens
# =========================================================================

KEYWORDS = {
    "fn", "let", "var", "type", "match", "if", "else", "return",
    "for", "in", "while", "true", "false", "read", "edit", "take",
}

# Multi-char operators, longest first.
OPS = [
    "...", "..=", "..", "|>", "->", "==", "!=", "<=", ">=", "&&", "||", "++",
    "(", ")", "{", "}", "[", "]", ",", ":", ";",
    "+", "-", "*", "/", "%", "<", ">", "=", "|", "?", ".", "!", "_",
]


class Tok:
    __slots__ = ("kind", "val", "line")

    def __init__(self, kind, val, line):
        self.kind = kind
        self.val = val
        self.line = line

    def __repr__(self):
        return f"{self.kind}({self.val!r})@{self.line}"


class LangError(Exception):
    def __init__(self, code, line, msg):
        self.code = code
        self.line = line
        self.msg = msg

    def render(self):
        return f"error[{self.code}] line {self.line}: {self.msg}"


def lex(src):
    toks = []
    i, line, n = 0, 1, len(src)
    # Bracket stack. A newline is insignificant only when the innermost open
    # bracket is '(' or '['. Inside '{' (a block) newlines separate statements.
    stack = []
    while i < n:
        c = src[i]
        if c == "\n":
            line += 1
            i += 1
            if not stack or stack[-1] == "{":
                if toks and toks[-1].kind != "NEWLINE":
                    toks.append(Tok("NEWLINE", "\\n", line - 1))
            continue
        if c in " \t\r":
            i += 1
            continue
        if c == "/" and i + 1 < n and src[i + 1] == "/":
            while i < n and src[i] != "\n":
                i += 1
            continue
        if c == '"':
            j = i + 1
            buf = []
            while j < n and src[j] != '"':
                if src[j] == "\\" and j + 1 < n:
                    esc = src[j + 1]
                    buf.append({"n": "\n", "t": "\t", '"': '"', "\\": "\\"}.get(esc, esc))
                    j += 2
                else:
                    if src[j] == "\n":
                        raise LangError("E900", line, "unterminated string literal")
                    buf.append(src[j])
                    j += 1
            if j >= n:
                raise LangError("E900", line, "unterminated string literal")
            toks.append(Tok("STRING", "".join(buf), line))
            i = j + 1
            continue
        if c.isdigit():
            j = i
            while j < n and src[j].isdigit():
                j += 1
            # float: digit '.' digit  (but not '..' range)
            if j < n and src[j] == "." and j + 1 < n and src[j + 1].isdigit():
                j += 1
                while j < n and src[j].isdigit():
                    j += 1
                toks.append(Tok("FLOAT", float(src[i:j]), line))
            else:
                toks.append(Tok("INT", int(src[i:j]), line))
            i = j
            continue
        if c.isalpha() or c == "_":
            j = i
            while j < n and (src[j].isalnum() or src[j] == "_"):
                j += 1
            word = src[i:j]
            if word == "_" :
                toks.append(Tok("_", "_", line))
            elif word in KEYWORDS:
                toks.append(Tok(word, word, line))
            else:
                toks.append(Tok("IDENT", word, line))
            i = j
            continue
        for op in OPS:
            if src.startswith(op, i):
                if op in ("(", "[", "{"):
                    stack.append(op)
                elif op in (")", "]", "}"):
                    if stack:
                        stack.pop()
                toks.append(Tok(op, op, line))
                i += len(op)
                break
        else:
            raise LangError("E901", line, f"unexpected character {c!r}")
    toks.append(Tok("NEWLINE", "\\n", line))
    toks.append(Tok("EOF", None, line))
    return toks


# =========================================================================
# AST
# =========================================================================

class Node:
    def __init__(self, line, **kw):
        self.line = line
        for k, v in kw.items():
            setattr(self, k, v)


def N(kind, line, **kw):
    node = Node(line, **kw)
    node.kind = kind
    return node

# Declarations: Fn(name, params[(conv,name)], body), TypeDecl(name, variants[(name, nfields)])
# Statements:  Let(pat, expr), Var(name, expr), Assign(target, expr), Return(expr|None),
#              For(pat, iter, body), While(cond, body), ExprStmt(expr)
# Expressions: Int, Float, Str, Bool, Unit, Ident, ListLit(items: (is_spread, expr)),
#              Tuple(items), Range(lo, hi, incl), Bin(op, l, r), Un(op, e),
#              Call(fnexpr, args), Method(recv, name, args), Index(seq, idx),
#              Lambda(params, body), If(cond, then, els), Match(scrut, arms),
#              Block(stmts), Try(e)  # postfix '?'
# Patterns:    PWild, PLit(value), PBind(name), PCtor(name, subs), PTuple(subs),
#              PList(subs, rest_name|None)


# =========================================================================
# Parser
# =========================================================================

class Parser:
    def __init__(self, toks):
        self.toks = toks
        self.pos = 0

    def peek(self, k=0):
        return self.toks[min(self.pos + k, len(self.toks) - 1)]

    def at(self, kind):
        return self.peek().kind == kind

    def eat(self, kind=None):
        t = self.peek()
        if kind is not None and t.kind != kind:
            raise LangError("E910", t.line, f"expected {kind!r}, found {t.kind!r}")
        self.pos += 1
        return t

    def skip_nl(self):
        while self.at("NEWLINE") or self.at(";"):
            self.pos += 1

    def end_stmt(self):
        if self.at("NEWLINE") or self.at(";"):
            self.pos += 1
        elif self.at("}") or self.at("EOF"):
            pass
        else:
            t = self.peek()
            raise LangError("E911", t.line, f"expected end of statement, found {t.kind!r}")

    # ---- program ----------------------------------------------------------

    def parse_program(self):
        fns, types = {}, {}
        self.skip_nl()
        while not self.at("EOF"):
            if self.at("fn"):
                f = self.parse_fn()
                if f.name in fns:
                    raise LangError("E912", f.line, f"duplicate function '{f.name}'")
                fns[f.name] = f
            elif self.at("type"):
                td = self.parse_type_decl()
                types[td.name] = td
            else:
                t = self.peek()
                raise LangError("E913", t.line, "expected 'fn' or 'type' at top level")
            self.skip_nl()
        return fns, types

    def parse_fn(self):
        line = self.eat("fn").line
        name = self.eat("IDENT").val
        self.eat("(")
        params = []
        self.skip_nl()
        while not self.at(")"):
            conv = "read"
            if self.peek().kind in ("read", "edit", "take"):
                conv = self.eat().kind
            pname = self.eat("IDENT").val
            if self.at(":"):
                self.eat(":")
                self.parse_type_ref()  # parsed, not yet checked
            params.append((conv, pname))
            self.skip_nl()
            if self.at(","):
                self.eat(",")
                self.skip_nl()
        self.eat(")")
        if self.at("->"):
            self.eat("->")
            self.parse_type_ref()
        body = self.parse_block()
        return N("Fn", line, name=name, params=params, body=body)

    def parse_type_ref(self):
        if self.at("("):  # tuple / unit type
            self.eat("(")
            if not self.at(")"):
                self.parse_type_ref()
                while self.at(","):
                    self.eat(",")
                    self.parse_type_ref()
            self.eat(")")
            return
        self.eat("IDENT")
        if self.at("["):
            self.eat("[")
            self.parse_type_ref()
            while self.at(","):
                self.eat(",")
                self.parse_type_ref()
            self.eat("]")

    def parse_type_decl(self):
        line = self.eat("type").line
        name = self.eat("IDENT").val
        self.eat("=")
        variants = []
        self.skip_nl()
        if self.at("|"):
            self.eat("|")
        variants.append(self.parse_variant())
        while True:
            save = self.pos
            self.skip_nl()
            if self.at("|"):
                self.eat("|")
                variants.append(self.parse_variant())
            else:
                self.pos = save
                break
        return N("TypeDecl", line, name=name, variants=variants)

    def parse_variant(self):
        t = self.eat("IDENT")
        nfields = 0
        if self.at("("):
            self.eat("(")
            while not self.at(")"):
                # field: [name ':'] TypeRef
                if self.peek().kind == "IDENT" and self.peek(1).kind == ":":
                    self.eat("IDENT")
                    self.eat(":")
                self.parse_type_ref()
                nfields += 1
                if self.at(","):
                    self.eat(",")
            self.eat(")")
        return (t.val, nfields, t.line)

    # ---- statements -------------------------------------------------------

    def parse_block(self):
        line = self.eat("{").line
        stmts = []
        self.skip_nl()
        while not self.at("}"):
            stmts.append(self.parse_stmt())
            self.skip_nl()
        self.eat("}")
        return N("Block", line, stmts=stmts)

    def parse_stmt(self):
        t = self.peek()
        if t.kind == "let":
            self.eat("let")
            pat = self.parse_pattern()
            self.eat("=")
            e = self.parse_expr()
            self.end_stmt()
            return N("Let", t.line, pat=pat, expr=e)
        if t.kind == "var":
            self.eat("var")
            name = self.eat("IDENT").val
            self.eat("=")
            e = self.parse_expr()
            self.end_stmt()
            return N("Var", t.line, name=name, expr=e)
        if t.kind == "return":
            self.eat("return")
            e = None
            if not (self.at("NEWLINE") or self.at(";") or self.at("}")):
                e = self.parse_expr()
            self.end_stmt()
            return N("Return", t.line, expr=e)
        if t.kind == "for":
            self.eat("for")
            pat = self.parse_pattern()
            self.eat("in")
            it = self.parse_expr()
            body = self.parse_block()
            self.end_stmt()
            return N("For", t.line, pat=pat, iter=it, body=body)
        if t.kind == "while":
            self.eat("while")
            cond = self.parse_expr()
            body = self.parse_block()
            self.end_stmt()
            return N("While", t.line, cond=cond, body=body)
        e = self.parse_expr()
        if self.at("="):
            self.eat("=")
            if e.kind not in ("Ident", "Index"):
                raise LangError("E914", t.line, "invalid assignment target")
            rhs = self.parse_expr()
            self.end_stmt()
            return N("Assign", t.line, target=e, expr=rhs)
        self.end_stmt()
        return N("ExprStmt", t.line, expr=e)

    # ---- patterns ---------------------------------------------------------

    def parse_pattern(self):
        t = self.peek()
        if t.kind == "_":
            self.eat("_")
            return N("PWild", t.line)
        if t.kind in ("INT", "FLOAT", "STRING"):
            self.eat()
            return N("PLit", t.line, value=t.val)
        if t.kind in ("true", "false"):
            self.eat()
            return N("PLit", t.line, value=(t.kind == "true"))
        if t.kind == "-" and self.peek(1).kind in ("INT", "FLOAT"):
            self.eat("-")
            v = self.eat()
            return N("PLit", t.line, value=-v.val)
        if t.kind == "(":
            self.eat("(")
            subs = [self.parse_pattern()]
            while self.at(","):
                self.eat(",")
                subs.append(self.parse_pattern())
            self.eat(")")
            if len(subs) == 1:
                return subs[0]
            return N("PTuple", t.line, subs=subs)
        if t.kind == "[":
            self.eat("[")
            subs, rest = [], None
            self.skip_nl()
            while not self.at("]"):
                if self.at("..."):
                    self.eat("...")
                    rest = self.eat("IDENT").val
                    break
                subs.append(self.parse_pattern())
                if self.at(","):
                    self.eat(",")
                    self.skip_nl()
            self.eat("]")
            return N("PList", t.line, subs=subs, rest=rest)
        if t.kind == "IDENT":
            self.eat()
            name = t.val
            if name[0].isupper():  # constructor pattern by convention
                subs = []
                if self.at("("):
                    self.eat("(")
                    subs.append(self.parse_pattern())
                    while self.at(","):
                        self.eat(",")
                        subs.append(self.parse_pattern())
                    self.eat(")")
                return N("PCtor", t.line, name=name, subs=subs)
            return N("PBind", t.line, name=name)
        raise LangError("E915", t.line, f"invalid pattern near {t.kind!r}")

    # ---- expressions (Pratt) ----------------------------------------------

    BIN_POWER = {
        "|>": 10, "||": 20, "&&": 30,
        "==": 40, "!=": 40,
        "<": 50, "<=": 50, ">": 50, ">=": 50,
        "..": 60, "..=": 60,
        "++": 70,
        "+": 80, "-": 80,
        "*": 90, "/": 90, "%": 90,
    }

    # Operators allowed to start a continuation line. '-' is excluded because
    # a statement may legitimately begin with unary minus.
    CONT_OPS = set(BIN_POWER) - {"-"}

    def parse_expr(self, min_bp=0):
        left = self.parse_unary()
        while True:
            t = self.peek()
            if t.kind == "NEWLINE":
                k = self.pos
                while self.toks[k].kind == "NEWLINE":
                    k += 1
                nxt = self.toks[k]
                if nxt.kind in self.CONT_OPS and self.BIN_POWER[nxt.kind] >= min_bp:
                    self.pos = k  # line continuation
                    continue
                return left
            bp = self.BIN_POWER.get(t.kind)
            if bp is None or bp < min_bp:
                return left
            self.eat()
            self.skip_nl()
            if t.kind == "|>":
                right = self.parse_expr(bp + 1)
                # x |> f      == f(x)
                # x |> f(a)   == f(x, a)
                if right.kind == "Call":
                    right.args = [left] + right.args
                    left = right
                elif right.kind == "Method":
                    right.args = [left] + right.args
                    left = right
                else:
                    left = N("Call", t.line, fn=right, args=[left])
            elif t.kind in ("..", "..="):
                right = self.parse_expr(bp + 1)
                left = N("Range", t.line, lo=left, hi=right, incl=(t.kind == "..="))
            else:
                right = self.parse_expr(bp + 1)
                left = N("Bin", t.line, op=t.kind, l=left, r=right)

    def parse_unary(self):
        t = self.peek()
        if t.kind in ("-", "!"):
            self.eat()
            e = self.parse_unary()
            return N("Un", t.line, op=t.kind, e=e)
        return self.parse_postfix()

    def parse_postfix(self):
        e = self.parse_primary()
        while True:
            t = self.peek()
            if t.kind == "(":
                self.eat("(")
                args = self.parse_args()
                self.eat(")")
                e = N("Call", t.line, fn=e, args=args)
            elif t.kind == "[":
                self.eat("[")
                idx = self.parse_expr()
                self.eat("]")
                e = N("Index", t.line, seq=e, idx=idx)
            elif t.kind == ".":
                self.eat(".")
                name = self.eat("IDENT").val
                if self.at("("):
                    self.eat("(")
                    args = self.parse_args()
                    self.eat(")")
                    e = N("Method", t.line, recv=e, name=name, args=args)
                else:
                    raise LangError("E916", t.line, "field access is not in v0; use methods")
            elif t.kind == "?":
                self.eat("?")
                e = N("Try", t.line, e=e)
            else:
                return e

    def parse_args(self):
        args = []
        self.skip_nl()
        while not self.at(")"):
            args.append(self.parse_expr())
            self.skip_nl()
            if self.at(","):
                self.eat(",")
                self.skip_nl()
        return args

    def parse_primary(self):
        t = self.peek()
        if t.kind == "INT":
            self.eat()
            return N("Int", t.line, value=t.val)
        if t.kind == "FLOAT":
            self.eat()
            return N("Float", t.line, value=t.val)
        if t.kind == "STRING":
            self.eat()
            return N("Str", t.line, value=t.val)
        if t.kind in ("true", "false"):
            self.eat()
            return N("Bool", t.line, value=(t.kind == "true"))
        if t.kind == "IDENT":
            self.eat()
            return N("Ident", t.line, name=t.val)
        if t.kind == "(":
            self.eat("(")
            if self.at(")"):
                self.eat(")")
                return N("Unit", t.line)
            e = self.parse_expr()
            if self.at(","):
                items = [e]
                while self.at(","):
                    self.eat(",")
                    if self.at(")"):
                        break
                    items.append(self.parse_expr())
                self.eat(")")
                return N("Tuple", t.line, items=items)
            self.eat(")")
            return e
        if t.kind == "[":
            self.eat("[")
            items = []
            self.skip_nl()
            while not self.at("]"):
                if self.at("..."):
                    self.eat("...")
                    items.append((True, self.parse_expr()))
                else:
                    items.append((False, self.parse_expr()))
                self.skip_nl()
                if self.at(","):
                    self.eat(",")
                    self.skip_nl()
            self.eat("]")
            return N("ListLit", t.line, items=items)
        if t.kind == "|":
            self.eat("|")
            params = []
            while not self.at("|"):
                params.append(self.eat("IDENT").val)
                if self.at(","):
                    self.eat(",")
            self.eat("|")
            body = self.parse_block() if self.at("{") else self.parse_expr()
            return N("Lambda", t.line, params=params, body=body)
        if t.kind == "if":
            return self.parse_if()
        if t.kind == "match":
            return self.parse_match()
        if t.kind == "{":
            return self.parse_block()
        raise LangError("E917", t.line, f"unexpected token {t.kind!r} in expression")

    def parse_if(self):
        line = self.eat("if").line
        cond = self.parse_expr()
        then = self.parse_block()
        els = None
        if self.at("else"):
            self.eat("else")
            els = self.parse_if() if self.at("if") else self.parse_block()
        return N("If", line, cond=cond, then=then, els=els)

    def parse_match(self):
        line = self.eat("match").line
        scrut = self.parse_expr()
        self.eat("{")
        arms = []
        self.skip_nl()
        while not self.at("}"):
            pat = self.parse_pattern()
            self.eat("->")
            self.skip_nl()
            body = self.parse_block() if self.at("{") else self.parse_expr()
            arms.append((pat, body))
            if self.at(","):
                self.eat(",")
            self.skip_nl()
        self.eat("}")
        return N("Match", line, scrut=scrut, arms=arms)


# =========================================================================
# Ownership checker (read / edit / take)
# =========================================================================
#
# Binding kinds:
#   let        immutable owned local (movable)
#   var        mutable owned local  (movable, assignable, editable)
#   read       read parameter: shared borrow  (not movable, not mutable)
#   edit       edit parameter: exclusive borrow (mutable, NOT movable)
#   take       take parameter: owned by callee (mutable, movable)
#   loop       loop variable: immutable view   (not movable, not mutable)
#
# State per name: "live" or ("moved", line). Assignment revives a moved var.

EDIT_METHODS = {"push", "pop"}
READ_METHODS = {"len", "map", "filter", "contains", "clone", "get"}
BUILTIN_FNS = {"print": None, "str": None, "int": None, "float": None, "panic": None}
BUILTIN_CTORS = {"Ok": 1, "Err": 1, "Some": 1, "None": 0}

MOVABLE = {"let", "var", "take"}
MUTABLE = {"var", "edit", "take"}


class Scope:
    def __init__(self, parent=None):
        self.parent = parent
        self.names = {}  # name -> dict(kind, state, line)

    def declare(self, name, kind, line):
        self.names[name] = {"kind": kind, "state": "live", "line": line}

    def lookup(self, name):
        s = self
        while s:
            if name in s.names:
                return s.names[name]
            s = s.parent
        return None

    def flat(self):
        out = {}
        s = self
        chain = []
        while s:
            chain.append(s)
            s = s.parent
        for s in reversed(chain):
            for k, v in s.names.items():
                out[k] = v
        return out


class Checker:
    def __init__(self, fns, types):
        self.fns = fns
        self.ctors = dict(BUILTIN_CTORS)
        for td in types.values():
            for (vname, nfields, vline) in td.variants:
                if vname in self.ctors:
                    raise LangError("E918", vline, f"duplicate constructor '{vname}'")
                self.ctors[vname] = nfields
        self.errors = []
        self._seen = set()

    def err(self, code, line, msg):
        key = (code, line, msg)
        if key in self._seen:  # one diagnostic per distinct problem
            return
        self._seen.add(key)
        self.errors.append(LangError(code, line, msg))

    def check_program(self):
        if "main" not in self.fns:
            self.err("E930", 1, "no 'main' function")
        for f in self.fns.values():
            self.check_fn(f)
        self.errors.sort(key=lambda e: (e.line, e.code))
        return self.errors

    def check_fn(self, f):
        scope = Scope()
        for (conv, pname) in f.params:
            scope.declare(pname, conv, f.line)
        self.exec_block(f.body, scope, loop_outer=None)

    # ---- statement / expression walkers -----------------------------------

    def exec_block(self, block, scope, loop_outer):
        inner = Scope(scope)
        for st in block.stmts:
            self.exec_stmt(st, inner, loop_outer)

    def exec_stmt(self, st, scope, loop_outer):
        k = st.kind
        if k == "Let":
            self.eval_rhs(st.expr, scope, loop_outer)
            self.bind_pattern(st.pat, scope, "let")
        elif k == "Var":
            self.eval_rhs(st.expr, scope, loop_outer)
            scope.declare(st.name, "var", st.line)
        elif k == "Assign":
            self.eval_rhs(st.expr, scope, loop_outer)
            self.check_assign_target(st.target, scope, loop_outer)
        elif k == "Return":
            if st.expr is not None:
                self.eval_rhs(st.expr, scope, loop_outer)
        elif k == "ExprStmt":
            self.eval_expr(st.expr, scope, loop_outer)
        elif k == "For":
            self.eval_expr(st.iter, scope, loop_outer)
            inner = Scope(scope)
            self.bind_pattern(st.pat, inner, "loop")
            outer = set(scope.flat().keys())
            self.exec_block(st.body, inner, loop_outer=(loop_outer or set()) | outer)
        elif k == "While":
            self.eval_expr(st.cond, scope, loop_outer)
            outer = set(scope.flat().keys())
            self.exec_block(st.body, scope, loop_outer=(loop_outer or set()) | outer)
        else:
            raise LangError("E999", st.line, f"checker: unknown stmt {k}")

    def bind_pattern(self, pat, scope, kind):
        k = pat.kind
        if k == "PBind":
            scope.declare(pat.name, kind, pat.line)
        elif k == "PTuple":
            for s in pat.subs:
                self.bind_pattern(s, scope, kind)
        elif k == "PList":
            for s in pat.subs:
                self.bind_pattern(s, scope, kind)
            if pat.rest:
                scope.declare(pat.rest, kind, pat.line)
        elif k == "PCtor":
            if pat.name not in self.ctors:
                self.err("E020", pat.line, f"unknown constructor '{pat.name}'")
            for s in pat.subs:
                self.bind_pattern(s, scope, kind)
        # PWild / PLit: nothing

    def check_assign_target(self, target, scope, loop_outer):
        if target.kind == "Ident":
            info = scope.lookup(target.name)
            if info is None:
                self.err("E020", target.line, f"unknown name '{target.name}'")
                return
            if info["kind"] == "let":
                self.err("E010", target.line,
                         f"cannot assign to immutable 'let' binding '{target.name}'; declare it with 'var'")
            elif info["kind"] == "read":
                self.err("E004", target.line,
                         f"cannot mutate 'read' parameter '{target.name}'")
            elif info["kind"] == "loop":
                self.err("E004", target.line,
                         f"cannot mutate loop variable '{target.name}'")
            info["state"] = "live"  # assignment revives a moved var
        elif target.kind == "Index":
            self.eval_expr(target.idx, scope, loop_outer)
            base = target.seq
            if base.kind != "Ident":
                self.err("E914", target.line, "index assignment target must be a variable")
                return
            info = scope.lookup(base.name)
            if info is None:
                self.err("E020", base.line, f"unknown name '{base.name}'")
                return
            self.use_read(base, scope)
            if info["kind"] not in MUTABLE:
                code = "E004" if info["kind"] in ("read", "loop") else "E010"
                self.err(code, target.line,
                         f"cannot mutate '{base.name}' ({info['kind']} binding)")

    # RHS of let/var/assign: a bare identifier moves.
    def eval_rhs(self, e, scope, loop_outer):
        if e.kind == "Ident":
            self.use_move(e, scope, loop_outer, why="bound to a new owner")
        else:
            self.eval_expr(e, scope, loop_outer)

    def use_read(self, ident, scope):
        info = scope.lookup(ident.name)
        if info is None:
            if ident.name in self.fns or ident.name in BUILTIN_FNS or ident.name in self.ctors:
                return
            self.err("E020", ident.line, f"unknown name '{ident.name}'")
            return
        if info["state"] != "live":
            _, mline = info["state"]
            self.err("E001", ident.line,
                     f"use of moved value '{ident.name}' (moved at line {mline})")

    def use_move(self, ident, scope, loop_outer, why):
        info = scope.lookup(ident.name)
        if info is None:
            if ident.name in self.fns or ident.name in BUILTIN_FNS or ident.name in self.ctors:
                return
            self.err("E020", ident.line, f"unknown name '{ident.name}'")
            return
        if info["state"] != "live":
            _, mline = info["state"]
            self.err("E001", ident.line,
                     f"use of moved value '{ident.name}' (moved at line {mline})")
            return
        kind = info["kind"]
        if kind in ("read", "edit"):
            self.err("E006", ident.line,
                     f"cannot move '{ident.name}': it is a borrowed '{kind}' parameter")
            return
        if kind == "loop":
            # OPEN QUESTION (see DESIGN.md, error code E007 reserved).
            # Moving an element out of a borrowed collection is only sound for
            # Copy types. Without a type checker the oracle cannot tell, so it
            # permits the move and does not mark the binding as consumed.
            return
        if loop_outer and ident.name in loop_outer:
            self.err("E005", ident.line,
                     f"cannot move '{ident.name}' inside a loop: it was declared outside the loop")
            return
        info["state"] = ("moved", ident.line)

    def require_editable(self, ident, scope, ctx):
        info = scope.lookup(ident.name)
        if info is None:
            self.err("E020", ident.line, f"unknown name '{ident.name}'")
            return
        if info["state"] != "live":
            _, mline = info["state"]
            self.err("E001", ident.line,
                     f"use of moved value '{ident.name}' (moved at line {mline})")
            return
        kind = info["kind"]
        if kind == "read":
            self.err("E004", ident.line,
                     f"cannot mutate 'read' parameter '{ident.name}' ({ctx})")
        elif kind == "loop":
            self.err("E004", ident.line,
                     f"cannot mutate loop variable '{ident.name}' ({ctx})")
        elif kind == "let":
            self.err("E002", ident.line,
                     f"'{ident.name}' is an immutable 'let' binding; {ctx} requires 'var'")

    def collect_idents(self, e, acc):
        k = e.kind
        if k == "Ident":
            acc.append(e.name)
        elif k == "Bin":
            self.collect_idents(e.l, acc)
            self.collect_idents(e.r, acc)
        elif k == "Un":
            self.collect_idents(e.e, acc)
        elif k == "Call":
            self.collect_idents(e.fn, acc)
            for a in e.args:
                self.collect_idents(a, acc)
        elif k == "Method":
            self.collect_idents(e.recv, acc)
            for a in e.args:
                self.collect_idents(a, acc)
        elif k == "Index":
            self.collect_idents(e.seq, acc)
            self.collect_idents(e.idx, acc)
        elif k == "ListLit":
            for (_, it) in e.items:
                self.collect_idents(it, acc)
        elif k == "Tuple":
            for it in e.items:
                self.collect_idents(it, acc)
        elif k == "Range":
            self.collect_idents(e.lo, acc)
            self.collect_idents(e.hi, acc)
        elif k == "Try":
            self.collect_idents(e.e, acc)
        # Lambda/If/Match/Block: skipped for aliasing purposes (checked separately)

    def eval_expr(self, e, scope, loop_outer):
        k = e.kind
        if k in ("Int", "Float", "Str", "Bool", "Unit"):
            return
        if k == "Ident":
            self.use_read(e, scope)
            return
        if k == "Bin":
            self.eval_expr(e.l, scope, loop_outer)
            self.eval_expr(e.r, scope, loop_outer)
            return
        if k == "Un":
            self.eval_expr(e.e, scope, loop_outer)
            return
        if k == "Range":
            self.eval_expr(e.lo, scope, loop_outer)
            self.eval_expr(e.hi, scope, loop_outer)
            return
        if k == "ListLit":
            for (_, it) in e.items:
                self.eval_expr(it, scope, loop_outer)
            return
        if k == "Tuple":
            for it in e.items:
                self.eval_expr(it, scope, loop_outer)
            return
        if k == "Index":
            self.eval_expr(e.seq, scope, loop_outer)
            self.eval_expr(e.idx, scope, loop_outer)
            return
        if k == "Try":
            self.eval_expr(e.e, scope, loop_outer)
            return
        if k == "Block":
            self.exec_block(e, scope, loop_outer)
            return
        if k == "If":
            self.eval_expr(e.cond, scope, loop_outer)
            before = self.snapshot(scope)
            self.exec_block(e.then, scope, loop_outer)
            after_then = self.snapshot(scope)
            self.restore(scope, before)
            if e.els is not None:
                if e.els.kind == "Block":
                    self.exec_block(e.els, scope, loop_outer)
                else:
                    self.eval_expr(e.els, scope, loop_outer)
            after_else = self.snapshot(scope)
            self.restore(scope, self.join(after_then, after_else))
            return
        if k == "Match":
            self.eval_expr(e.scrut, scope, loop_outer)
            before = self.snapshot(scope)
            joined = None
            for (pat, body) in e.arms:
                self.restore(scope, before)
                inner = Scope(scope)
                self.bind_pattern(pat, inner, "let")
                if body.kind == "Block":
                    self.exec_block(body, inner, loop_outer)
                else:
                    self.eval_expr(body, inner, loop_outer)
                snap = self.snapshot(scope)
                joined = snap if joined is None else self.join(joined, snap)
            if joined is not None:
                self.restore(scope, joined)
            return
        if k == "Lambda":
            inner = Scope(scope)
            for p in e.params:
                inner.declare(p, "read", e.line)
            captured = self.free_names(e.body, set(e.params))
            for (name, line) in captured:
                info = scope.lookup(name)
                if info is None:
                    continue
                if info["kind"] in ("var", "edit", "take"):
                    self.err("E011", line,
                             f"closure cannot capture mutable binding '{name}' in v0 "
                             f"(capture immutable bindings only)")
                if info["state"] != "live":
                    _, mline = info["state"]
                    self.err("E001", line,
                             f"use of moved value '{name}' (moved at line {mline})")
            if e.body.kind == "Block":
                self.exec_block(e.body, inner, loop_outer=None)
            else:
                self.eval_expr(e.body, inner, loop_outer=None)
            return
        if k == "Method":
            recv = e.recv
            if e.name in EDIT_METHODS:
                if recv.kind == "Ident":
                    self.require_editable(recv, scope, f"method '.{e.name}()'")
                else:
                    self.eval_expr(recv, scope, loop_outer)
                # exclusivity: receiver must not reappear in args
                if recv.kind == "Ident":
                    others = []
                    for a in e.args:
                        self.collect_idents(a, others)
                    if recv.name in others:
                        self.err("E003", e.line,
                                 f"'{recv.name}' is exclusively borrowed by '.{e.name}()' "
                                 f"and cannot also be used in its arguments")
            else:
                if e.name not in READ_METHODS:
                    self.err("E022", e.line, f"unknown method '.{e.name}()'")
                self.eval_expr(recv, scope, loop_outer)
            for a in e.args:
                self.eval_expr(a, scope, loop_outer)
            return
        if k == "Call":
            fn = e.fn
            convs = None
            if fn.kind == "Ident":
                if fn.name in self.fns:
                    f = self.fns[fn.name]
                    convs = [c for (c, _) in f.params]
                    if len(e.args) != len(convs):
                        self.err("E021", e.line,
                                 f"'{fn.name}' expects {len(convs)} argument(s), got {len(e.args)}")
                        convs = None
                elif fn.name in self.ctors:
                    want = self.ctors[fn.name]
                    if len(e.args) != want:
                        self.err("E021", e.line,
                                 f"constructor '{fn.name}' expects {want} argument(s), got {len(e.args)}")
                    convs = ["take"] * len(e.args)  # constructors take ownership
                elif fn.name in BUILTIN_FNS:
                    convs = ["read"] * len(e.args)
                else:
                    self.use_read(fn, scope)  # a local holding a closure
            else:
                self.eval_expr(fn, scope, loop_outer)
            edit_names = []
            if convs is None:
                convs = ["read"] * len(e.args)
            for conv, arg in zip(convs, e.args):
                if conv == "take":
                    if arg.kind == "Ident":
                        self.use_move(arg, scope, loop_outer, why="passed to a 'take' parameter")
                    else:
                        self.eval_expr(arg, scope, loop_outer)
                elif conv == "edit":
                    if arg.kind == "Ident":
                        self.require_editable(arg, scope, "'edit' parameter")
                        edit_names.append(arg.name)
                    else:
                        self.err("E002", arg.line,
                                 "argument to an 'edit' parameter must be a mutable variable")
                        self.eval_expr(arg, scope, loop_outer)
                else:
                    self.eval_expr(arg, scope, loop_outer)
            # exclusivity across the whole call
            if edit_names:
                all_idents = []
                for conv, arg in zip(convs, e.args):
                    if conv == "edit" and arg.kind == "Ident":
                        all_idents.append(arg.name)
                    else:
                        self.collect_idents(arg, all_idents)
                for name in edit_names:
                    if all_idents.count(name) > 1:
                        self.err("E003", e.line,
                                 f"'{name}' is passed as 'edit' and also used elsewhere "
                                 f"in the same call (exclusive borrow conflict)")
            return
        raise LangError("E999", e.line, f"checker: unknown expr {k}")

    # ---- branch state helpers ---------------------------------------------

    def snapshot(self, scope):
        return {name: info["state"] for name, info in scope.flat().items()}

    def restore(self, scope, snap):
        for name, state in snap.items():
            info = scope.lookup(name)
            if info is not None:
                info["state"] = state

    def join(self, a, b):
        out = {}
        for name in set(a) | set(b):
            sa, sb = a.get(name, "live"), b.get(name, "live")
            if sa == "live" and sb == "live":
                out[name] = "live"
            else:
                out[name] = sa if sa != "live" else sb
        return out

    def free_names(self, node, bound):
        """Collect (name, line) referenced in a lambda body but not locally bound."""
        out = []

        def walk(n, bound):
            k = n.kind
            if k == "Ident":
                if n.name not in bound and n.name not in self.fns \
                        and n.name not in BUILTIN_FNS and n.name not in self.ctors:
                    out.append((n.name, n.line))
            elif k == "Block":
                b = set(bound)
                for st in n.stmts:
                    walk_stmt(st, b)
            elif k == "Lambda":
                walk(n.body, bound | set(n.params))
            elif k == "If":
                walk(n.cond, bound)
                walk(n.then, bound)
                if n.els is not None:
                    walk(n.els, bound)
            elif k == "Match":
                walk(n.scrut, bound)
                for (pat, body) in n.arms:
                    walk(body, bound | pat_names(pat))
            elif k == "Bin":
                walk(n.l, bound)
                walk(n.r, bound)
            elif k == "Un":
                walk(n.e, bound)
            elif k == "Range":
                walk(n.lo, bound)
                walk(n.hi, bound)
            elif k == "Try":
                walk(n.e, bound)
            elif k == "Call":
                walk(n.fn, bound)
                for a in n.args:
                    walk(a, bound)
            elif k == "Method":
                walk(n.recv, bound)
                for a in n.args:
                    walk(a, bound)
            elif k == "Index":
                walk(n.seq, bound)
                walk(n.idx, bound)
            elif k == "ListLit":
                for (_, it) in n.items:
                    walk(it, bound)
            elif k == "Tuple":
                for it in n.items:
                    walk(it, bound)

        def pat_names(pat):
            k = pat.kind
            if k == "PBind":
                return {pat.name}
            if k == "PTuple":
                s = set()
                for sub in pat.subs:
                    s |= pat_names(sub)
                return s
            if k == "PList":
                s = set()
                for sub in pat.subs:
                    s |= pat_names(sub)
                if pat.rest:
                    s.add(pat.rest)
                return s
            if k == "PCtor":
                s = set()
                for sub in pat.subs:
                    s |= pat_names(sub)
                return s
            return set()

        def walk_stmt(st, bound):
            k = st.kind
            if k == "Let":
                walk(st.expr, bound)
                bound |= pat_names(st.pat)
            elif k == "Var":
                walk(st.expr, bound)
                bound.add(st.name)
            elif k == "Assign":
                walk(st.expr, bound)
                walk(st.target, bound)
            elif k == "Return":
                if st.expr is not None:
                    walk(st.expr, bound)
            elif k == "ExprStmt":
                walk(st.expr, bound)
            elif k == "For":
                walk(st.iter, bound)
                b2 = bound | pat_names(st.pat)
                for s2 in st.body.stmts:
                    walk_stmt(s2, b2)
            elif k == "While":
                walk(st.cond, bound)
                for s2 in st.body.stmts:
                    walk_stmt(s2, bound)

        walk(node, set(bound))
        return out


# =========================================================================
# Interpreter
# =========================================================================

class Unit:
    _inst = None

    def __new__(cls):
        if cls._inst is None:
            cls._inst = super().__new__(cls)
        return cls._inst

    def __repr__(self):
        return "()"


UNIT = Unit()


class Variant:
    __slots__ = ("name", "fields")

    def __init__(self, name, fields):
        self.name = name
        self.fields = list(fields)

    def __eq__(self, other):
        return isinstance(other, Variant) and self.name == other.name \
            and self.fields == other.fields

    def __repr__(self):
        return self.name if not self.fields else \
            f"{self.name}({', '.join(map(fmt_inner, self.fields))})"


class Closure:
    __slots__ = ("params", "convs", "body", "env", "name")

    def __init__(self, params, convs, body, env, name="<lambda>"):
        self.params = params
        self.convs = convs
        self.body = body
        self.env = env
        self.name = name


class Cell:
    __slots__ = ("value",)

    def __init__(self, value):
        self.value = value


class Env:
    def __init__(self, parent=None):
        self.parent = parent
        self.slots = {}

    def declare(self, name, value):
        self.slots[name] = Cell(value)

    def declare_cell(self, name, cell):
        self.slots[name] = cell

    def cell(self, name):
        e = self
        while e:
            if name in e.slots:
                return e.slots[name]
            e = e.parent
        return None


class RtError(Exception):
    def __init__(self, line, msg):
        self.line = line
        self.msg = msg


class ReturnSig(Exception):
    def __init__(self, value):
        self.value = value


class EarlySig(Exception):  # raised by '?' on Err(..) / None
    def __init__(self, value):
        self.value = value


def fmt_inner(v):
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, str):
        return '"' + v.replace("\\", "\\\\").replace('"', '\\"') + '"'
    if isinstance(v, float):
        return repr(v)
    if isinstance(v, list):
        return "[" + ", ".join(fmt_inner(x) for x in v) + "]"
    if isinstance(v, tuple):
        return "(" + ", ".join(fmt_inner(x) for x in v) + ")"
    if isinstance(v, Closure):
        return f"<fn {v.name}>"
    return repr(v)


def fmt_top(v):
    if isinstance(v, str):
        return v
    return fmt_inner(v)


class Interp:
    def __init__(self, fns, ctors):
        self.fns = fns
        self.ctors = ctors
        self.globals = Env()
        for name, f in fns.items():
            convs = [c for (c, _) in f.params]
            params = [p for (_, p) in f.params]
            self.globals.declare(name, Closure(params, convs, f.body, self.globals, name))

    def run(self):
        main = self.globals.cell("main").value
        self.call_closure(main, [], [], 0)

    # ---- calls ------------------------------------------------------------

    def call_closure(self, clo, arg_values, arg_cells, line):
        env = Env(clo.env)
        for i, p in enumerate(clo.params):
            conv = clo.convs[i] if i < len(clo.convs) else "read"
            if conv == "edit" and arg_cells[i] is not None:
                env.declare_cell(p, arg_cells[i])  # share the caller's cell
            else:
                env.declare(p, arg_values[i])
        try:
            if clo.body.kind == "Block":
                v = self.eval_block(clo.body, env)
            else:  # lambda with an expression body
                v = self.eval(clo.body, env)
        except ReturnSig as r:
            return r.value
        except EarlySig as r:  # '?' bubbled to function boundary
            return r.value
        return v

    # ---- eval -------------------------------------------------------------

    def eval_block(self, block, env):
        inner = Env(env)
        last = UNIT
        for st in block.stmts:
            last = self.exec_stmt(st, inner)
        return last

    def exec_stmt(self, st, env):
        k = st.kind
        if k == "Let":
            v = self.eval(st.expr, env)
            if not self.match_bind(st.pat, v, env):
                raise RtError(st.line, "let pattern did not match the value")
            return UNIT
        if k == "Var":
            env.declare(st.name, self.eval(st.expr, env))
            return UNIT
        if k == "Assign":
            v = self.eval(st.expr, env)
            t = st.target
            if t.kind == "Ident":
                cell = env.cell(t.name)
                cell.value = v
            else:  # Index
                seq = self.eval(t.seq, env)
                idx = self.eval(t.idx, env)
                self.index_check(seq, idx, t.line)
                seq[idx] = v
            return UNIT
        if k == "Return":
            raise ReturnSig(self.eval(st.expr, env) if st.expr is not None else UNIT)
        if k == "ExprStmt":
            return self.eval(st.expr, env)
        if k == "For":
            it = self.eval(st.iter, env)
            if not isinstance(it, list):
                raise RtError(st.line, "for-in requires a List")
            for elem in it:
                inner = Env(env)
                if not self.match_bind(st.pat, elem, inner):
                    raise RtError(st.line, "for pattern did not match an element")
                self.eval_block(st.body, inner)
            return UNIT
        if k == "While":
            while True:
                c = self.eval(st.cond, env)
                if c is not True:
                    if c is not False:
                        raise RtError(st.line, "while condition must be a Bool")
                    break
                self.eval_block(st.body, env)
            return UNIT
        raise RtError(st.line, f"interp: unknown stmt {k}")

    def index_check(self, seq, idx, line):
        if not isinstance(seq, list):
            raise RtError(line, "indexing requires a List")
        if not isinstance(idx, int) or isinstance(idx, bool):
            raise RtError(line, "index must be an Int")
        if idx < 0 or idx >= len(seq):
            raise RtError(line, f"index {idx} out of range (len {len(seq)})")

    def eval(self, e, env):
        k = e.kind
        if k == "Int" or k == "Float" or k == "Str" or k == "Bool":
            return e.value
        if k == "Unit":
            return UNIT
        if k == "Ident":
            cell = env.cell(e.name)
            if cell is None:
                if e.name in self.ctors and self.ctors[e.name] == 0:
                    return Variant(e.name, [])
                raise RtError(e.line, f"unknown name '{e.name}'")
            return cell.value
        if k == "Bin":
            return self.eval_bin(e, env)
        if k == "Un":
            v = self.eval(e.e, env)
            if e.op == "-":
                if isinstance(v, bool) or not isinstance(v, (int, float)):
                    raise RtError(e.line, "unary '-' requires a number")
                return -v
            if v is True or v is False:
                return not v
            raise RtError(e.line, "'!' requires a Bool")
        if k == "Range":
            lo = self.eval(e.lo, env)
            hi = self.eval(e.hi, env)
            if not isinstance(lo, int) or not isinstance(hi, int) \
                    or isinstance(lo, bool) or isinstance(hi, bool):
                raise RtError(e.line, "range bounds must be Int")
            return list(range(lo, hi + 1)) if e.incl else list(range(lo, hi))
        if k == "ListLit":
            out = []
            for (spread, item) in e.items:
                v = self.eval(item, env)
                if spread:
                    if not isinstance(v, list):
                        raise RtError(e.line, "'...' spread requires a List")
                    out.extend(v)
                else:
                    out.append(v)
            return out
        if k == "Tuple":
            return tuple(self.eval(it, env) for it in e.items)
        if k == "Index":
            seq = self.eval(e.seq, env)
            idx = self.eval(e.idx, env)
            self.index_check(seq, idx, e.line)
            return seq[idx]
        if k == "Try":
            v = self.eval(e.e, env)
            if isinstance(v, Variant):
                if v.name == "Ok" or v.name == "Some":
                    return v.fields[0]
                if v.name == "Err":
                    raise EarlySig(v)
                if v.name == "None":
                    raise EarlySig(v)
            raise RtError(e.line, "'?' requires a Result or Option value")
        if k == "Block":
            return self.eval_block(e, env)
        if k == "If":
            c = self.eval(e.cond, env)
            if c is not True and c is not False:
                raise RtError(e.line, "if condition must be a Bool")
            if c:
                return self.eval_block(e.then, env)
            if e.els is None:
                return UNIT
            if e.els.kind == "Block":
                return self.eval_block(e.els, env)
            return self.eval(e.els, env)
        if k == "Match":
            v = self.eval(e.scrut, env)
            for (pat, body) in e.arms:
                inner = Env(env)
                if self.match_bind(pat, v, inner):
                    if body.kind == "Block":
                        return self.eval_block(body, inner)
                    return self.eval(body, inner)
            raise RtError(e.line, f"no match arm matched value {fmt_inner(v)}")
        if k == "Lambda":
            return Closure(e.params, ["read"] * len(e.params), e.body, env)
        if k == "Method":
            return self.eval_method(e, env)
        if k == "Call":
            return self.eval_call(e, env)
        raise RtError(e.line, f"interp: unknown expr {k}")

    def eval_bin(self, e, env):
        op = e.op
        if op == "&&":
            l = self.eval(e.l, env)
            if l is not True and l is not False:
                raise RtError(e.line, "'&&' requires Bool operands")
            return self.eval_bool(e.r, env, "'&&'") if l else False
        if op == "||":
            l = self.eval(e.l, env)
            if l is not True and l is not False:
                raise RtError(e.line, "'||' requires Bool operands")
            return True if l else self.eval_bool(e.r, env, "'||'")
        l = self.eval(e.l, env)
        r = self.eval(e.r, env)
        if op == "==":
            return self.equal(l, r)
        if op == "!=":
            return not self.equal(l, r)
        if op == "++":
            if isinstance(l, str) and isinstance(r, str):
                return l + r
            if isinstance(l, list) and isinstance(r, list):
                return l + r
            raise RtError(e.line, "'++' concatenates two Strings or two Lists")
        if op in ("<", "<=", ">", ">="):
            if not self.both_numbers(l, r) and not (isinstance(l, str) and isinstance(r, str)):
                raise RtError(e.line, f"'{op}' requires two numbers or two Strings")
            return {"<": l < r, "<=": l <= r, ">": l > r, ">=": l >= r}[op]
        if op in ("+", "-", "*", "/", "%"):
            if not self.both_numbers(l, r):
                raise RtError(e.line, f"'{op}' requires numbers ('++' concatenates)")
            if op == "+":
                return l + r
            if op == "-":
                return l - r
            if op == "*":
                return l * r
            if op == "/":
                if r == 0:
                    raise RtError(e.line, "division by zero")
                if isinstance(l, int) and isinstance(r, int):
                    q = abs(l) // abs(r)
                    return q if (l >= 0) == (r >= 0) else -q  # truncate toward zero
                return l / r
            if op == "%":
                if not (isinstance(l, int) and isinstance(r, int)):
                    raise RtError(e.line, "'%' requires Int operands")
                if r == 0:
                    raise RtError(e.line, "division by zero")
                return l - r * (abs(l) // abs(r) if (l >= 0) == (r >= 0) else -(abs(l) // abs(r)))
        raise RtError(e.line, f"unknown operator {op!r}")

    def eval_bool(self, e, env, ctx):
        v = self.eval(e, env)
        if v is not True and v is not False:
            raise RtError(e.line, f"{ctx} requires Bool operands")
        return v

    def both_numbers(self, l, r):
        ok = lambda v: isinstance(v, (int, float)) and not isinstance(v, bool)
        return ok(l) and ok(r)

    def equal(self, l, r):
        if isinstance(l, bool) != isinstance(r, bool):
            return False
        return l == r

    def eval_method(self, e, env):
        recv = self.eval(e.recv, env)
        args = [self.eval(a, env) for a in e.args]
        name = e.name
        if isinstance(recv, list):
            if name == "len":
                return len(recv)
            if name == "push":
                recv.append(args[0])
                return UNIT
            if name == "pop":
                if recv:
                    return Variant("Some", [recv.pop()])
                return Variant("None", [])
            if name == "get":
                i = args[0]
                if isinstance(i, int) and not isinstance(i, bool) and 0 <= i < len(recv):
                    return Variant("Some", [recv[i]])
                return Variant("None", [])
            if name == "map":
                f = args[0]
                return [self.call_closure(f, [x], [None], e.line) for x in recv]
            if name == "filter":
                f = args[0]
                out = []
                for x in recv:
                    keep = self.call_closure(f, [x], [None], e.line)
                    if keep is True:
                        out.append(x)
                    elif keep is not False:
                        raise RtError(e.line, ".filter() predicate must return a Bool")
                return out
            if name == "contains":
                return any(self.equal(x, args[0]) for x in recv)
            if name == "clone":
                return _copy.deepcopy(recv)
        if isinstance(recv, str):
            if name == "len":
                return len(recv)
            if name == "clone":
                return recv
            if name == "contains":
                return isinstance(args[0], str) and args[0] in recv
        if isinstance(recv, Variant) and name == "clone":
            return _copy.deepcopy(recv)
        raise RtError(e.line, f"no method '.{name}()' for value {fmt_inner(recv)}")

    def eval_call(self, e, env):
        fn = e.fn
        if fn.kind == "Ident":
            name = fn.name
            if env.cell(name) is None:
                if name in self.ctors:
                    vals = [self.eval(a, env) for a in e.args]
                    return Variant(name, vals)
                if name == "print":
                    vals = [self.eval(a, env) for a in e.args]
                    sys.stdout.write(" ".join(fmt_top(v) for v in vals) + "\n")
                    return UNIT
                if name == "str":
                    return fmt_top(self.eval(e.args[0], env))
                if name == "int":
                    v = self.eval(e.args[0], env)
                    if isinstance(v, bool):
                        raise RtError(e.line, "int() requires a number or numeric String")
                    if isinstance(v, (int, float)):
                        return int(v)
                    if isinstance(v, str):
                        try:
                            return int(v.strip())
                        except ValueError:
                            raise RtError(e.line, f"int() could not parse {v!r}")
                    raise RtError(e.line, "int() requires a number or numeric String")
                if name == "float":
                    v = self.eval(e.args[0], env)
                    if isinstance(v, (int, float)) and not isinstance(v, bool):
                        return float(v)
                    raise RtError(e.line, "float() requires a number")
                if name == "panic":
                    v = self.eval(e.args[0], env) if e.args else "panic"
                    raise RtError(e.line, f"panic: {fmt_top(v)}")
                raise RtError(e.line, f"unknown function '{name}'")
            clo = env.cell(name).value
        else:
            clo = self.eval(fn, env)
        if not isinstance(clo, Closure):
            raise RtError(e.line, f"value {fmt_inner(clo)} is not callable")
        values, cells = [], []
        for i, a in enumerate(e.args):
            conv = clo.convs[i] if i < len(clo.convs) else "read"
            if conv == "edit" and a.kind == "Ident":
                cell = env.cell(a.name)
                cells.append(cell)
                values.append(cell.value)
            else:
                cells.append(None)
                values.append(self.eval(a, env))
        if len(values) != len(clo.params):
            raise RtError(e.line,
                          f"'{clo.name}' expects {len(clo.params)} argument(s), got {len(values)}")
        return self.call_closure(clo, values, cells, e.line)

    # ---- pattern matching -------------------------------------------------

    def match_bind(self, pat, v, env):
        k = pat.kind
        if k == "PWild":
            return True
        if k == "PLit":
            return self.equal(v, pat.value)
        if k == "PBind":
            env.declare(pat.name, v)
            return True
        if k == "PTuple":
            if not isinstance(v, tuple) or len(v) != len(pat.subs):
                return False
            return all(self.match_bind(s, x, env) for s, x in zip(pat.subs, v))
        if k == "PList":
            if not isinstance(v, list):
                return False
            if pat.rest is None:
                if len(v) != len(pat.subs):
                    return False
            else:
                if len(v) < len(pat.subs):
                    return False
            for s, x in zip(pat.subs, v):
                if not self.match_bind(s, x, env):
                    return False
            if pat.rest is not None:
                env.declare(pat.rest, list(v[len(pat.subs):]))
            return True
        if k == "PCtor":
            if not isinstance(v, Variant) or v.name != pat.name:
                return False
            if len(pat.subs) != len(v.fields):
                return False
            return all(self.match_bind(s, x, env) for s, x in zip(pat.subs, v.fields))
        return False


# =========================================================================
# Driver
# =========================================================================

def compile_file(path):
    with open(path, "r", encoding="utf-8") as fh:
        src = fh.read()
    toks = lex(src)
    fns, types = Parser(toks).parse_program()
    checker = Checker(fns, types)
    errors = checker.check_program()
    return fns, checker.ctors, errors


def main(argv):
    if len(argv) != 3 or argv[1] not in ("run", "check"):
        print("usage: langc.py [run|check] file.lang")
        return 2
    mode, path = argv[1], argv[2]
    try:
        fns, ctors, errors = compile_file(path)
    except LangError as le:
        print(le.render())
        return 1
    if errors:
        for err in errors:
            print(err.render())
        return 1
    if mode == "check":
        print("ok")
        return 0
    try:
        Interp(fns, ctors).run()
    except RtError as re:
        print(f"runtime error line {re.line}: {re.msg}")
        return 1
    except RecursionError:
        print("runtime error: stack overflow (recursion too deep)")
        return 1
    except Exception as ex:  # a crash is a compiler bug, not user output
        print(f"internal error: {type(ex).__name__}: {ex}")
        return 70
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
