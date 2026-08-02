# Syntax guide

## Files

The standard extension is `.kofun`.

UTF-8 is the standard, and identifiers may use Unicode.

```kofun
let 合計 = 40 + 2
```

Two distinct identifier spellings with the same UTS #39 confusable skeleton
in one compilation unit are a hard error (`EUNICODE006`). This security check
does not change identifier equality or name resolution. Cross-module
confusable collision detection is not implemented. Its accepted resolver
contract checks only each module's effective visible bindings, per semantic
namespace, and will use the distinct hard-error code `EUNICODE008`; private or
unselected dependency names do not participate. Implementation is tracked by
#1018.

## Comments

```kofun
# line comment
```

Nested block comments are planned:

```kofun
/* outer
   /* inner */
*/
```

## Bindings

```kofun
let name = "Ada"
let mut count = 0
let own file = File.open(path)
```

The default is immutable.

## Functions

```kofun
fn add(left: Int, right: Int) -> Int {
    return left + right
}
```

single-expression form:

```kofun
fn add(left: Int, right: Int) -> Int = left + right
```

parameter mode:

```kofun
fn inspect(read file: File) -> Metadata
fn update(edit file: File) -> Void
fn consume(take file: File) -> Void
```

The parameter list fixes the function's exact arity. A two-parameter
declaration has callable type `(A, B) -> R`; it does not accept one argument
and return a partially applied function. Declaration result types continue to
use `->`.

## Lambdas

Kofun accepts three lambda spellings. The canonical form starts with `fn`:

```kofun
fn(x) => x + 1
fn(x: Int, y: Int) => x + y
```

Only the canonical form has a block-body spelling:

```kofun
fn(x: Int) {
    let squared = x * x
    return squared + 1
}
```

An expression-body lambda may omit `fn`. Parentheses remain available for
multiple parameters or annotations; a bare lambda has exactly one unannotated
parameter:

```kofun
(x, y) => x + y
value => value * 2
```

`x => expression` is a lambda in expression position and a match arm at an arm
boundary. The parser uses that position, rather than token shape alone, to keep
the two meanings distinct. Call-arguments v1 additionally restricts the lambda
written after a closed call to the canonical `fn(...)` spelling.

## Conditionals

```kofun
if score >= 90 {
    print("A")
} else if score >= 80 {
    print("B")
} else {
    print("C")
}
```

`if` is an expression.

```kofun
let label = if ready { "ready" } else { "waiting" }
```

## Nullability

```kofun
let port: Int? = null
let effective = port ?? 8080
```

`nil` and `None` are not keywords.

## Lists and tuples

```kofun
let numbers = [1, 2, 3]
let pair = ("answer", 42)
```

planned map/set literals:

```kofun
let counts = { "a": 1, "b": 2 }
let ids = set { 10, 20, 30 }
```

