# Array checkpoint

This directory defines a bounded, platform-independent reference checkpoint
for `Array`. It does not claim completion of the generated 20-step lifecycle
in issue #506.

## Array v1 surface

[`array.kofun`](array.kofun) exposes `IntArray1`, the rank-one `Int`
specialization of the planned `Array[T, Rank]` type:

- `array1_int_empty`, `array1_int_from_list`, `array1_int_filled`, and
  `array1_int_to_list` construct arrays or take an ordered snapshot;
- `array1_int_length`, `array1_int_is_empty`, and `array1_int_get` inspect a
  dynamic rank-one shape;
- value-returning `array1_int_set` updates one checked position without
  changing the shape; and
- `array1_int_map`, `array1_int_zip_map`, `array1_int_fold`, and
  `array1_int_contains` provide deterministic eager pipelines.

Array order is index order. `map`, `zip_map`, and `fold` invoke callbacks from
index zero to the final index. All functions are deterministic and have no
clock, randomness, I/O, global state, or locale dependency. Callback effects
are the caller's responsibility.

This checkpoint treats an Array as dense and fixed-shape after construction.
It intentionally has no `push`, `concat`, or `filter`: those operations change
length and belong on `List` or `Vector`, or need a separately named Array shape
operation. `array1_int_zip_map` requires equal lengths and does not broadcast.

## Errors, costs, and ownership

`array1_int_filled` returns `ArrayNegativeLength(requested)` for a negative
length. Checked indexes use the existing Kofun sequence rule: nonnegative
indexes count from the front and negative indexes from the end (`-1` is last).
An index outside `-length .. length - 1` returns
`ArrayIndexOutOfBounds(requested, length)`. Zip-map returns
`ArrayShapeMismatch(left_length, right_length)` before invoking its callback
when lengths differ. No integer is reserved as a sentinel.

The correctness-first reference stores elements in `List[Int]`. Length,
emptiness, and checked access are O(1) under the current indexed List model.
Construction, snapshot, set, map, zip-map, fold, and containment are O(n)
element work. Because current `push` is conservatively O(k), filled, set, and
zip-map may allocate and copy O(n^2) total storage; this is not a performance
promise. Inputs remain unchanged and returned arrays are independent values.

`Int` is Copy, so element return and callback arguments have precise ownership.
The API does not expose capacity, storage identity, mutable views, strides, or
an ABI layout. The record is intended to be opaque, although the current
compiler does not enforce module visibility.

## Executable boundary

The canonical source begins with a record and typed error, and the active
Stage 2 compiler cannot yet lower it as an importable module. The focused gate
records that boundary rather than claiming canonical code generation.

[`tests/checkpoint.kofun`](tests/checkpoint.kofun) executes real List-backed
length, indexing, map, fold, and empty-shape observations through direct
x86-64. [`tests/projection.kofun`](tests/projection.kofun) is an audited
three-scalar projection of negative indexing, checked update, map/fold
observations, containment, same-shape zip-map, and typed error selection. It
runs through C11 and direct x86-64 with byte-identical output. The three
scalars are only a finite semantic projection, not the public Array
representation or a maximum length.

## Relationship to scientific arrays

The long-term design in [`SCIENTIFIC_COMPUTING.md`](../../docs/SCIENTIFIC_COMPUTING.md)
uses `Array[T, Rank]` with contiguous specialized storage, multidimensional
shapes, slicing, broadcasting, and numerical kernels. `IntArray1` fixes rank
to one and specializes the element type so this checkpoint can state honest
ownership and shape rules now. It does not implement or revise the broader
scientific design.

## Remaining lifecycle work

Generic and affine elements, enforced representation privacy, multidimensional
shapes, views and strides, broadcasting, reshape/slicing, edit APIs,
contiguous specialized storage, lazy iteration, numeric kernels, SIMD and
parallel execution, serialization, foreign interop, property/fuzz suites,
performance baselines, tooling views, allocation failure policy, and release
stabilization remain open.

Run the Python-free focused gate with:

```sh
sh stdlib/array/tests/verify.sh
```
