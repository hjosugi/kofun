# Kofun one-day tutorial

このtutorialは、8時間でbasic applicationとinterview algorithmを書ける状態を目標にする。

## Hour 1: Run and values

```bash
./bin/kofun run examples/hello.kf
```

```kofun
fn main() {
    let name = "Kofun"
    let answer = 40 + 2
    print(name)
    print(answer)
}
```

覚えること:

- entry pointは`fn main()`
- bindingは`let`
- semicolon不要
- stringはdouble quote

## Hour 2: Types and null

```kofun
let count: Int = 10
let ratio: Float = 0.5
let enabled: Bool = true
let title: Text = "report"
let missing: Int? = null
```

fallback:

```kofun
let effective = missing ?? 0
```

`null`はoptional typeにしか入らない。

## Hour 3: Functions and branches

```kofun
fn grade(score: Int) -> Text {
    return if score >= 90 {
        "A"
    } else if score >= 80 {
        "B"
    } else {
        "C"
    }
}
```

functionはvalueを返す。`if`もvalueを返せる。

## Hour 4: Lists and pipelines

```kofun
fn main() {
    let values = 1 .. 8
    let answer = values
        |> map(fn(x: Int) => x * x)
        |> filter(fn(x: Int) => x % 2 == 0)
        |> sum()

    print(answer)
}
```

覚えること:

- `0 .. 10`は10を含まない
- lambdaは`fn(x) => expression`
- `|>`は左のvalueを次のfunctionの先頭argumentへ渡す

## Hour 5: Loops and interview code

```kofun
fn binary_search(values: List[Int], target: Int) -> Int {
    let mut left = 0
    let mut right = len(values) - 1

    while left <= right {
        let middle = left + (right - left) // 2
        let value = values[middle]

        if value == target {
            return middle
        } else if value < target {
            left = middle + 1
        } else {
            right = middle - 1
        }
    }
    return -1
}
```

局所mutationは`let mut`で使える。`//`はinteger division。

## Hour 6: Ownership

```kofun
fn inspect(read socket: Resource) -> Bool {
    return is_open(socket)
}

fn consume(take socket: Resource) {
    print(inspect(socket))
}

fn main() {
    let own socket = resource("demo")
    print(inspect(socket))
    consume(socket)
}
```

覚えること:

- 普通のdataはGC-managed
- resourceは`let own`
- `read`は読むだけ
- `edit`はexclusive update
- `take`はownership transfer
- taken valueは再利用できない

## Hour 7: Scientific computing

```kofun
fn main() {
    let x = linspace(0.0, 1.0, 5)
    let y = vmul(x, x)

    print(mean(y))
    print(dot(x, y))
}
```

Stage 0ではList-based vector API。production designではunboxed N-dimensional arrayへ置き換える。

## Hour 8: Tools and tests

check:

```bash
./bin/kofun check examples/pipeline.kf
```

format:

```bash
./bin/kofun fmt -w examples/pipeline.kf
```

test file:

```kofun
# expect: 42
fn main() {
    print(40 + 2)
}
```

run:

```bash
./bin/kofun test tests/kofun
```

native subset:

```bash
./bin/kofun build examples/fibonacci_native.kf -o build/fibonacci
./build/fibonacci
```

Monad則をcompilerで確認:

```bash
./bin/kofun laws examples/proven_optional_bool_monad.kf \
  --require-assurance proven-finite \
  --output artifacts/optional-bool-monad.evidence.json
```

壊れた実装ではleft identity、right identity、associativityの反例がcompile errorになります。

## First-day cheat sheet

```text
fn name(args) -> Type { ... }
let x = value
let mut x = value
let own resource = open()
if ... { ... } else if ... { ... } else { ... }
for x in values { ... }
while condition { ... }
fn(x) => expression
value |> function(arg)
T?
null
value ?? fallback
read T
edit T
take T
law monad Name { ... }
```

## What not to learn on day one

- lifetime parameter
- raw pointer
- effect handler internals
- higher-kinded type encoding
- macro hygiene internals
- native ABI
- GC write barrier

これらは必要になった段階で学べるようにする。
