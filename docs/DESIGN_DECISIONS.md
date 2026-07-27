# Initial design decisions

This is the readable narrative of what Kofun decided. It is not a status
report: a decision recorded here may be fully implemented, partly implemented,
or built only in someone's head.

[`rfcs/index.json`](../rfcs/index.json) is the machine-checked ledger that says
which. A decision indexed there carries its state, the evidence that bounds it,
and any amendment; `docs/RFC_PROCESS.md` describes how entries move between
states. Decisions that predate the ledger are migrated into it as they become
relevant, so absence from the ledger means "not yet indexed", not "not decided".

Where a decision's semantics have changed, the original wording stays here and
the amendment is announced beside it by its fully-qualified id, such as
`DD-010/A01`. The ledger checker fails if a marker here has no amendment, or an
amendment has no marker, so a superseded sentence cannot sit in this file
reading as current.

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

`//` performs integer/floor division.

Amended by `DD-010/A01`: `/` is not defined on `Int`. It is reserved and
refused with one diagnostic rather than returning a fractional value, because
no fractional type exists to return. The original decision read "`/` returns
floating division"; three documents said so while four backends truncated and
one refused the operator outright, and #687 settled it in favour of refusal.
`spec/semantics.md`, `docs/SYNTAX.md` and `docs/TYPE_SYSTEM.md` state the
current meaning; `rfcs/index.json` records the amendment and its compatibility
analysis.

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

Keep `law` as the standard term, but recognize it contextually rather than
reserving it globally. `Monad`, `Monoid`, and other family names are ordinary
library identifiers and never select compiler family-specific code.

The source model has three distinct structures: a law family containing typed
operations and equations, a named implementation supplying those operations,
and a named `check laws` request supplying domains, equality, assurance, and a
budget. V1 substitutes ground types before evaluation and does not require
higher-kinded types. A law failure fails the normal check/build path.

## DD-017: Evidence levels are never conflated

Treat `bounded-exhaustive`, `proven-finite`, and `proven` as separate assurance
levels. A finite sample can yield only `bounded-exhaustive`.
`proven-finite` requires compiler-certified complete finite carriers, complete
total-function spaces where used, and certified typed equality. `proven` is
reserved for a future trusted proof kernel. The engine computes assurance; a
requested minimum is only a build gate.

Search order and shrinking are deterministic. Equation/parameter/domain order
defines Cartesian enumeration; a failure is shrunk by structural size,
canonical encoded length, then canonical bytes. Evaluation and shrinking share
the same versioned resource budget and require an empty effect set.

## DD-018: Versioned machine-readable law evidence

The accepted target artifact is the deliberately incompatible
`kofun.law-evidence/v2`. Its purpose-separated evaluation-cache and evidence
SHA-256 identities bind compiler/evaluator semantics, law and ground types,
normalized equations, implementation and dependency digests, ordered domains,
equality, the `kofun.law-eval/standard-v1` budget, enumeration version, cases,
computed assurance, outcome, and canonical counterexample. Requested assurance,
display paths, and wall time do not change the reusable result identity.

Failed, stale, weaker, wrong-model, wrong-ground-type, or
dependency-mismatched evidence cannot authorize an optimization or cache hit.
The old `kofun.law-evidence/v1` JSON schema remains historical migration
material and is never silently interpreted as v2.

## DD-019: Self-hosting means a fixed point

The existence of compiler source written in Kofun is not by itself self-hosting. Only when Stage 1 self-recompile and Stage 1/Stage 2 artifact equivalence are both satisfied is it called a fixed-point bootstrap.

## DD-020: Two Stage 1 execution paths

In the early bootstrap stages, compare the Stage 1 output of the Stage 0 interpreter build against the Stage 1 output of the native build produced by the Stage 0 C11 backend. Agreement between the two is the differential gate that precedes Stage 2.
