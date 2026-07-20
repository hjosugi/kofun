# Bounded Bool match exhaustiveness

This document is the executable Stage 2 checkpoint for issue #30. It does not
claim the general ADT exhaustiveness algorithm described by the full type
system. The finite constructor set in this checkpoint is exactly:

```text
Bool = { true, false }
```

## Accepted Core

The statement-position form follows the normative match syntax:

```text
match bool-expression {
    bool-pattern => { core-statements },
    ...
}

bool-pattern := "true" | "false" | "_"
```

`bool-expression` is currently a Bool literal or one integer comparison using
`==`, `!=`, `<`, `<=`, `>`, or `>=`. Each arm must use a block. The scrutinee
is evaluated exactly once, and only the selected arm executes. Match statements
may be nested.

`true` and `false` together are exhaustive without `_`. `_` covers every
constructor not handled by an earlier arm and may therefore appear alone.

## Static coverage algorithm

The checker starts with the uncovered set `{true, false}` and visits arms in
source order:

1. `true` removes `true`;
2. `false` removes `false`;
3. `_` removes every remaining constructor and makes every later arm
   unreachable.

An explicit constructor already removed from the set is a duplicate,
unreachable pattern. A `_` arm is unreachable when the set is already empty.
Compilation succeeds only when the uncovered set is empty. A failure names
`true`, `false`, or both, rather than deferring the failure to runtime.

## Diagnostics

- `E2S24` reports syntax or capability-boundary errors, including non-Bool
  scrutinees, unsupported patterns, guards, and non-block arms.
- `E2S25` reports non-exhaustiveness and names every missing Bool pattern.
- `E2S26` reports duplicate or otherwise unreachable arms.

All three diagnostics carry the relevant source byte. They are compile errors,
and Stage 2 emits no C artifact for them.

## Deliberate boundary

This checkpoint does not implement value-producing match, arm type unification,
bindings, algebraic variants, constructor payloads, or-patterns, nested
patterns, ownership-aware destructuring, or guards. It does not claim the issue
#30 acceptance criterion for guarded and nested ADT patterns. Those require the
general typed pattern matrix after ADT representation and name/type resolution
are available.

Assignment remains block-local in the current Core. A match arm may declare and
change its own mutable binding, but changing an outer binding from an arm is
rejected with `E2S22`.
