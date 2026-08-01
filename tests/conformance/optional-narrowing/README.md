# Optional narrowing

Executable evidence for #312, layered on the typed `Optional(TypeId)` node that
#70 landed in `bootstrap/stage2/optional_frontend.c`.

Run:

```sh
sh tests/conformance/optional-narrowing/run.sh
sh tests/fuzz/optional_narrowing.sh
```

A refinement is a fact about one edge of the control-flow graph. It never
changes a declared type and it never leaves its function. `x` is `Optional(T)`
everywhere the declaration is read; only a *use* on a refined edge is typed as
`T`.

## Recognized conditions

| Condition | Refined edge |
|---|---|
| `x != null` | true |
| `null != x` | true |
| `x == null` | false |
| `null == x` | false |

Either shape as the condition of an `if` whose taken branch definitely returns
is an early-return guard, and the opposite edge's refinement continues past it.
`x` must be a direct local binding — a parameter or a `let` — declared
`Optional(T)`. There is no operator overloading and no user-defined equality in
this slice, so "the comparison is not overloaded" holds by construction.

## Invalidation

The rule when anything is uncertain is to discard, never to assume.

| Event | Effect |
|---|---|
| sibling branch | never sees the fact; each edge has its own environment |
| join | merged by intersection |
| `x = value` | discarded, after `value` is checked against declared `Optional(T)` |
| mutable `x` passed to a call | discarded after the call |
| immutable `let x` passed to a call | retained |
| loop backedge | discards every mutable binding the body mentions |

Ownership modes are refused by this frontend, so every call here has unknown
effects and the conservative rule is the only rule available.

## What reaches typed IR

`positive.ir` is the golden. Three row kinds carry the analysis:

- `refinement` — an established fact, keyed by binding identity, printing the
  declared type beside the refined one and naming the edge it holds on;
- `narrowed-use` — a use the frontend typed as `T` rather than `Optional(T)`;
- `refinement-discarded` — an invalidation with its `reason`, so every rule is
  checkable from the IR rather than only from the absence of an error.

## Refusals

Each refusal names an accepted counterpart in `positive.kofun`, and the gate
asserts that counterpart is present in the IR. A refusal therefore proves the
rule fired, not that narrowing never worked in the first place.

| Fixture | Code | Refuses | Counterpart |
|---|---|---|---|
| `sibling_branch_leak` | E2S136 | the else branch using the then branch's fact | `not_null_name_first` |
| `non_terminating_guard` | E2S136 | a guard whose branch does not definitely return | `guard` |
| `assignment_discards` | E2S136 | using `x` as `T` after `x = null` | `mutable_until_call` |
| `assignment_checked` | E2S136 | `x = "text"` against declared `Optional(Int)` | `mutable_until_call` |
| `call_discards` | E2S136 | using a mutable `x` as `T` after passing it to a call | `immutable_across_call` |
| `non_null_comparison` | E2S136 | assuming `x != 0` narrows | `not_null_name_first` |
| `property_path` | E2S142 | `x.value != null` | `not_null_name_first` |
| `index_path` | E2S142 | `xs[0] != null` | `not_null_name_first` |
| `alias_does_not_transfer` | E2S136 | narrowing `y` to conclude anything about `x` | `not_null_name_first` |
| `loop_backedge` | E2S136 | assuming a mutable refinement on the next iteration | `invariant_across_loop` |
| `immutable_assignment` | E2S143 | reassigning an immutable binding | `mutable_until_call` |
| `unrecognized_null_comparison` | E2S142 | `null == null` | `is_null_name_first` |
| `null_compared_to_concrete` | E2S135 | comparing a concrete `Int` to `null` | `not_null_name_first` |

The gate asserts its refusal list is the same size as the glob, so a fixture
added without a gate entry stops the build (DD-022).

## Out of scope

Compound boolean conditions, property and index paths, aliases, captured
variables, interprocedural summaries, `match`, safe navigation, truthiness,
user-defined equality, general union narrowing, and runtime representation.
Narrowing from a `while` header is also outside this slice: the header types
normally and refines nothing.

Unsupported shapes stay errors rather than optimistic assumptions. The gate
asserts the IR names no `tag`, `niche`, `layout`, `discriminant`, `unwrap`, or
`unchecked`, so no runtime support can be inferred from a refinement.

`E2S142` and `E2S143` are registered in `tests/diagnostics/registry.tsv` under
the `optional-narrowing` adapter, with this suite as their executable evidence.
`E2S134`–`E2S141` remain unregistered; see `tests/conformance/optional/README.md`.
