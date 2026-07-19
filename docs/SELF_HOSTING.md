# Self-hosting and bootstrap

## 完了条件

Cofn compilerを「自己ホスト済み」と呼ぶための最低条件は次です。

1. Stage 0 compilerがCofnで書かれたStage 1 compilerをbuildできる。
2. Stage 1 compilerが自分自身のsourceをbuildできる。
3. 生成されたStage 2 compilerが同じsourceを再buildできる。
4. Stage 1とStage 2の意味的artifactが一致する。
5. conformance、law checking、ownership checking、backend differential testsが両方で一致する。

現在は1が動作しています。Stage 0はStage 1 sourceをC11へlowerし、native compiler executableをbuildできます。2以降、特にStage 2 fixed pointは未達です。

## 現在のstage

`bootstrap/manifest.json`が機械可読なstatusです。

| Stage | Implementation | Status |
|---|---|---|
| Stage 0 | Python reference toolchain | working |
| Stage 1 | `bootstrap/stage1/compiler.cofn` | working native seed |
| Stage 2 | Stage 1 self-recompile | open |

Stage 0には次が含まれます。

- lexer
- Pratt parser
- static type checker
- affine ownership checker prototype
- compiler-integrated Monad law checker
- reference evaluator
- formatter
- C11 backend
- CLI

Stage 1 seedはCofnで書かれています。現在は次のCofn Core subsetをC11へ変換します。

```cofn
fn main() {
    print((6 + 1) * 6)
}
```

対応expression:

- integer digits
- whitespace
- `+`
- `-`
- `*`
- `/`
- `%`
- parentheses

Stage 1 seedはsourceを読み、expressionを検証し、standalone C11を出力します。Stage 0 interpreter上でも動き、Stage 0 C11 backendからnative executableとしてbuildしても動きます。

machine-readable gate status:

| Gate | Status |
|---|---|
| Stage 0 type-checks Stage 1 | working |
| Stage 0 interprets Stage 1 | working |
| Stage 0 builds native Stage 1 | working |
| interpreted/native Stage 1 output equivalence | working |
| Stage 1 compiles its own source | open |
| Stage 1/Stage 2 fixed-point equivalence | open |
| diverse double compilation | open |

## Bootstrap test

```bash
PYTHONPATH=src python3 bootstrap/check_bootstrap.py
```

実行内容:

```text
Stage 0 parses and type-checks bootstrap/stage1/compiler.cofn
  -> Path A: Stage 0 interpreter runs the Cofn-written compiler
  -> Path B: Stage 0 C11 backend builds a native Stage 1 compiler
  -> both Stage 1 paths compile bootstrap/fixtures/answer.cofn
  -> generated C11 artifacts must be byte-for-byte identical
  -> host C compiler builds the generated C11
  -> native program runs
  -> output must be 42
```

true self-hostingをrelease gateとして要求する場合:

```bash
PYTHONPATH=src python3 bootstrap/check_bootstrap.py --require-stage2
```

現在このcommandは意図どおりfailureになります。`bootstrap/manifest.json`のStage 2 statusが`open`だからです。

## Stage 2へ必要な実装

Stage 1を自分自身をcompileできる範囲まで拡張します。

```text
UTF-8 lexer
  -> parser
  -> name resolution
  -> type inference/checking
  -> ownership checking
  -> law checking
  -> HIR/MIR lowering
  -> C11 or native backend
  -> linker driver
```

必要なlanguage surface:

- ADTまたはcompiler用tagged records
- exhaustive `match`
- generic List/Map
- Text builder
- file and process APIs
- structured diagnostics
- module/import system
- deterministic iteration
- stable serialization

## Artifact comparison

raw binary byte equalityだけを必須にすると、timestamp、path、object orderの差でfalse negativeになります。比較は段階化します。

1. normalized HIR hash
2. normalized MIR hash
3. target-independent symbol graph
4. reproducible object bytes
5. executable behavior and conformance corpus

production bootstrapでは`SOURCE_DATE_EPOCH`相当、path remapping、deterministic archive orderを必須にします。

## Trusted computing base

現在のTCB:

- Python interpreter
- Stage 0 Python source
- host C compiler and linker
- operating system process/filesystem layer

self-hosting後もbootstrap attackを避けるため、次を計画します。

- source-to-binary provenance manifest
- compiler artifact signatures
- reproducible builds
- diverse double compilation
- two independent bootstrap paths
- generated C inspection mode
- minimal seed archive
- offline bootstrap test

## Honest status rule

次の表現を区別します。

| Label | Meaning |
|---|---|
| Cofn-written compiler seed | compiler codeの一部がCofnで動く |
| native Cofn-written compiler seed | Stage 0がCofn compiler seedをnative executableへbuild可能 |
| self-hosting frontend | frontend自身をCofnでbuild可能 |
| self-hosting compiler | Stage 1が自分自身をbuild可能 |
| fixed-point bootstrap | Stage 1/Stage 2 artifact equivalenceを確認済み |

このrepositoryの現在のlabelは**native Cofn-written compiler seed**です。Stage 1 self-recompileがopenなので、self-hosting compilerまたはfixed-point bootstrapとは呼びません。
