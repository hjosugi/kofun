"""Direct x86-64 native backend.

This lowers Kofun straight to machine code. Nothing in this path shells out:
no C, no clang, no `as`, no `ld`, no libc. The output is a static ELF64
executable that talks to the kernel through syscalls.

Scope is deliberately narrow and every gap fails loudly. The backend handles
the scalar core -- Int, Bool, Text literals, arithmetic, comparisons,
short-circuit logic, if/while/for-range, direct calls and recursion. Features
the reference interpreter supports but this backend does not raise
`BackendFailure` rather than silently changing meaning.

Semantics follow the reference interpreter, which is the oracle. In particular
`//` and `%` are *floored*, matching Python, not truncated as C does. The
correction sequence after `idiv` exists for exactly that reason.
"""

from __future__ import annotations

from dataclasses import dataclass, field

from . import ast, elf
from .c_backend import BackendFailure
from .typesys import BOOL, INT, TEXT, VOID, Type
from .x64 import (
    ARG_REGS,
    CC_AE,
    CC_B,
    CC_BE,
    CC_E,
    CC_GE,
    CC_NE,
    CC_NS,
    RAX,
    RCX,
    RDI,
    RDX,
    RBP,
    RSI,
    RSP,
    R8,
    R9,
    R10,
    Assembler,
)

# Linux x86-64 syscall numbers.
SYS_WRITE = 1
SYS_MMAP = 9
SYS_EXIT = 60
STDOUT = 1
STDERR = 2

# mmap flags, from the kernel's uapi headers.
PROT_READ, PROT_WRITE = 0x1, 0x2
MAP_PRIVATE, MAP_ANONYMOUS = 0x02, 0x20

PAGE_SIZE = 0x1000
#: Chunk size requested from the kernel when the heap runs dry. Large enough
#: that mmap is rare, small enough that a trivial program stays cheap.
CHUNK_BYTES = 1 << 20

COMPARISONS = {"==": CC_E, "!=": CC_NE, "<": 12, "<=": 14, ">": 15, ">=": CC_GE}


@dataclass(slots=True)
class _Loop:
    continue_label: str
    break_label: str


@dataclass(slots=True)
class _Function:
    name: str
    params: list[Type]
    result: Type


