# 直接機械語バックエンド (Direct Machine Code Backend)

Kofun compiles straight to x86-64 machine code. The native path shells out to
nothing: no C, no clang, no `as`, no `ld`, no libc. The compiler encodes
instructions itself, writes the ELF64 container itself, and talks to the kernel
through syscalls.

```
Kofun source (.kf)
      │
      ▼
  lexer / parser
      │
      ▼
  type check + ownership check
      │
      ▼
  native_backend.py      ← emits x86-64 instruction bytes
      │
      ▼
  x64.py                 ← instruction encoder (REX / ModRM / SIB)
      │
      ▼
  elf.py                 ← static ELF64 writer (replaces ld)
      │
      ▼
  statically linked executable
```

## Usage

Native is the default backend.

```bash
kofun build program.kf -o program     # direct machine code
kofun build program.kf --backend c    # C11 + system compiler
```

## Measured results

`examples/fibonacci_native.kf`, x86-64, best of 5.

| | native | C11 + `cc -O3` |
|---|---|---|
| build (end to end) | 111 ms | 345 ms |
| codegen only | **0.13 ms** | ~200 ms (compiler process) |
| binary size | **452 bytes** | 22,320 bytes |
| linkage | **static, no deps** | dynamic, needs libc |
| `fib(30)` runtime | 7 ms | **2 ms** |

Read this honestly. The native backend wins decisively on **build** — codegen is
0.13 ms, so end-to-end time is dominated by Python interpreter startup, not by
compilation. It loses on **runtime**, by roughly 3.5x, because it performs no
optimization at all: single pass, no register allocator, no inlining, no
strength reduction. `cc -O3` does all of that.

For reference, the tree-walking interpreter runs the same `fib(30)` in 22,789 ms,
so the native backend is ~3,250x faster than interpreting.

Closing the runtime gap is tracked work, not a solved problem. See the
optimizer issues in the tracker.

## Semantics: the interpreter is the oracle

`//` and `%` are **floored**, matching the reference interpreter (and Python),
not truncated as C does. These disagree whenever exactly one operand is
negative:

```
-7 // 2   ==  -4        (floored)      not  -3  (truncated)
-7 %  2   ==   1        (floored)      not  -1  (truncated)
```

`idiv` truncates toward zero, so the backend emits a correction after each
division: when the remainder is non-zero and its sign differs from the
divisor's, the quotient is decremented and the remainder adjusted by the
divisor.

The C11 backend currently maps `//` onto C `/` and so **diverges from the
interpreter on negative operands**. That is a real bug in the C backend, not a
deliberate difference; it is filed in the tracker.

`tests/test_native_backend.py` enforces agreement: every program is run
interpreted and compiled, and the two stdout streams must match byte for byte.

## Supported subset

The backend is deliberately narrow, and every gap raises `BackendFailure`
rather than silently changing meaning.

Supported: `Int`, `Bool`, `Text` literals, arithmetic (`+ - * / // %`), unary
`-` and `!`, comparisons, short-circuit `&&` / `||`, `if`/`else`, `while`,
`for x in A .. B`, `break`/`continue`, `let`/`var`, assignment, direct calls,
recursion, up to 6 parameters, `print` of `Int` / `Bool` / `Text` literal.

Not yet supported: `Float`, lists, strings beyond literals, closures, pattern
matching, `read`/`edit`/`take` parameter modes, more than 6 parameters. Use
`--backend c` for those until the native path catches up.

## Implementation notes

**Calling convention.** System V AMD64. Arguments in `rdi, rsi, rdx, rcx, r8,
r9`; return value in `rax`; frame is `push rbp; mov rbp, rsp; sub rsp, N` with
locals at `[rbp - 8k]`. `rsp` is kept 16-byte aligned at every call site — the
backend tracks outstanding expression-stack pushes and inserts an 8-byte
adjustment when the depth is odd.

**Expression evaluation.** Results land in `rax`. Binary operators evaluate the
left side, push it, evaluate the right side into `rcx`, then pop the left back
into `rax`. Simple and obviously correct; also the main reason runtime lags
`-O3`.

**Frame sizing.** The frame size is unknown until the body has been walked, so
the prologue emits `sub rsp, imm32` as a fixed-width placeholder and patches
the immediate afterwards.

**Runtime.** `rt.print_int` formats decimals into a stack buffer filled
backwards (so no reversal pass) and issues `write(2)`. `rt.print_str` is a bare
`write(2)`. `_start` calls `main` and issues `exit(2)` with its return value.
Three routines, all emitted as machine code.

**ELF.** One `PT_LOAD` segment, `R|X`, base `0x400000`, no section headers, no
dynamic linking, no relocations. The kernel maps it and jumps to `e_entry`.

## Verifying the encoder

A wrong instruction encoding is a miscompile that no later stage can catch, so
the encoder is differential-tested against `objdump`: each case emits one
instruction and asserts on the disassembly. Run it with the checked-in test
suite, and see `tests/test_native_backend.py` for the end-to-end differential
tests against the interpreter.
