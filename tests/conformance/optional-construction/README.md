# Executable `Optional(Int)` — corpus (#924)

Gate: `sh tests/conformance/optional-construction/run.sh`, also `task
optional-construction`.

This corpus proves the one artifact #314 named as missing: a reference backend
that constructs present and absent `Optional(Int)` values in the accepted
AggregateLayout v1 representation, carries them across a same-typed boundary,
and observes presence through the already accepted narrowing rules — now
lowered, so the narrowed use actually runs.

## The representation, and where it comes from

`spec/aggregate-layout-v1/examples/core.x86_64-linux.json` carries the accepted
`Optional[Int]` descriptor:

| field | value |
|---|---|
| `kind` | `optional` |
| `size` / `align` | 16 / 8 |
| `tag_width` / `tag_offset` | 1 / 0 |
| `payload_offset` / `payload_size` | 8 / 8 |
| `constructors` | `None` tag 0 (no payload), `Some` tag 1 (`Int`) |

Stage 2 emits that descriptor as a C type:

```c
typedef struct {
    uint8_t tag;
    int64_t payload;
} KofunOptionalInt;
```

The tag is explicit because `spec/aggregate-layout-v1.md` says it is: "No
backend may invent its own niche optimization." The emitted translation unit
carries six `_Static_assert`s pinning size, alignment, both offsets, the tag
width, and the payload size.

The gate does not read those numbers from a checked-in copy. It recomputes the
descriptor with `spec/aggregate-layout-v1/layout.mjs` and compares the emitted
C against the result, then compiles the emitted representation on its own and
reads the stored tag bytes back at run time. A drift on either side — a
backend that widens the tag, or a contract change that moves the payload —
fails here rather than passing quietly.

## Positives

| Fixture | What it observes |
|---|---|
| `construction.kofun` | present and absent construction; a value crossing an `Int?` argument and an `Int?` return; a call declared `Int?`; an `Int?` copied into another `Int?` |
| `not_null_name_first.kofun` | `x != null` refines the true edge |
| `not_null_null_first.kofun` | `null != x`, the other operand order |
| `is_null_name_first.kofun` | `x == null` refines the *false* edge, so the narrowed use is in the `else` |
| `is_null_null_first.kofun` | `null == x`, the other operand order |
| `guard.kofun` | a definitely-returning guard carries the opposite edge past it; an outer refinement dominates a nested branch; an immutable refinement survives a call |

Presence is observed by printing the narrowed `Int`. There is no extraction
operator here, and none was added anywhere: the payload is read only under a
tag test that dominates the read.

## Negatives

Each one pins a rule rather than an implementation accident. All exit 1 and
emit no backend artifact.

| Fixture | Code | Rule |
|---|---|---|
| `direct_use.kofun` | `E2S147` | `Optional(Int)` may not enter `Int` arithmetic |
| `sibling_branch.kofun` | `E2S147` | a refinement never leaks to a sibling branch |
| `non_dominating_guard.kofun` | `E2S147` | a non-returning `if` carries nothing past itself |
| `mutable_binding.kofun` | `E2S147` | `let mut x: Int?` is outside this slice |
| `assignment.kofun` | `E2S147` | an `Int?` binding cannot be reassigned here |
| `nested_optional.kofun` | `E2S147` | one optional layer; `Int??` is not a type |
| `coalescing.kofun` | `E2S147` | `??` is not part of this change |
| `property_path.kofun` | `E2S147` | property and index paths are not narrowed |
| `optional_as_int_argument.kofun` | `E2S147` | an `Int?` does not satisfy an `Int` parameter |
| `optional_result_as_int.kofun` | `E2S147` | an `Int?` result does not satisfy an `Int` binding |
| `unconstrained_null.kofun` | `E2S35` | `null` never injects into a concrete `Int` |
| `null_argument.kofun` | `E2S35` | `null` needs an explicit `Int?` annotation to be typed |

The last two carry `E2S35` rather than `E2S147` deliberately. `null` is
exempted from lexical resolution *only* where an `Int?` is expected — the
whole initializer of a `let ...: Int? =`, an operand of a recognized null
comparison, and the whole value of a `return` from a function declared `Int?`.
In every other position it keeps the verdict it had before this slice existed,
so no program that compiled or failed on `main` compiles or fails differently
now. Giving those positions a nicer message would mean changing the behaviour
of programs this change is not about.

## What this slice deliberately does not do

- **`??` coalescing** stays with #314. It has a single-evaluation and
  lazy-right-side contract that is that issue's work to decide, and guessing at
  it here would be the wrong place to decide it.
- **Optional `match`** stays sequenced behind the general match engine (#30),
  so that exhaustiveness is checked once rather than by a second Optional-only
  algorithm.
- **`?` propagation, safe navigation, `Optional(Text)`, optional records,
  optional enums, `T??`, optional ownership modes, and stdlib combinators** are
  all out of scope, as is any niche optimization.
- **No extraction or force unwrap** exists, in syntax, implementation, or
  documentation. That constraint is inherited from #543 and the gate asserts it
  over both the corpus and `bootstrap/stage2/compiler.c`.

## Two bounds worth stating plainly

**Every `Int?` binding this slice lowers is immutable.** Parameters are
by-value, and `let mut x: Int?` is refused (`mutable_binding.kofun`). The
invalidation rules that need mutation — reassignment, an `edit`/`own`/
unknown-effect call on a mutable binding, and a loop backedge that is not
loop-invariant — therefore cannot arise: the declaration that would create one
is refused first. They remain pinned for the *frontend*, which does admit a
mutable `Int?`, by `tests/conformance/optional-narrowing/run.sh`, whose goldens
this change leaves untouched.

**`while` is not lowered by the Stage 2 C11 slice at all**, independently of
Optional. A loop-backedge *positive* — an immutable refinement surviving a
backedge — is therefore not expressible here, and no fixture claims it. The
frontend gate covers that case.
