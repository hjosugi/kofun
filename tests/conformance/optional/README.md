# Bounded Optional frontend

Executable evidence for #70. `bootstrap/stage2/optional_frontend.c` classifies
`null` as a keyword, parses one postfix `?` on a primary type, and represents
the result as `Optional(TypeId)` in typed IR.

Run:

```sh
sh tests/conformance/optional/run.sh
```

The normative surface is already fixed by #50/#51 in
`tests/conformance/syntax/issues_48_60/surface-cases.tsv` and
`spec/syntax/EXPRESSIONS_AND_LITERALS.md`. That inventory is a table gate; this
suite makes the same cases executable. It consumes the contract rather than
restating it.

## The suffix binds to the whole primary type

`List[Int]?` is `Optional(List(Int))` and `List[Int?]` is `List(Optional(Int))`.
They are structurally distinct, and only the first has an absent value — the
gate pins both, and `positive.kofun` deliberately cannot initialise a
`List[Int?]` with `null`.

## Typing

- `null` has no standalone type; without an expected type it is refused.
- Under an expected `Optional(T)`, `null` has type `Optional(T)`.
- Under an expected `T`, `null` is refused.
- A `T` satisfies an expected `Optional(T)` through one injection rule, written
  once in `assignable`. The IR records the injection (`written=builtin:Int`,
  `injected=yes`) rather than silently rewriting the type, so the rule cannot
  spread unnoticed.
- An `Optional(T)` never satisfies an expected `T`.

## Frontend-only boundary

Runtime representation is deferred. The gate asserts the IR names no `tag`,
`niche`, `layout`, or `discriminant`, so no representation can be inferred from
the typed node. Coalescing (#314), narrowing and inference (#312), matching
(#30), and propagation (#409) are all outside this slice — `??` is not even
parsed.

## Recovery

A malformed suffix reports once, then parsing resumes at the next declaration
boundary. `recovery_after_suffix` proves it reaches the independent error after
the broken statement — two diagnostics, in source order — and the gate asserts
no typed IR is written from the broken input.

## Refusals

| Fixture | Code | Refuses |
|---|---|---|
| `unconstrained_null` | E2S134 | `let value = null` with no expected type |
| `null_under_concrete` | E2S135 | `let value: Int = null` |
| `optional_argument` | E2S136 | passing `Int?` where `Int` is required |
| `optional_arithmetic` | E2S136 | an optional operand in arithmetic |
| `nested_optional` | E2S137 | `Int??` |
| `optional_void` | E2S137 | `Void?` |
| `prefix_optional` | E2S138 | `?Int` |
| `optional_ownership_mode` | E2S138 | `read List[Int]?` |
| `nil_is_an_identifier` | E2S139 | `nil` and `None` as absence |
| `null_condition` | E2S140 | `if null`, because there is no truthiness |
| `recovery_after_suffix` | E2S137 | and reports the later error too |

The gate asserts its own refusal list is the same size as the glob, so a
fixture added without a gate entry stops the build (DD-022).

The frontend owns `E2S134`–`E2S141`.
