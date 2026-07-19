# Compiler architecture

## Architecture goals

- reference semanticsとoptimized backendを分離する
- compiler stageごとのinvariantをtestできる
- incremental compilation
- fast IDE feedback
- multiple native backends
- precise GC metadata
- ownership、effects、typesをoptimizationへ利用する
- deterministic and reproducible builds

## Planned pipeline

```text
UTF-8 source
  -> lexer
  -> lossless syntax tree
  -> macro token expansion
  -> module graph + name resolution
  -> HIR
  -> type/effect/ownership checking
  -> typed HIR
  -> law declaration checking + evidence emission
  -> typed macro expansion
  -> MIR / control-flow graph
  -> borrow/view validation
  -> SSA IR
  -> optimization
  -> backend
       - native object
       - Wasm
       - bytecode VM
       - C bootstrap
```

## Syntax tree

lossless tree requirements:

- comments and whitespace preserved
- malformed nodes represented
- stable node identity for IDE
- incremental reparse
- UTF-8 byte offsets plus display columns
- macro token tree support

Stage 0 ASTはlosslessではなくsemantic dataclass treeである。

## HIR

HIR resolves syntax sugar while preserving user concepts.

Examples:

- pipeline becomes call with source mapping
- method syntax becomes resolved function call
- `for` becomes iterator protocol
- optional operations become explicit control flow
- record shorthand becomes explicit fields

HIR carries:

- symbol IDs
- module IDs
- source spans
- generic parameters
- preliminary type variables

## Typed HIR

adds:

- concrete or constrained types
- effect rows
- parameter modes
- move/read/edit operations
- trait selection
- nullability refinement
- macro origin

## Law evidence stage

law declarationはtype checking後、optimization前に検査する。

```text
typed functions + typed law model
  -> deterministic compile-time evaluator
  -> bounded exhaustive checker or finite proof checker
  -> counterexample diagnostics
  -> law evidence artifact
```

evidence artifactには最低限次を含める。

```text
law family
implementation identity
type substitution
assurance level
model or proof digest
compiler/kernel version
dependency evidence hashes
```

assurance level:

- `bounded-exhaustive`: declared model内のみ
- `proven-finite`: complete finite carrier全体
- `proven`: generic proof termをtrusted kernelで検査

optimizer passはrequired evidence levelを宣言する。bounded evidenceだけでgeneric Monad rewriteを行わない。

## MIR

control-flow oriented IR for safety and lowering.

```text
basic blocks
locals
places
moves
borrows/views
drops
branches
calls
panic/cancellation edges
```

ownership checking should operate on MIR rather than surface syntax in production.

## SSA IR

optimization-level representation.

- explicit blocks and phi values
- unboxed primitive types
- aggregate layouts
- GC safepoints
- precise roots
- alias metadata
- effect metadata
- vector operations
- deoptimization metadata for JIT if added

## Optimization

mandatory correctness-first order:

1. canonicalization
2. constant folding
3. dead code elimination
4. inlining
5. escape analysis
6. allocation sinking
7. bounds-check elimination
8. ownership-based reuse
9. map/filter fusion
10. vectorization
11. profile-guided passes

各passはdisable可能にし、interpreterまたはunoptimized backendとのdifferential testを行う。

## Backends

### C11 bootstrap

現在実装済み。

supported:

- Int, Float, Bool, Text
- direct functions and recursion
- let/assignment
- if/else, while, numeric for range
- arithmetic and comparison
- print
- basic math calls
- Stage 1向け`List[Text]`、process args、Text indexing/slicing/search
- Stage 1向けtext file read/write

unsupported casesはcompile error。

### Native production backend

候補を交換可能にする。

- LLVM-class backend: optimizationとtarget coverage
- Cranelift-class backend: compile latencyとJIT/fast debug
- custom minimal backend: bootstrap and auditing

特定backendにlanguage semanticsを依存させない。

### VM

用途:

- REPL
- fast development
- macro sandbox
- portable tests
- deterministic execution

bytecode verifierを必須にする。

## Runtime interface

compiler/runtime ABI:

- allocation
- GC safepoints
- stack maps
- panic/error propagation
- task scheduling
- resource drop
- text/collection layout
- reflection metadata
- profiler hooks

ABI versionをartifactへ埋め込む。

## Incremental compilation

query-based architectureを想定する。

key examples:

```text
parse(file_hash)
module_graph(manifest_hash)
resolve(module_id)
typecheck(item_id, dependency_fingerprints)
lower(item_id)
codegen(monomorphized_item, target, profile)
```

cache requirements:

- deterministic key
- compiler version and edition
- target and feature flags
- macro input hashes
- safe corruption recovery
- local and optional remote cache

## Diagnostics

stageはstructured diagnosticを返す。

```text
code
severity
primary span
secondary spans
message
notes
fix edits
explanation key
```

backend stderrをそのままuserへ投げず、source mappingできる範囲をFrost diagnosticへ変換する。

## Testing strategy

- lexer/parser golden tests
- positive/negative type tests
- ownership state tests
- interpreter semantics tests
- backend differential tests
- generated C compile tests
- optimizer property tests
- law checker positive/negative/counterexample tests
- proof certificate mutation tests
- fuzzing
- mutation testing
- compile-time benchmarks
- runtime benchmarks
- platform matrix

## Stage 0 source layout

```text
src/frost/ast.py
src/frost/tokens.py
src/frost/lexer.py
src/frost/parser.py
src/frost/typesys.py
src/frost/laws.py
src/frost/evaluator.py
src/frost/c_backend.py
src/frost/c_runtime.py
src/frost/formatter.py
src/frost/frontend.py
src/frost/cli.py
```

依存packageなしで動くため、language UXとtest corpusを先に育てられる。

## Bootstrap architecture

```text
Stage 0: Python reference compiler
  -> type-checks and interprets Stage 1 source
  -> lowers Stage 1 source to C11 and builds a native Stage 1
  -> compares interpreted/native Stage 1 output on a fixture

Stage 1: compiler written in Frost
  -> currently compiles the arithmetic Frost Core fixture
  -> future gate: builds its own Stage 1 source

Stage 2: self-compiled compiler
  -> rebuilds the same source
  -> normalized artifact equality
```

現在はStage 0からnative Stage 1 arithmetic compiler seedをbuildでき、interpreted/native Stage 1が生成するfixture C11はbyte-for-byteで一致する。Stage 1 self-recompileとStage 2 fixed pointはopenである。機械可読statusは`bootstrap/manifest.json`に置く。

fixed-point gateではraw executable behaviorだけでなく、normalized HIR/MIR hash、symbol graph、reproducible object bytesを比較する。
