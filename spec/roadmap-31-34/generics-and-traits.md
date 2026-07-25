# Issue 31: generics and traits

## Verified starting point

Square-bracket type application is a documented design choice, and the
typed-only Stage 2 checkpoint now parses and type-checks explicitly
instantiated, unbounded generic functions. Its executable evidence is limited
to direct calls, declaration-scoped type-parameter identities, and explicit
substitution over `Int`, `Bool`, and `Text`. The active Stage 1 compiler still
does not lower generic declarations, and generic nominal types, traits,
implementations, bounds, associated types, inference, and generic law
propositions remain open. Design-era examples beyond the focused checkpoint
in `docs/TYPE_SYSTEM.md` and `docs/LAW_SYSTEM.md` are not implementation
evidence.

## Lowering decision

Dictionary passing is the semantic baseline for trait-bounded generic calls.
The resolver must choose a coherent implementation at compile time and pass a
statically shaped method dictionary; runtime instance search is forbidden.
Unconstrained generic value layout must remain explicit in typed IR rather than
being inferred by a backend.

Monomorphization is an optional typed-IR optimization, not the source-language
semantics or the only ABI. A specialization must preserve the observable result
of the dictionary form and must be removable without changing whether a program
type-checks. This baseline keeps separate compilation possible while retaining
a path to direct calls and unboxed layouts for hot, concrete instantiations.

This decision does **not** satisfy the issue's measured-justification
criterion. It is provisional until the following experiment is implemented and
recorded.

## Coherence and retroactive implementation decision

Status: accepted M2-alpha design for #403.

Kofun retains the `trait` keyword and permits a retroactive implementation
only when the implementing package owns the trait or the outer nominal type
constructor of the self type. This is the local-trait-or-local-outer-type
orphan rule.

For `impl Trait[Arguments] for SelfType`, define:

- the trait owner as the `PackageId` that declares the trait `SymbolId`;
- the type owner as the `PackageId` that declares the outer nominal `TypeId`
  of `SelfType`; and
- the coherence key as the fully resolved trait identity and arguments plus
  the canonical self `TypeRef`.

The implementation is legal exactly when the implementing `PackageId` equals
the trait owner or the type owner. Importing, aliasing, or re-exporting an
identity never changes its owner. Primitive types and imported C/Rust ABI
types are foreign. A locally declared nominal wrapper is local. For a generic
self type, ownership follows the outer nominal constructor rather than its
arguments.

### Resolution and overlap

There is at most one applicable `ImplementationId` for one fully resolved
coherence key across the complete dependency graph. The compiler must reject
overlap while combining declarations or validated interfaces; it must not
delay the choice to execution.

Source order, lexical scope, import order, re-export order, link order, and
hash-table iteration never select an implementation. M2-alpha has no private
or lexical implementation that can shadow another candidate.

Blanket implementations, negative implementations, specialization, and
ordered fallback are unsupported in M2-alpha. A parser or resolver that sees
one must report an unsupported-form diagnostic. A future specialization
contract must be explicit and versioned, preserve a unique dictionary
identity, and not change whether a program without specialization
type-checks.

### Visibility and exported signatures

Trait and nominal-type visibility follows
[`../modules/visibility.md`](../modules/visibility.md). An exported signature
may expose only public traits and public nominal types, and a trait bound is a
semantic interface fact.

Every accepted implementation participates in dependency-graph coherence.
An implementation cannot be hidden to create local resolution precedence. If
an implementation is not present in the semantic interface visible to a
consumer, it cannot satisfy that consumer's bound or affect how the consumer
type-checks an exported API.

Dictionary selection remains compile-time-only. No visibility form enables
runtime instance search or dynamic dispatch.

### Identity and law evidence

The selected dictionary carries a stable `ImplementationId` under
[`../modules/module-identity.md`](../modules/module-identity.md). Its canonical
semantic inputs identify the implementing package, trait and canonical trait
arguments, self outer nominal type and canonical arguments, implementation
declaration/binders/constraints, and coherence mode. Compiler artifacts and
cache keys pair it with the dictionary ABI version. Source spans, source
order, import order, and discovery order are excluded.

A trait declaration owns its law declarations. Proof or bounded-checking
results are separate versioned law-evidence artifacts keyed to the exact
selected `ImplementationId`. Their `LawEvidenceId` additionally commits to
the law declaration, evidence-contract version, quantified type arguments,
and semantic evidence digest. Evidence is not transferable to another
implementation with the same displayed types, and one assurance level is
never relabeled as another.

