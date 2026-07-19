# Law system

## 目的

Cofnでは、`Monad`、`Functor`、`Monoid`のような抽象を「method signatureだけ合えば実装完了」としません。
型クラスやtraitが要求する代数則をcompiler artifactとして扱い、次を実現する設計です。

- lawをsource codeに宣言する
- compilerが実装と有限modelを評価する
- 失敗したlawと具体的な反例をcompile errorにする
- 検証の強さをlabelで区別する
- optimizerは必要なevidence levelを満たす場合だけlaw-based rewriteを使う
- 将来はsmall proof kernelでgeneric proof termを検査する

Stage 0では`Monad`の3法則を実装しています。

## 実装済み構文

```cofn
fn list_pure(value: Int) -> List[Int] {
    return [value]
}

fn list_bind(values: List[Int], next: Fn[Int, List[Int]]) -> List[Int] {
    return fold(
        values,
        [],
        fn(acc: List[Int], value: Int) => concat(acc, next(value))
    )
}

fn keep(value: Int) -> List[Int] {
    return [value]
}

fn duplicate(value: Int) -> List[Int] {
    return [value, value]
}

law monad ListMonad {
    pure = list_pure
    bind = list_bind
    values = [-1, 0, 1]
    monads = [[], [0], [1, -1]]
    functions = [keep, duplicate]
    limit = 1000
}
```

確認:

```bash
./bin/cofn laws examples/lawful_list_monad.cofn
```

出力例:

```text
law ListMonad: passed monad; assurance=bounded-exhaustive; cases=105/105; model=...
```

`cofn check`、`cofn run`、`cofn build`もlaw checkingを実行します。law違反があるprogramは通常のcompile pathを通過しません。

## Compilerが検査するMonad則

`pure`を`return`、`bind(m, f)`を`m >>= f`と考えます。

### Left identity

```text
bind(pure(a), f) == f(a)
```

### Right identity

```text
bind(m, pure) == m
```

### Associativity

```text
bind(bind(m, f), g)
==
bind(m, fn(x) => bind(f(x), g))
```

Stage 0は次の全組合せを走査します。

```text
left identity:  values × functions
right identity: monads
associativity:  monads × functions × functions
```

総case数は次です。

```text
|values| × |functions|
+ |monads|
+ |monads| × |functions|²
```

`limit`を超える場合は`L004`で停止します。compiler resource exhaustionを避けるため、defaultは100,000 casesです。

## Assurance levels

### `bounded-exhaustive`

宣言された有限sampleの全組合せを検査済みです。

これはrandom property testより強い一方、sample外まで含む普遍証明ではありません。compilerは`proven`と表示しません。

例:

```text
values = [-2, -1, 0, 1, 2]
monads = [[], [0], [1], [-1, 2]]
functions = [keep, increment, duplicate, discard]
```

上記model内では全件を検査しますが、任意長の`List[Int]`すべてを証明したことにはなりません。

### `proven-finite`

compilerがcarrierの完全性と全関数空間を確認できる有限型では、その有限carrierに対する普遍証明として扱います。

Stage 0は次を認識します。

- value carrier: complete `Bool` = `[false, true]`
- monadic carrier: complete `Bool`またはcomplete `Optional[Bool]`
- functions: `finite_functions(values, monads)`が生成した全total functions

```cofn
fn optional_pure(value: Bool) -> Bool? {
    return value
}

fn optional_bind(value: Bool?, next: Fn[Bool, Bool?]) -> Bool? {
    return if value == null {
        null
    } else {
        next(unwrap(value))
    }
}

law monad OptionalBoolMonad {
    pure = optional_pure
    bind = optional_bind
    values = [false, true]
    monads = [null, false, true]
    functions = finite_functions([false, true], [null, false, true])
    complete = true
}
```

`Bool -> Optional[Bool]`のtotal functionは`3² = 9`通りです。compilerはその9関数を生成し、次の264 casesを検査します。

```text
2 × 9 + 3 + 3 × 9² = 264
```

```bash
./bin/cofn laws examples/proven_optional_bool_monad.cofn
```

```text
law OptionalBoolMonad: passed monad; assurance=proven-finite; cases=264/264; model=...
```

`complete = true`はuser assertionとして信用されません。carrierが欠けている、重複している、または`functions`が`finite_functions`由来でない場合、`L008`でrejectします。

この証明は`Optional[Bool]`という有限instanceに対して普遍的です。genericな`Optional[A]`全体の証明ではありません。

## Evidence artifactとcompiler gate

law checkerの結果はhuman-readable textだけでなく、CI、package registry、optimizerが読めるversioned JSONとして出力できます。

