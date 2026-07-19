# Syntax guide

## Files

標準extensionは仮に`.frost`とする。

UTF-8を標準とし、identifierはUnicodeを許可する。

```frost
let 合計 = 40 + 2
```

public APIではconfusable文字にwarningを出す。

## Comments

```frost
# line comment
```

nested block commentはplanned:

```frost
/* outer
   /* inner */
*/
```

## Bindings

```frost
let name = "Ada"
let mut count = 0
let own file = File.open(path)
```

defaultはimmutable。

## Functions

```frost
fn add(left: Int, right: Int) -> Int {
    return left + right
}
```

single-expression form:

```frost
fn add(left: Int, right: Int) -> Int = left + right
```

parameter mode:

```frost
fn inspect(read file: File) -> Metadata
fn update(edit file: File) -> Void
fn consume(take file: File) -> Void
```

## Lambdas

```frost
fn(x) => x + 1
fn(x: Int, y: Int) => x + y
```

block lambda:

```frost
fn(x: Int) {
    let squared = x * x
    return squared + 1
}
```

## Conditionals

```frost
if score >= 90 {
    print("A")
} else if score >= 80 {
    print("B")
} else {
    print("C")
}
```

`if`はexpression。

```frost
let label = if ready { "ready" } else { "waiting" }
```

## Nullability

```frost
let port: Int? = null
let effective = port ?? 8080
```

`nil`、`None`はkeywordではない。

## Lists and tuples

```frost
let numbers = [1, 2, 3]
let pair = ("answer", 42)
```

planned map/set literals:

```frost
let counts = { "a": 1, "b": 2 }
let ids = set { 10, 20, 30 }
```

literal syntaxはblock/recordとのambiguityをUX testして確定する。

## Operators

precedenceは高い順に概ね次の通り。

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

```frost
7 / 2  # 3.5
7 // 2 # 3
```

## Pipelines

```frost
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

```frost
0 .. 10
```

upper boundはexclusive。

planned inclusive range:

```frost
0 ..= 10
```

## Loops

```frost
for index in 0 .. len(values) {
    print(values[index])
}
```

```frost
while queue.len() > 0 {
    # ...
}
```

## Pattern matching

planned syntax:

```frost
match message {
    Quit => stop()
    Move(x, y) if x >= 0 => move_to(x, y)
    Move(_, _) => error("negative coordinate")
}
```

## Types

```frost
Int
Int?
List[Int]
Map[Text, Int]
Result[User, Error]
fn(Int, Int) -> Int
```

## ADT declarations

planned:

```frost
type Result[T, E] =
    | Ok(T)
    | Err(E)
```

## Records

planned:

```frost
type Point = {
    x: Float,
    y: Float,
}

let point = Point { x: 1.0, y: 2.0 }
```

## Traits and implementations

planned:

```frost
trait Show[T] {
    fn show(read value: T) -> Text
}

impl Show[Point] {
    fn show(read value: Point) -> Text =
        "Point(${value.x}, ${value.y})"
}
```

## Compile-time law declarations

Stage 0では`Monad`則をtop-levelで宣言できます。

```frost
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

entryはnewlineまたはcommaで区切れます。必須entryは`pure`、`bind`、`values`、`monads`、`functions`です。任意entryは`equal`、`limit`、`complete`です。

`law` declarationはruntime statementではありません。type checking後、code generation前にdeterministic compile-time evaluatorで検査されます。違反はcompile errorになり、C backendへは出力されません。

## Imports

planned:

```frost
import science
import data.csv as csv
from collections import Map, Set
```

## Semicolons

不要。newlineと`}`でstatement boundaryを決める。

line continuationはdelimiter内、またはoperatorで明示する。

```frost
let answer = values
    |> map(transform)
    |> sum()
```

Stage 0 lexerはleading pipelineを特別に継続扱いする。
