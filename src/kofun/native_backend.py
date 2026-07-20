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
from .typesys import BOOL, INT, LIST, TEXT, VOID, Type
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
    RBX,
    RCX,
    RDI,
    RDX,
    RBP,
    RSI,
    RSP,
    R8,
    R9,
    R10,
    R12,
    R13,
    R14,
    R15,
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

#: The condition that holds exactly when its key does not. Used to branch
#: directly on a comparison instead of materialising a boolean and testing it.
INVERSE_CC = {CC_E: CC_NE, CC_NE: CC_E, 12: CC_GE, CC_GE: 12, 14: 15, 15: 14}


#: Registers a local can live in across a call. The System V ABI requires a
#: callee to preserve these, so a value parked here survives recursion without
#: the code generator spilling anything. Caller-saved registers would not: rax,
#: rcx, rdx, rsi, rdi and r8-r11 are all clobbered by the expression evaluator
#: or by argument passing.
CALLEE_SAVED = (RBX, R12, R13, R14, R15)


@dataclass(frozen=True, slots=True)
class _Loc:
    """Where a binding lives: a callee-saved register, or a frame slot."""

    in_register: bool
    value: int      # register number, or a negative offset from rbp


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
        self.scopes: list[dict[str, _Loc]] = []
        self.slot_count = 0
        self.max_slots = 0
        self.registers: tuple[int, ...] = ()    # pool for the current function
        self.register_next = 0
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

    def _alloc_slot(self) -> _Loc:
        self.slot_count += 1
        self.max_slots = max(self.max_slots, self.slot_count)
        return _Loc(in_register=False, value=-8 * self.slot_count)

    def _alloc(self) -> _Loc:
        """Take the next register if this function got a register budget."""
        if self.register_next < len(self.registers):
            location = _Loc(in_register=True, value=self.registers[self.register_next])
            self.register_next += 1
            return location
        return self._alloc_slot()

    def _declare(self, name: str) -> _Loc:
        location = self._alloc()
        self.scopes[-1][name] = location
        return location

    def _lookup(self, name: str, span: ast.Span) -> _Loc:
        for scope in reversed(self.scopes):
            if name in scope:
                return scope[name]
        raise BackendFailure(f"native backend: unknown binding `{name}`", span)

    def _store(self, location: _Loc, src: int = RAX) -> None:
        """Write `src` into a binding."""
        if location.in_register:
            self.asm.mov_rr(location.value, src)
        else:
            self.asm.mov_mr(RBP, location.value, src)

    def _load(self, location: _Loc, dst: int) -> None:
        """Read a binding into `dst`."""
        if location.in_register:
            self.asm.mov_rr(dst, location.value)
        else:
            self.asm.mov_rm(dst, RBP, location.value)

    def _count_bindings(self, fn: ast.FunctionDecl) -> int:
        """How many named bindings the function body introduces, params included.

        Scopes reuse frame slots but not registers, so this deliberately counts
        every declaration rather than the peak live count. It only decides
        whether the whole function fits in the register pool; overflow falls
        back to slots, so an over-count costs nothing but a missed optimisation.
        """
        total = len(fn.params)

        def walk(node: object) -> None:
            nonlocal total
            if isinstance(node, ast.LetStmt):
                total += 1
                walk(node.value)
            elif isinstance(node, ast.ForStmt):
                total += 2          # the loop variable, plus a hidden limit
                walk(node.iterable)
                walk(node.body)
            elif isinstance(node, ast.Block):
                for statement in node.statements:
                    walk(statement)
            elif isinstance(node, ast.WhileStmt):
                walk(node.condition)
                walk(node.body)
            elif isinstance(node, ast.IfExpr):
                walk(node.condition)
                walk(node.then_branch)
                if node.else_branch is not None:
                    walk(node.else_branch)
            elif isinstance(node, ast.ExprStmt):
                walk(node.expr)
            elif isinstance(node, ast.ReturnStmt):
                if node.value is not None:
                    walk(node.value)
            elif isinstance(node, ast.AssignStmt):
                walk(node.value)
            elif isinstance(node, ast.BinaryExpr):
                walk(node.left)
                walk(node.right)
            elif isinstance(node, ast.UnaryExpr):
                walk(node.operand)
            elif isinstance(node, ast.CallExpr):
                for argument in node.args:
                    walk(argument)

        walk(fn.body)
        return total

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
        self._emit_print_text()
        self._emit_memcpy()
        self._emit_text_concat()
        self._emit_text_eq()
        self._emit_text_len()
        self._emit_list_new()
        self._emit_list_index()
        self._emit_list_concat()
        self._emit_index_error()
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

        # Give the function a register budget only if every binding fits. A
        # partial allocation would need spill decisions, which is a much larger
        # change; falling back wholesale to slots keeps this predictable.
        needed = self._count_bindings(fn)
        fits_in_registers = needed <= len(CALLEE_SAVED)
        self.registers = CALLEE_SAVED[:needed] if fits_in_registers else ()
        self.register_next = 0
        self.saved = self.registers

        # When every binding lives in a register there is nothing to address
        # relative to rbp, so the frame pointer itself can go. That removes
        # push/mov on entry and mov/pop on exit -- four instructions per call,
        # which for a small recursive function is a real fraction of its cost.
        self.use_frame = not fits_in_registers
        # Entry leaves rsp ≡ 8 (mod 16); each push flips it. Without a frame to
        # absorb the difference, an even number of saved registers needs a
        # manual adjustment so calls see the alignment the ABI requires.
        pad = 8 if (not self.use_frame and len(self.saved) % 2 == 0) else 0
        self.frame_pad = pad

        asm.label(f"fn.{fn.name}")
        sub_imm_at = None
        if self.use_frame:
            asm.push(RBP)
            asm.mov_rr(RBP, RSP)
        for register in self.saved:
            asm.push(register)

        if self.use_frame:
            # The frame size is not known until the body has been scanned, so
            # `sub rsp, N` is emitted as a fixed-width placeholder and patched.
            asm.sub_ri(RSP, 0x7FFFFFFF)     # forces the 32-bit imm encoding
            sub_imm_at = asm.offset - 4
        elif pad:
            asm.sub_ri(RSP, pad)

        for index, param in enumerate(fn.params):
            self._store(self._declare(param.name), ARG_REGS[index])

        for statement in fn.body.statements:
            self._statement(statement)

        # Falling off the end returns 0, matching the interpreter's unit value.
        asm.xor_rr(RAX, RAX)
        asm.label(f"fn.{fn.name}.epilogue")
        self._emit_epilogue()

        if sub_imm_at is not None:
            # An odd number of saved registers flips the alignment the ABI
            # requires at the next call; the frame absorbs it.
            frame = (self.max_slots * 8 + 15) & ~15
            if len(self.saved) % 2 == 1:
                frame += 8
            asm.code[sub_imm_at:sub_imm_at + 4] = frame.to_bytes(4, "little")

    def _emit_epilogue(self) -> None:
        """Restore saved registers, drop the frame if there is one, and return."""
        asm = self.asm
        if self.use_frame:
            if self.saved:
                # rbp marks the slot holding the caller's rbp, so the saved
                # registers sit immediately below it.
                asm.mov_rr(RSP, RBP)
                asm.sub_ri(RSP, 8 * len(self.saved))
            else:
                asm.mov_rr(RSP, RBP)
            for register in reversed(self.saved):
                asm.pop(register)
            asm.pop(RBP)
        else:
            if self.frame_pad:
                asm.add_ri(RSP, self.frame_pad)
            for register in reversed(self.saved):
                asm.pop(register)
        asm.ret()

    # ---- statements --------------------------------------------------------

    def _statement(self, node: ast.Stmt) -> None:
        asm = self.asm
        if isinstance(node, ast.LetStmt):
            self._expr(node.value)
            self._store(self._declare(node.name))
            return
        if isinstance(node, ast.AssignStmt):
            self._expr(node.value)
            self._store(self._lookup(node.name, node.span))
            return
        if isinstance(node, ast.ReturnStmt):
            if node.value is not None:
                self._expr(node.value)
            else:
                asm.xor_rr(RAX, RAX)
            self._emit_epilogue()
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
        self._branch_unless(node.condition, end)
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
        index = self._declare(node.name)
        self._store(index)

        self._expr(iterable.right)
        limit = self._alloc()
        self._store(limit)

        top = self._label("for")
        step = self._label("fstep")
        end = self._label("fend")
        asm.label(top)
        self._load(index, RAX)
        self._load(limit, RCX)
        asm.cmp_rr(RAX, RCX)
        asm.jcc(CC_GE, end)

        self.loops.append(_Loop(continue_label=step, break_label=end))
        self._block(node.body)
        self.loops.pop()

        asm.label(step)
        self._load(index, RAX)
        asm.add_ri(RAX, 1)
        self._store(index)
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
            self._load(self._lookup(node.name, node.span), RAX)
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

        if isinstance(node, ast.ListLiteral):
            self._list_literal(node)
            return

        if isinstance(node, ast.IndexExpr):
            self._index(node)
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
            self._text_binary(node)
            return
        if left_type.name == "List":
            self._list_binary(node)
            return

        self._operands(node)

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

    def _is_simple(self, node: ast.Expr) -> bool:
        """True when `node` can be loaded into a register with one instruction.

        Integer and boolean literals and local variables qualify. Text literals
        do not: they need a RIP-relative `lea`, and Text has no arithmetic yet.
        """
        if isinstance(node, ast.Literal):
            return node.kind in ("Int", "Bool")
        return isinstance(node, ast.Variable)

    def _load_simple(self, node: ast.Expr, reg: int) -> None:
        """Load a literal or local directly into `reg`, touching no other state."""
        if isinstance(node, ast.Literal):
            if node.kind == "Int":
                self.asm.mov_ri(reg, int(node.value))
            else:
                self.asm.mov_ri(reg, 1 if node.value else 0)
            return
        self._load(self._lookup(node.name, node.span), reg)

    def _operands(self, node: ast.BinaryExpr) -> None:
        """Leave the left operand in rax and the right in rcx.

        When the right operand is a literal or a local it is loaded straight
        into rcx, which removes the push/pop of the left operand. That pair is
        two memory round-trips per operation, and in code like `n - 1` or
        `n < 2` it was the entire cost of the expression.
        """
        self._expr(node.left)
        if self._is_simple(node.right):
            self._load_simple(node.right, RCX)
            return
        self._push(RAX)
        self._expr(node.right)
        self.asm.mov_rr(RCX, RAX)
        self._pop(RAX)

    def _branch_unless(self, condition: ast.Expr, label: str) -> None:
        """Evaluate `condition` and jump to `label` when it is false.

        A comparison used as a condition previously materialised a boolean it
        immediately discarded: `cmp` / `setcc` / `movzx` / `test` / `jcc`. Here
        the comparison drives the branch directly, so `if n < 2` becomes
        `cmp` / `jge` -- five instructions down to two.
        """
        if (
            isinstance(condition, ast.BinaryExpr)
            and condition.op in COMPARISONS
            and self._expr_type(condition.left) != TEXT
        ):
            self._operands(condition)
            self.asm.cmp_rr(RAX, RCX)
            self.asm.jcc(INVERSE_CC[COMPARISONS[condition.op]], label)
            return

        self._expr(condition)
        self.asm.test_rr(RAX, RAX)
        self.asm.jcc(CC_E, label)

    def _list_literal(self, node: ast.ListLiteral) -> None:
        """Allocate a list and fill it, leaving the pointer in rax.

        The pointer is parked on the stack across each element, because an
        element can be an arbitrary expression -- including a call, which would
        clobber any caller-saved register holding it.
        """
        asm = self.asm
        asm.mov_ri(RDI, len(node.items))
        self._aligned_call("rt.list_new")
        self._push(RAX)

        for index, item in enumerate(node.items):
            self._expr(item)
            asm.mov_rm(RCX, RSP, 0)     # the list, without disturbing the stack
            asm.mov_mr(RCX, 8 + 8 * index, RAX)

        self._pop(RAX)

    def _index(self, node: ast.IndexExpr) -> None:
        asm = self.asm
        target_type = self._expr_type(node.target)
        if target_type.name != "List":
            raise BackendFailure(
                f"native backend can only index List, not {target_type}", node.span
            )

        self._expr(node.target)
        self._push(RAX)
        self._expr(node.index)
        asm.mov_rr(RSI, RAX)
        self._pop(RDI)
        self._aligned_call("rt.list_index")

    def _list_binary(self, node: ast.BinaryExpr) -> None:
        asm = self.asm
        if node.op != "+":
            raise BackendFailure(
                f"native backend does not support `{node.op}` on List", node.span
            )
        self._expr(node.left)
        self._push(RAX)
        self._expr(node.right)
        asm.mov_rr(RSI, RAX)
        self._pop(RDI)
        self._aligned_call("rt.list_concat")

    def _text_binary(self, node: ast.BinaryExpr) -> None:
        """Lower `+`, `==`, and `!=` on Text. Ordering is not defined yet."""
        asm = self.asm
        if node.op not in {"+", "==", "!="}:
            raise BackendFailure(
                f"native backend does not support `{node.op}` on Text", node.span
            )

        # Both operands must survive into the call, so neither can sit in a
        # scratch register while the other is evaluated.
        self._expr(node.left)
        self._push(RAX)
        self._expr(node.right)
        asm.mov_rr(RSI, RAX)
        self._pop(RDI)

        if node.op == "+":
            self._aligned_call("rt.text_concat")
            return

        self._aligned_call("rt.text_eq")
        if node.op == "!=":
            # rt.text_eq returns 0 or 1, so flipping it is a single test.
            asm.test_rr(RAX, RAX)
            asm.setcc(CC_E, RAX)
            asm.movzx_r8(RAX, RAX)

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

        self._branch_unless(node.condition, else_label)
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

        if name == "len":
            if len(node.args) != 1:
                raise BackendFailure("`len` takes exactly one argument", node.span)
            argument_type = self._expr_type(node.args[0])
            self._expr(node.args[0])
            if argument_type == TEXT:
                # Text length is a codepoint count, so it has to walk the bytes.
                asm.mov_rr(RDI, RAX)
                self._aligned_call("rt.text_len")
                return
            if argument_type.name == "List":
                # A list stores its length in its header; no call needed.
                asm.mov_rm(RAX, RAX, 0)
                return
            raise BackendFailure(
                f"native backend cannot take `len` of {argument_type}", node.span
            )

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
            self._expr(arg)
            asm.mov_rr(RDI, RAX)
            self._aligned_call("rt.print_text")
            return

        if arg_type == BOOL:
            # `true` and `false` are ordinary interned Text values, so this
            # goes through the same printer as any other string.
            true_label = self._label("btrue")
            end = self._label("bend")
            self._expr(arg)
            asm.test_rr(RAX, RAX)
            asm.jcc(CC_NE, true_label)
            asm.lea_label(RDI, self._string_label("false"))
            asm.jmp(end + ".call")
            asm.label(true_label)
            asm.lea_label(RDI, self._string_label("true"))
            asm.label(end + ".call")
            self._aligned_call("rt.print_text")
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
        message = "kofun: out of memory\n"
        asm.label("rt.oom")
        # Interned Text carries an 8-byte length header, so the bytes start
        # after it and the length is read from it.
        asm.lea_label(RSI, self._string_label(message))
        asm.mov_rm(RDX, RSI, 0)
        asm.add_ri(RSI, 8)
        asm.mov_ri(RDI, STDERR)
        asm.mov_ri(RAX, SYS_WRITE)
        asm.syscall()
        asm.mov_ri(RDI, 1)               # match the interpreter's exit code
        asm.mov_ri(RAX, SYS_EXIT)
        asm.syscall()
        asm.ud2()                        # unreachable: exit does not return

    # ---- Text runtime ------------------------------------------------------
    #
    # A Text value is a single pointer to `[byte length: i64][utf-8 bytes]`.
    # Literals are emitted into rodata in exactly that layout, so a literal and
    # a heap-built string are indistinguishable at runtime and no operation has
    # to branch on which it got. Text is immutable, which is what makes sharing
    # the read-only segment safe.

    def _emit_memcpy(self) -> None:
        """`rt.memcpy(rdi = dst, rsi = src, rdx = n)` -- byte at a time."""
        asm = self.asm
        asm.label("rt.memcpy")
        done = "rt.memcpy.done"
        loop = "rt.memcpy.loop"
        asm.test_rr(RDX, RDX)
        asm.jcc(CC_E, done)
        asm.label(loop)
        asm.movzx_r8_m(RAX, RSI, 0)
        asm.mov_m8_r(RDI, 0, RAX)
        asm.add_ri(RSI, 1)
        asm.add_ri(RDI, 1)
        asm.sub_ri(RDX, 1)
        asm.jcc(CC_NE, loop)
        asm.label(done)
        asm.ret()

    def _emit_text_concat(self) -> None:
        """`rt.text_concat(rdi = a, rsi = b) -> rax` -- a fresh joined Text."""
        asm = self.asm
        asm.label("rt.text_concat")
        asm.push(RBP)
        asm.mov_rr(RBP, RSP)
        for register in (RBX, R12, R13, R14, R15):
            asm.push(register)
        asm.sub_ri(RSP, 8)          # five pushes leave rsp misaligned for calls

        asm.mov_rr(RBX, RDI)                 # a
        asm.mov_rr(R12, RSI)                 # b
        asm.mov_rm(R13, RBX, 0)              # length of a
        asm.mov_rm(R14, R12, 0)              # length of b

        asm.mov_rr(RDI, R13)
        asm.add_rr(RDI, R14)
        asm.add_ri(RDI, 8)                   # header
        asm.call("rt.alloc")
        asm.mov_rr(R15, RAX)                 # the new block

        asm.mov_rr(RCX, R13)
        asm.add_rr(RCX, R14)
        asm.mov_mr(R15, 0, RCX)              # combined length

        asm.mov_rr(RDI, R15)
        asm.add_ri(RDI, 8)
        asm.mov_rr(RSI, RBX)
        asm.add_ri(RSI, 8)
        asm.mov_rr(RDX, R13)
        asm.call("rt.memcpy")

        asm.mov_rr(RDI, R15)
        asm.add_ri(RDI, 8)
        asm.add_rr(RDI, R13)                 # just past a's bytes
        asm.mov_rr(RSI, R12)
        asm.add_ri(RSI, 8)
        asm.mov_rr(RDX, R14)
        asm.call("rt.memcpy")

        asm.mov_rr(RAX, R15)
        asm.add_ri(RSP, 8)
        for register in (R15, R14, R13, R12, RBX):
            asm.pop(register)
        asm.pop(RBP)
        asm.ret()

    def _emit_text_eq(self) -> None:
        """`rt.text_eq(rdi = a, rsi = b) -> rax` as 1 or 0."""
        asm = self.asm
        asm.label("rt.text_eq")
        false_label = "rt.text_eq.false"
        true_label = "rt.text_eq.true"
        loop = "rt.text_eq.loop"

        asm.mov_rm(RAX, RDI, 0)
        asm.mov_rm(RCX, RSI, 0)
        asm.cmp_rr(RAX, RCX)
        asm.jcc(CC_NE, false_label)          # different lengths cannot be equal

        asm.mov_rr(RDX, RAX)
        asm.add_ri(RDI, 8)
        asm.add_ri(RSI, 8)
        asm.label(loop)
        asm.test_rr(RDX, RDX)
        asm.jcc(CC_E, true_label)
        asm.movzx_r8_m(RAX, RDI, 0)
        asm.movzx_r8_m(RCX, RSI, 0)
        asm.cmp_rr(RAX, RCX)
        asm.jcc(CC_NE, false_label)
        asm.add_ri(RDI, 1)
        asm.add_ri(RSI, 1)
        asm.sub_ri(RDX, 1)
        asm.jmp(loop)

        asm.label(true_label)
        asm.mov_ri(RAX, 1)
        asm.ret()
        asm.label(false_label)
        asm.xor_rr(RAX, RAX)
        asm.ret()

    def _emit_text_len(self) -> None:
        """`rt.text_len(rdi = text) -> rax`, counted in codepoints.

        The reference interpreter is Python, where `len` on a string counts
        codepoints rather than bytes, so `len("héllo")` is 5 and not 6. Matching
        that means counting bytes which are not UTF-8 continuation bytes, i.e.
        those whose top two bits are not `10`.
        """
        asm = self.asm
        asm.label("rt.text_len")
        loop = "rt.text_len.loop"
        skip = "rt.text_len.skip"
        done = "rt.text_len.done"

        asm.mov_rm(RDX, RDI, 0)              # length in bytes
        asm.add_ri(RDI, 8)
        asm.xor_rr(RAX, RAX)                 # codepoint count

        asm.label(loop)
        asm.test_rr(RDX, RDX)
        asm.jcc(CC_E, done)
        asm.movzx_r8_m(RCX, RDI, 0)
        asm.and_ri(RCX, 0xC0)
        asm.cmp_ri(RCX, 0x80)
        asm.jcc(CC_E, skip)                  # continuation byte: not a new one
        asm.add_ri(RAX, 1)
        asm.label(skip)
        asm.add_ri(RDI, 1)
        asm.sub_ri(RDX, 1)
        asm.jmp(loop)
        asm.label(done)
        asm.ret()

    def _emit_print_text(self) -> None:
        """`rt.print_text(rdi = text)` -- the bytes, then a newline."""
        asm = self.asm
        asm.label("rt.print_text")
        asm.push(RBP)
        asm.mov_rr(RBP, RSP)

        asm.mov_rm(RDX, RDI, 0)              # byte length
        asm.mov_rr(RSI, RDI)
        asm.add_ri(RSI, 8)                   # first byte
        asm.mov_ri(RAX, SYS_WRITE)
        asm.mov_ri(RDI, STDOUT)
        asm.syscall()

        asm.lea_label(RSI, "rt.newline")
        asm.mov_ri(RDX, 1)
        asm.mov_ri(RAX, SYS_WRITE)
        asm.mov_ri(RDI, STDOUT)
        asm.syscall()

        asm.mov_rr(RSP, RBP)
        asm.pop(RBP)
        asm.ret()

    # ---- List runtime ------------------------------------------------------
    #
    # A List value is one pointer to `[length: i64][element: i64] * n`, the same
    # length-prefixed shape as Text so the two stay consistent. Every element is
    # eight bytes, which is exactly an Int, a Bool, or a pointer to a Text or
    # another List -- so one representation serves every element type the
    # backend can currently produce.

    def _emit_list_new(self) -> None:
        """`rt.list_new(count in rdi) -> rax`, elements left uninitialised."""
        asm = self.asm
        asm.label("rt.list_new")
        asm.push(RBP)
        asm.mov_rr(RBP, RSP)
        asm.push(RBX)
        asm.sub_ri(RSP, 8)              # keep rsp aligned for the call below

        asm.mov_rr(RBX, RDI)            # remember the count
        asm.shl_ri(RDI, 3)              # elements are eight bytes each
        asm.add_ri(RDI, 8)              # plus the length header
        asm.call("rt.alloc")
        asm.mov_mr(RAX, 0, RBX)

        asm.add_ri(RSP, 8)
        asm.pop(RBX)
        asm.pop(RBP)
        asm.ret()

    def _emit_list_index(self) -> None:
        """`rt.list_index(list in rdi, index in rsi) -> rax`.

        A negative index counts from the end, matching the reference
        interpreter -- `xs[-1]` is the last element, not an error. Anything
        still out of range after that adjustment is a diagnostic and an exit,
        never a read of whatever happened to be next in memory.
        """
        asm = self.asm
        asm.label("rt.list_index")
        nonneg = "rt.list_index.nonneg"
        bad = "rt.list_index.bad"

        asm.mov_rm(RDX, RDI, 0)         # length
        asm.test_rr(RSI, RSI)
        asm.jcc(CC_NS, nonneg)
        asm.add_rr(RSI, RDX)            # negative: count from the end
        asm.label(nonneg)

        asm.test_rr(RSI, RSI)
        asm.jcc(8, bad)                 # CC_S: still negative
        asm.cmp_rr(RSI, RDX)
        asm.jcc(CC_GE, bad)

        asm.mov_rr(RAX, RSI)
        asm.shl_ri(RAX, 3)
        asm.add_rr(RAX, RDI)
        asm.mov_rm(RAX, RAX, 8)         # skip the header
        asm.ret()

        asm.label(bad)
        asm.call("rt.index_error")
        asm.ud2()

    def _emit_list_concat(self) -> None:
        """`rt.list_concat(a in rdi, b in rsi) -> rax`, a fresh joined list."""
        asm = self.asm
        asm.label("rt.list_concat")
        asm.push(RBP)
        asm.mov_rr(RBP, RSP)
        for register in (RBX, R12, R13, R14, R15):
            asm.push(register)
        asm.sub_ri(RSP, 8)

        asm.mov_rr(RBX, RDI)
        asm.mov_rr(R12, RSI)
        asm.mov_rm(R13, RBX, 0)         # length of a
        asm.mov_rm(R14, R12, 0)         # length of b

        asm.mov_rr(RDI, R13)
        asm.add_rr(RDI, R14)
        asm.call("rt.list_new")
        asm.mov_rr(R15, RAX)

        # Elements are opaque eight-byte words, so copying them is a byte copy.
        asm.mov_rr(RDI, R15)
        asm.add_ri(RDI, 8)
        asm.mov_rr(RSI, RBX)
        asm.add_ri(RSI, 8)
        asm.mov_rr(RDX, R13)
        asm.shl_ri(RDX, 3)
        asm.call("rt.memcpy")

        asm.mov_rr(RDI, R15)
        asm.add_ri(RDI, 8)
        asm.mov_rr(RAX, R13)
        asm.shl_ri(RAX, 3)
        asm.add_rr(RDI, RAX)
        asm.mov_rr(RSI, R12)
        asm.add_ri(RSI, 8)
        asm.mov_rr(RDX, R14)
        asm.shl_ri(RDX, 3)
        asm.call("rt.memcpy")

        asm.mov_rr(RAX, R15)
        asm.add_ri(RSP, 8)
        for register in (R15, R14, R13, R12, RBX):
            asm.pop(register)
        asm.pop(RBP)
        asm.ret()

    def _emit_index_error(self) -> None:
        """Report an out-of-range index on stderr and exit."""
        asm = self.asm
        message = "kofun: list index out of range\n"
        asm.label("rt.index_error")
        asm.lea_label(RSI, self._string_label(message))
        asm.mov_rm(RDX, RSI, 0)
        asm.add_ri(RSI, 8)
        asm.mov_ri(RDI, STDERR)
        asm.mov_ri(RAX, SYS_WRITE)
        asm.syscall()
        asm.mov_ri(RDI, 1)              # match the interpreter's exit code
        asm.mov_ri(RAX, SYS_EXIT)
        asm.syscall()
        asm.ud2()

    def _emit_strings(self) -> None:
        asm = self.asm
        asm.label("rt.newline")
        asm.emit_bytes(b"\n")
        for value, label in self.strings.items():
            encoded = value.encode("utf-8")
            # The length field is read as a 64-bit word, so it must be aligned.
            asm.emit_bytes(b"\0" * (-asm.offset % 8))
            asm.label(label)
            asm.emit_bytes(len(encoded).to_bytes(8, "little"))
            asm.emit_bytes(encoded)

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
