# Type system

## Design target

Cofnの型システムは、二つの入口を持つ。

1. beginnerはannotationなしでlocal programを書ける。
2. advanced userはADT、traits、effects、row polymorphism、type-level computationを使える。

難しい型機能を使わないコードに、その複雑さを漏らさない。

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

`String`はaliasとして提供する可能性があるが、canonical nameは`Text`とする。

## Optional types

```cofn
let age: Int? = null
let safe_age = age ?? 0
```

rule:

- `null`は`T?`にのみ代入できる
- `T`へimplicit null injectionしない
- `T?`から`T`へimplicit conversionしない
- `??`、pattern matching、guardでnarrowingする

予定するpattern:

```cofn
match user.name {
    null => "anonymous"
    name => name
}
```

`None`や`Nil`という別constructorはoptional用途では使わない。domain-specific ADTでは任意のconstructor名を使える。

## Type inference

```cofn
let count = 42          # Int
let ratio = 0.5         # Float
let names = ["a", "b"] # List[Text]
```

推論範囲:

- local binding
- return type
- lambda parameter when call context exists
- generic arguments
- effects
- optional branch joins

public APIでは、stabilityとdocumentationのためannotationを推奨する。

## Numeric conversion

予定rule:

- widening conversionは限定的にimplicit
- narrowing conversionはexplicit
- `Int + Float -> Float`
- `Int / Int -> Float`
- `Int // Int -> Int`
- overflow modeはdebug/releaseで暗黙に変えず、build profileで明示する

```cofn
let exact = 7 // 2 # 3
let ratio = 7 / 2  # 3.5
```

## Generics

angle bracketではなくsquare bracketを使う。

```cofn
fn identity[T](value: T) -> T = value

type Pair[A, B] = {
    first: A,
    second: B,
}
```

理由:

- comparison operatorとのlexer ambiguityを減らす
- `List[Int]`がPython/TypeScript userにも読める
- type applicationとindexingはparser contextで区別できる

## Algebraic data types

```cofn
type Tree[T] =
    | Empty
    | Node(value: T, left: Tree[T], right: Tree[T])
```

pattern matching:

```cofn
fn size[T](tree: Tree[T]) -> Int {
    return match tree {
        Empty => 0
        Node(_, left, right) => 1 + size(left) + size(right)
    }
}
```

compilerはexhaustivenessとunreachable patternを検査する。

## Records

nominal record:

```cofn
type User = {
    id: Int,
    name: Text,
}
```

structural record boundary:

```cofn
fn render(user: { name: Text, ..R }) -> Text
```

row polymorphismはJSON、web API、data frame、testing doubleで有用だが、layout-sensitive system APIではnominal typeを優先する。

## Union and intersection types

TypeScriptの表現力は取り入れるが、uncontrolled union explosionは避ける。

```cofn
type Input = Text | Bytes
```

主な用途:

- external data boundary
- gradual migration
- generated API bindings
- pattern narrowing

internal domain modelではADTを推奨する。

intersection typeはcapability compositionなど限定用途にする。

## Traits

```cofn
trait Eq[T] {
    fn equals(read left: T, read right: T) -> Bool
}

trait Iterator[I, Item] {
    fn next(edit iterator: I) -> Item?
}
```

features:

- generic traits
- associated types
- default methods
- auto traits for send/share/copy
- coherence
- orphan rule
- specializationは明示的で限定的

## Effects

普通の関数syntaxを維持しつつ、effect rowを推論する。

概念型:

```text
fn parse(Text) -> User
fn load(Path) -> User ! {io, error[FsError]}
fn fetch(Url) -> User ! {async, io, error[HttpError]}
```

source上でeffect annotationを毎回書く必要はない。public API、trait contract、no-effect保証では明示できる。

```cofn
pure fn normalize(value: Float) -> Float
```

`pure` keywordを採用するかは、effect inferenceとdiagnostic UXを検証して決める。

## Result and error propagation

```cofn
fn load_user(path: Path) -> Result[User, LoadError] {
    let text = File.read_text(path)?
    return Json.decode[User](text)?
}
```

`?`はoptional suffixとの文脈衝突をparserが解決する。

errorはtype parameterとして保持でき、context追加APIを標準化する。

## Ownership in types

parameter modeはtype constructorではなくcall contractとして表す。

```cofn
fn hash(read bytes: Bytes) -> Digest
fn fill(edit buffer: Buffer) -> Void
fn submit(take request: Request) -> Response
```

これにより、`&T`、`&mut T`、explicit lifetimeの記号負荷を減らす。

advanced APIではview lifetimeを型レベルに公開できるが、standard user codeには出さない。

## Const generics and shapes

```cofn
fn dot[N](left: Array[Float, N], right: Array[Float, N]) -> Float
```

N-dimensional arraysではrankと一部shapeをcompile-time valueとして扱う。

ただしdynamic shapeも第一級にする。

```cofn
Array[Float, rank = 2]
DynArray[Float]
```

## Type-level functions

```cofn
type fn OutputShape[A, B] = Broadcast[A, B]
```

制約:

- terminationまたはfuel limit
- deterministic
- no IO
- good diagnostics
- cacheable

## Stage 0 implementation

実装済み:

- `Int`、`Float`、`Bool`、`Text`、`Null`、`Void`、`Any`
- `List[T]`、Tuple
- `T?`
- basic function types
- local inference
- numeric promotion
- branch/list joins
- built-in polymorphic behaviorの一部
- `read` / `edit` / `take` parameter metadata
- typed `law monad` declarations and compiler-integrated law checking
- evidence distinction between `bounded-exhaustive` and `proven-finite`

未実装:

- user-defined generics
- ADT、match
- traits
- union/intersection
- row polymorphism
- effect rows
- const generics
- type-level functions
- principal-type guarantee
- higher-kinded types and lawful traits
- generic proof terms and trusted proof kernel