### Resolution matrix and worked examples

| Trait owner | Outer type owner | Implementing package | Result |
| --- | --- | --- | --- |
| local | local | owns either | accept |
| local | foreign | trait owner | accept |
| foreign | local | type owner | accept |
| foreign | foreign | owns neither | reject; declare a local nominal wrapper |

Design examples:

```kofun
# Package app owns Displayable.
trait Displayable[T] {
    fn display(read value: T) -> Text
}

# Accepted: the trait is local even though the self type is foreign.
impl Displayable[dependency.Widget] for dependency.Widget {
    # methods
}

# Accepted: app owns the outer nominal type.
type LocalWidget = {
    value: dependency.Widget,
}

impl dependency.Hash[LocalWidget] for LocalWidget {
    # methods
}

# Rejected: dependency.Hash and ffi.Handle are both foreign.
impl dependency.Hash[ffi.Handle] for ffi.Handle {
    # error: wrap ffi.Handle in a local nominal type
}

# Accepted: generic arguments do not make the local outer type foreign.
type LocalBox[T] = {
    value: T,
}

impl dependency.Hash[LocalBox[ffi.Handle]] for LocalBox[ffi.Handle] {
    # methods
}
```

A type alias of `ffi.Handle` remains foreign and does not repair the rejected
case. A nominal `LocalHandle` record containing `ffi.Handle` is local and may
implement `dependency.Hash`.

If the trait-owning package and type-owning package each publish a candidate
for the same tuple, a consumer that combines them reports deterministic
overlap. Reordering the two imports produces the same error and candidate
identities.

`trait` is retained because it matches the existing syntax and accurately
describes static dictionary selection. `protocol` is rejected as a
vocabulary-only migration that may imply runtime protocol dispatch.
`interface` with declaration-site-only conformance is rejected because it
would forbid both local-trait/foreign-type and
foreign-trait/local-type adaptation. Unrestricted foreign-trait/foreign-type
conformance is rejected because it cannot preserve deterministic separate
compilation.

## Required measurement

Implement compiler switches that select `dictionary`, `monomorphic`, and
`hybrid` lowering for the same typed program corpus. The corpus must include:

- a small single-instantiation generic function;
- ten and one hundred concrete instantiations of one generic function;
- a trait-bound call in a hot numeric loop;
- a separately compiled library and consumer;
- a lawful generic trait declaration once generic law propositions exist.

For every mode, record the compiler commit, host description, optimization
level, and ten measured runs after one warm-up. Publish the median and median
absolute deviation for:

- clean build time;
- one-file incremental build time;
- peak compiler resident memory;
- emitted text-section bytes; and
- runtime for the hot loop.

The baseline is accepted only if the results support it. Otherwise this
decision document must be amended with the data that selects another strategy.

## Type-checking contract

The first implementation slice must accept a generic identity function, a
generic nominal type, a trait, an implementation, and a function with a trait
bound. It must reject:

- an unsatisfied bound at the call site;
- duplicate or overlapping implementations;
- an implementation outside the local-trait-or-local-outer-type orphan rule;
- a method whose signature does not match its trait;
- an unconstrained type parameter; and
- recursive bound resolution that exceeds a deterministic limit.

Diagnostics must identify the declaration span, requested bound, candidate
implementations and stable identities where disclosure permits, and the
reason each candidate was rejected. Import reordering must not change the
selected implementation or diagnostic candidate order.

## Generic law contract

A law over a type parameter is accepted only after normal name resolution and
type checking. The proposition must retain:

- quantified type and value parameters;
- required trait dictionaries;
- the normalized typed proposition;
- source and compiler digests;
- proof-assurance level; and
- a proof certificate understood by a small trusted kernel.

`bounded-exhaustive` and `proven-finite` evidence for concrete carriers must
never be relabeled as a generic `proven` result. Dictionary specialization may
consume a generic rewrite only when compatible `proven` evidence is attached
to the selected implementation.

## Executable close checklist

- [ ] Parser fixtures cover generic functions, types, bounds, traits, and
      implementations.
- [ ] Type-check pass and compile-fail fixtures cover the contract above.
- [ ] Dictionary lowering executes through every supported backend.
- [ ] Optional specialization differentially matches dictionary lowering.
- [ ] The strategy benchmark is committed with raw measurements and summary.
- [ ] A generic law proposition reaches the proof kernel and emits versioned
      evidence.
- [ ] Bounded concrete evidence is rejected when generic proof is required.
