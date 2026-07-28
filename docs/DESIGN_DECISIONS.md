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

## DD-021: Records declare with `type` and construct with labels

Nominal records are declared `type Name = { field: Type, ... }` and constructed
`Name(field: value, ...)`. A second `record Name { ... }` declaration family and
the `Name { ... }` brace construction are both rejected.

Reason:

- one declaration vocabulary already covers aliases, sum types, and records;
- the parenthesized labelled call form cannot collide with blocks, control-flow
  conditions, loop iterables, or the still-open map literal, so records do not
  have to be sequenced behind #52/#624;
- a construction that never uses braces removes the parser-context suppression
  that brace construction forces on `if`, `while`, and `for`.

Every declared field is supplied exactly once in any written order. Arguments
evaluate left to right in written order; storage, layout, and drop follow
declaration order. Fields are immutable in v1, `take` moves a whole record, and
`take value.field` is rejected, so no partially moved record exists.
[`spec/records-v1.md`](../spec/records-v1.md) is normative.

## DD-022: Redundancy that is evidence is not duplication

Some repeated work in this repository exists *because* it is repeated. Where two
things are derived independently and a gate asserts they agree, the agreement is
the evidence, and sharing the derivation deletes it — the gate keeps passing and
proves nothing. Those sites are not refactoring targets, and DD-020 is the
general case of the rule.

Load-bearing redundancy, which must stay:

- `bootstrap/stage1/compiler.kofun` and `bootstrap/stage1/compiler.c` — the
  Kofun source and its hand-audited C transliteration. One change is written
  twice on purpose: a host C11 compiler alone must be able to start the
  Kofun-written compiler, and the differential is what says the transliteration
  is faithful.
- `valid_source` and `emit_statements` inside that seed — two structural walks
  that repeat the same block and scope bookkeeping rather than sharing it, so
  every name resolves to the binding *both* walks agreed on.
- The audited seed and the compiler built from `S.c`, compared on every accept
  and reject corpus by `bootstrap/selfhost/check-compiler-driver.sh`.

Ordinary duplication, which should be removed:

- harness scaffolding — the per-corpus setup, comparison, and execution sequence
  around a differential. Collapsing it changes how the comparison is *invoked*,
  not what is compared, so the evidence is untouched.
- parallel hand-written lists of the same set. A list beside the thing it
  describes drifts silently; derive it, and assert a count so a deliberate
  change stays reviewable. The refusal corpora are globbed for this reason.

The test that separates the two: **if this were shared, would any gate still
fail when the underlying property breaks?** If no gate would fail, the
repetition was the gate. If some gate still fails, the repetition was scaffolding.

Two copies of a *constant* are acceptable where a mismatch fails loudly —
`REJECT_FIXTURE_COUNT` is asserted in both gates, so a stale copy stops the
build. The defect DD-022 targets is silent disagreement, not repetition itself.

## DD-023: A native target declares facts, not policy

A native target supplies only what its ABI decides — its register file, its
calling convention, and its instruction emitter — and adds nothing else.
Anything derivable from those facts is written once in target-independent code,
and every target runs that one copy.

Concretely, a target declares a `TargetRegisterFile`: its caller-saved scratch
class, its call-safe class, and the value meaning "no register". It does not
bring a register allocator. The allocation policy — scratch first unless a
value must survive a call, call-safe otherwise, nothing for a binding read
fewer than twice — lives once in
[`bootstrap/native/core_compiler.c`](../bootstrap/native/core_compiler.c) and
reads the declared file.
[`docs/NATIVE_BACKEND.md`](NATIVE_BACKEND.md) is normative for the contract.

Reason:

- x86-64 and AArch64 each carried a private copy of the four `take_*_register`
  functions. After normalising the `X64_`/`A64_` prefixes the copies were
  **identical**, so the pair could not disagree and the native gate proved
  nothing about them. Under DD-022 that is ordinary duplication, not evidence:
  sharing it loses nothing and gains one tested implementation.
- Every queued codegen item is otherwise priced per target, and the multiplier
  grows with each new backend.

This decision is deliberately narrow, and DD-022 is why. It shares only the
layer whose duplicate copies were byte-for-byte the same algorithm. The
lowering pairs — `function_expression`, `function_divide`, `function_compare`
and the rest listed in `docs/NATIVE_BACKEND.md` — are **not** shared by this
decision. Those are two genuinely independent lowerings that the native gate
requires to agree on observable behaviour and on `R010` diagnostic bytes, so
the agreement is the evidence and sharing them would delete it. Whether to
share them anyway, and what would replace the lost differential, is a separate
decision this one does not make.

A new target that re-adds its own copy of the shared allocator is a defect. The
`function_register_allocation` fixture pins the leaf prologue for both targets,
so perturbing the shared path fails the native gate on each of them.
