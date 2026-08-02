# Value-producing concrete-enum match

Corpus for issue #921. `check.sh` is the gate; `task enum-match-value` runs it.

Statement-position enum match was already executable, and value-position
`match` was already executable for the bounded Bool slice. This corpus holds
the join of the two: a `match` over a concrete-enum scrutinee producing one
`Int` in `let`, `print`, assignment, and `return`.

## What the fixtures hold

| Fixture | Holds |
| --- | --- |
| `positions.kofun` | the four value positions over a two-constructor enum, one payload-free and one carrying an `Int`, matched exhaustively without a catch-all |
| `selected_arm.kofun` | evaluation order: the scrutinee's initializer runs once, guards run in source order, a payload binding reaches a nested value match, and every arm that must not run divides by zero |
| `missing_constructor.kofun` | `E2S25` names the missing constructor |
| `guard_only_coverage.kofun` | `E2S25`: a guarded arm proves no static coverage |
| `duplicate_arm.kofun` | `E2S26` names the constructor an earlier arm already covered |
| `unreachable_catchall.kofun` | `E2S26`: a catch-all after full coverage is unreachable |
| `void_arm.kofun` | `E2S30`: a `print` arm produces nothing |
| `empty_arm.kofun` | `E2S30`: an empty arm block produces nothing |
| `multi_value_arm.kofun` | `E2S30`: an arm block holds exactly one final `Int` expression |
| `foreign_constructor.kofun` | `E2S32`: constructor identity is nominal, not spelling |
| `enum_valued_arm.kofun` | `E2S32`: the result join is `Int` only, so an arm producing the enum value is refused |

Every `.stdout` beside a negative fixture is the exact compiler output, byte
for byte. No new diagnostic code was allocated: value position reuses the codes
statement position and the Bool value match already own, which is what #921
asks for.

## Scrutinee-once evidence

Two independent readings, because neither alone is conclusive:

- **Source level.** `selected_arm.kofun` initializes its scrutinee with a call
  that prints. Three matches over that binding follow and the program prints
  the probe line once.
- **Emitted C.** `check.sh` counts `KofunEnumValue kofun_match_value = k_b…` in
  the lowering of `positions.kofun` and requires one per written match, checks
  that every arm test reads `kofun_match_value.tag` rather than the scrutinee
  binding, and requires the copy to precede the first arm test.

## Deliberately not here

- **Dispatch shape.** Value position emits the ordered if-chain the value
  positions already emit. Dense `switch` lowering is #554's, which owns
  statement position; this gate asserts no dispatch shape for value position,
  so that lane can extend it without fighting a golden here.
- **Enum-valued results.** An arm producing an enum value rather than an `Int`
  stays refused by the landed match-only rule (`E2S32`), pinned here by
  `enum_valued_arm.kofun`; only the `Int` join is in this slice.
- **Wider, nested, or multi-field payloads and generic enums**, which the
  bounded declaration rules still refuse.
- **`Never` and diverging arms.** Stage 2 Core has no such arm to join, so the
  result join has no bottom case; there is nothing here to test.
