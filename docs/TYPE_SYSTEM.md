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
let ratio = 0.5         # Decimal, not Float: see `docs/DECIMAL.md`
let binary = 0.5f64     # Float
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

## Callable types

Every callable has a fixed, exact arity. The canonical forms are:

```kofun
Int -> Text
(Int, Text) -> Bool
() -> Int
Int -> (Text -> Bool)
Tuple[Int, Text] -> Bool
```

`A -> R` is unary, `(A, B) -> R` is binary, and `() -> R` is nullary.
`A -> (B -> R)` is a unary callable whose result is another callable.
`Tuple[A, B] -> R` is also unary: its one argument is a tuple. These types are
distinct, and the type checker performs no implicit currying, partial
application, curry/uncurry conversion, or tuple/parameter-list conversion.

The callable type of a declaration follows its written parameter list
exactly:

```kofun
fn add(left: Int, right: Int) -> Int
# callable type: (Int, Int) -> Int
```

Calling `add(1)` is therefore an arity error. Partial application is written
explicitly with a function value.

`->` is the lowest-precedence type operator and associates to the right.
`A -> B?` parses as `A -> (B?)`, while `(A -> B)?` is an optional callable.
Writing `A -> (B -> R)` makes the callable-valued result explicit; it is not
equivalent to `(A, B) -> R`.

Parameter ownership modes participate in callable type identity:

```kofun
read File -> Metadata
(edit Buffer, take Request) -> Response
```

An omitted mode is value mode. Parameter names are documentation at the
declaration and are excluded from callable type identity in v1.

The historical `Fn[...]` form is removed rather than kept as an alias.
Migration diagnostics must provide a targeted fix from `Fn[A, R]` to
`A -> R` and from historical multi-argument forms to `(A, B, ...) -> R`.
Once migrated, `Fn` is an ordinary identifier. Function declarations retain
`->` before the result type; a bare Go-style result type is rejected.

## Numeric conversion

Planned rules:

- there are no implicit numeric conversions in either direction; a mixed-type
  arithmetic expression is a type error rather than a promotion
- one operator set is resolved per operand type, so there is no separate `+.`
  family for fractional values
- mixing a fractional type with `Int` in one expression is a type error —
  `Int + Float` does not promote
- `Int // Int -> Int`, taking the floor of the quotient
- the overflow mode is not changed implicitly between debug and release; it is stated explicitly in the build profile

`Int / Int` is a compile error today: `/` is not defined on `Int`, because with
no promotion it cannot produce a fractional value from two `Int` operands. It is
left without a meaning rather than given the truncating one, so it can be
defined later without silently changing any expression that compiles now.

