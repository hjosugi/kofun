# Bounded payload-free enum match exhaustiveness

This document is the next executable Stage 2 checkpoint for issue #30. It
generalizes the finite-set coverage already used by `Bool` to named, concrete
enums without claiming payload patterns, generics, or a general type checker.

## Accepted Core

The bounded declaration and use grammar is:

```text
enum-declaration := "type" IDENT "=" enum-constructor+
enum-constructor := "|" IDENT

enum-binding     := "let" IDENT ":" IDENT "=" IDENT

enum-match       := "match" IDENT "{"
                    enum-arm ("," enum-arm)* ","? "}"
enum-arm         := (IDENT | "_")
                    ("if" bool-expression)? "=>"
                    "{" core-statements "}"
```

An enum declaration is top-level, non-generic, and contains one or more
payload-free constructors. Stage 2 accepts at most 32 enum types in one
compilation unit and at most 64 constructors in one enum. Exceeding either
limit is `E2S31`; the compiler must not truncate a constructor set or silently
change the program.

The bounded use validator accepts at most 256 enum-related identifier
occurrences in one function, counting declarations, initializers, scrutinees,
and patterns whose names intersect that function's enum bindings or the
compilation unit's constructors. The 257th occurrence is `E2S32`. Unrelated
identifiers take a fast path and do not consume this budget.

Type names are unique in the compilation unit. Constructor names are unique
across the complete compilation unit, including across different enum types,
and the bounded type and constructor namespaces are disjoint.
An enum cannot shadow the Stage 2 built-in type names, and `_` is reserved for
catch-all patterns rather than a type or constructor name.
Constructor tags are assigned from zero in declaration order. Tags are an
internal lowering detail and cannot be observed or converted to `Int` by
Kofun source.

This slice requires an immutable local binding with both an explicit enum type
and a constructor initializer:

```kofun
type Signal = | Red | Yellow | Green

fn main() {
    let signal: Signal = Green
    match signal {
        Red => { print(1) },
        Yellow => { print(2) },
        Green => { print(3) },
    }
}
```

The initializer constructor must belong to the declared type. A match
scrutinee is a simple local enum-binding name, and normal lexical visibility
allows a binding from an enclosing block to be matched in a nested block.
Constructor literals in general expressions, inferred enum bindings, mutation,
parameters, results, value-producing enum matches, and cross-function enum
values are outside this checkpoint and are rejected before C emission.

Each arm uses either a constructor belonging to the scrutinee type or `_`.
Arm bodies are statement-position Core blocks. Guards use the existing bounded
Bool grammar: a Bool literal or one checked Int comparison. The scrutinee is
read once. Arms are tested in source order; a guard runs once only after its
constructor matches; and the selected arm alone executes.

## Stable structural IR

The Stage 2 structural IR records declarations before lowering. Records use
source byte spans and declaration-order tags:

```text
type|Signal|3|START|END
constructor|Red|Signal|0|START|END
constructor|Yellow|Signal|1|START|END
constructor|Green|Signal|2|START|END
```

The `type` record fields are name, constructor count, start, and end. The
`constructor` record fields are constructor name, type name, tag, start, and
end. Existing function records remain unchanged. This checkpoint does not add
a typed expression IR.

## Static coverage algorithm

For a match over enum type `T`, the checker starts with the constructor set of
`T` in declaration order and visits arms in source order:

1. A guarded constructor or guarded `_` removes nothing because its guard may
   be false at runtime.
2. An unguarded constructor removes that constructor.
3. An unguarded `_` removes every remaining constructor and makes every later
   arm unreachable.
4. A constructor already removed by an earlier unguarded arm is unreachable,
   even when the later arm has a guard.

Compilation succeeds only when the uncovered set is empty. An `E2S25`
diagnostic names every missing constructor in declaration order. Explicit
constructor coverage therefore needs no catch-all. A guarded constructor may
repeat until an unguarded arm covers it.

## Diagnostics

- `E2S25` reports non-exhaustiveness and names every missing constructor.
- `E2S26` reports duplicate or otherwise unreachable enum arms.
- `E2S29` reports a guard outside the bounded Bool grammar.
- `E2S31` reports a malformed declaration, a duplicate type or constructor,
  a reserved-name or compilation-unit constructor namespace collision, or a
  32-type/64-constructor limit violation.
- `E2S32` reports an unknown enum type or constructor, a constructor from the
  wrong enum, an invalid enum initializer, or an enum match whose binding/type
  cannot be resolved, as well as the per-function enum-use limit.

Diagnostics carry the offending source byte. A rejected program emits no C
artifact. `E2S25` and `E2S26` are deliberately shared with Bool coverage: they
describe coverage failures rather than one scrutinee representation.

## Representation and deliberate boundary

The bounded C11 lowerer represents a payload-free enum as its declaration-order
`int64_t` tag. This is not a public ABI.
The direct native, wasm, and C ABI profiles do not gain enum support from this
checkpoint and must reject these sources rather than selecting a different
representation.

Generic enums, constructor payloads, pattern bindings, nested constructor
patterns, or-patterns, value-producing enum matches, enum equality,
serialization, ownership-aware destructuring, and layout stabilization remain
open. In particular, Kofun optional values continue to use `T?` and `null`;
this slice does not introduce `Option[T]`, `Some`, or `None` as a second
optional-value model.
