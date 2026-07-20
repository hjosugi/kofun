# Tuple checkpoint

This directory defines the first platform-independent standard-library
checkpoint for `Tuple`. It is a bounded reference surface, not completion of
the generated 20-step lifecycle in issue #508.

## Tuple v1 surface

[`tuple.kofun`](tuple.kofun) exposes pure helpers specialized to
`Tuple[Int, Int]`:

- `TUPLE2_ARITY` records the fixed arity of this checkpoint;
- `tuple2_int` constructs a pair;
- `tuple2_int_first` and `tuple2_int_second` read its fixed positions;
- `tuple2_int_swap` reverses the positions;
- `tuple2_int_map` and `tuple2_int_bimap` transform values from left to right;
- `tuple2_int_fold` combines the first value and then the second;
- `tuple2_int_contains` and `tuple2_int_same` provide value comparisons; and
- `tuple2_int_to_list` projects the homogeneous pair to an ordered List.

Tuple literals use the language's existing comma syntax: `(first, second)`.
Their type includes both arity and position types, here `Tuple[Int, Int]`.
Tuples are immutable, so every transformation returns a new value and leaves
its input unchanged. All helpers are deterministic and have no I/O, clock,
randomness, global state, or locale dependency. Callbacks are invoked in tuple
order; callback effects are the caller's responsibility.

## Errors, costs, and ownership

The v1 helpers only use statically valid positions zero and one. Wrong arity,
wrong element types, and dynamic tuple positions are outside this surface and
must be rejected statically when tuple checking is implemented; no integer or
`null` sentinel stands for a missing element. Kofun does not define an empty
tuple. The one-element spelling is `(value,)`, distinct from grouped `(value)`.

Construction, access, swap, map, fold, containment, equality, and List
projection are all O(1) because this checkpoint fixes the arity at two.
Returned tuples and Lists may allocate O(1) storage. Inputs are read-only and
remain unchanged. Version 1 is intentionally `Tuple[Int, Int]`: `Int` is Copy.
Generic tuples need an explicit Copy rule or move-aware accessors before
non-Copy elements can honestly leave a borrowed tuple. Tuple or record Copy
inference remains an open design decision in [`DESIGN.md`](../../DESIGN.md).

## Executable boundary

The canonical API uses tuple types, tuple literals, fixed-position access, and
higher-order signatures. The active Stage 2 compiler only treats parentheses
as grouping in its executable Core, so the gate does not claim that
`tuple.kofun` is importable or code-generated today.

[`tests/checkpoint.kofun`](tests/checkpoint.kofun) is a deterministic scalar
projection: each pair is represented as two ordered Int parameters, without
claiming a tuple memory layout. It covers first/second access, swap, ordered
map/fold observations, containment, and equality through both C11 and direct
x86-64 backends with byte-identical golden output.

## Remaining lifecycle work

Executable tuple parsing, type checking and lowering, generic and affine
elements, tuple destructuring and iteration adapters, property/fuzz suites,
benchmarks, SIMD/parallel execution, serialization and foreign ABI layouts,
tooling views, and release stabilization remain open.

Run the Python-free focused gate with:

```sh
sh stdlib/tuple/tests/verify.sh
```