The literal syntax will be settled by UX testing the ambiguity against blocks.
Records are already settled and do not constrain it: record construction uses
no braces (see [Records](#records)).

## Operators

Precedence, roughly from highest to lowest.

```text
postfix:       call, member, index
unary:         ! + -
power:         **
multiplicative:* / // %
additive:      + -
range:         ..
comparison:    < <= > >=
equality:      == !=
logical:       && ||
coalescing:    ??
pipeline:      |>
assignment:    =
```

## Division

`//` is the integer quotient and takes the **floor**, so it disagrees with C-style
truncation on negative operands. `%` is paired with that quotient.

```kofun
7 // 2   # 3
-7 // 2  # -4, not -3
-7 % 2   # 1, the divisor's sign
```

`/` is not defined on `Int`. Kofun performs no implicit numeric conversion, so
`/` cannot produce a fractional value from two `Int` operands; `7 / 2` is a
compile error rather than either `3` or `3.5`. See `docs/TYPE_SYSTEM.md`.

## Pipelines

```kofun
let answer = values
    |> filter(fn(x) => x > 0)
    |> map(fn(x) => x * x)
    |> sum()
```

rewrite rule:

```text
value |> function          => function(value)
value |> function(a, b)    => function(value, a, b)
```

## Ranges

```kofun
0 .. 10
```

The upper bound is exclusive.

planned inclusive range:

```kofun
0 ..= 10
```

## Loops

```kofun
for index in 0 .. len(values) {
    print(values[index])
}
```

```kofun
while queue.len() > 0 {
    # ...
}
```

## Pattern matching

planned syntax:

```kofun
match message {
    Quit => stop()
    Move(x, y) if x >= 0 => move_to(x, y)
    Move(_, _) => error("negative coordinate")
}
```

## Types

```kofun
Int
Int?
List[Int]
Map[Text, Int]
Result[User, Error]
Int -> Text
(Int, Text) -> Bool
() -> Int
Int -> (Text -> Bool)
Tuple[Int, Text] -> Bool
(read File, take Buffer) -> Result[Response, Error]
```

`->` has the lowest precedence in a type and associates to the right.
Consequently `A -> B?` means `A -> (B?)`; an optional callable is written
`(A -> B)?`.

Callable domains have fixed arity:

- `A -> R` takes one argument;
- `(A, B) -> R` takes two arguments;
- `() -> R` takes no arguments;
- `A -> (B -> R)` takes one argument and returns a callable; and
- `Tuple[A, B] -> R` takes one tuple argument.

There is no implicit currying, partial application, or conversion between a
multi-argument callable and a tuple-taking callable. Ownership modes such as
`read`, `edit`, and `take` are part of each callable parameter type. Parameter
names are not part of callable type identity in v1.

The former `Fn[...]` notation is not an alias. Migration diagnostics must
offer a targeted rewrite from `Fn[A, R]` to `A -> R` and from historical
multi-argument forms to a parenthesized fixed-arity domain. `Fn` is otherwise
an ordinary identifier.

## ADT declarations

The executable Stage 2 C11 checkpoint accepts concrete enums with zero or one
named `Int` payload:

```kofun
type Signal =
    | Red
    | Yellow(code: Int)
    | Green
```

The checkpoint requires explicit local typing (`let signal: Signal = Green`),
passes and returns enum values through same-typed functions, and supports
exhaustive statement-position matching with `Yellow(code)`, `Yellow(_)`, `_`,
or a binding catch-all. Type parameters and wider payloads remain planned:

```kofun
type Result[T, E] =
    | Ok(T)
    | Err(E)
```

See `spec/enum-match-exhaustiveness.md` for the bounded constructor namespace,
coverage, diagnostic, and implementation limits.

## Records

Accepted design, gated by a bounded frontend rather than the production
compiler. See [`spec/records-v1.md`](../spec/records-v1.md).

```kofun
type Point = {
    x: Float,
    y: Float,
}

let point = Point(x: 1.0, y: 2.0)
let x = point.x
```

Construction is always the parenthesized labelled call form. `Point { ... }` is
rejected, so `{` never starts an expression and blocks, control-flow
conditions, and the planned map literal cannot collide with a record. Every
declared field is supplied exactly once, in any written order; arguments
evaluate left to right in written order and are stored in declaration order.
Fields are immutable in v1, `take` moves a whole record, and `take point.x` is
rejected.

## Traits and implementations

planned:

```kofun
trait Show[T] {
    fn show(read value: T) -> Text
}

impl Show[Point] {
    fn show(read value: Point) -> Text =
        "Point(${value.x}, ${value.y})"
}
```

## Compile-time law declarations

The accepted target separates a law family, a named implementation, and a
named check/model request:

```kofun
law Monad[A, MA] {
    operation pure(value: A) -> MA
    operation bind(value: MA, next: A -> MA) -> MA

    equation left_identity(
        value: A in values,
        next: A -> MA in functions,
    ) = bind(pure(value), next) == next(value)

    equation right_identity(
        value: MA in monads,
    ) = bind(value, pure) == value
}

impl OptionalBoolMonad: Monad[Bool, Bool?] {
    pure = optional_pure
    bind = optional_bind
}

check laws OptionalBoolMonadEvidence {
    instance = OptionalBoolMonad
    domain values: Bool = all
    domain monads: Bool? = all
    domain functions: Bool -> Bool? = all_functions(values, monads)
    equality Bool? = structural
    require assurance = proven_finite
    budget = standard
}
```

`law` and `check laws` are contextual top-level constructs. `operation`,
`equation`, the colon form of `impl`, `domain`, `equality`,
`require assurance`, and `budget` are contextual only inside those constructs.
`Monad` is an ordinary library identifier; `monad` is not a keyword or a
compiler-known law family.

The schema parameters are first-order slots. Every first executable check
substitutes ground types before evaluation; higher-kinded types are not
required. Equation parameters quantify over named typed domains, and equality
is selected per operand type. `all` and `all_functions` require
compiler-certified complete finite carriers.

Law checking is compile-time-only, has an empty effect set, and runs under the
versioned `kofun.law-eval/standard-v1` budget. The active compiler does not yet
implement this syntax: it rejects the retained historical `law monad` examples
with `E2S02`. See [Law system](LAW_SYSTEM.md) for the full normative target,
assurance levels, evidence identity, and historical migration boundary.

## Visibility

The normative contract is
[`spec/modules/visibility.md`](../spec/modules/visibility.md). Stage 2 accepts
the four basic forms on top-level functions:

```kofun
# Public API is intentional. Omission is private.
pub fn create_user(name: Text) -> User
internal fn generate_id() -> UserId
private fn validate_name(name: Text)
fn normalize_name(name: Text) -> Text
```

`internal` means the package/build unit defined by the package-root contract,
not a directory or textual module. `pub(to ancestor.path)` is specified for
restricted module access but is deferred beyond the first executable slice.
There is no `protected` modifier, and capitalization does not affect access.

The executable syntax slice records visibility, implicit versus explicit
origin, and modifier/declaration spans. A separate top-level declaration gate
now assigns production module-scoped `SymbolId` values to bounded functions,
ADTs, and constructors, while the identity-only access engine enforces
private, internal, public, and enclosing boundaries for already-resolved IDs.
General imports and public-signature checking are still not routed through the
active compiler. `pub`, `internal`, and `private` remain ordinary identifier
tokens outside a supported declaration-prefix position.

## Modules

Manifest source files use an explicit module header under the normative
[`spec/modules/source-file-mapping.md`](../spec/modules/source-file-mapping.md)
contract:

```kofun
module user.service
```

The manifest chooses which files belong to the package; the header alone
chooses the semantic module path. Paths and directories are not fallback
module names. Anonymous single-file builds instead use one synthetic root and
do not accept a module header in v1. This is an accepted design contract; the
active compiler does not yet parse a general multi-file module graph.

### Semantic namespaces

The normative
[`spec/modules/namespaces.md`](../spec/modules/namespaces.md) contract assigns
every named declaration to exactly one of four namespaces:

| Namespace | Examples |
| --- | --- |
| value | functions, constants, locals, constructors, methods |
| type | nominal types, aliases, type parameters, traits |
| module | declared modules and imported module qualifiers |
| meta | macros, meta functions, and named laws |

The same spelling may coexist across different namespaces but is rejected
twice in the same namespace and scope. The syntactic use site chooses one
namespace, so lookup never retries another namespace after failing. A
selective import may bind all exported meanings of one spelling, one per
namespace. Capitalization has no effect on namespace assignment. This is an
accepted design contract; the active compiler does not yet implement the
general resolver.

## Imports and re-exports

The ordinary qualified, qualified-alias, and selective forms below have
bounded executable Stage 2 checkpoints. They are not yet routed through the
general CLI/package resolver:

```kofun
import science
import data.csv as csv
from collections import Map, Set
```

Public forwarding uses the normative
[`spec/modules/re-exports.md`](../spec/modules/re-exports.md) forms:

```kofun
pub import collections
pub from collections import Map, Set
```

An ordinary import or module alias never becomes public implicitly. A
re-export preserves the original target identity, creates a distinct
`ExportBindingId`, and fails if any target/enclosing/signature boundary is less
than public. Re-export aliases, wildcards, internal forwarding, and an
`export` keyword are not part of the first re-export slice;
`pub import ... as ...` is rejected.

## Semicolons

Not required. Statement boundaries are determined by newlines and `}`.

Line continuation is either inside a delimiter or made explicit with an operator.

```kofun
let answer = values
    |> map(transform)
    |> sum()
```

The Stage 0 lexer treats a leading pipeline as a continuation as a special case.