`Decimal` and `Float` are types the checker knows (#710 slice 3): literals
carry them, `let` bindings carry them, annotations are checked against them,
and mixing two numeric types in one operator is a type error. No fractional
*arithmetic* is implemented — an expression that reaches lowering with a
`Decimal` or `Float` in it is refused, naming the slice that will evaluate it.

So `let x: Float = 0.5` is still rejected, but for the reason the type system
gives rather than for the compiler being unfinished: `0.5` is a `Decimal`, and
there is no implicit conversion to `Float`. `let x: Float = 0.5f64` passes the
checker and stops at lowering. Which fractional type `/` eventually takes is
#545's question.

```kofun
let exact = 7 // 2 # 3
let floor = -7 // 2 # -4, the floor rather than the truncation
# let ratio = 7 / 2 # compile error: `/` is not defined on Int
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

Executable checkpoint: the separate Stage 2 generic-function frontend
type-checks explicitly instantiated, unbounded direct calls such as
`identity[Int](42)`. It assigns each declaration-scoped type parameter a
stable identity, substitutes explicit type arguments before checking value
arguments, and preserves the original declaration identity and source spans
in typed IR. The checkpoint does not infer omitted arguments, accept generic
nominal types or bounds, select trait dictionaries, monomorphize, or emit
backend code; see `tests/conformance/generics/README.md`.

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

`trait` is the only public keyword for this abstraction. It describes a
compile-time contract and statically selected dictionary, not a runtime
interface or message-dispatch object. `protocol` and `interface` are not
aliases.

### Coherence and orphan rule

For an implementation of the form:

```kofun
impl Trait[Arguments] for SelfType {
    # methods
}
```

the implementing package must own either the trait or the outer nominal type
constructor of `SelfType`.

Ownership is based on stable declaration identity:

- a package owns a trait only when it declares that trait;
- a package owns a type only when it declares its outer nominal constructor;
- importing or re-exporting a declaration does not transfer ownership;
- a type alias does not create ownership;
- primitive types and imported C or Rust ABI types are foreign; and
- a locally declared nominal wrapper is local, even when its field or generic
  argument is foreign.

Generic arguments do not decide ownership. If the current package declares
`LocalBox[T]`, then `LocalBox[foreign.Handle]` is local because its outer
nominal constructor is `LocalBox`.

The resulting matrix is:

| Trait | Outer nominal type | Result |
| --- | --- | --- |
| local | local | accepted |
| local | foreign | accepted |
| foreign | local | accepted |
| foreign | foreign | rejected; introduce a local nominal wrapper |

A fully resolved trait/self tuple has at most one applicable implementation
across the complete dependency graph. Duplicate or overlapping candidates are
compile errors when declarations or dependency interfaces are combined.
Lexical order, import order, link order, and runtime state never choose a
winner.

M2-alpha rejects blanket implementations, negative implementations,
specialization, and ordered fallback. These forms must fail explicitly rather
than acquire provisional precedence. A later specialization design must be
versioned, preserve coherent dictionary identity, and leave programs with no
specialization semantically unchanged.

### Visibility and exported APIs

M2-alpha has no private, package-local, or lexical `impl` candidate. An
accepted implementation participates in dependency-graph coherence; hiding it
cannot create local precedence.

An exported signature may mention only public traits and public nominal types
under the normal visibility rules. A trait bound is part of that signature,
not an implementation detail. An implementation absent from the
consumer-visible semantic interface cannot satisfy a consumer's bound or
change how the consumer type-checks an exported API.

Visibility does not cause runtime implementation lookup. The compiler selects
one implementation from validated semantic interfaces and passes its
statically shaped dictionary.

### Worked examples

These examples state the design contract; traits are not yet accepted by the
active compiler.

```kofun
# This package owns Printable, so a foreign type may implement it.
trait Printable[T] {
    fn print_value(read value: T) -> Text
}

impl Printable[dependency.Widget] for dependency.Widget {
    # accepted: local trait
}

# This package owns LocalWidget, so it may implement a foreign trait.
type LocalWidget = {
    value: dependency.Widget,
}

impl dependency.Hash[LocalWidget] for LocalWidget {
    # accepted: local outer nominal type
}
```

The following direct implementation is rejected because both identities are
foreign:

```kofun
impl dependency.Hash[ffi.Handle] for ffi.Handle {
    # error: foreign trait for foreign type
}
```

The remedy is a local nominal wrapper, not an alias:

```kofun
type LocalHandle = {
    raw: ffi.Handle,
}

impl dependency.Hash[LocalHandle] for LocalHandle {
    # accepted: LocalHandle is local
}
```

If the trait-owning package and the type-owning package both publish an
implementation for the same fully resolved tuple, a consumer that combines
those interfaces reports overlap. Reordering the imports cannot select either
candidate.

### Implementation and law evidence identity

The selected dictionary is keyed by a stable `ImplementationId`. Its semantic
identity covers the implementing package, trait identity and canonical
arguments, canonical self type including its outer nominal identity and
arguments, implementation declaration/binders/constraints, and coherence
mode. The dictionary ABI version is carried with that identity in compiler
artifacts and cache keys. Source location, source order, import order, and
discovery order do not participate.

Trait declarations own their laws. Evidence for those laws is stored in a
separate versioned artifact and names the exact selected
`ImplementationId`. `LawEvidenceId` also commits to the law declaration,
evidence contract version, quantified type arguments, and semantic evidence
digest. Evidence for one implementation cannot be reused for a different
implementation merely because the surface types or method bodies look equal.
The assurance levels `bounded-exhaustive`, `proven-finite`, and `proven`
remain distinct.

Planned trait capabilities include generic traits, associated types, default
methods, and auto traits for send/share/copy. They do not weaken the coherence
rules above. The first implementation slice remains the concrete,
non-overlapping frontend described in
[`../spec/roadmap-31-34/generics-and-traits.md`](../spec/roadmap-31-34/generics-and-traits.md).

## Effects

Ordinary function syntax is kept, while the effect row is inferred.

Conceptual types:

```text
Text -> User
Path -> User ! {io, error[FsError]}
Url -> User ! {async, io, error[HttpError]}
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

- typed law-family, law-implementation, and law-check declarations
- compiler-integrated finite-model law evaluation and evidence emission
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

Historical `law monad` examples and v1 JSON artifacts document an earlier
bounded prototype, but the active CLI rejects that syntax. The accepted
concrete-first replacement is documented in
[`LAW_SYSTEM.md`](LAW_SYSTEM.md); its parser, evaluator, and v2 evidence
emitter remain unimplemented and do not require higher-kinded types.
