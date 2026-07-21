# Syntax guide

## Files

The standard extension is tentatively `.kofun`.

UTF-8 is the standard, and identifiers may use Unicode.

```kofun
let 合計 = 40 + 2
```

Confusable characters produce a warning in public APIs.

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

## Lambdas

```kofun
fn(x) => x + 1
fn(x: Int, y: Int) => x + y
```

block lambda:

```kofun
fn(x: Int) {
    let squared = x * x
    return squared + 1
}
```

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

The literal syntax will be settled by UX testing the ambiguity against blocks and records.

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

```kofun
7 / 2  # 3.5
7 // 2 # 3
```

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
fn(Int, Int) -> Int
```

## ADT declarations

The executable Stage 2 checkpoint accepts concrete payload-free enums:

```kofun
type Signal =
    | Red
    | Yellow
    | Green
```

The checkpoint requires explicit local typing (`let signal: Signal = Green`)
and supports exhaustive statement-position matching. Type parameters and
constructor payloads remain planned:

```kofun
type Result[T, E] =
    | Ok(T)
    | Err(E)
```

See `spec/enum-match-exhaustiveness.md` for the bounded constructor namespace,
coverage, diagnostic, and implementation limits.

## Records

planned:

```kofun
type Point = {
    x: Float,
    y: Float,
}

let point = Point { x: 1.0, y: 2.0 }
```

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

In Stage 0, `Monad` laws can be declared at the top level.

```kofun
law monad OptionalBoolMonad {
    pure = optional_pure
    bind = optional_bind
    values = [false, true]
    monads = [null, false, true]
    functions = finite_functions([false, true], [null, false, true])
    complete = true
    limit = 1000
}
```

Entries are separated by a newline or a comma. The required entries are `pure`, `bind`, `values`, `monads`, and `functions`. The optional entries are `equal`, `limit`, and `complete`.

A `law` declaration is not a runtime statement. It is checked by a deterministic compile-time evaluator after type checking and before code generation. A violation becomes a compile error and is not emitted to the C backend.

## Visibility

planned; the normative contract is
[`spec/modules/visibility.md`](../spec/modules/visibility.md):

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

## Imports

planned:

```kofun
import science
import data.csv as csv
from collections import Map, Set
```

## Semicolons

Not required. Statement boundaries are determined by newlines and `}`.

Line continuation is either inside a delimiter or made explicit with an operator.

```kofun
let answer = values
    |> map(transform)
    |> sum()
```

The Stage 0 lexer treats a leading pipeline as a continuation as a special case.
