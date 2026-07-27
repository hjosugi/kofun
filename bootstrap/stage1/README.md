# Python-free Stage 1 seed

`compiler.kofun` is the canonical source. `compiler.c` is its checked-in,
auditable bootstrap seed. A host C11 compiler is enough to build and run this
Kofun-written compiler.

```sh
sh bootstrap/stage1/check.sh
```

Stage 1 accepts the documented Int/Bool Core:

```text
kofun-stage1 INPUT.kofun OUTPUT.c
```

The compatibility parser requires one explicit line-oriented `fn main() {`
body containing only `let` statements, Int-valued `print(...)` statements, and
`if`/`else if`/`else` blocks, plus blank lines and comments. `let` may infer or
explicitly name `Int` or `Bool`. Unknown structural lines are rejected; they are
never ignored while extracting an otherwise valid `print`.

## Expressions are compiled, not deferred

Each expression is tokenized, parsed by precedence, name-resolved and
range-checked *by this compiler*, then lowered to a C11 statement sequence over
checked Int64 helpers. The emitted program contains no parser, no symbol table,
and no fragment of the user's source text — the earlier seed passed each
expression's source to an `evaluate()` interpreter that it emitted verbatim
from string literals, which meant arithmetic, precedence, name lookup and
overflow checking all happened at the emitted program's runtime.

One C statement is emitted per operator, into a temporary named after that
operator's path from the root of the expression. That is what makes evaluation
order observable and fixed: C leaves the order of function arguments
unspecified, so a nested call tree would let the host C compiler decide which
of two failing operators reports its diagnostic first.

Because names are resolved here, these are now compile errors rather than
runtime traps in the emitted program:

- a reference to a name that is not bound, including one whose block has closed
- a second `let` for a name already visible (the language rejects shadowing)
- a binding named `true` or `false` (the Bool literals are reserved)
- an explicit annotation other than the inferred `Int` or `Bool`
- an integer literal outside the Int64 range
- `/`, which is not defined on Int (#687); `//` is the integer quotient
- arithmetic or ordered comparisons with a `Bool` operand
- `&&`, `||`, or `!` with an `Int` operand
- the non-Core single-character `|` or `&` operators
- a `Bool` passed to the Int-only `print` boundary
- a block condition that is not `Bool`
- an `else` with no `if` to attach to, or a second `else` in one chain
- a block left open at the end of the source, or a `}` that closes nothing

The six Int comparisons produce `Bool`; `==` and `!=` additionally compare two
Bool operands. `&&` and `||` short-circuit their right operands, and `!` has
unary precedence. The compiler tracks each local as `Int` or `Bool` before
emission, so the two types never become interchangeable merely because C can
represent both as integers.

`-9223372036854775808` still compiles: a negated decimal literal is folded at
compile time, so the one magnitude with no positive counterpart keeps a C
spelling.

## Blocks nest, and their bindings leave scope

`if COND {`, `} else if COND {`, `} else {` and `}` each occupy their own line.
Both structural walks keep one stack of open blocks, so the depth they agree on
is the stack's length rather than a separate counter, and an `else` is accepted
only when the block it closes is a branch no `else` has followed yet. A `let`
inside a block is visible until that block's `}`, after which its name is free
again — the scope stack lives in the same text as the bindings, so leaving a
block truncates back to the marker its `{` pushed.

Each Kofun block becomes exactly one C branch brace, and each `}` closes exactly
one, so an `else if` chain never leaves the closing line counting braces. A
chain also gets one enclosing C scope holding the flag that says an earlier
branch already ran; that flag is what keeps a later `else if` condition
unevaluated, exactly as `&&` and `||` keep their right operands unevaluated.

Nothing in the accepted Core returns a value yet, so `main` has no path that
must end in `return`; the per-branch `returned` state arrives with the
declaration slice that introduces `return` (#751).

It does not yet semantically compile its complete own source, so the Stage 2
fixed-point gate remains open.
