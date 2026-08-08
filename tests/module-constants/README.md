# Module constants

`task module-constants` gates the top-level `let NAME = <integer literal>`
declaration form.

## What the form is

A module constant is immutable, `Int`-typed, and visible to every function in
its compilation unit. It lowers to one `static const int64_t` at C file scope,
which is what makes it *not* a lexical binding: nothing in the scope HIR
resolves it, and a function-local `let` of the same name shadows it inside
that scope only.

Constants carry no visibility modifier here. They stay internal to their
compilation unit, so this form adds nothing to KIF and does not interact with
the visibility gates.

## Why the parser needed more than one change

The blocker was never only the top-level parse branch. Kofun's frontend has
several walkers that step over one top-level declaration at a time, and each
one had to learn that a constant is a declaration:

- `parse_program` accepts the form and emits `constant|NAME|VALUE|start|end`;
- `top_level_end` reports its extent, which is what `enum_declaration_start`
  and the other by-name lookups advance with — without this an enum declared
  after a constant reported `E2S31: concrete enum must declare a constructor`,
  because the walk never reached its constructors;
- `next_function_start` steps over constants as it already stepped over types,
  or the first constant is mistaken for a function and its `let` tokens are
  collected as that function's bindings;
- `type_declaration_end` accepts `let` as a following declaration, or a type
  immediately before a constant stops being a well-formed declaration.

`ordering.kofun` exists for exactly this: it puts a constant before, between,
and after a record and an enum, so a regression in any one walker fails the
gate rather than surfacing later as a confusing diagnostic about the
declaration next to the constant.

## Fixtures

| Fixture | Proves |
|---|---|
| `values.kofun` | small, negative, and both Int64 bounds reach the backend intact |
| `ordering.kofun` | constants interleave with a record and an enum in either order |
| `shadowing.kofun` | a local binding shadows the constant in its own scope only |
| `non_literal.kofun` | `E2S159` — the initializer is not one integer literal |
| `function_clash.kofun` | `E2S159` — the name collides with a function |
| `type_clash.kofun` | `E2S159` — the name collides with a type |
| `duplicate.kofun` | `E2S160` — the same constant is declared twice |
| `mutable.kofun` | `E2S161` — `let mut` is refused as mutable module state |

Every refusal is checked for its exact golden, a nonzero exit, and the absence
of a backend artifact, so a constant that fails to parse can never leave a
half-written `.c` behind.

## Outside this slice

Mutable module state, non-`Int` constants, constant expressions, and exported
constants. Each needs its own decision; none is implied by this form.
