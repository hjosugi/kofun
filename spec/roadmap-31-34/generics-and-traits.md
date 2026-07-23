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
- an implementation outside the coherence/orphan policy;
- a method whose signature does not match its trait;
- an unconstrained type parameter; and
- recursive bound resolution that exceeds a deterministic limit.

Diagnostics must identify the declaration span, requested bound, candidate
implementations, and the reason each candidate was rejected.

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
