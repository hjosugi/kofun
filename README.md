# Kofun

Kofunは、次の相反しやすい目標を一つの言語設計にまとめる実験的プログラミング言語です。

- Rust由来のメモリ安全性とデータ競合防止
- GCによる普段使いの簡単さ
- `read` / `edit` / `take`による、記号の少ない所有権
- Haskell、Gleam、ML系に近い関数型プログラミング
- Python、Juliaに近い科学計算と対話実行
- coding interviewで迷わない標準コレクションと制御構文
- TypeScriptのように試しやすく、Haskellのように型を表現できる型システム
- Monadなどの代数則をcompilerが検査し、反例をcompile errorとして返すlaw system
- Lisp的なquote/unquoteと、型付き・衛生的なメタプログラミング
- Goのような単一CLIと充実した標準ツール
- 最終的なself-hostingとreproducible bootstrap
- AOTコンパイル、unboxed values、specialization、SIMDによるC/Rust級性能の追求

`Kofun`は現時点では**ワーキングタイトル**です。同名のRust crateなどが存在するため、公開前に名称・商標・package namespaceを再調査します。詳しくは[docs/NAMING.md](docs/NAMING.md)を参照してください。

## このリポジトリに含まれるもの

このリポジトリは構想だけではありません。Stage 0として、次を実行できます。

- UTF-8 lexer
- Pratt parser
- 静的型検査
- `Int?`と`null`
- `??`
- immutable-by-defaultと`let mut`
- `let own`、`read`、`edit`、`take`
- use-after-takeの静的検出
- 再帰関数、ラムダ、closure
- `if` expressionと`else if`
- `for`、`while`、range、indexing
- pipeline operator `|>`
- List向け`map`、`filter`、`fold`
- 科学計算用の基礎vector関数
- REPL
- formatter
- test runner
- project generator
- **直接x86-64機械語backend**（C・clang・`as`・`ld`・libcを一切使わない。static ELFを自力生成）
- 数値・制御構文向けC11 AOT backend
- `law monad`のcompiler-integrated law checker
- complete finite carrier向け`proven-finite` verification
- `kofun.law-evidence/v1`形式のJSON evidenceとassurance gate
- Kofun自身で書かれ、Stage 0からnative buildできるStage 1 compiler seed
- Python unit tests
- 13,500件の実装backlog

ただし、これはproduction compilerではありません。現在のreference interpreterはPythonのGCを使い、両backendとも数値中心のsubsetです。native backendは機械語を直接吐きますが**最適化を一切行いません**（single pass、register allocatorなし）。そのため実行速度は`cc -O3`経由に約3.5倍劣ります。完全なtracing GC、optimizer、ADT、match、generics、traits、effect rows、typed macrosは設計済みですが未実装です。
Stage 1 compiler seedは動作しますが、Stage 1が自分自身を再compileするStage 2 fixed pointはまだ未達です。

## 30秒で試す

必要環境:

- Python 3.11以上
- **native buildにC compilerは不要です。** `--backend c`を使う場合のみ`cc`、`clang`、または`gcc`

インストールなしで実行できます。

```bash
cd kofun-lang
./bin/kofun run examples/hello.kf
./bin/kofun run examples/pipeline.kf
./bin/kofun run examples/science.kf
```

型検査:

```bash
./bin/kofun check examples/ownership.kf
```

Monad則の検査:

```bash
./bin/kofun laws examples/lawful_list_monad.kf
./bin/kofun laws examples/proven_optional_bool_monad.kf \
  --require-assurance proven-finite \
  --output build/optional-bool-monad.evidence.json
```

Kofunで書かれたStage 1 compiler seedの検証:

```bash
PYTHONPATH=src python3 bootstrap/check_bootstrap.py
```

直接機械語へのbuild（既定。外部toolchain不要）:

```bash
./bin/kofun build examples/fibonacci_native.kf -o build/fibonacci
./build/fibonacci                  # 6765
file build/fibonacci               # ELF 64-bit, statically linked
```

452バイトのstatic ELFが生成されます。C compilerもlinkerも動きません。詳細は[docs/NATIVE_BACKEND.md](docs/NATIVE_BACKEND.md)を参照してください。

