# Kofun one-day tutorial

This tutorial aims to get you to the point where you can write a basic
application and interview algorithms in eight hours.

## Hour 1: Run and values

```bash
./bin/kofun run examples/hello.kofun
```

```kofun
fn main() {
    let name = "Kofun"
    let answer = 40 + 2
    print(name)
    print(answer)
}
```

What to remember:

- the entry point is `fn main()`
- bindings use `let`
- no semicolons
- strings use double quotes

## Hour 2: Types and null

```kofun
let count: Int = 10
let ratio: Float = 0.5
let enabled: Bool = true
let title: Text = "report"
let missing: Int? = null
```

Fallback:

```kofun
let effective = missing ?? 0
```

`null` fits only into an optional type.

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

A function returns a value. `if` can return a value too.

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

What to remember:

- `0 .. 10` does not include 10
- a lambda is `fn(x) => expression`
- `|>` passes the value on the left as the first argument of the next function

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

Local mutation is available with `let mut`. `//` is integer division.

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

What to remember:

- ordinary data is GC-managed
- resources use `let own`
- `read` only reads
- `edit` is an exclusive update
- `take` transfers ownership
- a taken value cannot be used again

## Hour 7: Scientific computing

```kofun
fn main() {
    let x = linspace(0.0, 1.0, 5)
    let y = vmul(x, x)

    print(mean(y))
    print(dot(x, y))
}
```

Stage 0 has a List-based vector API. The production design calls for replacing
it with unboxed N-dimensional arrays.

## Hour 8: Tools and tests

check:

```bash
./bin/kofun check examples/pipeline.kofun
```

format:

```bash
./bin/kofun fmt -w examples/pipeline.kofun
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
./bin/kofun build examples/fibonacci_native.kofun -o build/fibonacci
./build/fibonacci
```

Check the monad laws with the compiler:

```bash
./bin/kofun laws examples/proven_optional_bool_monad.kofun \
  --require-assurance proven-finite \
  --output artifacts/optional-bool-monad.evidence.json
```

For a broken implementation, counterexamples to left identity, right identity,
and associativity become compile errors.

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

These are meant to be learnable at the point where you actually need them.
