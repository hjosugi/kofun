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
    bool-pattern ("if" bool-expression)? => { core-statements },
    ...
}

bool-pattern := "true" | "false" | "_"
```

`bool-expression` is currently a Bool literal or one integer comparison using
`==`, `!=`, `<`, `<=`, `>`, or `>=`. Each arm must use a block. The scrutinee
is evaluated exactly once. Arms are considered in source order. A guard is
evaluated exactly once only after its pattern matches; a false guard continues
with the next arm. Once an arm is selected, no later arm or guard executes.
Comparison operands are evaluated left to right, and a checked failure in the
left operand prevents evaluation of the right operand. Match statements may be
nested.

`true` and `false` together are exhaustive without `_`. `_` covers every
constructor not handled by an earlier arm and may therefore appear alone.

## Static coverage algorithm

The checker starts with the uncovered set `{true, false}` and visits arms in
source order:

1. A guarded arm removes nothing, because its guard may be false at runtime.
2. An unguarded `true` removes `true`.
3. An unguarded `false` removes `false`.
4. An unguarded `_` removes every remaining constructor and makes every later
   arm unreachable.

A guarded constructor may repeat until an unguarded arm removes that
constructor. An explicit constructor already removed from the set is an
unreachable pattern, whether or not it has a guard. A `_` arm is unreachable
when the set is already empty. Compilation succeeds only when the uncovered
set is empty. This deliberately conservative rule means even `if true` does
not provide static coverage; an unguarded fallback remains mandatory. A
failure names `true`, `false`, or both, rather than deferring the failure to
runtime.

## Diagnostics

- `E2S24` reports syntax or capability-boundary errors, including non-Bool
  scrutinees, unsupported patterns, and non-block arms.
- `E2S25` reports non-exhaustiveness and names every missing Bool pattern.
- `E2S26` reports duplicate or otherwise unreachable arms.
- `E2S29` reports a guard that is not a Bool literal or an integer comparison.

All four diagnostics carry the relevant source byte. They are compile errors,
and Stage 2 emits no C artifact for them.

## Deliberate boundary

This checkpoint does not implement value-producing match, arm type unification,
bindings, algebraic variants, constructor payloads, or-patterns, nested
patterns, or ownership-aware destructuring. It advances the issue #30 guard
requirement only for this finite Bool slice; guarded and nested ADT patterns
still require the general typed pattern matrix after ADT representation and
name/type resolution are available.

Assignment remains block-local in the current Core. A match arm may declare and
change its own mutable binding, but changing an outer binding from an arm is
rejected with `E2S22`.