class NativeBackend:
    """Lowers a type-checked program to x86-64 machine code."""

    def __init__(self) -> None:
        self.asm = Assembler()
        self.functions: dict[str, _Function] = {}
        self.scopes: list[dict[str, int]] = []
        self.slot_count = 0
        self.max_slots = 0
        self.depth = 0          # outstanding 8-byte pushes, for call alignment
        self.loops: list[_Loop] = []
        self.strings: dict[str, str] = {}   # text value -> label
        self.counter = 0

    # ---- helpers -----------------------------------------------------------

    def _label(self, prefix: str) -> str:
        self.counter += 1
        return f".{prefix}{self.counter}"

    def _push(self, reg: int) -> None:
        self.asm.push(reg)
        self.depth += 1

    def _pop(self, reg: int) -> None:
        self.asm.pop(reg)
        self.depth -= 1

    def _alloc_slot(self) -> int:
        self.slot_count += 1
        self.max_slots = max(self.max_slots, self.slot_count)
        return -8 * self.slot_count

    def _declare(self, name: str) -> int:
        slot = self._alloc_slot()
        self.scopes[-1][name] = slot
        return slot

    def _lookup(self, name: str, span: ast.Span) -> int:
        for scope in reversed(self.scopes):
            if name in scope:
                return scope[name]
        raise BackendFailure(f"native backend: unknown binding `{name}`", span)

    def _string_label(self, value: str) -> str:
        if value not in self.strings:
            self.strings[value] = f".str{len(self.strings)}"
        return self.strings[value]

    def _expr_type(self, node: ast.Expr) -> Type:
        value = getattr(node, "inferred_type", None)
        if isinstance(value, Type):
            return value
        if isinstance(node, ast.Literal):
            return {"Int": INT, "Bool": BOOL, "Text": TEXT}.get(node.kind, INT)
        if isinstance(node, ast.CallExpr) and isinstance(node.callee, ast.Variable):
            fn = self.functions.get(node.callee.name)
            if fn is not None:
                return fn.result
        return INT

    def _type_from_ref(self, ref: ast.TypeRef | None, span: ast.Span) -> Type:
        if ref is None:
            return VOID
        mapping = {"Int": INT, "Bool": BOOL, "Text": TEXT, "Unit": VOID}
        if ref.name not in mapping or ref.args:
            raise BackendFailure(
                f"native backend does not support type `{ref}` yet", span
            )
        return mapping[ref.name]

    # ---- program -----------------------------------------------------------

    def emit_program(
        self, program: ast.Program, source_name: str = "<input>"
    ) -> tuple[bytes, int, int]:
        """Return (blob, entry offset, offset where writable data begins)."""
        functions = [n for n in program.declarations if isinstance(n, ast.FunctionDecl)]
        rejected = [
            n for n in program.declarations
            if not isinstance(n, (ast.FunctionDecl, ast.LawDecl))
        ]
        if rejected:
            raise BackendFailure(
                "the native backend requires all executable code inside functions",
                rejected[0].span,
            )
        if not any(fn.name == "main" for fn in functions):
            raise BackendFailure("the native backend requires `fn main()`", program.span)

        for fn in functions:
            params = []
            for param in fn.params:
                if param.mode != "value":
                    raise BackendFailure(
                        "native backend does not lower read/edit/take parameters yet",
                        param.span,
                    )
                params.append(self._type_from_ref(param.annotation, param.span))
            if len(params) > len(ARG_REGS):
                raise BackendFailure(
                    f"native backend supports at most {len(ARG_REGS)} parameters",
                    fn.span,
                )
            self.functions[fn.name] = _Function(
                name=fn.name,
                params=params,
                result=self._type_from_ref(fn.return_type, fn.span),
            )

        self._emit_start()
        for fn in functions:
            self._emit_function(fn)
        self._emit_print_int()
        self._emit_print_str()
        self._emit_alloc()
        self._emit_chunk()
        self._emit_oom()
        self._emit_strings()
        data_offset = self._emit_data()

        code = self.asm.link()
        return code, self.asm.labels["_start"], data_offset

    def _emit_start(self) -> None:
        """Process entry: call main, then exit with its value (or 0)."""
        asm = self.asm
        asm.label("_start")
        asm.call("fn.main")
        if self.functions["main"].result == INT:
            asm.mov_rr(RDI, RAX)
        else:
            asm.xor_rr(RDI, RDI)
        asm.mov_ri(RAX, SYS_EXIT)
        asm.syscall()
        asm.ud2()   # unreachable: exit never returns

    # ---- functions ---------------------------------------------------------

    def _emit_function(self, fn: ast.FunctionDecl) -> None:
        asm = self.asm
        info = self.functions[fn.name]
        self.scopes = [{}]
        self.slot_count = 0
        self.max_slots = 0
        self.depth = 0
        self.loops = []

        asm.label(f"fn.{fn.name}")
        asm.push(RBP)
        asm.mov_rr(RBP, RSP)

        # The frame size is not known until the body has been scanned, so the
        # `sub rsp, N` is emitted as a fixed-width placeholder and patched.
        sub_at = asm.offset
        asm.sub_ri(RSP, 0x7FFFFFFF)     # forces the 32-bit imm encoding
        sub_imm_at = asm.offset - 4

        for index, param in enumerate(fn.params):
            slot = self._declare(param.name)
            asm.mov_mr(RBP, slot, ARG_REGS[index])

        for statement in fn.body.statements:
            self._statement(statement)

        # Falling off the end returns 0, matching the interpreter's unit value.
        asm.xor_rr(RAX, RAX)
        asm.label(f"fn.{fn.name}.epilogue")
        asm.mov_rr(RSP, RBP)
        asm.pop(RBP)
        asm.ret()

        frame = (self.max_slots * 8 + 15) & ~15
        asm.code[sub_imm_at:sub_imm_at + 4] = frame.to_bytes(4, "little")
        assert sub_at < sub_imm_at

    # ---- statements --------------------------------------------------------

    def _statement(self, node: ast.Stmt) -> None:
        asm = self.asm
        if isinstance(node, ast.LetStmt):
            self._expr(node.value)
            slot = self._declare(node.name)
            asm.mov_mr(RBP, slot, RAX)
            return
        if isinstance(node, ast.AssignStmt):
            self._expr(node.value)
            asm.mov_mr(RBP, self._lookup(node.name, node.span), RAX)
            return
        if isinstance(node, ast.ReturnStmt):
            if node.value is not None:
                self._expr(node.value)
            else:
                asm.xor_rr(RAX, RAX)
            asm.mov_rr(RSP, RBP)
            asm.pop(RBP)
            asm.ret()
            return
        if isinstance(node, ast.ExprStmt):
            self._expr(node.expr, discard=True)
            return
        if isinstance(node, ast.WhileStmt):
            self._while(node)
            return
        if isinstance(node, ast.ForStmt):
            self._for_range(node)
            return
        if isinstance(node, ast.BreakStmt):
            if not self.loops:
                raise BackendFailure("`break` outside a loop", node.span)
            asm.jmp(self.loops[-1].break_label)
            return
        if isinstance(node, ast.ContinueStmt):
            if not self.loops:
                raise BackendFailure("`continue` outside a loop", node.span)
            asm.jmp(self.loops[-1].continue_label)
            return
        raise BackendFailure(
            f"native backend cannot lower {type(node).__name__} yet", node.span
        )

    def _block(self, block: ast.Block) -> None:
        self.scopes.append({})
        saved = self.slot_count
        for statement in block.statements:
            self._statement(statement)
        self.slot_count = saved     # slots are reusable once the scope closes
        self.scopes.pop()

    def _while(self, node: ast.WhileStmt) -> None:
        asm = self.asm
        top = self._label("while")
        end = self._label("wend")
        asm.label(top)
        self._expr(node.condition)
        asm.test_rr(RAX, RAX)
        asm.jcc(CC_E, end)
        self.loops.append(_Loop(continue_label=top, break_label=end))
        self._block(node.body)
        self.loops.pop()
        asm.jmp(top)
        asm.label(end)

    def _for_range(self, node: ast.ForStmt) -> None:
        """Lower `for x in A .. B`. Other iterables are not supported yet."""
        asm = self.asm
        iterable = node.iterable
        if not (isinstance(iterable, ast.BinaryExpr) and iterable.op == ".."):
            raise BackendFailure(
                "native backend supports only `for x in A .. B` ranges", node.span
            )

        self.scopes.append({})
        saved = self.slot_count

        self._expr(iterable.left)
        index_slot = self._declare(node.name)
        asm.mov_mr(RBP, index_slot, RAX)

        self._expr(iterable.right)
        limit_slot = self._alloc_slot()
        asm.mov_mr(RBP, limit_slot, RAX)

        top = self._label("for")
        step = self._label("fstep")
        end = self._label("fend")
        asm.label(top)
        asm.mov_rm(RAX, RBP, index_slot)
        asm.mov_rm(RCX, RBP, limit_slot)
        asm.cmp_rr(RAX, RCX)
        asm.jcc(CC_GE, end)

        self.loops.append(_Loop(continue_label=step, break_label=end))
        self._block(node.body)
        self.loops.pop()

        asm.label(step)
        asm.mov_rm(RAX, RBP, index_slot)
        asm.add_ri(RAX, 1)
        asm.mov_mr(RBP, index_slot, RAX)
        asm.jmp(top)
        asm.label(end)

        self.slot_count = saved
        self.scopes.pop()

    # ---- expressions -------------------------------------------------------

    def _expr(self, node: ast.Expr, *, discard: bool = False) -> None:
        """Evaluate `node`, leaving its value in rax."""
        asm = self.asm

        if isinstance(node, ast.Literal):
            if node.kind == "Int":
                asm.mov_ri(RAX, int(node.value))
            elif node.kind == "Bool":
                asm.mov_ri(RAX, 1 if node.value else 0)
            elif node.kind == "Text":
                asm.lea_label(RAX, self._string_label(node.value))
            else:
                raise BackendFailure(
                    f"native backend cannot lower {node.kind} literals yet", node.span
                )
            return

        if isinstance(node, ast.Variable):
            asm.mov_rm(RAX, RBP, self._lookup(node.name, node.span))
            return

        if isinstance(node, ast.UnaryExpr):
            self._expr(node.operand)
            if node.op == "-":
                asm.neg(RAX)
            elif node.op == "!":
                asm.test_rr(RAX, RAX)
                asm.setcc(CC_E, RAX)
                asm.movzx_r8(RAX, RAX)
            else:
                raise BackendFailure(
                    f"native backend cannot lower unary `{node.op}`", node.span
                )
            return

        if isinstance(node, ast.BinaryExpr):
            self._binary(node)
            return

        if isinstance(node, ast.CallExpr):
            self._call(node, discard=discard)
            return

        if isinstance(node, ast.IfExpr):
            self._if_expr(node, discard=discard)
            return

        raise BackendFailure(
            f"native backend cannot lower {type(node).__name__} yet", node.span
        )

    def _binary(self, node: ast.BinaryExpr) -> None:
        asm = self.asm
        op = node.op

        if op in {"&&", "||"}:
            self._short_circuit(node)
            return

        if op == "..":
            raise BackendFailure(
                "ranges are only supported directly in `for` loops", node.span
            )

        left_type = self._expr_type(node.left)
        if left_type == TEXT:
            raise BackendFailure(
                "native backend does not support Text operators yet", node.span
            )

        self._expr(node.left)
        self._push(RAX)
        self._expr(node.right)
        asm.mov_rr(RCX, RAX)
        self._pop(RAX)

        if op == "+":
            asm.add_rr(RAX, RCX)
        elif op == "-":
            asm.sub_rr(RAX, RCX)
        elif op == "*":
            asm.imul_rr(RAX, RCX)
        elif op in {"/", "//"}:
            self._floor_div(node)
        elif op == "%":
            self._floor_mod(node)
        elif op in COMPARISONS:
            asm.cmp_rr(RAX, RCX)
            asm.setcc(COMPARISONS[op], RAX)
            asm.movzx_r8(RAX, RAX)
        else:
            raise BackendFailure(
                f"native backend cannot lower operator `{op}`", node.span
            )

    def _floor_div(self, node: ast.BinaryExpr) -> None:
        """rax = floor(rax / rcx), matching the interpreter rather than C.

        `idiv` truncates toward zero. When the remainder is non-zero and its
        sign differs from the divisor's, the truncated quotient is one greater
        than the floored one.
        """
        asm = self.asm
        done = self._label("divdone")
        asm.cqo()
        asm.idiv(RCX)
        asm.test_rr(RDX, RDX)
        asm.jcc(CC_E, done)
        asm.mov_rr(R8, RDX)
        asm.xor_rr(R8, RCX)         # sign bit set iff the signs differ
        asm.test_rr(R8, R8)
        asm.jcc(CC_NS, done)
        asm.sub_ri(RAX, 1)
        asm.label(done)

    def _floor_mod(self, node: ast.BinaryExpr) -> None:
        """rax = rax mod rcx with the sign of the divisor (Python semantics)."""
        asm = self.asm
        done = self._label("moddone")
        asm.cqo()
        asm.idiv(RCX)
        asm.test_rr(RDX, RDX)
        asm.jcc(CC_E, done)
        asm.mov_rr(R8, RDX)
        asm.xor_rr(R8, RCX)
        asm.test_rr(R8, R8)
        asm.jcc(CC_NS, done)
        asm.add_rr(RDX, RCX)
        asm.label(done)
        asm.mov_rr(RAX, RDX)

    def _short_circuit(self, node: ast.BinaryExpr) -> None:
        asm = self.asm
        end = self._label("scend")
        shortcut = self._label("sc")

        if node.op == "&&":
            self._expr(node.left)
            asm.test_rr(RAX, RAX)
            asm.jcc(CC_E, shortcut)
            self._expr(node.right)
            asm.test_rr(RAX, RAX)
            asm.jcc(CC_E, shortcut)
            asm.mov_ri(RAX, 1)
            asm.jmp(end)
            asm.label(shortcut)
            asm.xor_rr(RAX, RAX)
        else:
            self._expr(node.left)
            asm.test_rr(RAX, RAX)
            asm.jcc(CC_NE, shortcut)
            self._expr(node.right)
            asm.test_rr(RAX, RAX)
            asm.jcc(CC_NE, shortcut)
            asm.xor_rr(RAX, RAX)
            asm.jmp(end)
            asm.label(shortcut)
            asm.mov_ri(RAX, 1)
        asm.label(end)

    def _if_expr(self, node: ast.IfExpr, *, discard: bool) -> None:
        asm = self.asm
        else_label = self._label("else")
        end = self._label("endif")

        self._expr(node.condition)
        asm.test_rr(RAX, RAX)
        asm.jcc(CC_E, else_label)
        self._block(node.then_branch)
        asm.jmp(end)
        asm.label(else_label)
        if node.else_branch is not None:
            self._block(node.else_branch)
        asm.label(end)

    def _call(self, node: ast.CallExpr, *, discard: bool) -> None:
        asm = self.asm
        if not isinstance(node.callee, ast.Variable):
            raise BackendFailure(
                "native backend supports only direct function calls", node.span
            )
        name = node.callee.name

        if name == "print":
            self._print(node)
            return

        fn = self.functions.get(name)
        if fn is None:
            raise BackendFailure(
                f"native backend does not support call to `{name}`", node.span
            )
        if len(node.args) != len(fn.params):
            raise BackendFailure(
                f"`{name}` expects {len(fn.params)} arguments, got {len(node.args)}",
                node.span,
            )

        for arg in node.args:
            self._expr(arg)
            self._push(RAX)
        for index in reversed(range(len(node.args))):
            self._pop(ARG_REGS[index])
        self._aligned_call(f"fn.{name}")

    def _aligned_call(self, label: str) -> None:
        """Emit a call, keeping rsp 16-byte aligned as the ABI requires."""
        asm = self.asm
        misaligned = self.depth % 2 == 1
        if misaligned:
            asm.sub_ri(RSP, 8)
        asm.call(label)
        if misaligned:
            asm.add_ri(RSP, 8)

    def _print(self, node: ast.CallExpr) -> None:
        asm = self.asm
        if len(node.args) != 1:
            raise BackendFailure("`print` takes exactly one argument", node.span)
        arg = node.args[0]
        arg_type = self._expr_type(arg)

        if arg_type == TEXT:
            if not (isinstance(arg, ast.Literal) and arg.kind == "Text"):
                raise BackendFailure(
                    "native backend can only print Text literals yet", arg.span
                )
            value = arg.value
            asm.lea_label(RDI, self._string_label(value))
            asm.mov_ri(RSI, len(value.encode("utf-8")) + 1)   # include newline
            self._aligned_call("rt.print_str")
            return

        if arg_type == BOOL:
            true_label = self._label("btrue")
            end = self._label("bend")
            self._expr(arg)
            asm.test_rr(RAX, RAX)
            asm.jcc(CC_NE, true_label)
            asm.lea_label(RDI, self._string_label("false"))
            asm.mov_ri(RSI, len("false") + 1)
            asm.jmp(end + ".call")
            asm.label(true_label)
            asm.lea_label(RDI, self._string_label("true"))
            asm.mov_ri(RSI, len("true") + 1)
            asm.label(end + ".call")
            self._aligned_call("rt.print_str")
            asm.label(end)
            return

        self._expr(arg)
        asm.mov_rr(RDI, RAX)
        self._aligned_call("rt.print_int")

    # ---- emitted runtime ---------------------------------------------------

    def _emit_print_int(self) -> None:
        """write(1, decimal(rdi) + "\\n").

        Digits are produced least-significant first into a stack buffer that is
        filled backwards, so no reversal pass is needed.
        """
        asm = self.asm
        asm.label("rt.print_int")
        asm.push(RBP)
        asm.mov_rr(RBP, RSP)
        asm.sub_ri(RSP, 48)

        asm.mov_rr(RAX, RDI)
        asm.xor_rr(R10, R10)            # negative?
        positive = "rt.print_int.positive"
        asm.test_rr(RAX, RAX)
        asm.jcc(CC_NS, positive)
        asm.mov_ri(R10, 1)
        asm.neg(RAX)
        asm.label(positive)

        # rsi walks backwards from the end of the buffer.
        asm.mov_rr(RSI, RBP)
        asm.sub_ri(RSI, 1)
        asm.mov_ri(RDX, 0x0A)           # '\n'
        asm.mov_m8_r(RSI, 0, RDX)

        asm.mov_ri(RCX, 10)
        loop = "rt.print_int.loop"
        asm.label(loop)
        asm.cqo()
        asm.idiv(RCX)                   # rax = quotient, rdx = digit
        asm.add_ri(RDX, 0x30)           # '0'
        asm.sub_ri(RSI, 1)
        asm.mov_m8_r(RSI, 0, RDX)
        asm.test_rr(RAX, RAX)
        asm.jcc(CC_NE, loop)

        write = "rt.print_int.write"
        asm.test_rr(R10, R10)
        asm.jcc(CC_E, write)
        asm.mov_ri(RDX, 0x2D)           # '-'
        asm.sub_ri(RSI, 1)
        asm.mov_m8_r(RSI, 0, RDX)
        asm.label(write)

        asm.mov_rr(RDX, RBP)
        asm.sub_rr(RDX, RSI)            # length = one-past-end - start
        asm.mov_ri(RAX, SYS_WRITE)
        asm.mov_ri(RDI, STDOUT)
        asm.syscall()

        asm.mov_rr(RSP, RBP)
        asm.pop(RBP)
        asm.ret()

    def _emit_print_str(self) -> None:
        """write(1, rdi, rsi)"""
        asm = self.asm
        asm.label("rt.print_str")
        asm.mov_rr(RDX, RSI)
        asm.mov_rr(RSI, RDI)
        asm.mov_ri(RAX, SYS_WRITE)
        asm.mov_ri(RDI, STDOUT)
        asm.syscall()
        asm.ret()

    def _emit_alloc(self) -> None:
        """`rt.alloc(size in rdi) -> pointer in rax`.

        A bump allocator over chunks obtained from `mmap`. There is no libc, so
        memory comes straight from the kernel.

        Nothing is freed yet. Reclamation belongs with the ownership model --
        `take` already records statically where a value dies -- and wiring that
        up is separate work. Until then a program's peak memory equals its total
        allocation, which is acceptable for a compiler that runs once and exits,
        and unacceptable for a server. That distinction is tracked, not ignored.
        """
        asm = self.asm
        asm.label("rt.alloc")
        asm.push(RBP)
        asm.mov_rr(RBP, RSP)

        # Round the request up to 16 bytes so every pointer returned is aligned
        # for any scalar the backend can store.
        asm.add_ri(RDI, 15)
        asm.and_ri(RDI, -16)

        asm.lea_label(RCX, "data.heap_ptr")
        asm.mov_rm(RAX, RCX, 0)          # current bump pointer
        asm.mov_rm(RDX, RCX, 8)          # end of the current chunk

        asm.mov_rr(R8, RAX)
        asm.add_rr(R8, RDI)              # where the pointer would land
        asm.cmp_rr(R8, RDX)
        asm.jcc(CC_BE, "rt.alloc.fits")

        # Out of room: take a fresh chunk, then redo the bump against it.
        asm.push(RDI)
        asm.call("rt.chunk")
        asm.pop(RDI)
        asm.lea_label(RCX, "data.heap_ptr")
        asm.mov_rm(RAX, RCX, 0)
        asm.mov_rr(R8, RAX)
        asm.add_rr(R8, RDI)

        asm.label("rt.alloc.fits")
        asm.mov_mr(RCX, 0, R8)           # commit the bump pointer
        asm.mov_rr(RSP, RBP)
        asm.pop(RBP)
        asm.ret()

    def _emit_chunk(self) -> None:
        """`rt.chunk(minimum in rdi)` -- mmap a chunk and install it as the heap."""
        asm = self.asm
        asm.label("rt.chunk")
        asm.push(RBP)
        asm.mov_rr(RBP, RSP)

        # length = max(request, CHUNK_BYTES), rounded up to a whole page.
        asm.mov_rr(RSI, RDI)
        asm.mov_ri(RAX, CHUNK_BYTES)
        asm.cmp_rr(RSI, RAX)
        asm.jcc(CC_AE, "rt.chunk.sized")
        asm.mov_rr(RSI, RAX)
        asm.label("rt.chunk.sized")
        asm.add_ri(RSI, PAGE_SIZE - 1)
        asm.and_ri(RSI, -PAGE_SIZE)

        asm.xor_rr(RDI, RDI)                              # addr: let the kernel pick
        asm.mov_ri(RDX, PROT_READ | PROT_WRITE)
        asm.mov_ri(R10, MAP_PRIVATE | MAP_ANONYMOUS)      # syscalls use r10, not rcx
        asm.mov_ri(R8, -1)                                # fd
        asm.xor_rr(R9, R9)                                # offset
        asm.mov_ri(RAX, SYS_MMAP)
        asm.syscall()

        # The raw syscall reports failure as a small negative value rather than
        # MAP_FAILED: anything in [-4095, -1] is an errno.
        asm.cmp_ri(RAX, -4095)
        asm.jcc(CC_B, "rt.chunk.ok")
        asm.call("rt.oom")

        asm.label("rt.chunk.ok")
        asm.lea_label(RCX, "data.heap_ptr")
        asm.mov_mr(RCX, 0, RAX)          # heap_ptr = base
        asm.add_rr(RAX, RSI)
        asm.mov_mr(RCX, 8, RAX)          # heap_end = base + length
        asm.mov_rr(RSP, RBP)
        asm.pop(RBP)
        asm.ret()

    def _emit_oom(self) -> None:
        """Report allocation failure on stderr and exit, never return garbage."""
        asm = self.asm
        message = "kofun: out of memory"
        asm.label("rt.oom")
        asm.lea_label(RSI, self._string_label(message))
        asm.mov_ri(RDX, len(message.encode("utf-8")) + 1)
        asm.mov_ri(RDI, STDERR)
        asm.mov_ri(RAX, SYS_WRITE)
        asm.syscall()
        asm.mov_ri(RDI, 70)              # EX_SOFTWARE
        asm.mov_ri(RAX, SYS_EXIT)
        asm.syscall()
        asm.ud2()                        # unreachable: exit does not return

    def _emit_strings(self) -> None:
        for value, label in self.strings.items():
            self.asm.label(label)
            self.asm.emit_bytes(value.encode("utf-8") + b"\n")

    def _emit_data(self) -> int:
        """Emit the writable section, returning its offset within the blob.

        Virtual addresses track file offsets exactly, so padding to a page
        boundary here is what lets the ELF writer hand this region its own
        read-write segment while the code stays read-execute.
        """
        asm = self.asm
        asm.emit_bytes(b"\0" * elf.padding_to_page(asm.offset))
        data_offset = asm.offset
        asm.label("data.heap_ptr")
        asm.emit_bytes((0).to_bytes(8, "little"))
        asm.label("data.heap_end")
        asm.emit_bytes((0).to_bytes(8, "little"))
        return data_offset


def compile_to_executable(
    program: ast.Program, output: str, source_name: str = "<input>"
) -> str:
    backend = NativeBackend()
    code, entry, data_offset = backend.emit_program(program, source_name)
    return str(elf.write_executable(output, code, entry, data_offset))