C11経由のbuild（対応機能は多いがC compilerが必要）:

```bash
./bin/kofun build examples/fibonacci_native.kf --backend c -o build/fibonacci
./build/fibonacci
```

生成Cを確認する場合:

```bash
./bin/kofun build examples/fibonacci_native.kf \
  -o build/fibonacci \
  --emit-c build/fibonacci.c
```

### backendの比較（実測、best of 5）

| | native | C11 + `cc -O3` |
|---|---|---|
| build（全体） | 111 ms | 345 ms |
| codegenのみ | **0.13 ms** | ~200 ms |
| binary size | **452 bytes** | 22,320 bytes |
| linkage | **static, 依存なし** | dynamic, libc必要 |
| `fib(30)`実行 | 7 ms | **2 ms** |

buildはnativeが圧勝、実行は`-O3`が3.5倍速い、というのが現状の正直な数字です。native backendにoptimizerが無いためで、これはissueとして追跡しています。

テスト:

```bash
make test
make verify
```

editable installも可能です。

```bash
python3 -m pip install -e .
kofun --version
```

## 言語の見た目

```kofun
fn classify(score: Int) -> Text {
    return if score >= 90 {
        "excellent"
    } else if score >= 70 {
        "good"
    } else {
        "keep going"
    }
}

fn main() {
    let values = 1 .. 8
    let answer = values
        |> map(fn(x: Int) => x * x)
        |> filter(fn(x: Int) => x % 2 == 0)
        |> sum()

    let missing: Int? = null
    let fallback = missing ?? 42

    print(classify(82))
    print(answer)
    print(fallback)
}
```

意図して採用した表記:

| 目的 | Kofun |
|---|---|
| function | `fn` |
| immutable binding | `let x = ...` |
| mutable binding | `let mut x = ...` |
| null value | `null` |
| optional type | `Int?` |
| fallback | `value ?? fallback` |
| branch | `else if` |
| integer division | `//` |
| pipeline | `|>` |
| generic | `List[Int]` |
| lambda | `fn(x) => x + 1` |

`nil`、`None`、`elif`は使いません。semicolonは不要です。

## 分かりやすい所有権

Rustの`&T`、`&mut T`、move、lifetime annotationを、日常コードでより直接読める語に置き換えます。

```kofun
fn inspect(read file: File) -> Metadata {
    # read-only, non-owning view
}

fn rewrite(edit file: File, bytes: Bytes) {
    # exclusive mutable view
}

fn upload(take file: File) -> Receipt {
    # ownership moves into this function
}
```

local resourceは`let own`で宣言します。

```kofun
fn main() {
    let own socket = Socket.connect("example.com:443")
    print(socket.peer())
    send(take socket)

    # compile error: socket was already taken
    # print(socket.peer())
}
```

基本モデル:

1. 普通の値はimmutableかつGC-managedで、Pythonのように扱える。
2. file、socket、lock、GPU bufferなどは`own`でdeterministic cleanupする。
3. `read T`はread-only view。
4. `edit T`はexclusive mutable view。
5. `take T`はownership transfer。
6. v1のviewはnon-escapingなので、通常コードにlifetime parameterを書かない。
7. shared ownershipが必要な場合だけ`share(value)`へ明示的に昇格する。

詳細は[docs/MEMORY_MODEL.md](docs/MEMORY_MODEL.md)を参照してください。

## FPを中心にするが、面接コードも書きやすくする

普段はimmutable dataとpipelineを使います。

```kofun
let score = numbers
    |> filter(fn(x) => x > 0)
    |> map(fn(x) => x * x)
    |> sum()
```

一方、binary searchなどでは局所的mutationを普通に使えます。

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

「FPであるために不自然な面接回答になる」ことは避けます。pure core、immutable-by-default、ADT、pattern matching、pipelineを中心にしつつ、algorithmic codeに必要なloop、index、mutable localも標準機能にします。

## 科学計算

Stage 0には、小さなvector APIを含みます。

```kofun
fn main() {
    let x = linspace(0.0, 1.0, 5)
    let squared = vmul(x, x)

    print(mean(squared))
    print(dot(x, squared))
}
```

