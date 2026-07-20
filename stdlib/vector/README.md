# Vector checkpoint

This directory defines a bounded, platform-independent reference checkpoint
for `Vector`. It does not claim completion of the generated 20-step lifecycle
in issue #505.

## Vector v1 surface

[`vector.kofun`](vector.kofun) exposes an immutable `IntVector` API:

- `vector_int_empty`, `vector_int_from_list`, and `vector_int_to_list`;
- `vector_int_length`, `vector_int_is_empty`, and `vector_int_get`;
- value-returning `vector_int_push`, `vector_int_concat`, and
  `vector_int_reverse`;
- `vector_int_map`, `vector_int_filter`, `vector_int_fold`, and
  `vector_int_contains`.

Vectors preserve insertion order. `map`, `filter`, and `fold` visit elements
from index zero to the last index, and `filter` is stable. All operations are
deterministic and have no clock, randomness, I/O, global state, or locale
dependency.

Checked indexes use the existing Kofun sequence rule: nonnegative indexes
count from the front and negative indexes from the end (`-1` is last). An
index outside `-length .. length - 1` returns
`VectorIndexOutOfBounds(requested, length)`. No integer is reserved as a
sentinel, and checked access does not rely on the raw indexing trap.

## Costs and ownership

The version-1 reference stores an `IntVector` in an ordinary `List[Int]` and
copies at both List conversion boundaries. Length, emptiness, and checked
access are O(1) under the current indexed List model. `map`, `filter`, `fold`,
`reverse`, `contains`, and conversion are O(n); concatenation is O(n + m).
`push` is conservatively O(n). Value-returning operations may allocate
O(output length) storage; `fold` and checked access use O(1) extra storage.

Inputs are read-only and remain unchanged. `Int` is Copy, so returned elements
and callback arguments do not create an undocumented borrow. This checkpoint
does not expose storage identity, capacity, mutable views, or an amortized
growth promise. It is an ordered collection API, not the floating-point
scientific helpers currently named `linspace`, `vmul`, and `mean`.

## Executable boundary

The canonical source begins with a record and typed error, which the active
Stage 2 compiler cannot yet lower as an importable module. The focused gate
checks and records that boundary rather than claiming canonical codegen.

[`tests/checkpoint.kofun`](tests/checkpoint.kofun) executes actual List-backed
ordered collection behavior through the direct x86-64 backend.
[`tests/projection.kofun`](tests/projection.kofun) is an audited scalar
projection of negative-index normalization and typed outcome selection; it
runs through both C11 and direct x86-64 with byte-identical output.

## Remaining lifecycle work

Generic and affine elements, editable and capacity APIs, slices and iterators,
sorting/searching, specialized storage, SIMD and parallel execution,
serialization, foreign interop, property/fuzz suites, performance baselines,
tooling views, and release stabilization remain open. Raw indexing keeps its
existing runtime trap; callers requiring a value-level failure use
`vector_int_get`.

Run the Python-free focused gate with:

```sh
sh stdlib/vector/tests/verify.sh
```
