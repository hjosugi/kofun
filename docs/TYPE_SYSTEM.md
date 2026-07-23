# Type system

## Design target

The Kofun type system has two entry points.

1. Beginners can write local programs without annotations.
2. Advanced users can use ADTs, traits, effects, row polymorphism, and type-level computation.

Code that does not use the hard type features does not pay for their complexity.

## Primitive types

```text
Bool
Int
Float
Decimal
Complex
Text
Bytes
Null
Void
Never
```

`String` may be provided as an alias, but the canonical name is `Text`.

## Optional types

```kofun
let age: Int? = null
let safe_age = age ?? 0
```

Rules:

- `null` can only be assigned to `T?`
- no implicit null injection into `T`
- no implicit conversion from `T?` to `T`
- narrowing happens through `??`, pattern matching, or guards

Planned pattern:

```kofun
match user.name {
    null => "anonymous"
    name => name
}
```

Separate constructors named `None` or `Nil` are not used for the optional case. Domain-specific ADTs may use any constructor name.

## Type inference

```kofun
let count = 42          # Int
let ratio = 0.5         # Float
let names = ["a", "b"] # List[Text]
```

Inference covers:

- local bindings
- return types
- lambda parameters when a call context exists
- generic arguments
- effects
- optional branch joins

For public APIs, annotations are recommended for stability and documentation.

## Numeric conversion

Planned rules:

- widening conversions are implicit in limited cases
- narrowing conversions are explicit
- `Int + Float -> Float`
- `Int / Int -> Float`
- `Int // Int -> Int`
- the overflow mode is not changed implicitly between debug and release; it is stated explicitly in the build profile

```kofun
let exact = 7 // 2 # 3
let ratio = 7 / 2  # 3.5
```

## Generics

Square brackets are used instead of angle brackets.

```kofun
fn identity[T](value: T) -> T = value

type Pair[A, B] = {
    first: A,
    second: B,
}
```

Reasons:

- it reduces lexer ambiguity with comparison operators
- `List[Int]` is readable to Python and TypeScript users as well
- type application and indexing can be distinguished by parser context

## Algebraic data types

```kofun
type Tree[T] =
    | Empty
    | Node(value: T, left: Tree[T], right: Tree[T])
```

Pattern matching:

```kofun
fn size[T](tree: Tree[T]) -> Int {
    return match tree {
        Empty => 0
        Node(_, left, right) => 1 + size(left) + size(right)
    }
}
```

The compiler checks exhaustiveness and unreachable patterns.

Executable checkpoints: Stage 2 performs this check for bounded statement-
position and Int-valued `Bool` matches over `true`, `false`, and `_`, including
ordered Bool guards with conservative unguarded coverage. It also accepts
concrete payload-free enum declarations, explicitly typed local constructor
bindings, and exhaustive statement-position enum matches. See
`spec/bool-match-exhaustiveness.md` and
`spec/enum-match-exhaustiveness.md`. Generic and payload constructors,
nested patterns, ownership-aware destructuring, and general arm-type
unification remain planned.

A separate typed-only Stage 2 checkpoint now accepts one bounded payload
surface before layout and matching are implemented:

```kofun
type MaybeInt =
    | Missing
    | Present(value: Int)

fn present() -> MaybeInt {
    return Present(42)
}
```

It supports non-generic top-level ADTs with at least two constructors, where a
constructor has zero fields or exactly one named `Int` field. All constructors
are collected before function bodies are resolved, and typed IR records
nominal ADT/constructor identities plus declaration and use spans. The
checkpoint emits no runtime layout or backend code and does not add payload
patterns or exhaustiveness; see `tests/conformance/adt/README.md`.

The separate top-level declaration-table checkpoint assigns these bounded ADT
types and constructors production module-scoped `SymbolId` values alongside
functions. It proves namespace separation and declaration-order independence,
but still performs only same-module lookup; imports and cross-module calls are
the next module-resolution slice.

## Records

Nominal record:

```kofun
type User = {
    id: Int,
    name: Text,
}
```

Structural record boundary:

```kofun
fn render(user: { name: Text, ..R }) -> Text
```

Row polymorphism is useful for JSON, web APIs, data frames, and testing doubles, but nominal types are preferred for layout-sensitive system APIs.

## Union and intersection types

The expressiveness of TypeScript is adopted, but uncontrolled union explosion is avoided.

```kofun
type Input = Text | Bytes
```

Main uses:

- external data boundaries
- gradual migration
- generated API bindings
- pattern narrowing

For internal domain models, ADTs are recommended.

Intersection types are restricted to limited uses such as capability composition.

## Traits

```kofun
trait Eq[T] {
    fn equals(read left: T, read right: T) -> Bool
}

trait Iterator[I, Item] {
    fn next(edit iterator: I) -> Item?
}
```

Features:

- generic traits
- associated types
- default methods
- auto traits for send/share/copy
- coherence
- orphan rule
- specialization is explicit and limited

## Effects

Ordinary function syntax is kept, while the effect row is inferred.

Conceptual types:

```text
fn parse(Text) -> User
fn load(Path) -> User ! {io, error[FsError]}
fn fetch(Url) -> User ! {async, io, error[HttpError]}
```

Effect annotations do not have to be written every time in source. They can be stated explicitly for public APIs, trait contracts, and no-effect guarantees.

```kofun
pure fn normalize(value: Float) -> Float
```

Whether to adopt the `pure` keyword will be decided after evaluating effect inference and diagnostic UX.

## Result and error propagation

```kofun
fn load_user(path: Path) -> Result[User, LoadError] {
    let text = File.read_text(path)?
    return Json.decode[User](text)?
}
```

The parser resolves the contextual conflict between `?` and the optional suffix.

Errors can be carried as a type parameter, and the API for adding context is standardized.

## Ownership in types

Parameter modes are expressed as a call contract, not as a type constructor.

```kofun
fn hash(read bytes: Bytes) -> Digest
fn fill(edit buffer: Buffer) -> Void
fn submit(take request: Request) -> Response
```

This reduces the notational load of `&T`, `&mut T`, and explicit lifetimes.

Advanced APIs can expose view lifetimes at the type level, but standard user code does not see them.

## Const generics and shapes

```kofun
fn dot[N](left: Array[Float, N], right: Array[Float, N]) -> Float
```

For N-dimensional arrays, the rank and some shapes are treated as compile-time values.

Dynamic shapes remain first class as well.

```kofun
Array[Float, rank = 2]
DynArray[Float]
```

## Type-level functions

```kofun
type fn OutputShape[A, B] = Broadcast[A, B]
```

Constraints:

- termination or a fuel limit
- deterministic
- no IO
- good diagnostics
- cacheable

## Current implementation

Implemented:

- `Int`, `Float`, `Bool`, `Text`, `Null`, `Void`, `Any`
- `List[T]`, Tuple
- `T?`
- basic function types
- local inference
- numeric promotion
- branch/list joins
- part of the built-in polymorphic behavior
- `read` / `edit` / `take` parameter metadata

Not implemented:

- typed `law monad` declarations and compiler-integrated law checking
- active assurance checking for `bounded-exhaustive` or `proven-finite`
- user-defined generics
- ADTs, match
- traits
- union/intersection
- row polymorphism
- effect rows
- const generics
- type-level functions
- principal-type guarantee
- higher-kinded types and lawful traits
- generic proof terms and trusted proof kernel

Historical Monad examples and JSON artifacts document an earlier bounded
prototype, but the active CLI rejects the law syntax. Issue
[#551](https://github.com/hjosugi/kofun/issues/551) tracks a concrete-first
replacement without making a higher-kinded type system a prerequisite.