長期設計では次を標準science stackとして扱います。

- unboxed N-dimensional arrays
- slicingとstrides
- broadcasting
- `a + b`のelementwise演算
- `a @ b`のmatrix multiplication
- BLAS/LAPACK/FFT interop
- SIMD、multithreading、GPU kernels
- automatic differentiation
- statistics、random distributions
- data framesと`T?`によるmissing data
- notebook protocol

詳細は[docs/SCIENTIFIC_COMPUTING.md](docs/SCIENTIFIC_COMPUTING.md)を参照してください。

## 型システムの方向性

目標は「annotationを書かなくても始められ、必要ならかなり深く遊べる」です。

```kofun
fn identity[T](value: T) -> T = value

type Shape =
    | Circle(radius: Float)
    | Rect(width: Float, height: Float)

trait Show[T] {
    fn show(read value: T) -> Text
}
```

予定する中核:

- local type inference
- generics
- algebraic data types
- exhaustive pattern matching
- traitsとassociated types
- structural recordsとrow polymorphism
- union/intersection typesを境界用途に限定して提供
- effect rows
- const generics
- type-level functions
- typed, hygienic macros

高度な型機能が、普通の`fn main()`を難しくしないことを設計原則にします。

## Monad則をcompilerで検査する

Stage 0では`law monad`宣言を通常のcompile pathへ統合しています。

```kofun
law monad ListMonad {
    pure = list_pure
    bind = list_bind
    values = [-1, 0, 1]
    monads = [[], [0], [1, -1]]
    functions = [keep, increment, duplicate]
}
```

compilerはleft identity、right identity、associativityを、宣言されたfinite modelの全組合せで確認します。失敗した場合は`L101`〜`L103`と具体的な反例を返します。

検証labelを混同しません。

| Label | Meaning |
|---|---|
| `bounded-exhaustive` | 宣言された有限sampleの全組合せを検査 |
| `proven-finite` | compiler-knownな完全有限carrierと全関数空間を検査 |
| `proven` | 将来のproof kernelでgeneric proof termを検査 |

`Optional[Bool]`では`Bool -> Optional[Bool]`の9個のtotal functionsをcompilerが生成し、264 casesを網羅できます。ただしこれは`Optional[Bool]`に対する有限普遍証明であり、generic `Optional[A]`全体の証明ではありません。

CIやoptimizer向けには、検査結果をversioned JSON artifactとして保存できます。

```bash
./bin/kofun laws examples/proven_optional_bool_monad.kf \
  --json \
  --require-assurance proven-finite
```

schema IDは`kofun.law-evidence/v1`です。source SHA-256、compiler version、assurance、model digest、case数、diagnostics、counterexamplesを含みます。`kofun check`と`kofun build`にも`--require-law-assurance`があり、要求より弱いevidenceを`L200`でrejectします。

このZIPには実際に生成・再検証された[OptionalBoolMonad evidence](artifacts/optional-bool-monad.evidence.json)も同梱しています。
全体の検証結果は[verification summary](artifacts/verification-summary.json)にも機械可読で記録しています。

詳細は[docs/LAW_SYSTEM.md](docs/LAW_SYSTEM.md)を参照してください。

## メタプログラミング

Lispの楽しさは取り入れますが、文字列置換macroにはしません。

```kofun
meta fn derive_json(info: TypeInfo) -> Decl {
    quote {
        impl Json[$(info.type)] {
            fn encode(read value: $(info.type)) -> Text {
                $(generate_encoder(info))
            }
        }
    }
}

@derive(json)
type User = {
    id: Int,
    name: Text,
}
```

設計条件:

- hygienic
- typed AST
- deterministic
- sandboxed
- source spanを保持
- expansionを`kofun expand`で確認可能
- build cacheを壊さない

## 単一CLI

最終的には、GoやRustのtoolchainと同様に一つのcommandで基本作業を完結させます。

```text
kofun new
kofun run
kofun check
kofun laws
kofun build
kofun test
kofun fmt
kofun lint
kofun doc
kofun bench
kofun repl
kofun explain E330
kofun expand
kofun profile
```

Stage 0で実装済みなのは`new`、`run`、`check`、`laws`、`build`、`test`、`fmt`、`repl`、`ast`です。

