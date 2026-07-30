# Bounded concrete enum match exhaustiveness

This document defines the executable Stage 2 C11 checkpoint for issue #30 and
#782. It generalizes the finite-set coverage already used by `Bool` to named,
concrete enums whose constructors carry zero or one `Int` payload, without
claiming generics, wider payloads, or a general type checker.

## Accepted Core

The bounded declaration and use grammar is:

```text
enum-declaration := "type" IDENT "=" enum-constructor+
enum-constructor := "|" IDENT ("(" IDENT ":" "Int" ")")?

enum-binding     := "let" IDENT ":" IDENT "=" IDENT
                  | "let" IDENT ":" IDENT "=" IDENT "(" int-expression ")"
                  | "let" IDENT ":" IDENT "=" enum-returning-call

enum-parameter   := IDENT ":" IDENT
enum-result      := "->" IDENT
enum-return      := "return" (IDENT | IDENT "(" int-expression ")")

enum-match       := "match" IDENT "{"
                    enum-arm ("," enum-arm)* ","? "}"
enum-arm         := (IDENT | IDENT "(" (IDENT | "_") ")" | "_")
                    ("if" bool-expression)? "=>"
                    "{" core-statements "}"
```

An enum declaration is top-level, non-generic, and contains one or more
constructors. A constructor declares either no field or one named `Int` field.
Stage 2 accepts at most 32 enum types in one compilation unit and at most 64
constructors in one enum. Exceeding either limit is `E2S31`; the compiler must
not truncate a constructor set or silently change the program.

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

An enum local is immutable and carries an explicit enum type. Its initializer
is a same-typed constructor or a call returning that enum:

```kofun
type Signal = | Red | Yellow(code: Int) | Green

fn choose(code: Int) -> Signal {
    return Yellow(code)
}

fn inspect(signal: Signal) -> Int {
    let mut result = 0
    match signal {
        Red => { result = 1 },
        Yellow(code) if code > 0 => { result = code },
        Yellow(_) => { result = 2 },
        other => {
            match other {
                Green => { result = 3 },
                _ => { result = 4 },
            }
        },
    }
    return result
}

fn main() -> Int {
    let signal: Signal = choose(42)
    print(inspect(signal))
    print(inspect(Yellow(7)))
    return 0
}
```

The initializer constructor must belong to the declared type. A match
scrutinee is a simple enum-binding name, including a same-typed parameter or
binding catch-all. Normal lexical visibility allows a binding from an
enclosing block to be matched in a nested block. A constructor may appear in
an explicitly typed initializer, a same-typed function argument, or a
same-typed return. Inferred enum bindings, mutation, enum values in Int
expressions, and value-producing enum matches are rejected before C emission.

Each arm uses a constructor belonging to the scrutinee type, `_`, or a fresh
binding catch-all. A one-payload constructor must use `C(name)` or `C(_)`; the
bound `Int` is visible in the guard and arm body. A binding catch-all receives
the whole enum value and may be re-matched. A bare ASCII-uppercase name is
constructor-shaped and keeps the unknown-constructor diagnostic when it is not
declared; a fresh value-style name is the binding catch-all. A declared
constructor remains a constructor regardless of case. Arm bodies are
statement-position Core blocks. Guards use the existing bounded Bool grammar:
a Bool literal or one checked Int comparison. The scrutinee is read once. Arms
are tested in source order; a guard runs once only after its constructor
matches; and the selected arm alone executes.

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
3. An unguarded `_` or binding catch-all removes every remaining constructor
   and makes every later arm unreachable.
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

The bounded C11 lowerer represents every supported enum value as an internal
two-word aggregate: its declaration-order `int64_t` tag and one `int64_t`
payload slot. Payload-free constructors store zero in the unused slot.
Same-typed functions pass and return this aggregate by value. This is not a
public ABI.
The direct native, wasm, and C ABI profiles do not gain enum support from this
checkpoint and must reject these sources rather than selecting a different
representation.

Generic enums, payload types other than `Int`, more than one payload field,
nested constructor patterns, or-patterns in the executable C11 path,
value-producing enum matches, enum equality, serialization, ownership-aware
destructuring, and public layout stabilization remain open. In particular,
Kofun optional values continue to use `T?` and `null`; this slice does not
introduce `Option[T]`, `Some`, or `None` as a second optional-value model.
