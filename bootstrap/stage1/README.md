# Python-free Stage 1 seed

`compiler.kofun` is the canonical source. `compiler.c` is its checked-in,
auditable bootstrap seed. A host C11 compiler is enough to build and run this
Kofun-written compiler.

```sh
sh bootstrap/stage1/check.sh
```

Stage 1 accepts the documented arithmetic Core:

```text
kofun-stage1 INPUT.kofun OUTPUT.c
```

The compatibility parser requires one explicit line-oriented `fn main() {`
body containing only `let` and `print(...)` statements, plus blank lines and
comments. Unknown structural lines are rejected; they are never ignored while
extracting an otherwise valid `print`.

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

- a reference to a name that is not bound
- a second `let` for a name already bound (the language rejects same-scope
  shadowing)
- an integer literal outside the Int64 range
- `/`, which is not defined on Int (#687); `//` is the integer quotient

`-9223372036854775808` still compiles: a negated decimal literal is folded at
compile time, so the one magnitude with no positive counterpart keeps a C
spelling.

It does not yet semantically compile its complete own source, so the Stage 2
fixed-point gate remains open.