## コンパイラ計画

```text
source
  -> lossless syntax tree
  -> resolved HIR
  -> typed HIR + effects + ownership
  -> law evidence checking
  -> MIR / CFG
  -> SSA IR
  -> optimization
  -> native backend / VM / Wasm
```

Stage 0:

```text
source
  -> Python lexer/parser/type checker
  -> bounded/proven-finite Monad law checker
  -> reference interpreter
  -> restricted C11 backend
  -> cc -O3
```

production compilerでは、bootstrap frontendを自己ホスト実装へ置き換え、LLVMまたはCranelift級backendを利用できる構造にします。backendは交換可能にし、interpreterとのdifferential testを必須にします。

現在のbootstrap statusは次です。

```text
Stage 0 Python toolchain       working
Stage 1 native Kofun seed     working seed
Stage 2 self-recompile        open
```

Stage 0はStage 1 sourceをtype-checkし、interpreterで実行できるだけでなく、C11経由でnative compilerへbuildできます。interpreted Stage 1とnative Stage 1が同じfixtureから生成するC11はbyte-for-byteで一致します。ただしStage 1自身をStage 1でcompileするStage 2 gateはopenです。したがって、現時点では「native Kofun-written compiler seed」であり、full self-hosting済みとは表現しません。詳細は[docs/SELF_HOSTING.md](docs/SELF_HOSTING.md)を参照してください。

「C/Rustに近い速度」は現在の達成事実ではなく、次の仕組みで追う性能目標です。

- primitive valueのunboxing
- monomorphizationとspecialization
- escape analysis
- owned allocationとin-place reuse
- bounds-check elimination
- map/filter fusion
- SIMD vectorization
- profile-guided optimization
- native BLAS/GPU dispatch
- no-GC / embedded profile

## Backlog

[backlog/README.md](backlog/README.md)に**13,500件**あります。

構成:

- 27領域
- 各領域25 subject
- 各subject 20 lifecycle issues
- 1領域500件
- 合計13,500件

検証:

```bash
python3 scripts/verify_backlog.py
```

検証scriptは次を確認します。

- 13,500件あること
- IDが`KOFUN-00001`から`KOFUN-13500`まで連続すること
- IDが重複しないこと
- fingerprintが重複しないこと
- 各area fileに500件あること
- 必須cellが空でないこと

## Repository layout

```text
kofun-lang/
├── bin/kofun                 # zero-install launcher
├── src/kofun/                # lexer, parser, checker, law checker, interpreter, C backend, CLI
├── bootstrap/                # Kofun-written Stage 1 seed and bootstrap gates
├── artifacts/                # checked law-evidence examples
├── examples/                 # runnable programs
├── tests/                    # Python unit tests and .kf language tests
├── docs/                     # language and implementation design
├── spec/                     # grammar and normative draft
├── benchmarks/               # native/interpreter benchmark seeds
├── backlog/                  # 13,500 generated issues
├── scripts/                  # backlog and repository verification
├── editor/vscode/            # basic editor metadata
└── Makefile
```

## Current limitations

- reference implementationはPython上で動く
- ordinary heap valuesはPython GCを利用する
- `read` / `edit` / `take`はStage 0では局所的なaffine analysis
- C backendはprimitive numerics、Bool、Text、control flow、direct callsが中心
- C backendのList対応はStage 1に必要な`List[Text]` runtimeだけ。generic List、closure、GC tracing、resource ownershipは未実装
- parserはfull lossless treeではない
- formatterは保守的なindent formatter
- module/package systemは未実装
- ADT、match、generics、traits、effects、macrosは未実装
- law checkerは現在Monadのみ。generic proof kernelは未実装
- `proven-finite`は現在Bool/Optional[Bool] carrierのみ
- Stage 2 self-recompile fixed pointは未完了
- security hardeningとproduction fuzzingは未完了

実装済み／未実装の詳細は[docs/MVP_IMPLEMENTED.md](docs/MVP_IMPLEMENTED.md)にあります。

## License

[Apache License 2.0](LICENSE-APACHE)または[MIT License](LICENSE-MIT)のdual licenseです。