```bash
./bin/cofn laws examples/proven_optional_bool_monad.cofn \
  --require-assurance proven-finite \
  --output build/optional-bool-monad.evidence.json
```

stdoutへJSONを出す場合:

```bash
./bin/cofn laws examples/proven_optional_bool_monad.cofn --json
```

artifact schemaは`cofn.law-evidence/v1`です。JSON Schemaは[spec/law-evidence.schema.json](../spec/law-evidence.schema.json)にあります。

主要field:

```text
compiler name/version
source path/SHA-256
overall status
required assurance
assurance violations
diagnostics and source spans
law family/status/assurance
cases checked/planned
model digest
counterexamples
```

compile gate:

```bash
./bin/cofn check module.cofn --require-law-assurance proven-finite
./bin/cofn build module.cofn --require-law-assurance proven-finite
```

宣言されたlawのassuranceが要求levelより弱い場合、`L200`でrejectします。これにより「law testは存在するがrelease buildでは確認されていない」という状態を避けます。

CofnがHaskell系の抽象を超えるための狙いは、型class相当のmethod shapeだけでなく、law evidenceをcompiler artifact、build policy、optimizer preconditionへ接続することです。ただしStage 0で普遍証明済みなのはcompiler-knownな有限carrierだけであり、generic proof kernelは未実装です。

### 将来の`proven`

generic lawにはproof termをsmall kernelで検査する予定です。kernel外のSMT solverやproof searchは、proof certificateを生成できても最終的な信用根にはなりません。

提案中の構文イメージであり、Stage 0では未実装です。

```cofn
lawful trait Monad[M[_]] {
    fn pure[A](value: A) -> M[A]
    fn bind[A, B](value: M[A], next: Fn[A, M[B]]) -> M[B]

    law left_identity(a, f) {
        bind(pure(a), f) == f(a)
    }

    law right_identity(m) {
        bind(m, pure) == m
    }

    law associativity(m, f, g) {
        bind(bind(m, f), g)
            == bind(m, fn(x) => bind(f(x), g))
    }
}

impl Monad for Optional {
    # implementation

    prove laws with OptionalMonadProof
}
```

予定しているproof pipeline:

```text
source law
  -> typed proposition
  -> normalization
  -> proof search / SMT / user proof
  -> compact certificate
  -> trusted proof kernel
  -> evidence artifact
```

## Counterexample diagnostics

壊れた`pure`:

```cofn
fn broken_pure(value: Int) -> List[Int] {
    return [value, value]
}
```

compile error:

```text
error[L101]: monad law `BrokenListMonad` failed left identity
for a=0, f=keep: left=[0, 0], right=[0]
```

code:

| Code | Meaning |
|---|---|
| `L004` | case count exceeds declared limit |
| `L005` | model entry is not a List |
| `L006` | finite carrier is empty |
| `L008` | `complete = true` could not be certified |
| `L200` | assurance is below the compiler/CI requirement |
| `L101` | left identity failure |
| `L102` | right identity failure |
| `L103` | associativity failure |

反例はstructural size、rendered length、lexical orderで小さいcaseから試します。Stage 0のshrinkingは簡易版であり、将来はtype-directed shrinkingへ置き換えます。

## Deterministic sandbox

law checkingはcompile-time executionです。そのためStage 0 law evaluatorでは次を無効化します。

- `print`
- `debug`
- `clock_ms`
- `args`
- `read_text`
- `write_text`

law implementationがeffectを呼ぶとcompile errorになります。将来はbytecode verifier、instruction budget、allocation budget、capability-based macro sandboxを追加します。

## Law-aware optimization policy

lawを使ったrewriteにはrequired evidenceを持たせます。

| Rewrite scope | Required evidence |
|---|---|
| test build warning | `bounded-exhaustive` |
| concrete finite specialization | matching `proven-finite` |
| generic semantic rewrite | `proven` |
| cross-package rewrite | `proven` plus compatible evidence ABI |

たとえばgeneric `bind(pure(x), f) -> f(x)`を、bounded sampleだけを根拠に最適化してはいけません。

## Stage 0の限界

- law familyは`Monad`のみ
- higher-kinded typesとlawful traitsは未実装
- `proven-finite` carrierはBool系のみ
- generic proof term kernelは未実装
- user-defined finite ADTの自動enumerationは未実装
- effect rowによるpurity enforcementは未実装
- custom equalityのtotality proofは未実装
- model checkerはsingle-process

実装backlogは[backlog/issues-24-laws.md](../backlog/issues-24-laws.md)に500件あります。全体のID rangeは[backlog/README.md](../backlog/README.md)がsource of truthです。
