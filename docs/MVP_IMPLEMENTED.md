# Stage 0 implementation status

## Status meaning

- `implemented`: code and automated tests exist
- `prototype`: works for a limited subset
- `designed`: documented, not implemented
- `not started`: backlog only

## Frontend

| Feature | Status | Notes |
|---|---|---|
| UTF-8 source | implemented | Unicode identifiers work in interpreter |
| line comments | implemented | `#` |
| integer literals | implemented | underscore separators supported |
| float literals | implemented | decimal and exponent scanning |
| text literals | implemented | common escapes |
| automatic statement termination | prototype | newline insertion plus leading pipeline continuation |
| Pratt parser | implemented | explicit precedence table |
| source spans | implemented | line and column |
| parser recovery | prototype | statement/block synchronization |
| lossless syntax tree | not started | comments are not represented in AST |
| incremental parser | not started | backlog |

## Syntax and control flow

| Feature | Status | Notes |
|---|---|---|
| `fn` declaration | implemented | block and expression body |
| lambda | implemented | `fn(x) => expr`, block lambda |
| `let` | implemented | immutable by default |
| `let mut` | implemented | local assignment |
| `let own` | implemented | affine resource binding |
| `if` expression | implemented | branch value joins |
| `else if` | implemented | canonical spelling |
| `while` | implemented | break/continue supported |
| `for` | implemented | iterable protocol through host runtime |
| ranges | implemented | exclusive `a .. b` |
| pipeline | implemented | `value |> f(args)` |
| integer division | implemented | `//` |
| null coalescing | implemented | `??` |
| List literal | implemented | homogeneous type join |
| Tuple literal | implemented | runtime and type support |
| member calls | implemented | selected List methods |
| indexing | implemented | List, Tuple, Text |
| map/set literals | designed | runtime helpers exist, syntax not fixed |
| match | designed | parser not implemented |
| ADT declaration | designed | parser not implemented |

## Type checker

| Feature | Status | Notes |
|---|---|---|
| primitive types | implemented | Int, Float, Bool, Text, Null, Void, Any |
| List type | implemented | `List[T]` |
| Tuple type | implemented | basic joins |
| optional type | implemented | `T?` |
| null restriction | implemented | `null` rejected for non-optional annotations |
| local inference | implemented | literal, list, calls, operators |
| function signatures | implemented | forward references and recursion |
| function return checking | prototype | conservative definite-return analysis |
| numeric promotion | implemented | Int to Float widening |
| function values | implemented | basic function type |
| lambda inference | prototype | annotation or Any fallback |
| polymorphic builtins | prototype | map/filter/fold/sum and selected methods |
| user generics | not started | designed |
| traits | not started | designed |
| effect rows | not started | designed |
| row polymorphism | not started | designed |
| union/intersection | not started | designed |

## Law checking

| Feature | Status | Notes |
|---|---|---|
| `law monad` syntax | implemented | top-level compile-time declaration |
| left identity | implemented | exhaustive over declared finite model |
| right identity | implemented | exhaustive over declared finite model |
| associativity | implemented | exhaustive over declared finite model |
| counterexample diagnostics | implemented | L101-L103 with concrete inputs |
| case budget | implemented | `limit`, default 100,000 |
| deterministic law sandbox | prototype | print, clock, args, and file I/O disabled |
| complete function enumeration | implemented | `finite_functions(domain, codomain)` |
| `proven-finite` Bool carrier | implemented | complete Bool and Optional[Bool] models |
| custom equality | implemented | optional `equal` function |
| JSON evidence artifact | implemented | `frost.law-evidence/v1` with source hash and model digest |
| assurance build gate | implemented | `--require-assurance` / `--require-law-assurance`, L200 |
| Functor/Applicative/Monoid laws | designed | backlog |
| generic proof-term kernel | not started | required for `proven` evidence |
| SMT certificate checking | not started | solver must remain outside trusted kernel |

## Ownership

