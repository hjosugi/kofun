# Initial design decisions

## DD-001: `fn`

Use `fn` for named functions and lambdas.

Reason:

- short
- familiar from Rust, Kotlin-related ecosystems, Gleam, and modern language design
- easy to scan

## DD-002: `null` and `T?`

Use `null` as the only optional empty literal, restricted to`T?`.

Do not use `nil`, `None`, or implicit nullable references.

## DD-003: `else if`

Use two ordinary words instead of `elif` or `elseif`.

## DD-004: Square-bracket generics

Use `List[Int]` and `fn identity[T]`.

Reason:

- readable to Python/TypeScript users
- avoids angle-bracket parsing complexity
- compact

## DD-005: Hybrid memory

Use GC-managed ordinary values and affine owned resources.

Reason:

- graph/application/scientific code stays concise
- resources retain deterministic cleanup
- compiler can optimize unique managed values

## DD-006: Word-based parameter modes

Use `read`, `edit`, `take` instead of `&`, `&mut`, explicit move markers, and routine lifetime annotations.

## DD-007: Immutable by default

Use `let`; mutation requires `let mut` or `edit` access.

## DD-008: Expression-oriented control flow

`if` and planned `match` return values.

## DD-009: Practical loops

Keep `for`, `while`, indexing, and local mutation. FP is a core style, not a ban on algorithmic control flow.

## DD-010: `/` and `//`

`/` returns floating division. `//` performs integer/floor division.

## DD-011: `|>` pipeline

Pass the left value as the first argument of the right call.

## DD-012: No silent backend fallback

If a backend cannot lower a construct, compilation fails with a source-located error.

## DD-013: Typed hygienic macros

No C-style textual preprocessor. Quote/unquote operates on token trees or typed public AST.

## DD-014: One standard tool

`kofun` owns build, run, check, test, format, lint, docs, packages, and profiling workflows.

## DD-015: C-speed as a measured goal

Do not claim C/Rust parity without workload-specific benchmarks. Build unboxed native paths and publish results.

## DD-016: Algebraic laws are compiler artifacts

`Monad`などの抽象はmethod signatureだけで完了としない。source-level `law` declarationをtype checking後に検査し、失敗をcompile errorにする。

## DD-017: Evidence levels are never conflated

`bounded-exhaustive`、`proven-finite`、`proven`を別のassuranceとして扱う。sample検査を普遍証明と表示せず、optimizerとCIは最低assuranceを明示する。

## DD-018: Versioned machine-readable law evidence

law resultは`kofun.law-evidence/v1` JSON artifactとして保存できる。source hash、compiler version、model digest、case count、diagnostics、counterexamplesを含める。

## DD-019: Self-hosting means a fixed point

Kofunで書かれたcompiler sourceが存在するだけではself-hostingと呼ばない。Stage 1 self-recompileとStage 1/Stage 2 artifact equivalenceを満たした時だけfixed-point bootstrapと呼ぶ。

## DD-020: Two Stage 1 execution paths

bootstrap初期段階では、Stage 0 interpreter版とStage 0 C11 backendからbuildしたnative版のStage 1 outputを比較する。両者の一致をStage 2以前のdifferential gateにする。
