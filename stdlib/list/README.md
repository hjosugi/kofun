# List checkpoint

This directory defines the first platform-independent standard-library
checkpoint for `List`. It is a bounded reference surface, not completion of
the generated 20-step lifecycle in issue #504.

## List v1 surface

[`list.kofun`](list.kofun) exposes an eager `List[Int]` API:

- `list_int_empty`, `list_int_length`, and `list_int_is_empty`;
- `list_int_get`, `list_int_first`, and `list_int_last` with typed failures;
- value-returning `list_int_push`, `list_int_concat`, and `list_int_reverse`;
- `list_int_map`, `list_int_filter`, `list_int_fold`, and
  `list_int_contains`.

Lists are ordered. `map`, `filter`, and `fold` invoke callbacks from the first
element to the last. `filter` is stable, and concatenation places every left
element before every right element. The functions are deterministic and have
no clock, randomness, I/O, global state, or locale dependency.

Indexes in `list_int_get` follow the existing Kofun List rule: nonnegative
indexes count from the front and negative indexes count from the end (`-1` is
last). An index outside `-len(values) .. len(values) - 1` returns
`ListIndexOutOfBounds(requested, length)`. `first` and `last` return
`ListEmpty` for an empty input. No valid integer value is reserved as a
sentinel, and the checked accessors do not rely on the raw indexing trap.

## Costs and ownership

Length and emptiness are O(1). Checked access is O(1) for the current indexed
List model. `map`, `filter`, `fold`, `reverse`, and `contains` are O(n), and
concatenation is O(n + m). `push` is documented conservatively as O(n); this
checkpoint does not promise spare capacity, amortization, structural sharing,
or storage identity. Value-returning operations may allocate O(output length)
storage; `fold` and checked access use O(1) additional storage.

Inputs are read-only and remain unchanged. Version 1 is intentionally
`List[Int]`: `Int` is Copy, which makes element return and callback ownership
precise under the current memory model. A generic `List[T]` API needs
move-aware callbacks, Copy constraints, and borrowed element results before it
can honestly cover non-Copy `Text`, nested Lists, records, and resources.

## Executable boundary

The canonical file starts with an ADT and also uses typed results and
higher-order signatures, which the active Stage 2 compiler cannot lower as a
module. The gate therefore does not claim that `list.kofun` is importable.

[`tests/checkpoint.kofun`](tests/checkpoint.kofun) executes real `List[Int]`
literals, indexing, `map`, `filter`, and `fold` through the direct x86-64
backend. [`tests/projection.kofun`](tests/projection.kofun) is a compact scalar
projection of index normalization and typed outcome selection; it runs through
both the C11 and direct x86-64 backends with byte-identical output. Together
they make the implemented slice executable while keeping unsupported ADT and
module codegen explicit.

## Remaining lifecycle work

Generic and affine elements, editable/capacity APIs, slices and iterators,
sorting/searching, parallel and SIMD execution, serialization, foreign
interop, property/fuzz suites, performance baselines, tooling views, and
release stabilization remain open. Raw `values[index]` still has the existing
runtime trap; callers that need a value-level error use `list_int_get`.

Run the Python-free focused gate with:

```sh
sh stdlib/list/tests/verify.sh
```