| Feature | Status | Notes |
|---|---|---|
| owned local | implemented | `let own` |
| explicit consume statement | implemented | `take name` |
| `take` parameter | implemented | caller binding is moved |
| `read` parameter | prototype | contract metadata and read-only convention |
| `edit` parameter | prototype | mutable callee binding; alias analysis incomplete |
| use-after-take diagnostic | implemented | E330 |
| duplicate consume diagnostic | implemented | checker and runtime |
| branch state merge | prototype | conservative union of moved states |
| loop state analysis | prototype | conservative |
| scope cleanup | implemented | calls `close()` when available |
| non-escaping borrow proof | not started | production MIR task |
| alias conflict checker | not started | production MIR task |
| async capture rules | not started | designed |

## Reference interpreter

| Feature | Status | Notes |
|---|---|---|
| lexical environments | implemented | nested scopes and closures |
| recursion | implemented | global function pre-registration |
| closures | implemented | captures runtime environment |
| control flow | implemented | return/break/continue exceptions internally |
| List FP methods | implemented | map/filter/fold/sort/reverse/etc. |
| scientific helpers | implemented | small List-based prototype |
| persistent-style Map/Set helpers | prototype | runtime builtins; checker surface incomplete |
| resource mock | implemented | testable deterministic close |
| module loading | not started | backlog |
| bytecode VM | not started | designed |

## C11 backend

| Feature | Status | Notes |
|---|---|---|
| C source emission | implemented | portable C11 subset |
| native executable build | implemented | `cc`, `clang`, or `gcc` |
| Int | implemented | `int64_t` |
| Float | implemented | `double` |
| Bool | implemented | C `bool` |
| Text | prototype | strings, comparison, concatenate, indexing, slice, and selected helpers |
| functions | implemented | direct calls and recursion |
| let/assignment | implemented | primitive values |
| if/else | implemented | statement lowering |
| while | implemented | primitive conditions |
| range for-loop | implemented | integer exclusive range |
| math calls | prototype | selected functions |
| `List[Text]` | prototype | Stage 1 args/chars runtime only |
| generic List | not started | explicit backend error |
| closure | not started | explicit backend error |
| GC values | not started | explicit backend error |
| ownership/resource lowering | not started | explicit backend error |
| debug info | not started | backlog |

## Bootstrap and self-hosting

| Feature | Status | Notes |
|---|---|---|
| Stage 0 Python toolchain | implemented | current reference implementation |
| Frost-written compiler seed | implemented | `bootstrap/stage1/compiler.frost` |
| Stage 1 Frost Core to C11 | prototype | arithmetic `print(EXPR)` subset |
| Stage 0 builds native Stage 1 | implemented | C11 backend plus host C compiler |
| interpreted/native Stage 1 equivalence | implemented | generated fixture C11 is byte-identical |
| Stage 1 native fixture | implemented | generated program returns 42 |
| machine-readable stage manifest | implemented | `bootstrap/manifest.json` |
| Stage 1 full frontend | not started | backlog |
| Stage 1 full type/ownership/law checker | not started | backlog |
| Stage 2 self-recompile | not started | true self-hosting gate |
| Stage 1/Stage 2 artifact equivalence | not started | fixed-point gate |
| diverse double compilation | not started | bootstrap security gate |

## Tooling

| Feature | Status | Notes |
|---|---|---|
| `frost run` | implemented | interpreter and `--native` |
| `frost check` | implemented | structured text diagnostics |
| `frost laws` | implemented | text/JSON evidence, model summary, assurance gate |
| `frost build` | implemented | C11 bootstrap |
| `frost fmt` | prototype | conservative indentation formatter |
| `frost test` | implemented | `# expect:` golden output |
| `frost repl` | prototype | persistent evaluator, no full incremental checker |
| `frost new` | implemented | project scaffold |
| `frost ast` | implemented | debug AST output |
| JSON diagnostics | not started | backlog |
| linter | not started | backlog |
| documentation generator | not started | backlog |
| package manager | not started | backlog |

## Automated verification

At the current repository revision:

- 31 Python unit tests pass
- 5 `.frost` language tests pass
- native Fibonacci demo compiles and returns 6765 (`fib(20)`); backend unit fixture also checks `fib(10) = 55`
- Stage 0 builds the Frost-written Stage 1 seed as native code
- interpreted and native Stage 1 emit byte-identical C11; the generated fixture returns 42
- `Optional[Bool]` Monad finite proof checks all 264 cases
- law evidence JSON and `proven-finite` assurance gate are covered by tests
- backlog verifier confirms 13,500 unique issues

Run again in the target environment:

```bash
make verify
```
